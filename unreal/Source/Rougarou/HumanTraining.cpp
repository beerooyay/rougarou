#include "LocomotionAnim.h"
#include "HumanRig.h"
#include "GymActor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
// An explicit, reproducible posture fit. Ordinary checks validate the promoted
// profile; only -humantrain runs optimization and writes a candidate, never assets.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumanTraining,"Rougarou.HumanTraining",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
struct FHumanSample {FHumanGoals Goals;int32 Route;};
struct FHumanMetrics
{
 double Cost=0, Miss=0, WorstMiss=0, Violation=0, Clearance=MAX_flt;int32 Contacts=0, Count=0;
 TSharedPtr<FJsonObject> Json() const
 {
  auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("poses"),Count);J->SetNumberField(TEXT("cost"),Cost/FMath::Max(1,Count));J->SetNumberField(TEXT("mean_contact_miss_cm"),Miss/FMath::Max(1,Contacts));J->SetNumberField(TEXT("worst_contact_miss_cm"),WorstMiss);J->SetNumberField(TEXT("limit_violation_degrees"),Violation);J->SetNumberField(TEXT("minimum_joint_wall_clearance_cm"),Clearance);return J;
 }
};
bool FHumanTraining::RunTest(const FString& Parameters)
{
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("training uses the actual native character"),Mesh))return false;
 FHumanRig Rig;if(!Rig.Initialize(Mesh->GetRefSkeleton(),FHumanRig::ProfilePath())){AddError(TEXT("motion profile missing"));return false;}
 FClimbSim Sim;FClimbSpec::Load(FPaths::ProjectContentDir()/TEXT("Data/routes.json"),Sim.Spec);Sim.Spec.Routes=AGymActor::Problems();
 TArray<FHumanSample> Samples;
 const FVector2D Shifts[]={{-1,0},{1,0},{0,1},{0,-1}};
 auto Step=[&](int32 Frames){for(int32 F=0;F<Frames;F++)Sim.Step(1.f/60);Sim.Lean=FVector2D::ZeroVector;};
 auto Capture=[&](){Samples.Add({FHumanGoals::FromSim(Sim),Sim.Route});};
 auto Score=[&](){float S=Sim.Won?1000:0;for(int32 L=0;L<4;L++){if(Sim.Grips[L]<0){S-=L<2?.1f:.3f;continue;}const float Y=Sim.Holds()[Sim.Grips[L]].Point.Y;S+=Y*(L>=2?1:.3f);if(L<2&&Y>Sim.Position.Y)S-=1;}return S;};
 for(Sim.Route=0;Sim.Route<Sim.Spec.Routes.Num();Sim.Route++)
 {
  Sim.Restart();Capture();
  for(int32 Move=0;Move<24&&!Sim.Won;Move++)
  {
   const auto Saved=Sim.Save();float Best=Score()+.01f;int32 Choice[3]={-1,-1,-1};
   for(int32 Lean=-1;Lean<4;Lean++)for(int32 L=0;L<4;L++)for(int32 H=0;H<Sim.Holds().Num();H++)
   {
    Sim.Load(Saved);if(Sim.Grips[L]==H)continue;
    if(Lean>=0){Sim.Lean=Shifts[Lean]*.6f;Step(30);if(Sim.Grips[2]<0||Sim.Grips[3]<0)continue;}
    if(!Sim.Grab(L,H))continue;Step(45);if(Sim.Grips[2]<0||Sim.Grips[3]<0)continue;
    const float S=Score();if(S>Best){Best=S;Choice[0]=Lean;Choice[1]=L;Choice[2]=H;}
   }
   Sim.Load(Saved);if(Choice[1]<0)break;
   if(Choice[0]>=0){Sim.Lean=Shifts[Choice[0]]*.6f;Step(30);}
   Sim.Grab(Choice[1],Choice[2]);Step(15);if(Move%3==0)Capture();Step(30);if(Move%3==0)Capture();
  }
  TestTrue(*FString::Printf(TEXT("training route %d still sends"),Sim.Route),Sim.Won);
 }
 auto Evaluate=[&](FHumanMix Mix,bool Heldout)
 {
  Rig.Mix=Mix;FHumanMetrics M;
  for(const auto& Sample:Samples)
  {
   if((Sample.Route%3==2)!=Heldout)continue;
   Rig.Reset();const auto P=Rig.Solve(Sample.Goals);M.Count++;M.Cost+=P.Cost(Sample.Goals);
   M.Violation=FMath::Max(M.Violation,double(P.MaxLimitViolation));M.Clearance=FMath::Min(M.Clearance,double(P.MinClearance));
   for(int32 L=0;L<4;L++)if(Sample.Goals.Held[L]){M.Miss+=P.Miss[L];M.WorstMiss=FMath::Max(M.WorstMiss,double(P.Miss[L]));M.Contacts++;}
   if(!P.Finite)M.Cost+=1.e9;
  }
  return M;
 };
 const FHumanMix Baseline=Rig.Mix;auto BaseTrain=Evaluate(Baseline,false),BaseTest=Evaluate(Baseline,true);
 FHumanMix Best=Baseline;double BestCost=BaseTrain.Cost;
 const bool Train=FParse::Param(FCommandLine::Get(),TEXT("humantrain"));
 TArray<TSharedPtr<FJsonValue>> History;
 if(Train)
 {
  // Cross-entropy search over distribution of trunk bend and scapular rhythm.
  // Hard ranges, limb lengths, targets and the held-out routes never enter the fit.
  FRandomStream Random(20261102);FHumanMix Mean=Baseline;float Sigma[4]={.14f,.14f,.16f,.08f};
  auto Vector=[](FHumanMix M){return TArray<float>{M.Waist,M.Spine,M.Neck,M.Scapula};};
  for(int32 Gen=0;Gen<7;Gen++)
  {
   struct FCandidate{FHumanMix Mix;double Cost;};TArray<FCandidate> Population;
   Population.Add({Best,BestCost});
   const auto V=Vector(Mean);
   for(int32 I=1;I<12;I++)
   {
    float P[4];for(int32 K=0;K<4;K++){const float R=FMath::Sqrt(-2*FMath::Loge(FMath::Max(.0001f,Random.FRand())))*FMath::Cos(2*PI*Random.FRand());P[K]=V[K]+R*Sigma[K];}
    FHumanMix M;M.Waist=FMath::Clamp(P[0],.05f,.65f);M.Spine=FMath::Clamp(P[1],M.Waist+.05f,.95f);M.Neck=FMath::Clamp(P[2],.15f,.85f);M.Scapula=FMath::Clamp(P[3],.05f,.4f);
    Population.Add({M,Evaluate(M,false).Cost});
   }
   Population.Sort([](const FCandidate&A,const FCandidate&B){return A.Cost<B.Cost;});
   Best=Population[0].Mix;BestCost=Population[0].Cost;
   float Avg[4]={};for(int32 I=0;I<4;I++){auto E=Vector(Population[I].Mix);for(int32 K=0;K<4;K++)Avg[K]+=E[K]/4;}
   Mean={Avg[0],Avg[1],Avg[2],Avg[3]};
   for(int32 K=0;K<4;K++){float Variance=0;for(int32 I=0;I<4;I++)Variance+=FMath::Square(Vector(Population[I].Mix)[K]-Avg[K])/4;Sigma[K]=FMath::Max(.015f,FMath::Sqrt(Variance));}
   History.Add(MakeShared<FJsonValueNumber>(BestCost/BaseTrain.Count));
   UE_LOG(LogTemp,Display,TEXT("Human fit generation %d train cost %.2f"),Gen,BestCost/BaseTrain.Count);
  }
 }
 const auto FitTrain=Evaluate(Best,false),FitTest=Evaluate(Best,true);
 auto Report=MakeShared<FJsonObject>();Report->SetStringField(TEXT("source_commit"),TEXT("3d30a187e736c28c76b58840d0d3248573175737"));Report->SetNumberField(TEXT("seed"),20261102);Report->SetStringField(TEXT("split"),TEXT("train routes 0,1,3,4,6,7; held out routes 2,5,8"));
 Report->SetObjectField(TEXT("baseline_train"),BaseTrain.Json());Report->SetObjectField(TEXT("baseline_heldout"),BaseTest.Json());Report->SetObjectField(TEXT("candidate_train"),FitTrain.Json());Report->SetObjectField(TEXT("candidate_heldout"),FitTest.Json());Report->SetArrayField(TEXT("training_cost"),History);
 const bool Promote=Train&&FitTrain.Cost<BaseTrain.Cost&&FitTest.Cost<BaseTest.Cost&&FitTest.WorstMiss<=BaseTest.WorstMiss+.1&&FitTest.Clearance>=BaseTest.Clearance-.1&&FitTest.Violation<.03;
 Report->SetBoolField(TEXT("eligible_for_promotion"),Promote);
 auto Mix=MakeShared<FJsonObject>();Mix->SetNumberField(TEXT("waist"),Best.Waist);Mix->SetNumberField(TEXT("spine"),Best.Spine);Mix->SetNumberField(TEXT("neck"),Best.Neck);Mix->SetNumberField(TEXT("scapula"),Best.Scapula);Report->SetObjectField(TEXT("native_mix"),Mix);
 const FString Directory=FPaths::ProjectSavedDir()/TEXT("HumanMotion");IFileManager::Get().MakeDirectory(*Directory,true);
 FString Json;FJsonSerializer::Serialize(Report,TJsonWriterFactory<>::Create(&Json));FFileHelper::SaveStringToFile(Json,*(Directory/(Train?TEXT("training.json"):TEXT("validation.json"))));
 AddInfo(FString::Printf(TEXT("Human profile: %d poses, heldout miss %.2f -> %.2f cm; cost %.2f -> %.2f; candidate %s"),Samples.Num(),BaseTest.Miss/BaseTest.Contacts,FitTest.Miss/FitTest.Contacts,BaseTest.Cost/BaseTest.Count,FitTest.Cost/FitTest.Count,Promote?TEXT("eligible"):TEXT("not promoted")));
 TestTrue(TEXT("heldout poses obey joint limits"),FitTest.Violation<.03);
 TestTrue(TEXT("heldout mean contact error stays below 3cm"),FitTest.Miss/FitTest.Contacts<3);
 return true;
}
#endif
