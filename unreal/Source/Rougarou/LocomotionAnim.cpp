#include "LocomotionAnim.h"
#include "Animation/AnimInstanceProxy.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimationPoseData.h"
#include "AnimationRuntime.h"
#include "Engine/SkeletalMesh.h"
#include "GarmentPhysics.h"
#include "Components/SkeletalMeshComponent.h"
#include "Animation/Skeleton.h"
#include "Misc/Paths.h"

struct FLocomotionProxy : FAnimInstanceProxy
{
 using FAnimInstanceProxy::FAnimInstanceProxy;
 UAnimSequence* Clips[6] = {};
 UAnimSequence* LandingClip = nullptr;
 float LandingTime = -10.f;
 float Speed=0, Target=0, Phase=0, IdleTime=0, TransitionTime=10, TransitionWeight=0;
 int32 Transition=-1;
 bool Moving=false;
 FGarmentPhysics Garments;
 FTransform World=FTransform::Identity;
 bool Climbing=false;
 bool FormOpen=false;int32 FormClip=-1;float FormTime=0;TArray<FTransform> FormFrozen,FormNeutral;
 TMap<int32,FHumanLimit> FormLimits;TMap<int32,FHumanFrame> FormFrames;bool FormClamp=true;
 TArray<FTransform> ClimbingPose, GroundNeutral;
 TMap<int32,FHumanLimit> GroundLimits;
 TMap<int32,FHumanFrame> GroundFrames;
 TMap<int32,FVector> GroundAnchors;
 TMap<int32,FQuat> GroundIdleRotations, FormIdleRotations;
 float FrameDelta=0;
 bool UseGarmentBones=false;
 float GarmentMotionScale=1;
 virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
 {
  FAnimInstanceProxy::PreUpdate(Instance,Delta);
  const auto* Anim=CastChecked<ULocomotionAnim>(Instance);
  Target=Anim->GroundSpeed;
  FormOpen=Anim->FormOpen;FormClip=Anim->FormClip;FormTime=Anim->FormTime;
  if(FormOpen){FormFrozen=Anim->FormFrozen;FormNeutral=Anim->FormNeutral;FormLimits=Anim->FormLimits;FormFrames=Anim->FormFrames;FormClamp=Anim->FormClamp;}
  if(FormOpen) FormIdleRotations=Anim->FormIdleRotations;
  GroundNeutral=Anim->GroundNeutral;
  GroundLimits=Anim->GroundLimits;
  GroundFrames=Anim->GroundFrames;
  GroundAnchors=Anim->GroundAnchors;
  GroundIdleRotations=Anim->GroundIdleRotations;
  Climbing=Anim->Climbing;
  LandingClip=Anim->LandingClip.Get();
  LandingTime=Anim->LandingTime;
  if(Climbing) ClimbingPose=Anim->ClimbingPose;
  const auto* Component=Instance->GetSkelMeshComponent();
  UseGarmentBones=Anim->ClothEnabled && !Component->GetSkeletalMeshAsset()->HasActiveClothingAssets();
  GarmentMotionScale=Component->GetSkeletalMeshAsset()->GetPathName().Contains(TEXT("PersonalPlayer"))?3.f:1.f;
  World=Component->GetComponentTransform();
  FrameDelta=Delta;
  for(int32 I=0;I<6;I++) Clips[I]=Anim->Clips.IsValidIndex(I)?Anim->Clips[I].Get():nullptr;
 }
 virtual void Update(float Delta) override
 {
  if(Climbing || FormOpen) return;
  const bool Now=Target>8;
  if(Now!=Moving) { Transition=Now?4:5; TransitionTime=0; Moving=Now; }
  TransitionTime+=Delta; IdleTime+=Delta;
  Speed=FMath::Lerp(Speed,Target,1-FMath::Exp(-14*Delta));
  // Stride length increases with speed; the gait clock never resets at a blend boundary.
  Phase=FMath::Fmod(Phase+Delta*FMath::Max(Speed,20.f)/FMath::Lerp(125.f,235.f,FMath::Clamp(Speed/380,0.f,1.f)),1.f);
  // Brief authored weight shifts into/out of locomotion, feathered on both ends.
  TransitionWeight=Transition>=0 ? .35f*FMath::Sin(PI*FMath::Clamp(TransitionTime/.4f,0.f,1.f)) : 0;
 }
 void Sample(FPoseContext& Pose,int32 Index,float Time)
 {
  Pose.ResetToRefPose();
  if(Clips[Index])
  {
   FAnimationPoseData Data(Pose);
   Clips[Index]->GetAnimationPose(Data,FAnimExtractContext(static_cast<double>(Time),false));
  }
 }
 virtual bool Evaluate(FPoseContext& Output) override
 {
  if(FormOpen)
  {
   Output.ResetToRefPose();
   if(FormClip>=0 && FormClip<6)Sample(Output,FormClip,FormTime);
   else if(FormClip==6 && LandingClip){FAnimationPoseData Data(Output);LandingClip->GetAnimationPose(Data,FAnimExtractContext(static_cast<double>(FormTime),false));}
   const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
   for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
   {
    const int32 I=Bones.MakeMeshPoseIndex(Index).GetInt();
    if(FormClip<0 && FormFrozen.IsValidIndex(I))Output.Pose[Index]=FormFrozen[I];
    if(FormClip>=0 && FormFrozen.IsValidIndex(I)) Output.Pose[Index].AddToTranslation(FormFrozen[I].GetTranslation()-Bones.GetRefPoseTransform(Index).GetTranslation());

   }
   if(FormClip==0) BalanceIdle(Output,1);
   if(FormClip<=0) RelaxShoulders(Output,FormClip==0?1.f:0.f);
   if(FormClip==0) ApplyIdleRotations(Output,FormIdleRotations,1);
   if(FormClip>=0 && FormClamp) ClampPose(Output,FormNeutral,FormLimits,FormFrames);
   return true;
  }
  if(Climbing)
  {
   Output.ResetToRefPose();
   const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
   for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
   {
    const int32 MeshIndex=Bones.MakeMeshPoseIndex(Index).GetInt();
    if(ClimbingPose.IsValidIndex(MeshIndex))Output.Pose[Index]=ClimbingPose[MeshIndex];
   }
   ApplyLanding(Output);
   ApplyGarments(Output);
   return true;
  }
  const float Speeds[]={0,150,260,380};
  int32 Low=0;
  while(Low<2 && Speed>Speeds[Low+1]) Low++;
  const int32 High=Low+1;
  float Alpha=FMath::Clamp((Speed-Speeds[Low])/(Speeds[High]-Speeds[Low]),0.f,1.f);
  Alpha=Alpha*Alpha*(3-2*Alpha);
  auto Time=[&](int32 I){ return Clips[I] ? (I==0 ? FMath::Fmod(IdleTime,Clips[I]->GetPlayLength()) : Phase*Clips[I]->GetPlayLength()) : 0.f; };
  if(Alpha <= 0) Sample(Output,Low,Time(Low));
  else if(Alpha >= 1) Sample(Output,High,Time(High));
  else
  {
   FPoseContext A(Output), B(Output);
   Sample(A,Low,Time(Low));
   Sample(B,High,Time(High));
   FAnimationPoseData AD(A), BD(B), Result(Output);
   FAnimationRuntime::BlendTwoPosesTogether(AD,BD,1-Alpha,Result);
  }
  if(Transition>=0 && TransitionWeight>KINDA_SMALL_NUMBER && Clips[Transition])
  {
   FPoseContext Shift(Output);
   Sample(Shift,Transition,FMath::Min(TransitionTime,Clips[Transition]->GetPlayLength()));
   FAnimationPoseData ShiftData(Shift), Result(Output);
   FAnimationRuntime::BlendTwoPosesTogetherInPlace(Result,ShiftData,1-TransitionWeight);
  }
  const float IdleWeight=1-FMath::SmoothStep(0.f,65.f,Speed);
  const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
  for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
   if(const FVector* Offset=GroundAnchors.Find(Bones.MakeMeshPoseIndex(Index).GetInt())) Output.Pose[Index].AddToTranslation(*Offset);
  BalanceIdle(Output,IdleWeight);
  RelaxShoulders(Output,IdleWeight);
  ApplyIdleRotations(Output,GroundIdleRotations,IdleWeight);
  ClampPose(Output,GroundNeutral,GroundLimits,GroundFrames);
  ApplyGarments(Output);
  Output.Pose.NormalizeRotations();
  return true;
 }
 // The authored clip supplies the brace while the procedural drop keeps the
 // pelvis trajectory and both feet on the mats.
 void ApplyLanding(FPoseContext& Output)
 {
  constexpr float Impact = .3f;
  if (!LandingClip || LandingTime <= -Impact || LandingTime >= .65f) return;
  const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
  const int32 SpineIndex = Bones.GetSkeletonAsset()->GetReferenceSkeleton().FindBoneIndex(TEXT("Spine"));
  if (SpineIndex == INDEX_NONE) return;
  const FCompactPoseBoneIndex Spine = Bones.GetCompactPoseIndexFromSkeletonIndex(SpineIndex);
  if (Spine == INDEX_NONE) return;
  FPoseContext Authored(Output);
  Authored.ResetToRefPose();
  FAnimationPoseData Data(Authored);
  const double SampleTime = FMath::Min(LandingTime + Impact, LandingClip->GetPlayLength());
  LandingClip->GetAnimationPose(Data, FAnimExtractContext(SampleTime, false));
  const float Weight = .8f * FMath::SmoothStep(-Impact, -.06f, LandingTime)
   * (1.f - FMath::SmoothStep(.18f, .65f, LandingTime));
  for (FCompactPoseBoneIndex Index : Output.Pose.ForEachBoneIndex())
  {
   if (Index == Spine || Bones.BoneIsChildOf(Index, Spine))
   {
    const FQuat Rotation = FQuat::Slerp(Output.Pose[Index].GetRotation(), Authored.Pose[Index].GetRotation(), Weight);
    Output.Pose[Index].SetRotation(Rotation.GetNormalized());
   }
  }
 }
 void ApplyIdleRotations(FPoseContext& Output,const TMap<int32,FQuat>& Rotations,float Weight)
 {
  if(Weight<=0||Rotations.IsEmpty()) return;
  const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
  for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
   if(const FQuat* Offset=Rotations.Find(Bones.MakeMeshPoseIndex(Index).GetInt()))
    Output.Pose[Index].SetRotation((Output.Pose[Index].GetRotation()*FQuat::Slerp(FQuat::Identity,*Offset,Weight)).GetNormalized());
 }
 void ClampPose(FPoseContext& Output,const TArray<FTransform>& NeutralPose,
  const TMap<int32,FHumanLimit>& Limits,const TMap<int32,FHumanFrame>& Frames)
 {
  const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
  for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
  {
   const int32 Bone=Bones.MakeMeshPoseIndex(Index).GetInt();
   const auto* Limit=Limits.Find(Bone);
   const auto* Frame=Frames.Find(Bone);
   if(!Limit||!Frame||!NeutralPose.IsValidIndex(Bone)) continue;
   const FQuat Neutral=NeutralPose[Bone].GetRotation();
   Output.Pose[Index].SetRotation((Neutral*FHumanRig::Clamp(Neutral.Inverse()*Output.Pose[Index].GetRotation(),*Frame,*Limit)).GetNormalized());
  }
 }
 // Average the authored idle with its reflection in the reference skeleton.
 // Apply player adjustments afterwards so deliberately asymmetric edits survive.
 void BalanceIdle(FPoseContext& Output,float Weight)
 {
  if(Weight<=0) return;
  const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
  const auto& Ref=Bones.GetSkeletonAsset()->GetReferenceSkeleton();
  auto IndexOf=[&](const FString& Name)
  {
   const int32 Index=Ref.FindBoneIndex(*Name);
   return Index==INDEX_NONE?FCompactPoseBoneIndex(INDEX_NONE):Bones.GetCompactPoseIndexFromSkeletonIndex(Index);
  };
  const auto LeftArm=IndexOf(TEXT("LeftArm")),RightArm=IndexOf(TEXT("RightArm"));
  if(LeftArm==INDEX_NONE||RightArm==INDEX_NONE) return;
  FPoseContext Reference(Output);
  Reference.ResetToRefPose();
  FCSPose<FCompactPose> Space;
  Space.InitPose(Reference.Pose);
  const FVector Side=(Space.GetComponentSpaceTransform(LeftArm).GetLocation()-Space.GetComponentSpaceTransform(RightArm).GetLocation()).GetSafeNormal();
  auto MirrorPair=[&](FCompactPoseBoneIndex Left,FCompactPoseBoneIndex Right)
  {
   if(Left==INDEX_NONE||Right==INDEX_NONE) return;
   const FQuat RefLeft=Reference.Pose[Left].GetRotation(),RefRight=Reference.Pose[Right].GetRotation();
   const FQuat WorldLeft=Space.GetComponentSpaceTransform(Left).GetRotation(),WorldRight=Space.GetComponentSpaceTransform(Right).GetRotation();
   const FQuat A=Output.Pose[Left].GetRotation(),B=Output.Pose[Right].GetRotation();
   auto Reflected=[&](FQuat Delta,FQuat From,FQuat To)
   {
    const FVector Axis=From.RotateVector(FVector(Delta.X,Delta.Y,Delta.Z));
    const FVector Mirrored=To.UnrotateVector(2*Side*FVector::DotProduct(Axis,Side)-Axis);
    return FQuat(Mirrored.X,Mirrored.Y,Mirrored.Z,Delta.W);
   };
   const FQuat TargetLeft=RefLeft*Reflected(RefRight.Inverse()*B,WorldRight,WorldLeft);
   const FQuat TargetRight=RefRight*Reflected(RefLeft.Inverse()*A,WorldLeft,WorldRight);
   Output.Pose[Left].SetRotation(FQuat::Slerp(A,TargetLeft,Weight*.5f).GetNormalized());
   Output.Pose[Right].SetRotation(FQuat::Slerp(B,TargetRight,Weight*.5f).GetNormalized());
  };
  for(const TCHAR* Joint:{TEXT("Spine"),TEXT("Spine1"),TEXT("Spine2"),TEXT("Neck"),TEXT("Head")})
  {
   const auto Index=IndexOf(Joint);
   MirrorPair(Index,Index);
  }
  for(const TCHAR* Joint:{TEXT("Shoulder"),TEXT("Arm"),TEXT("ForeArm"),TEXT("Hand")})
   MirrorPair(IndexOf(FString(TEXT("Left"))+Joint),IndexOf(FString(TEXT("Right"))+Joint));
  for(const TCHAR* Digit:{TEXT("Thumb"),TEXT("Index"),TEXT("Middle"),TEXT("Ring"),TEXT("Pinky")})
   for(int32 Segment=1;Segment<=3;++Segment)
    MirrorPair(IndexOf(FString::Printf(TEXT("LeftHand%s%d"),Digit,Segment)),IndexOf(FString::Printf(TEXT("RightHand%s%d"),Digit,Segment)));
 }
 void RelaxShoulders(FPoseContext& Output,float IdleWeight)
 {
  const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
  const auto& Skeleton=Bones.GetSkeletonAsset()->GetReferenceSkeleton();
  auto IndexOf=[&](const TCHAR* Name)
  {
   const int32 SkeletonIndex=Skeleton.FindBoneIndex(Name);
   return SkeletonIndex==INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.GetCompactPoseIndexFromSkeletonIndex(SkeletonIndex);
  };
  const FCompactPoseBoneIndex Hips=IndexOf(TEXT("Hips")),Head=IndexOf(TEXT("Head"));
  if(Hips==INDEX_NONE||Head==INDEX_NONE)return;
  FCSPose<FCompactPose> Space;
  Space.InitPose(Output.Pose);
  const FVector Up=(Space.GetComponentSpaceTransform(Head).GetLocation()-Space.GetComponentSpaceTransform(Hips).GetLocation()).GetSafeNormal();
  const float Drop=FMath::DegreesToRadians(5.f+2.f*IdleWeight);
  for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})
  {
   const FCompactPoseBoneIndex Collar=IndexOf(*(FString(Side)+TEXT("Shoulder"))),Arm=IndexOf(*(FString(Side)+TEXT("Arm")));
   if(Collar==INDEX_NONE||Arm==INDEX_NONE)continue;
   const FTransform Here=Space.GetComponentSpaceTransform(Collar);
   const FVector Along=(Space.GetComponentSpaceTransform(Arm).GetLocation()-Here.GetLocation()).GetSafeNormal();
   const FQuat Turn=FQuat::FindBetweenNormals(Along,(Along*FMath::Cos(Drop)-Up*FMath::Sin(Drop)).GetSafeNormal());
   const FQuat Parent=Space.GetComponentSpaceTransform(Bones.GetParentBoneIndex(Collar)).GetRotation();
   Output.Pose[Collar].SetRotation((Parent.Inverse()*Turn*Here.GetRotation()).GetNormalized());
   const auto Elbow=IndexOf(*(FString(Side)+TEXT("ForeArm")));
   if(IdleWeight<=0||Elbow==INDEX_NONE) continue;
   const auto ArmWorld=Space.GetComponentSpaceTransform(Arm);
   const FVector ArmDown=Turn.RotateVector((Space.GetComponentSpaceTransform(Elbow).GetLocation()-ArmWorld.GetLocation()).GetSafeNormal());
   const FVector Out=FVector::VectorPlaneProject(Along,Up).GetSafeNormal();
   const FVector ClearDirection=(ArmDown+Out*(.19f*IdleWeight)).GetSafeNormal();
   const FQuat Clear=FQuat::FindBetweenNormals(ArmDown,ClearDirection);
   Output.Pose[Arm].SetRotation((Here.GetRotation().Inverse()*Turn.Inverse()*Clear*Turn*ArmWorld.GetRotation()).GetNormalized());
  }
 }
 void ApplyGarments(FPoseContext& Output)
 {
  if (!UseGarmentBones)
  {
   Garments.Reset();
   return;
  }
  const FBoneContainer& Bones = Output.Pose.GetBoneContainer();
  const auto& Skeleton = Bones.GetSkeletonAsset()->GetReferenceSkeleton();
  auto IndexOf = [&](const TCHAR* Name)
  {
   const int32 SkeletonIndex=Skeleton.FindBoneIndex(Name);
   return SkeletonIndex==INDEX_NONE ? FCompactPoseBoneIndex(INDEX_NONE) : Bones.GetCompactPoseIndexFromSkeletonIndex(SkeletonIndex);
  };
  FCompactPoseBoneIndex Tips[8];
  for (int32 I = 0; I < 8; ++I)
  {
   Tips[I] = IndexOf(FGarmentPhysics::Bone(I));
   if (Tips[I] == INDEX_NONE) return;
  }
  FCSPose<FCompactPose> Space;
  Space.InitPose(Output.Pose);
  auto Point = [&](FCompactPoseBoneIndex Index)
  {
   return World.TransformPosition(Space.GetComponentSpaceTransform(Index).GetLocation());
  };
  FVector Goals[8];
  for (int32 I = 0; I < 8; ++I) Goals[I] = Point(Tips[I]);
  const TArray<FGarmentCapsule> Body = {
   {Point(IndexOf(TEXT("Hips"))), Point(IndexOf(TEXT("Spine2"))), 14},
   {Point(IndexOf(TEXT("LeftUpLeg"))), Point(IndexOf(TEXT("LeftLeg"))), 8},
   {Point(IndexOf(TEXT("RightUpLeg"))), Point(IndexOf(TEXT("RightLeg"))), 8},
   {Point(IndexOf(TEXT("LeftArm"))), Point(IndexOf(TEXT("LeftForeArm"))), 5},
   {Point(IndexOf(TEXT("RightArm"))), Point(IndexOf(TEXT("RightForeArm"))), 5}
  };
  Garments.Step(Goals, Body, FrameDelta, true,FVector::ZeroVector,GarmentMotionScale);
  for (int32 I = 0; I < 8; ++I)
  {
   const auto Parent = Bones.GetParentBoneIndex(Tips[I]);
   Output.Pose[Tips[I]].AddToTranslation(Space.GetComponentSpaceTransform(Parent)
    .InverseTransformVector(World.InverseTransformVector(Garments.Offset[I])));
  }
 }

};
ULocomotionAnim::ULocomotionAnim(){ bUseMultiThreadedAnimationUpdate=false; }
namespace
{
 const TCHAR* const ClipNames[] = {TEXT("idle"), TEXT("walk"), TEXT("jog"), TEXT("run"), TEXT("start"), TEXT("stop")};
}

