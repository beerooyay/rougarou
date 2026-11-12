#pragma once
#include "CoreMinimal.h"
#include "HumanRig.h"
#include "Animation/AnimInstance.h"
#include "LocomotionAnim.generated.h"

// Native graph: synchronized gait samples with continuous speed weights.
UCLASS(Transient)
class ROUGAROU_API ULocomotionAnim : public UAnimInstance
{
 GENERATED_BODY()
public:
 ULocomotionAnim();
 UPROPERTY(Transient) TArray<TObjectPtr<UAnimSequence>> Clips;
 UPROPERTY(Transient) TObjectPtr<UAnimSequence> LandingClip;
 float LandingTime = -10.f;
 bool Climbing=false;
 bool FormOpen=false;
 int32 FormClip=-1;
 float FormTime=0;
 TArray<FTransform> FormFrozen, FormNeutral;
 TMap<int32,FHumanLimit> FormLimits;
 TMap<int32,FHumanFrame> FormFrames;
 bool FormClamp=true;
 TArray<FTransform> GroundNeutral;
 TMap<int32,FHumanLimit> GroundLimits;
 TMap<int32,FHumanFrame> GroundFrames;
 TMap<int32,FVector> GroundAnchors;
 TMap<int32,FQuat> GroundIdleRotations, FormIdleRotations;
 TArray<FTransform> ClimbingPose;
 float GroundSpeed = 0;
 bool ClothEnabled=true;
 static USkeletalMesh* LoadPlayerMesh();
 static UAnimSequence* LoadClip(const USkeletalMesh& Mesh, const TCHAR* Name);
 virtual void NativeInitializeAnimation() override;
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
