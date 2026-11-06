#include "LocomotionAnim.h"
#include "HumanRig.h"
#include "GymActor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumanRigTest,"Rougarou.HumanRig",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHumanRigTest::RunTest(const FString& Parameters)
{
 FHumanFrame Frame;FHumanLimit Limit{90,25,60,20,-30,40};
 for(float Side:{-1.f,1.f})for(int32 I=0;I<200;I++)
 {
  Frame.Side=Side;
  const FQuat Q=(FQuat(FVector(1,2,3).GetSafeNormal(),I*.071)*FQuat(Frame.T,I*.13)).GetNormalized();
  const FQuat C=FHumanRig::Clamp(Q,Frame,Limit);
  TestTrue(TEXT("swing and mirrored twist clamp to the cone"),FHumanRig::Violation(C,Frame,Limit)<.02f);
  TestTrue(TEXT("clamping is idempotent"),C.AngularDistance(FHumanRig::Clamp(C,Frame,Limit))<.001f);
 }
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("native fitted mesh"),Mesh))return false;
 FHumanRig Rig;if(!TestTrue(TEXT("anatomical profile binds to native skeleton"),Rig.Initialize(Mesh->GetRefSkeleton(),FHumanRig::ProfilePath())))return false;
 TestEqual(TEXT("all native anatomical joints are mapped"),Rig.BoneMap().Num(),19);
 FClimbSim Sim;FClimbSpec::Load(FPaths::ProjectContentDir()/TEXT("Data/routes.json"),Sim.Spec);Sim.Spec.Routes=AGymActor::Problems();
 const auto& Rest=Mesh->GetRefSkeleton().GetRefBonePose();
 TArray<FTransform> Bind=Rest;
 const auto& Ref=Mesh->GetRefSkeleton();
 for(int32 I=0;I<Bind.Num();++I)
  if(Ref.GetParentIndex(I)>=0)Bind[I]*=Bind[Ref.GetParentIndex(I)];
 auto Bone=[&](const TCHAR* Name){return Ref.FindBoneIndex(Name);};
 auto At=[&](const TCHAR* Name){return Bind[Bone(Name)].GetLocation();};
 const FVector Up=(At(TEXT("LeftArm"))+At(TEXT("RightArm"))-At(TEXT("LeftUpLeg"))-At(TEXT("RightUpLeg"))).GetSafeNormal();
 const FVector Front=FVector::VectorPlaneProject(At(TEXT("LeftToeBase"))-At(TEXT("LeftFoot"))
  +At(TEXT("RightToeBase"))-At(TEXT("RightFoot")),Up).GetSafeNormal();
 for(Sim.Route=0;Sim.Route<Sim.Spec.Routes.Num();Sim.Route++)
 {
  Sim.Restart();Rig.Reset();
  auto Goals=FHumanGoals::FromSim(Sim);auto Pose=Rig.Solve(Goals);
  float Miss=0;for(float M:Pose.Miss)Miss+=M/4;

  AddInfo(FString::Printf(TEXT("%s mean miss %.2fcm wall %.2fcm comfort %.2f violation %.4f"),*Sim.Spec.Routes[Sim.Route].Name,Miss,Pose.MinClearance,Pose.Comfort,Pose.MaxLimitViolation));
  TestTrue(TEXT("pose is finite"),Pose.Finite);
  for(float M:Pose.Miss)TestTrue(TEXT("opening contact lands on its actual target"),M<1.f);
  if(Sim.Route==0)
  {
   TArray<FTransform> Posed=Pose.Local;
   for(int32 I=0;I<Posed.Num();++I)
    if(Ref.GetParentIndex(I)>=0)Posed[I]*=Posed[Ref.GetParentIndex(I)];
   for(int32 Limb=0;Limb<2;++Limb)
   {
    const auto& Surface=Sim.Holds()[Sim.Grips[Limb]].ShoeContact;
    if(!TestTrue(TEXT("starting shoe has a sampled resin contact"),Surface.IsSet()))continue;
    const int32 Foot=Bone(Limb==0?TEXT("RightFoot"):TEXT("LeftFoot"));
    // New modular assets carry a measured outsole landmark. Retain the
    // measured fallback for the earlier character rather than using an ankle
    // position as proof of shoe contact.
    const int32 Contact=Bone(Limb==0?TEXT("RightSoleContact"):TEXT("LeftSoleContact"));
    const FVector Point=Contact>=0?Bind[Contact].GetLocation():Bind[Foot].GetLocation()+Front*19-Up*4.7;
    const FVector Sole=Bind[Foot].InverseTransformPosition(Point);
    const FVector Actual=Pose.World.TransformPosition(Posed[Foot].TransformPosition(Sole));
    const double Gap=FVector::Dist(Actual,Surface.GetValue());
    TestTrue(FString::Printf(TEXT("outsole reaches the resin (%.2f cm gap)"),Gap),Gap<2.5);
   }
   for(int32 Limb=2;Limb<4;++Limb)
   {
    if(Goals.HandSurfaces[Limb-2].IsSet())
    {
     const FVector Knuckle=Pose.World.TransformPosition(Posed[Bone(Limb==2?TEXT("RightHandMiddle1"):TEXT("LeftHandMiddle1"))].GetLocation());
     TestTrue(TEXT("actual knuckle fits the sampled resin with skin clearance"),FVector::Dist(Knuckle,Goals.HandSurfaces[Limb-2].GetValue()+Goals.Out[Limb]*2.5f)<1.f);
    }
    const int32 Joint=Bone(Limb==2?TEXT("RightHandMiddle2"):TEXT("LeftHandMiddle2"));
    const int32 Tip=Bone(Limb==2?TEXT("RightHandMiddle3"):TEXT("LeftHandMiddle3"));
    const FVector Segment=Pose.World.TransformVectorNoScale(Posed[Tip].GetLocation()-Posed[Joint].GetLocation()).GetSafeNormal();
    TestTrue(TEXT("jug finger curls inward over the lip"),FVector::DotProduct(Segment,-Goals.Out[Limb])>.8);
   }
  }
  TestTrue(TEXT("anatomy is never relaxed to force contact"),Pose.MaxLimitViolation<.03f);
  TestTrue(TEXT("body correction stays bounded"),Pose.Shift.Size()<=Rig.MaxShift+.001f);
  for(int32 I=0;I<Rest.Num();I++)
  {
   TestTrue(TEXT("solver preserves bone lengths"),Pose.Local[I].GetTranslation().Equals(Rest[I].GetTranslation(),.001));
   TestTrue(TEXT("solver never scales a joint"),Pose.Local[I].GetScale3D().Equals(Rest[I].GetScale3D(),.001));
  }
  Rig.Reset();Pose=Rig.Solve(Goals,1.f/60);
  if(Sim.Route==0)
  {
   for(int32 Settle=0;Settle<180;++Settle)Pose=Rig.Solve(Goals,1.f/60);
   for(int32 L=0;L<2;++L)TestTrue(FString::Printf(TEXT("settled shoe %d stays on resin (%.3f cm)"),L,Pose.Miss[L]),Pose.Miss[L]<1.f);
  }
  for(int32 T=0;T<60;T++)
  {
   auto Previous=Pose;Goals.Ends[2].Z+=.2;
   if(Sim.Route==0)
   {
    Goals.Held[0]=false;Goals.ShoeSurfaces[0].Reset();
    Goals.Ends[0].Z+=.5f;
    Goals.Poles[0]=(FVector::UpVector*(T<30?1.f:-.4f)+Goals.Out[0]*.6f).GetSafeNormal();
   }
   Pose=Rig.Solve(Goals,1.f/60);
   TestTrue(TEXT("animated pose keeps anatomical bounds"),Pose.MaxLimitViolation<.03f);
   TestTrue(TEXT("moving joints remain in front of the wall"),Pose.MinClearance>=0.f);
   for(int32 I=0;I<Pose.Local.Num();I++)
   {
    const double Angle=Previous.Local[I].GetRotation().AngularDistance(Pose.Local[I].GetRotation());
    TestTrue(FString::Printf(TEXT("%s frame %d %s rotation %.5f stays within rate"),*Sim.Spec.Routes[Sim.Route].Name,T,*Ref.GetBoneName(I).ToString(),Angle),Angle<Rig.JointRate/60+.005f);
   }
  }
 }
 return true;
}
#endif
