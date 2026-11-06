#include "LocomotionAnim.h"
#include "HumanRig.h"
#include "GymActor.h"
#include "Engine/SkeletalMesh.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "HAL/PlatformTime.h"

#if WITH_DEV_AUTOMATION_TESTS
// The move gate must refuse only what the skeleton really cannot do. From each
// problem's start, every hold the sim's reach rules allow is asked of the gate.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHumanGateTest,"Rougarou.HumanGate",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FHumanGateTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("native fitted mesh"),Mesh))return false;
 FHumanRig Rig,Live;Rig.Initialize(Mesh->GetRefSkeleton(),FHumanRig::ProfilePath());Live.Initialize(Mesh->GetRefSkeleton(),FHumanRig::ProfilePath());Rig.ColdIterations=3;
 FClimbSim Sim;FClimbSpec::Load(FPaths::ProjectContentDir()/TEXT("Data/routes.json"),Sim.Spec);Sim.Spec.Routes=AGymActor::Problems();
 Sim.Gate=[&](const FClimbSim& Now,int32 Limb,int32 Hold)->FString
 {
  FClimbSim Try=Now;Try.Gate=nullptr;Try.Grips[Limb]=Hold;Try.Smeared[Limb]=false;Try.Latch[Limb]=0;Try.Tips[Limb]=Now.Holds()[Hold].Point;Try.Reaching=-1;
  Rig.WarmFrom(Live);const FHumanPose Pose=Rig.Solve(FHumanGoals::FromSim(Try),0);
  if(!Pose.Finite)return FString();
  if(Pose.Miss[Limb]>8)return TEXT("reach");
  if(Pose.MinClearance< -1)return TEXT("wall");
  return FString();
 };
 int32 Asked=0,Refused=0;double Seconds=0;
 for(Sim.Route=0;Sim.Route<Sim.Spec.Routes.Num();Sim.Route++)
 {
  Sim.Restart();for(int32 F=0;F<120;F++)Sim.Step(1.f/60);
  Live.Reset();Live.Solve(FHumanGoals::FromSim(Sim),0);
  int32 RouteRefused=0,RouteAsked=0;
  for(int32 Limb=0;Limb<4;Limb++)for(int32 Hold=0;Hold<Sim.Holds().Num();Hold++)
  {
   if(Sim.Grips[Limb]==Hold)continue;
   if(Sim.Distance(Limb,Sim.Holds()[Hold].Point)>Sim.Range(Limb)||!Sim.Open(Limb,Sim.Holds()[Hold].Point))continue;
   const double Start=FPlatformTime::Seconds();
   const FString Why=Sim.Check(Limb,Hold);
   Seconds+=FPlatformTime::Seconds()-Start;
   RouteAsked++;if(!Why.IsEmpty())RouteRefused++;
  }
  AddInfo(FString::Printf(TEXT("%s: gate refused %d of %d reachable moves"),*Sim.Spec.Routes[Sim.Route].Name,RouteRefused,RouteAsked));
  Asked+=RouteAsked;Refused+=RouteRefused;
 }
 AddInfo(FString::Printf(TEXT("gate refused %d of %d, %.1f ms per check"),Refused,Asked,Asked?Seconds*1000/Asked:0));
 TestTrue(TEXT("the gate leaves most reachable moves open"),Refused*3<Asked);
 TestTrue(TEXT("a gate check is fast enough to run while aiming"),Asked==0||Seconds/Asked<.02);
 return true;
}
#endif
