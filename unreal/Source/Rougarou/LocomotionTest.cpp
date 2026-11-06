#include "LocomotionAnim.h"
#include "Locomotion.h"
#include "GymActor.h"
#include "Animation/AnimSequence.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/Skeleton.h"
#include "Misc/AutomationTest.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocomotionTest,"Rougarou.Locomotion",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLocomotionTest::RunTest(const FString& Parameters)
{
 auto Free=[](FVector A,FVector B){return A+B;};
 FLocomotion L;
 L.Step(FVector::ForwardVector,false,1.f/60,Free);
 TestTrue(TEXT("start accelerates rather than teleporting to walk speed"),L.Velocity.Size()>0 && L.Velocity.Size()<30);
 for(int32 I=0;I<120;I++)L.Step(FVector::ForwardVector,true,1.f/60,Free);
 TestTrue(TEXT("running reaches its intended speed"),FMath::IsNearlyEqual(L.Velocity.Size(),380.,.1));
 const float OldYaw=L.Yaw;
 L.Step(-FVector::ForwardVector,true,1.f/60,Free);
 TestTrue(TEXT("reversal brakes before changing direction"),L.Velocity.X>0);
 TestTrue(TEXT("facing cannot snap on reversal"),FMath::Abs(FMath::FindDeltaAngleRadians(OldYaw,L.Yaw))<.13);
 for(int32 I=0;I<120;I++)L.Step(FVector::ZeroVector,false,1.f/60,Free);
 TestTrue(TEXT("release comes to a complete stop"),L.Velocity.IsNearlyZero());
 const float StoppedYaw=L.Yaw;
 L.Step(FVector::ZeroVector,false,.1,Free);
 TestEqual(TEXT("idle retains body facing"),L.Yaw,StoppedYaw);
 L.Step(FVector::ForwardVector,true,.1,[](FVector A,FVector B){return A;});
 TestTrue(TEXT("blocked movement also stops the animation speed"),L.Velocity.IsNearlyZero());
 FLocomotion A,B;
 for(int32 I=0;I<60;I++)A.Step(FVector(1,1,0),false,1.f/60,Free);
 for(int32 I=0;I<30;I++)B.Step(FVector(1,1,0),false,1.f/30,Free);
 TestTrue(TEXT("30 and 60 hz cover the same distance"),A.At.Equals(B.At,.02));
 TestTrue(TEXT("diagonal input cannot boost walking speed"),A.Velocity.Size()<=150.01);
 TestFalse(TEXT("the rest of the gym is accessible"),AGymActor::WalkerBlocked(FVector(750,-900,28)));
 TestTrue(TEXT("back wall blocks standing body"),AGymActor::WalkerBlocked(FVector(-60,0,28)));
 TestTrue(TEXT("arch leg blocks standing body"),AGymActor::WalkerBlocked(FVector(1400,-200,28)));
 TestFalse(TEXT("arch tunnel is walkable"),AGymActor::WalkerBlocked(FVector(1400,250,28)));
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FLocomotionAssetsTest,"Rougarou.LocomotionAssets",EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
bool FLocomotionAssetsTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("rebuilt mesh exists"),Mesh))return false;
 const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
 for(const TCHAR* Name:{TEXT("idle"),TEXT("walk"),TEXT("jog"),TEXT("run"),TEXT("start"),TEXT("stop")})
 {
  UAnimSequence* Clip=ULocomotionAnim::LoadClip(*Mesh,Name);
  if(!TestNotNull(Name,Clip))continue;
  TestEqual(TEXT("clips share the rebuilt skeleton"),Clip->GetSkeleton(),Mesh->GetSkeleton());
  TestTrue(TEXT("clip contains motion"),Clip->GetPlayLength()>.2);
  for(const TCHAR* Bone:{TEXT("LeftArm"),TEXT("LeftForeArm"),TEXT("LeftHand"),TEXT("RightUpLeg"),TEXT("RightLeg"),TEXT("RightFoot")})
  {
   const int32 Index=Ref.FindBoneIndex(Bone);
   if(!TestTrue(TEXT("required deform joint exists"),Index!=INDEX_NONE))continue;
   for(float Fraction:{0.f,.25f,.5f,.75f})
   {
    FTransform T;
    Clip->GetBoneTransform(T,FSkeletonPoseBoneIndex(Mesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(Bone)),FAnimExtractContext(double(Clip->GetPlayLength()*Fraction)),false);
    TestFalse(TEXT("baked joint is finite"),T.ContainsNaN());
    TestTrue(TEXT("bake preserves joint offsets"),T.GetTranslation().Equals(Ref.GetRefBonePose()[Index].GetTranslation(),.1));
    TestTrue(TEXT("bake never scales a limb"),T.GetScale3D().Equals(FVector::OneVector,.001));
   }
  }
 }
 return true;
}
#endif
