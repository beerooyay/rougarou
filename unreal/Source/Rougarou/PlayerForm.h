#pragma once
#include "CoreMinimal.h"
#include "HumanRig.h"
class USkeletalMeshComponent;
class FJsonObject;

// Draft joint limits use exactly the climbing solver's anatomical frames.
struct FPlayerForm
{
 bool Open=false,Playing=false,Mirror=true,LockLimits=true,ShowLimits=true;
 int32 Selected=0,Clip=-1;
 float Time=0;
 FString Name=TEXT("rig tuning"),Status;
 FHumanRig Draft;
 TMap<int32,FVector> Angles, AnchorOffsets, IdleAngles;
 static void LoadGroundProfile(USkeletalMeshComponent& Mesh);
 void ConfigureGround(USkeletalMeshComponent& Mesh,const FHumanRig& Rig) const;
 TMap<int32,FQuat> IdleRotations(const FHumanRig& Rig) const;
 void MoveAnchor(USkeletalMeshComponent& Mesh, FVector Offset);
 TArray<FTransform> Frozen;
 TSharedPtr<FJsonObject> Document;
 void Begin(USkeletalMeshComponent& Mesh);
 void End(USkeletalMeshComponent& Mesh);
 void Tick(USkeletalMeshComponent& Mesh,float Delta);
 float Get(int32 Axis) const;
 void Set(USkeletalMeshComponent& Mesh,int32 Axis,float Value);
 float Bound(int32 Axis,bool Maximum) const;
 void SetBound(USkeletalMeshComponent& Mesh,int32 Axis,bool Maximum,float Value);
 void CopyOther(USkeletalMeshComponent& Mesh);
 void ResetJoint(USkeletalMeshComponent& Mesh);
 void Reset();
 bool Defaults(USkeletalMeshComponent& Mesh);
 bool Save(USkeletalMeshComponent& Mesh);
 bool Load(USkeletalMeshComponent& Mesh,const FString& File);
 bool Apply(USkeletalMeshComponent& Mesh,FHumanRig& Live,FHumanRig& Gate,const FString& Destination=FString());
 FString Json(USkeletalMeshComponent& Mesh) const;
 FVector AxisWorld(USkeletalMeshComponent& Mesh,int32 Axis) const;
 static TArray<FString> Versions();
 static FString Folder();
private:
 bool Read(USkeletalMeshComponent& Mesh,const FString& Path);
 int32 Other(USkeletalMeshComponent& Mesh) const;
};
