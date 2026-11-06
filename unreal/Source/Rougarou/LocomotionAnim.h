#pragma once
#include "CoreMinimal.h"
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
 bool Climbing=false;
 TArray<FTransform> ClimbingPose;
 float GroundSpeed = 0;
 bool ClothEnabled=true;
 static USkeletalMesh* LoadPlayerMesh();
 static UAnimSequence* LoadClip(const USkeletalMesh& Mesh, const TCHAR* Name);
 virtual void NativeInitializeAnimation() override;
 virtual FAnimInstanceProxy* CreateAnimInstanceProxy() override;
 virtual void DestroyAnimInstanceProxy(FAnimInstanceProxy* Proxy) override;
};
