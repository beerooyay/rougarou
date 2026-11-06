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
 float Speed=0, Target=0, Phase=0, IdleTime=0, TransitionTime=10, TransitionWeight=0;
 int32 Transition=-1;
 bool Moving=false;
 FGarmentPhysics Garments;
 FTransform World=FTransform::Identity;
 bool Climbing=false;
 TArray<FTransform> ClimbingPose;
 float FrameDelta=0;
 bool UseGarmentBones=false;
 float GarmentMotionScale=1;
 virtual void PreUpdate(UAnimInstance* Instance, float Delta) override
 {
  FAnimInstanceProxy::PreUpdate(Instance,Delta);
  const auto* Anim=CastChecked<ULocomotionAnim>(Instance);
  Target=Anim->GroundSpeed;
  Climbing=Anim->Climbing;
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
  if(Climbing) return;
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
  if(Climbing)
  {
   Output.ResetToRefPose();
   const FBoneContainer& Bones=Output.Pose.GetBoneContainer();
   for(FCompactPoseBoneIndex Index:Output.Pose.ForEachBoneIndex())
   {
    const int32 MeshIndex=Bones.MakeMeshPoseIndex(Index).GetInt();
    if(ClimbingPose.IsValidIndex(MeshIndex))Output.Pose[Index]=ClimbingPose[MeshIndex];
   }
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
  ApplyGarments(Output);
  Output.Pose.NormalizeRotations();
  return true;
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
 // Import review is opt-in until both native motion and contact checks pass.
 if(FParse::Param(FCommandLine::Get(),TEXT("latestplayer")) || FParse::Param(FCommandLine::Get(),TEXT("blaizeplayer")) || FParse::Param(FCommandLine::Get(),TEXT("blaize2player")))
 {
  const TCHAR* Package=FParse::Param(FCommandLine::Get(),TEXT("latestplayer")) ? TEXT("/Game/Local/Blaize2ShoulderPlayer/climber.climber") : FParse::Param(FCommandLine::Get(),TEXT("blaize2player")) ? TEXT("/Game/Local/Blaize2Player/climber.climber") : TEXT("/Game/Local/BlaizePlayer/climber.climber");
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
 if (const auto* Mesh = GetSkelMeshComponent()->GetSkeletalMeshAsset())
  for (const TCHAR* Name : ClipNames) Clips.Add(LoadClip(*Mesh, Name));
}
FAnimInstanceProxy* ULocomotionAnim::CreateAnimInstanceProxy(){ return new FLocomotionProxy(this); }
void ULocomotionAnim::DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy){ delete Proxy; }