UAnimSequence* ULocomotionAnim::LoadClip(const USkeletalMesh& Mesh, const TCHAR* Name)
{
 const FString Folder = FPaths::GetPath(Mesh.GetOutermost()->GetName());
 return LoadObject<UAnimSequence>(nullptr, *FString::Printf(TEXT("%s/%s.%s"), *Folder, Name, Name), nullptr, LOAD_NoWarn);
}

USkeletalMesh* ULocomotionAnim::LoadPlayerMesh()
{
 // -clothplayer: the Blaize character split into body, tee and shorts, with the outfit on Chaos Cloth
 if(FParse::Param(FCommandLine::Get(),TEXT("clothplayer")))
 {
  auto* Cloth=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Local/BlaizeClothPlayer/climber.climber"),nullptr,LOAD_NoWarn);
  bool Complete=Cloth && Cloth->HasActiveClothingAssets();
  if(Cloth)for(const TCHAR* Name:ClipNames){const auto* Clip=LoadClip(*Cloth,Name);Complete &= Clip && Clip->GetSkeleton()==Cloth->GetSkeleton();}
  if(Complete)return Cloth;
  UE_LOG(LogTemp,Warning,TEXT("-clothplayer: BlaizeClothPlayer is missing its cloth or clips; using the default player"));
 }
 // Import review is opt-in until both native motion and contact checks pass.
 if(FParse::Param(FCommandLine::Get(),TEXT("latestplayer")) || FParse::Param(FCommandLine::Get(),TEXT("blaizeplayer")) || FParse::Param(FCommandLine::Get(),TEXT("blaize2player")))
 {
  // -shoulderv2 swaps in the second raised-arm weight pass on the same character, for comparison
  const TCHAR* Package=FParse::Param(FCommandLine::Get(),TEXT("latestplayer")) ? (FParse::Param(FCommandLine::Get(),TEXT("shoulderv2")) ? TEXT("/Game/Local/Blaize2ShoulderPlayerV2/climber.climber") : TEXT("/Game/Local/Blaize2ShoulderPlayer/climber.climber")) : FParse::Param(FCommandLine::Get(),TEXT("blaize2player")) ? TEXT("/Game/Local/Blaize2Player/climber.climber") : TEXT("/Game/Local/BlaizePlayer/climber.climber");
  auto* Blaize=LoadObject<USkeletalMesh>(nullptr,Package,nullptr,LOAD_NoWarn);
  bool Complete=Blaize && !Blaize->HasActiveClothingAssets();
  if(Blaize)for(const TCHAR* Name:ClipNames)
  {
   const auto* Clip=LoadClip(*Blaize,Name);
   Complete &= Clip && Clip->GetSkeleton()==Blaize->GetSkeleton();
  }
  if(Complete)return Blaize;
 }
 const bool Experimental=FParse::Param(FCommandLine::Get(),TEXT("garmentcandidate")) || FParse::Param(FCommandLine::Get(),TEXT("modularplayer"));
 if(!Experimental && !FParse::Param(FCommandLine::Get(),TEXT("coreybaseline")))
 {
  auto* Personal=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Local/PersonalPlayer/climber.climber"),nullptr,LOAD_NoWarn);
  bool Complete=Personal && !Personal->HasActiveClothingAssets();
  if(Personal)for(const TCHAR* Name:{TEXT("LeftSoleContact"),TEXT("RightSoleContact"),TEXT("PlayerEyes")})
   Complete &= Personal->GetRefSkeleton().FindBoneIndex(Name)>=0;
  if(Personal)for(const TCHAR* Name:ClipNames)
  {
   const auto* Clip=LoadClip(*Personal,Name);
   Complete &= Clip && Clip->GetSkeleton()==Personal->GetSkeleton();
  }
  if(Complete)return Personal;
 }
 // Preserve the original CC4 player as a fallback and explicit comparison.
 if(!FParse::Param(FCommandLine::Get(),TEXT("garmentcandidate")) && !FParse::Param(FCommandLine::Get(),TEXT("modularplayer")))
  return LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Local/CoreyPlayer/climber.climber"));
 const TCHAR* ModularPath=FParse::Param(FCommandLine::Get(),TEXT("garmentcandidate"))
  ?TEXT("/Game/Local/ModularPlayerCandidate/climber.climber"):TEXT("/Game/Local/ModularPlayer/climber.climber");
 if(auto* Modular=LoadObject<USkeletalMesh>(nullptr,ModularPath,nullptr,LOAD_NoWarn))
 {
  bool Complete=Modular->HasActiveClothingAssets() && Modular->GetMeshClothingAssets().Num()==1;
  for(const TCHAR* Name:ClipNames)
  {
   const auto* Clip=LoadClip(*Modular,Name);
   Complete &= Clip && Clip->GetSkeleton()==Modular->GetSkeleton();
  }
  if(Complete)return Modular;
 }
 auto* Candidate = LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Local/CoreyCloth/climber.climber"), nullptr, LOAD_NoWarn);
 if (Candidate && Candidate->HasActiveClothingAssets() && Candidate->GetMeshClothingAssets().Num() == 2)
 {
  bool Complete = true;
  for (const TCHAR* Name : ClipNames)
  {
   const auto* Clip = LoadClip(*Candidate, Name);
   Complete &= Clip && Clip->GetSkeleton() == Candidate->GetSkeleton();
  }
  if (Complete) return Candidate;
 }
 return LoadObject<USkeletalMesh>(nullptr, TEXT("/Game/Local/CoreyPlayer/climber.climber"), nullptr, LOAD_NoWarn);
}

void ULocomotionAnim::NativeInitializeAnimation()
{
 Super::NativeInitializeAnimation();
 Clips.Reset();
 LandingClip = nullptr;
 if (const auto* Mesh = GetSkelMeshComponent()->GetSkeletalMeshAsset())
 {
  for (const TCHAR* Name : ClipNames) Clips.Add(LoadClip(*Mesh, Name));
  if (UAnimSequence* Relaxed = LoadClip(*Mesh, TEXT("idle_relaxed"));
      Relaxed && Relaxed->GetSkeleton() == Mesh->GetSkeleton())
   Clips[0] = Relaxed;
  if (UAnimSequence* Landing = LoadClip(*Mesh, TEXT("fall_land"));
      Landing && Landing->GetSkeleton() == Mesh->GetSkeleton())
   LandingClip = Landing;
 }
}
FAnimInstanceProxy* ULocomotionAnim::CreateAnimInstanceProxy(){ return new FLocomotionProxy(this); }
void ULocomotionAnim::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){ delete Proxy; }
