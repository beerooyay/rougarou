#include "LocomotionAnim.h"
#include "HumanRig.h"
#include "GymActor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
// A held hand's fingertips should end on the resin: neither floating off it nor buried in it.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumanFingerTest,"Rougarou.HumanFingers",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHumanFingerTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("native fitted mesh"),Mesh))return false;
 const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
 FHumanRig Rig;Rig.Initialize(Ref,FHumanRig::ProfilePath());
 FClimbSim Sim;FClimbSpec::Load(FPaths::ProjectContentDir()/TEXT("Data/routes.json"),Sim.Spec);Sim.Spec.Routes=AGymActor::Problems();
 double Gap=0;int32 Count=0,Touching=0;
 for(Sim.Route=0;Sim.Route<Sim.Spec.Routes.Num();Sim.Route++)
 {
  Sim.Restart();for(int32 F=0;F<120;F++)Sim.Step(1.f/60);
  Rig.Reset();const FHumanGoals Goals=FHumanGoals::FromSim(Sim);FHumanPose Pose;
  for(int32 F=0;F<60;F++)Pose=Rig.Solve(Goals,F?1.f/60:0);
  TArray<FTransform> Space;Space.SetNum(Pose.Local.Num());
  for(int32 I=0;I<Pose.Local.Num();I++){const int32 P=Ref.GetParentIndex(I);Space[I]=P>=0?Pose.Local[I]*Space[P]:Pose.Local[I];}
  for(int32 L=2;L<4;L++)
  {
   const FHumanGoals::FDome* D=nullptr;for(const auto& X:Goals.Domes)if(X.Owners&(1<<L))D=&X;
   if(!D||!Goals.Held[L])continue;
   for(const TCHAR* Finger:{TEXT("Index"),TEXT("Middle"),TEXT("Ring"),TEXT("Pinky")})
   {
    const int32 Tip=Ref.FindBoneIndex(*FString::Printf(TEXT("%sHand%s3"),L==2?TEXT("Right"):TEXT("Left"),Finger));if(Tip<0)continue;
    const FVector P=Pose.World.TransformPosition(Space[Tip].GetLocation());
    // distance from the last knuckle to the resin dome's surface (an ellipsoid; approximate by scaled radius)
    const FVector Rel=P-D->C;const float Along=FMath::Max(0.f,float(FVector::DotProduct(Rel,D->N)));const FVector Lat=Rel-D->N*FVector::DotProduct(Rel,D->N);
    const float Q=FMath::Sqrt(FMath::Square(Lat.Size()/D->R)+FMath::Square(Along/FMath::Max(D->H,1.f)));
    const float Off=(Q-1)*FMath::Min(D->R,D->H);
    Gap+=FMath::Abs(Off);Count++;if(FMath::Abs(Off)<3.5f)Touching++;
   }
  }
 }
 AddInfo(FString::Printf(TEXT("held fingers: mean gap to resin %.2f cm, %d of %d within 3.5 cm"),Count?Gap/Count:0,Touching,Count));
 TestTrue(TEXT("most held fingers end on the resin"),Count>0&&Touching*2>Count);
 return true;
}
#endif
