#pragma once
#include "ClimbSim.h"

// Corey's first-person view and aiming rules, in Unreal centimeters.
struct FClimbView
{
 static FVector ClearEye(const FClimbRoute& Route,FVector Eye,float Margin=18)
 {
  for(const FClimbPanel& P:Route.Panels)
  {
   const float Along=FVector::DotProduct(Eye-P.Origin,P.Up)/100;
   if(Along<-.15 || Along>P.Length+.15)continue;
   const float Depth=FVector::DotProduct(Eye-P.Origin,P.Out);
   if(Depth<Margin && Depth>-60)Eye+=P.Out*(Margin-Depth);
  }
  return Eye;
 }
 static int32 HoldInSight(const FClimbRoute& Route,FVector Eye,FVector Direction)
 {
  int32 Best=INDEX_NONE;float Score=1;
  Direction.Normalize();
  for(int32 I=0;I<Route.Holds.Num();I++)
  {
   const FVector V=Route.Map(Route.Holds[I].Point,6)-Eye;
   const float Along=FVector::DotProduct(V,Direction);
   if(Along<20 || Along>400)continue;
   const float Off=(V-Direction*Along).Size()/FMath::Max(10.f,.08f*Along);
   if(Off<Score){Score=Off;Best=I;}
  }
  return Best;
 }
};
