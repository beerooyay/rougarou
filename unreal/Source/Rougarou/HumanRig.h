#pragma once

#include "CoreMinimal.h"
#include "ClimbSim.h"

struct FReferenceSkeleton;

// Ported from Corey's figure.js / limits.js at 3d30a18. These are mechanical
// joint constraints, not a claim that individual human ROM has been measured.
struct FHumanLimit
{
 float Forward=180, Back=180, Out=180, In=180, TwistMin=-180, TwistMax=180;
 float Reach(float Phi) const;
};
struct FHumanFrame
{
 FVector T=FVector::DownVector, F=FVector::RightVector, S=FVector::ForwardVector;
 float Side=1;
 // hips and shoulders measure twist the anatomical way: flexion, then the swing out, then the turn about the bone
 bool Euler=false;
};
struct FHumanAngles { float Swing=0, Bearing=0, Twist=0; };
struct FHumanMix { float Waist=.2583f, Spine=.6381f, Neck=.525f, Scapula=.28f; };
struct FHumanGoals
{
 FVector Roots[4], Ends[4], Poles[4], Along[4], Out[4];
 FString Grips[4];
 TOptional<FVector> ShoeSurfaces[2], HandSurfaces[2];
 bool Held[4]={true,true,true,true};
 TArray<FClimbPanel> Panels;
 FVector Look=FVector::ZeroVector;
 // the route's holds as resin domes; Owners marks the limbs on each (bit per limb) so a limb may touch its own hold
 struct FDome{FVector C,N;float R=0,H=0;uint8 Owners=0;};
 TArray<FDome> Domes;
 static FHumanGoals FromSim(const FClimbSim& Sim,float Gap=30,float Inside=0,float HipDepth=-1,float ChestDepth=-1);
};
struct FHumanPose
{
 TArray<FTransform> Local;
 FTransform World=FTransform::Identity;
 FVector Eye=FVector::ZeroVector;
 FVector Ends[4], Bends[4];
 float Miss[4]={}, Comfort=0, MinClearance=MAX_flt, MaxLimitViolation=0;
 FVector Shift=FVector::ZeroVector;
 bool Finite=true;
 float PelvisPitch=0, PelvisYaw=0;
 float Cost(const FHumanGoals& Goals) const;
};

class FHumanRig
{
public:
 bool Initialize(const FReferenceSkeleton& Ref,const FString& Profile);
 bool Ready() const{return Rest.Num()>0;}
 void Reset();
 FHumanPose Solve(const FHumanGoals& Goals,float Delta=0);
 // The searching solver Codex layered up; kept for comparison, off by default.
 FHumanPose SolveLegacy(const FHumanGoals& Goals,float Delta=0);
 bool UseLegacy=false;
 // Newton steps for a pose with no history (the move gate uses fewer)
 int32 ColdIterations=8;
 // start the next cold solve from another rig's body placement (the gate starts from the live climber)
 void WarmFrom(const FHumanRig& Other){Reset();Shift=Other.Shift;Pitch=Other.Pitch;Yaw=Other.Yaw;}
 FHumanMix Mix;
 bool LimitsEnabled=true;
 float MaxShift=12, JointRate=5, SwivelRate=2;
 static FHumanAngles Measure(FQuat Relative,const FHumanFrame& Frame);
 static FQuat Clamp(FQuat Relative,const FHumanFrame& Frame,const FHumanLimit& Limit);
 static float Violation(FQuat Relative,const FHumanFrame& Frame,const FHumanLimit& Limit);
 static FString ProfilePath();
 const TMap<FName,FName>& BoneMap() const{return Aliases;}
private:
 TArray<FName> Names;
 TArray<int32> Parents;
 TArray<TArray<int32>> Branches;
 TArray<bool> SlowJoints;
 TArray<FTransform> Rest, RestSpace, Neutral, NeutralSpace;
 TMap<FName,int32> Indices;
 TMap<FName,FName> Aliases;
 TMap<int32,FHumanLimit> Limits;
 TMap<int32,FHumanFrame> Frames;
 TArray<FQuat> Previous;
 FTransform PreviousWorld=FTransform::Identity;
 FVector RefUp, RefSide, RefFront, RefPelvis;
 FVector Shift=FVector::ZeroVector, LastPelvis=FVector::ZeroVector;
 FVector PreviousPoles[4];
 FHumanGoals SmoothGoals;
 FVector Push=FVector::ZeroVector;
 float Swivel[4]={};
 bool HasHistory=false;
 float Pitch=0, Yaw=0;
 int32 Bone(const TCHAR* Name) const;
 void Compose(const TArray<FTransform>& Local,TArray<FTransform>& Space) const;
 FHumanPose Pass(const FHumanGoals& Goals,FVector Offset,const float* Swivels,bool Search,float* Chosen,float PelvisPitch=0,float PelvisYaw=0,bool FitSoles=false);
 void PoseFingers(const FHumanGoals& Goals,FHumanPose& Pose) const;
 FQuat LimitLocal(int32 Index,FQuat Local) const;
};
