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
 TArray<FTransform> RestSpace=Ref.GetRefBonePose();
 for(int32 I=1;I<RestSpace.Num();++I)RestSpace[I]=RestSpace[I]*RestSpace[Ref.GetParentIndex(I)];
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
    // the fingertip: past the last joint by most of the middle segment's length, as the rig estimates it
    const int32 Mid=Ref.GetParentIndex(Tip);
    const FVector RestLast=RestSpace[Tip].GetLocation()-RestSpace[Mid].GetLocation();
    const FVector LocalLast=RestSpace[Tip].GetRotation().UnrotateVector(RestLast);
    const FVector Last=Space[Tip].GetRotation().RotateVector(LocalLast);
    const FVector P=Pose.World.TransformPosition(Space[Tip].GetLocation()+Last*.8f);
    if(!D->Surface.IsValid())continue;
    const float Off=D->Surface->Clear(P);
    TestTrue(FString::Printf(TEXT("%s %s fingertip stays out of the resin (%.2f cm)"),*Sim.Spec.Routes[Sim.Route].Name,Finger,Off),Off>-.5f);
    Gap+=FMath::Abs(Off);Count++;if(FMath::Abs(Off)<3.5f)Touching++;
   }
  }
 }
 AddInfo(FString::Printf(TEXT("held fingers: mean gap to resin %.2f cm, %d of %d within 3.5 cm"),Count?Gap/Count:0,Touching,Count));
 TestTrue(TEXT("most held fingers end on the resin"),Count>0&&Touching*2>Count);
 // A real move to the first crimp on Slab Dance, from a live starting stance.
 Sim.Route=0;
 Sim.Restart();
 Rig.Reset();
 FHumanPose Catch;
 for(int32 Frame=0;Frame<30;++Frame)
 {
  Sim.Step(1.f/60);
  Catch=Rig.Solve(FHumanGoals::FromSim(Sim),1.f/60);
 }
 constexpr int32 Hand=2, Crimp=7;
 Sim.Lean=FVector2D(-.25,.5);
 for(int32 Frame=0;Frame<40 && !Sim.Check(Hand,Crimp,-.35f).IsEmpty();++Frame)
 {
  Sim.Step(1.f/60);
  Catch=Rig.Solve(FHumanGoals::FromSim(Sim),1.f/60);
 }
 const bool Grabbed=Sim.Grab(Hand,Crimp,-.35f);
 if(!TestTrue(FString::Printf(TEXT("slab crimp accepts a reach with hips shifted (%s)"),*Sim.Hint),Grabbed)) return false;
 for(int32 Frame=0;Frame<60 && !Sim.Arrived(Hand) && !Sim.Fallen;++Frame)
 {
  Sim.Step(1.f/60);
  Catch=Rig.Solve(FHumanGoals::FromSim(Sim),1.f/60);
 }
 if(!TestTrue(TEXT("crimp reached its first loaded frame"),Sim.Arrived(Hand)&&Sim.Grips[Hand]==Crimp&&Catch.Finite)) return false;
 TArray<FTransform> Space=Catch.Local;
 for(int32 I=1;I<Space.Num();++I) Space[I]*=Space[Ref.GetParentIndex(I)];
 const auto& Surface=Sim.Holds()[Crimp].Surface;
 if(!TestTrue(TEXT("crimp uses rendered resin geometry"),Surface.IsValid())) return false;
 auto TipClear=[&](const TCHAR* Name)
 {
  const int32 Tip=Ref.FindBoneIndex(Name),Mid=Ref.GetParentIndex(Tip);
  const FVector Last=RestSpace[Tip].GetRotation().UnrotateVector(RestSpace[Tip].GetLocation()-RestSpace[Mid].GetLocation());
  return Surface->Clear(Catch.World.TransformPosition(Space[Tip].GetLocation()+Space[Tip].GetRotation().RotateVector(Last*.8)));
 };
 const int32 Proximal=Ref.FindBoneIndex(TEXT("RightHandMiddle1"));
 const int32 Middle=Ref.FindBoneIndex(TEXT("RightHandMiddle2"));
 const int32 Distal=Ref.FindBoneIndex(TEXT("RightHandMiddle3"));
 const FVector A=(Space[Middle].GetLocation()-Space[Proximal].GetLocation()).GetSafeNormal();
 const FVector B=(Space[Distal].GetLocation()-Space[Middle].GetLocation()).GetSafeNormal();
 const float Bend=FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(A|B,-1.0,1.0)));
 const float Finger=TipClear(TEXT("RightHandMiddle3")),Thumb=TipClear(TEXT("RightHandThumb3"));
 AddInfo(FString::Printf(TEXT("first loaded crimp: middle bend %.1f degrees, tip clearance %.2f cm, thumb clearance %.2f cm"),Bend,Finger,Thumb));
 TestTrue(TEXT("first loaded crimp is bent at the middle joint"),Bend>20);
 TestTrue(TEXT("first loaded crimp keeps finger and thumb tips outside the resin"),FMath::Min(Finger,Thumb)>-.5f);
 return true;
}
#endif
