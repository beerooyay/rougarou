#pragma once
#include "CoreMinimal.h"

// Camera-independent facing and physical acceleration. Units are cm / seconds.
struct FLocomotion
{
 FVector At = FVector(450, 0, 28), Velocity = FVector::ZeroVector;
 float Yaw = PI, TurnRate = 0;
 static constexpr float WalkSpeed = 150, RunSpeed = 380;
 void Step(FVector Wish, bool Run, float Delta, TFunctionRef<FVector(FVector,FVector)> Move)
 {
  float Left = FMath::Clamp(Delta, 0.f, .1f);
  while (Left > SMALL_NUMBER)
  {
   const float Dt = FMath::Min(Left, 1.f/120); Left -= Dt;
   Wish.Z = 0; Wish = Wish.GetClampedToMaxSize(1);
   const FVector Target = Wish * (Run ? RunSpeed : WalkSpeed);
   const bool Braking = Wish.IsNearlyZero() || FVector::DotProduct(Velocity, Target) < 0;
   Velocity = FMath::VInterpConstantTo(Velocity, Target, Dt, Braking ? 1100 : 700);
   const FVector Before = At;
   At = Move(At, Velocity * Dt);
   const FVector Actual = (At-Before)/Dt;
   // Collision must stop the gait as well as the body.
   Velocity.X=Actual.X; Velocity.Y=Actual.Y;
   if (Velocity.Size2D() > 5)
   {
    const float Desired = FMath::Atan2(Velocity.Y,Velocity.X);
    const float Angle = FMath::FindDeltaAngleRadians(Yaw,Desired);
    const float Change = FMath::Clamp(Angle*(1-FMath::Exp(-12*Dt)), -7*Dt, 7*Dt);
    Yaw = FMath::UnwindRadians(Yaw+Change);
    TurnRate = FMath::Lerp(TurnRate,Change/Dt,1-FMath::Exp(-10*Dt));
   }
   else TurnRate *= FMath::Exp(-10*Dt);
  }
 }
};
