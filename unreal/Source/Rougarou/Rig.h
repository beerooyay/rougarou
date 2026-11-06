#pragma once

#include "CoreMinimal.h"

struct FRig
{
	static FVector Knee(FVector Side, FVector Up, FVector Out, float Sign, float Height)
	{
		const float Lift = FMath::Clamp((Height + 20) / 40, 0.f, 1.f);
		return Side * Sign * FMath::Lerp(1.f, 0.3f, Lift) + Up * FMath::Lerp(0.2f, 1.f, Lift) - Out * 0.25f;
	}

	static FQuat Orient(FQuat Rest, FVector Axis, FVector Front, FVector Direction, FVector Facing)
	{
		const FQuat From = FRotationMatrix::MakeFromXY(Axis, Front).ToQuat();
		const FQuat To = FRotationMatrix::MakeFromXY(Direction, Facing).ToQuat();
		return (To * From.Inverse() * Rest).GetNormalized();
	}

	static void Limit(FVector Start, FVector End, FPlane Plane, float Margin, FVector& Bend)
	{
		if (Plane.PlaneDot(Bend) >= Margin) return;
		const FVector Axis = (End - Start).GetSafeNormal();
		const FVector Center = Start + Axis * FVector::DotProduct(Bend - Start, Axis);
		const FVector Offset = Bend - Center;
		const float Radius = Offset.Size();
		FVector Normal = FVector::VectorPlaneProject(Plane.GetNormal(), Axis);
		const float Length = Normal.Size();
		if (Radius < 0.001f || Length < 0.001f) return;
		Normal /= Length;
		const float Needed = (Margin - Plane.PlaneDot(Center)) / (Radius * Length);
		const float Across = FMath::Clamp(Needed, -1.f, 1.f);
		FVector Side = FVector::VectorPlaneProject(Offset, Normal).GetSafeNormal();
		if (Side.IsNearlyZero()) Side = (Axis ^ Normal).GetSafeNormal();
		Bend = Center + Radius * (Normal * Across + Side * FMath::Sqrt(FMath::Max(0.f, 1 - Across * Across)));
	}

	static void Solve(FVector Start, FVector Target, FVector Hint, float Upper, float Lower, FVector& Bend, FVector& End)
	{
		Upper = FMath::Max(Upper, 0.01f);
		Lower = FMath::Max(Lower, 0.01f);
		const FVector Gap = Target - Start;
		const float Distance = Gap.Size();
		// Deform bones keep their bind lengths, even beyond reach.
		const float Reach = FMath::Clamp(Distance, FMath::Abs(Upper - Lower) + 0.001f, Upper + Lower - 0.001f);
		const FVector Direction = Distance > 0.001f ? Gap / Distance : FVector::ForwardVector;
		FVector Pole = Hint - Start;
		Pole = (Pole - Direction * FVector::DotProduct(Pole, Direction)).GetSafeNormal();
		if (Pole.IsNearlyZero()) Pole = (Direction ^ FVector::UpVector).GetSafeNormal();
		if (Pole.IsNearlyZero()) Pole = (Direction ^ FVector::RightVector).GetSafeNormal();
		const float Along = (Upper * Upper + Reach * Reach - Lower * Lower) / (2 * Reach);
		Bend = Start + Direction * Along + Pole * FMath::Sqrt(FMath::Max(0.f, Upper * Upper - Along * Along));
		End = Start + Direction * Reach;
	}
};
