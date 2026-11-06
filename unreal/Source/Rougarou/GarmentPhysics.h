#pragma once
#include "CoreMinimal.h"

struct FGarmentCapsule
{
 FVector A,B;float Radius=10;
};
// Eight coarse cloth masses, attached to skinned garment tips. Simulation is
// in world centimeters; only the final displacement is converted to bind space.
// This gives hem/sleeve/cuff inertia and body collision without scaling limbs.
struct FGarmentPhysics
{
 FVector Offset[8]={}, Velocity[8]={}, Last[8]={}, LastVelocity[8]={};
 static const TCHAR* Bone(int32 I)
 {
  static const TCHAR* Names[]={TEXT("HemFront"),TEXT("HemBack"),TEXT("HemLeft"),TEXT("HemRight"),TEXT("SleeveLeft"),TEXT("SleeveRight"),TEXT("CuffLeft"),TEXT("CuffRight")};
  return Names[I];
 }
 bool Initialized=false;
 void Reset(){Initialized=false;for(int32 I=0;I<8;I++)Offset[I]=Velocity[I]=LastVelocity[I]=FVector::ZeroVector;}
 void Step(const FVector* Goals,const TArray<FGarmentCapsule>& Body,float Delta,bool Enabled,FVector Wind=FVector::ZeroVector,float MotionScale=1.f)
 {
  if(!Enabled){Reset();return;}
  for(int32 I=0;I<8;I++)if(Initialized&&FVector::DistSquared(Goals[I],Last[I])>120*120){Reset();break;}
  if(!Initialized){for(int32 I=0;I<8;I++)Last[I]=Goals[I];Initialized=true;return;}
  if(Delta<=SMALL_NUMBER)return;
  const float Dt=FMath::Clamp(Delta,.001f,.05f);
  for(int32 I=0;I<8;I++)
  {
   const FVector TargetVelocity=(Goals[I]-Last[I])/Dt;
   const FVector Accel=((TargetVelocity-LastVelocity[I])/Dt).GetClampedToMaxSize(3500);
   const float Max=(I<4?1.f:I<6?.5f:.7f)*FMath::Clamp(MotionScale,1.f,3.f);
   const FVector Force=-Accel*.45f+Wind*30+FVector(0,0,-100);
   for(float Left=Dt;Left>SMALL_NUMBER;)
   {
    const float H=FMath::Min(Left,1.f/120);Left-=H;
    Velocity[I]+=(Force-Offset[I]*130-Velocity[I]*20)*H;
    Offset[I]=(Offset[I]+Velocity[I]*H).GetClampedToMaxSize(Max);
    FVector At=Goals[I]+Offset[I];
    for(const auto& Capsule:Body)
    {
     const FVector Axis=Capsule.B-Capsule.A;
     const float T=FMath::Clamp(FVector::DotProduct(At-Capsule.A,Axis)/FMath::Max(1.,Axis.SizeSquared()),0.,1.);
     const FVector Near=Capsule.A+Axis*T, Gap=At-Near;
     const float Distance=Gap.Size(),Radius=Capsule.Radius+1;
     if(Distance<Radius&&Distance>.01f)
     {
      const FVector Normal=Gap/Distance;At=Near+Normal*Radius;
      const float Inward=FVector::DotProduct(Velocity[I],Normal);if(Inward<0)Velocity[I]-=Normal*Inward;
     }
    }
    Offset[I]=(At-Goals[I]).GetClampedToMaxSize(Max);
   }
   Last[I]=Goals[I];LastVelocity[I]=TargetVelocity;
  }
 }
};
