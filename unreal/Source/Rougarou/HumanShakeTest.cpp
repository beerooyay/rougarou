#include "LocomotionAnim.h"
#include "HumanRig.h"
#include "GymActor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"

#if WITH_DEV_AUTOMATION_TESTS
// Shake is frame-to-frame change in joint speed. Drive the sim with a slow hip
// sway and a reach, then compare mean joint angular acceleration per solver.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumanShakeTest,"Rougarou.HumanShake",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHumanShakeTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("native fitted mesh"),Mesh))return false;
 auto Measure=[&](bool Legacy)
 {
  FHumanRig Rig;Rig.Initialize(Mesh->GetRefSkeleton(),FHumanRig::ProfilePath());Rig.UseLegacy=Legacy;
  FClimbSim Sim;FClimbSpec::Load(FPaths::ProjectContentDir()/TEXT("Data/routes.json"),Sim.Spec);Sim.Spec.Routes=AGymActor::Problems();
  double Total=0;int32 Count=0;TArray<double> PerBone;PerBone.SetNumZeroed(Mesh->GetRefSkeleton().GetNum());
  for(Sim.Route=0;Sim.Route<Sim.Spec.Routes.Num();Sim.Route++)
  {
   Sim.Restart();Rig.Reset();
   TArray<FQuat> A,B;
   for(int32 F=0;F<240;F++)
   {
    Sim.Lean=FVector2D(FMath::Sin(F*.05f),FMath::Sin(F*.031f)*.5f)*.6f;
    Sim.Step(1.f/60);
    const FHumanPose Pose=Rig.Solve(FHumanGoals::FromSim(Sim),1.f/60);
    TArray<FQuat> C;for(const FTransform& T:Pose.Local)C.Add(T.GetRotation());
    if(B.Num()==C.Num() && A.Num()==C.Num() && F>30)
     for(int32 I=0;I<C.Num();I++)
     {
      // second difference of rotation: how much this frame's step differs from the last one
      const FQuat Step1=B[I]*A[I].Inverse(),Step2=C[I]*B[I].Inverse();
      const double D=Step1.AngularDistance(Step2);Total+=D;Count++;PerBone[I]+=D;
     }
    A=MoveTemp(B);B=MoveTemp(C);
   }
  }
  TArray<int32> Order;for(int32 I=0;I<PerBone.Num();I++)Order.Add(I);Order.Sort([&](int32 X,int32 Y){return PerBone[X]>PerBone[Y];});
  FString Top;for(int32 K=0;K<8;K++)Top+=FString::Printf(TEXT("%s=%.2f "),*Mesh->GetRefSkeleton().GetBoneName(Order[K]).ToString(),PerBone[Order[K]]);
  AddInfo(FString::Printf(TEXT("%s top: %s"),Legacy?TEXT("legacy"):TEXT("simple"),*Top));
  return Count?Total/Count:0.0;
 };
 const double Simple=Measure(false),Legacy=Measure(true);
 AddInfo(FString::Printf(TEXT("mean joint acceleration per frame: simple %.6f rad, legacy %.6f rad"),Simple,Legacy));
 TestTrue(TEXT("simple solver shakes less than the legacy solver"),Simple<Legacy);
 return true;
}
#endif
