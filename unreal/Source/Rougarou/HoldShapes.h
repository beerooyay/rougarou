#pragma once

#include "CoreMinimal.h"

// a library of sculpted low-poly climbing holds, baked straight into merged meshes so hundreds stay cheap.
// hold space: x points up the wall (the side you pull toward), y across, z out of the wall; centimeters.

// triangles with flat normals, ready for a procedural mesh section
struct FMeshSink
{
	TArray<FVector> Vertices;
	TArray<int32> Triangles;
	TArray<FVector> Normals;

	// one triangle, wound so unreal draws the side Facing points to; it is lit from that side
	void Add(FVector A, FVector B, FVector C, FVector Facing)
	{
		// unreal treats clockwise as the front, so the lit normal of a winding is (c - a) x (b - a)
		FVector Normal = ((C - A) ^ (B - A)).GetSafeNormal();
		if (FVector::DotProduct(Normal, Facing) < 0)
		{
			Swap(B, C);
			Normal = -Normal;
		}
		const int32 First = Vertices.Num();
		Vertices.Append({ A, B, C });
		Normals.Append({ Normal, Normal, Normal });
		Triangles.Append({ First, First + 1, First + 2 });
	}
};

struct FHoldTemplate
{
	const TCHAR* Name;
	// how it plays in the sim: jug, crimp, sloper, pinch, pocket, or volume (decor only)
	const TCHAR* Kind;
	// size in hold space for a hold of nominal size 1: up, across, out
	FVector Scale;
	float Noise;
	// shape tricks applied to the unit hemisphere
	enum EShape : uint8 { Blob, Lip, Horn, Smile, Star, Pocket, Pyramid, Wedge } Shape;
};

inline const TArray<FHoldTemplate>& HoldTemplates()
{
	static const TArray<FHoldTemplate> Library = {
		{ TEXT("bucket"), TEXT("jug"), FVector(0.8f, 1.f, 0.7f), 0.08f, FHoldTemplate::Lip },
		{ TEXT("horn"), TEXT("jug"), FVector(0.9f, 0.6f, 0.6f), 0.06f, FHoldTemplate::Horn },
		{ TEXT("star"), TEXT("jug"), FVector(1.1f, 1.1f, 0.55f), 0.05f, FHoldTemplate::Star },
		{ TEXT("handle"), TEXT("jug"), FVector(0.6f, 1.2f, 0.6f), 0.07f, FHoldTemplate::Lip },
		{ TEXT("edge"), TEXT("crimp"), FVector(0.28f, 1.2f, 0.2f), 0.04f, FHoldTemplate::Blob },
		{ TEXT("smile"), TEXT("crimp"), FVector(0.3f, 1.f, 0.22f), 0.04f, FHoldTemplate::Smile },
		{ TEXT("chip"), TEXT("crimp"), FVector(0.4f, 0.5f, 0.16f), 0.08f, FHoldTemplate::Blob },
		{ TEXT("rail"), TEXT("crimp"), FVector(0.22f, 1.9f, 0.22f), 0.03f, FHoldTemplate::Blob },
		{ TEXT("dome"), TEXT("sloper"), FVector(1.25f, 1.25f, 0.42f), 0.05f, FHoldTemplate::Blob },
		{ TEXT("pebble"), TEXT("sloper"), FVector(0.85f, 1.5f, 0.38f), 0.1f, FHoldTemplate::Blob },
		{ TEXT("fin"), TEXT("pinch"), FVector(1.1f, 0.3f, 0.55f), 0.05f, FHoldTemplate::Blob },
		{ TEXT("tufa"), TEXT("pinch"), FVector(1.6f, 0.38f, 0.5f), 0.09f, FHoldTemplate::Horn },
		{ TEXT("pocket"), TEXT("pocket"), FVector(0.9f, 0.9f, 0.45f), 0.06f, FHoldTemplate::Pocket },
		{ TEXT("knob"), TEXT("pinch"), FVector(0.5f, 0.5f, 0.45f), 0.08f, FHoldTemplate::Blob },
		{ TEXT("pyramid"), TEXT("volume"), FVector(1.f, 1.f, 0.6f), 0.f, FHoldTemplate::Pyramid },
		{ TEXT("wedge"), TEXT("volume"), FVector(1.3f, 0.9f, 0.5f), 0.f, FHoldTemplate::Wedge },
	};
	return Library;
}

// a template that plays as Kind, varied by Seed
inline int32 PickTemplate(const FString& Kind, int32 Seed)
{
	TArray<int32, TInlineAllocator<8>> Matches;
	const TArray<FHoldTemplate>& Library = HoldTemplates();
	for (int32 Index = 0; Index < Library.Num(); Index++)
		if (Kind == Library[Index].Kind) Matches.Add(Index);
	return Matches.Num() ? Matches[FMath::Abs(Seed) % Matches.Num()] : 0;
}

// a once-subdivided icosahedron: 80 faces, enough for a faceted hand-shaped hold
inline void Icosphere(TArray<FVector>& Points, TArray<FIntVector>& Faces)
{
	const float T = (1 + FMath::Sqrt(5.f)) / 2;
	Points = { { -1, T, 0 }, { 1, T, 0 }, { -1, -T, 0 }, { 1, -T, 0 }, { 0, -1, T }, { 0, 1, T }, { 0, -1, -T }, { 0, 1, -T }, { T, 0, -1 }, { T, 0, 1 }, { -T, 0, -1 }, { -T, 0, 1 } };
	TArray<FIntVector> Base = { { 0, 11, 5 }, { 0, 5, 1 }, { 0, 1, 7 }, { 0, 7, 10 }, { 0, 10, 11 }, { 1, 5, 9 }, { 5, 11, 4 }, { 11, 10, 2 }, { 10, 7, 6 }, { 7, 1, 8 },
		{ 3, 9, 4 }, { 3, 4, 2 }, { 3, 2, 6 }, { 3, 6, 8 }, { 3, 8, 9 }, { 4, 9, 5 }, { 2, 4, 11 }, { 6, 2, 10 }, { 8, 6, 7 }, { 9, 8, 1 } };
	for (FVector& Point : Points) Point.Normalize();
	TMap<uint64, int32> Middles;
	auto Middle = [&](int32 A, int32 B)
	{
		const uint64 Key = (uint64(FMath::Min(A, B)) << 32) | uint64(FMath::Max(A, B));
		if (const int32* Found = Middles.Find(Key)) return *Found;
		const int32 Index = Points.Add(((Points[A] + Points[B]) / 2).GetSafeNormal());
		Middles.Add(Key, Index);
		return Index;
	};
	Faces.Reset();
	for (const FIntVector& Face : Base)
	{
		const int32 AB = Middle(Face.X, Face.Y), BC = Middle(Face.Y, Face.Z), CA = Middle(Face.Z, Face.X);
		Faces.Append({ { Face.X, AB, CA }, { Face.Y, BC, AB }, { Face.Z, CA, BC }, { AB, BC, CA } });
	}
}

// bake one hold into the sinks. Where maps hold space onto the wall; Size is the nominal size in centimeters
inline void BuildHold(int32 Template, float Size, const FTransform& Where, int32 Seed, FMeshSink& Body, FMeshSink& Dark)
{
	const FHoldTemplate& Hold = HoldTemplates()[Template];
	const FVector Scale = Hold.Scale * Size / 2;
	FRandomStream Dice(Seed);
	auto Place = [&](FVector Local) { return Where.TransformPosition(Local); };
	const FVector Out = Where.TransformVectorNoScale(FVector(0, 0, 1));

	if (Hold.Shape == FHoldTemplate::Pyramid || Hold.Shape == FHoldTemplate::Wedge)
	{
		// volumes: a three-sided pyramid or a ridged wedge bolted flat to the wall
		const FVector Apex = Place(FVector(0, 0, Scale.Z * 2));
		TArray<FVector> Base;
		if (Hold.Shape == FHoldTemplate::Pyramid)
			for (int32 Corner = 0; Corner < 3; Corner++)
			{
				const float Turn = 2 * PI * Corner / 3 + Dice.FRandRange(-0.2f, 0.2f);
				Base.Add(Place(FVector(FMath::Cos(Turn) * Scale.X * 2, FMath::Sin(Turn) * Scale.Y * 2, 0)));
			}
		else
			Base = { Place(FVector(-Scale.X * 2, -Scale.Y * 2, 0)), Place(FVector(Scale.X * 2, -Scale.Y * 2, 0)), Place(FVector(Scale.X * 2, Scale.Y * 2, 0)), Place(FVector(-Scale.X * 2, Scale.Y * 2, 0)) };
		const FVector Center = Place(FVector(0, 0, Scale.Z * 0.5f));
		if (Hold.Shape == FHoldTemplate::Pyramid)
			for (int32 Corner = 0; Corner < 3; Corner++)
				Body.Add(Base[Corner], Base[(Corner + 1) % 3], Apex, (Base[Corner] + Base[(Corner + 1) % 3] + Apex) / 3 - Center);
		else
		{
			// a ridge along the middle, high at one end
			const FVector Low = Place(FVector(-Scale.X * 2, 0, Scale.Z * 0.6f)), High = Place(FVector(Scale.X * 2, 0, Scale.Z * 2));
			Body.Add(Base[0], Base[1], High, Out - Where.TransformVectorNoScale(FVector(0, 1, 0)));
			Body.Add(Base[0], High, Low, Out - Where.TransformVectorNoScale(FVector(0, 1, 0)));
			Body.Add(Base[3], Base[2], High, Out + Where.TransformVectorNoScale(FVector(0, 1, 0)));
			Body.Add(Base[3], High, Low, Out + Where.TransformVectorNoScale(FVector(0, 1, 0)));
			Body.Add(Base[1], Base[2], High, Where.TransformVectorNoScale(FVector(1, 0, 0)));
			Body.Add(Base[0], Base[3], Low, -Where.TransformVectorNoScale(FVector(1, 0, 0)));
		}
		return;
	}

	static TArray<FVector> Unit;
	static TArray<FIntVector> Faces;
	if (Unit.Num() == 0) Icosphere(Unit, Faces);
	TArray<FVector> Points;
	for (const FVector& Point : Unit)
	{
		// the front half of a sphere, flattened against the wall, sculpted, then roughed up
		FVector Local(Point.X, Point.Y, FMath::Max(Point.Z, 0.f));
		const float Theta = FMath::Atan2(Local.Y, Local.X);
		switch (Hold.Shape)
		{
		case FHoldTemplate::Lip:
			// the top bulges out into a lip you can wrap fingers over
			Local.Z += 0.45f * FMath::Max(Local.X, 0.f) * Local.Z;
			Local.X -= 0.2f * Local.Z * Local.Z;
			break;
		case FHoldTemplate::Horn:
			// one end swells into a horn
			Local.Z *= 1 + 0.6f * FMath::Max(Local.X, 0.f);
			Local.X *= 1 + 0.25f * FMath::Max(Local.X, 0.f);
			break;
		case FHoldTemplate::Smile:
			// a curved edge
			Local.X += 0.35f * Local.Y * Local.Y;
			break;
		case FHoldTemplate::Star:
			// three lobes, like the rounded triangles all over the photos
			Local *= FVector(1 + 0.3f * FMath::Cos(3 * Theta), 1 + 0.3f * FMath::Cos(3 * Theta), 1);
			break;
		case FHoldTemplate::Pocket:
			// a dished front: the hole itself is drawn dark
			Local.Z *= 1 - 0.5f * FMath::Exp(-(Local.X * Local.X + Local.Y * Local.Y) * 6);
			break;
		default:
			break;
		}
		Local += FVector(Dice.FRandRange(-1, 1), Dice.FRandRange(-1, 1), Dice.FRandRange(0, 1)) * Hold.Noise;
		Points.Add(Place(Local * Scale));
	}
	const FVector Center = Place(FVector(0, 0, Scale.Z * 0.3f));
	for (const FIntVector& Face : Faces)
	{
		// skip the hidden back faces flattened into the wall
		if (Unit[Face.X].Z < 0 && Unit[Face.Y].Z < 0 && Unit[Face.Z].Z < 0) continue;
		const FVector Mid = (Points[Face.X] + Points[Face.Y] + Points[Face.Z]) / 3;
		Body.Add(Points[Face.X], Points[Face.Y], Points[Face.Z], Mid - Center);
	}
	if (Hold.Shape == FHoldTemplate::Pocket)
	{
		// the dark finger hole: a shallow cone sunk into the dish
		const FVector Bottom = Place(FVector(0, 0, Scale.Z * 0.2f));
		for (int32 Step = 0; Step < 8; Step++)
		{
			const float A0 = 2 * PI * Step / 8, A1 = 2 * PI * (Step + 1) / 8;
			const FVector P0 = Place(FVector(FMath::Cos(A0) * Scale.X * 0.38f, FMath::Sin(A0) * Scale.Y * 0.38f, Scale.Z * 0.62f));
			const FVector P1 = Place(FVector(FMath::Cos(A1) * Scale.X * 0.38f, FMath::Sin(A1) * Scale.Y * 0.38f, Scale.Z * 0.62f));
			Dark.Add(P0, P1, Bottom, Out);
		}
	}
}

// hold space onto a surface: z along the face normal, x toward Up as far as the face allows
inline FTransform HoldFrame(FVector Spot, FVector Normal, FVector Up)
{
	const FVector Out = Normal.GetSafeNormal();
	FVector Top = (Up - Out * FVector::DotProduct(Up, Out)).GetSafeNormal();
	if (Top.IsNearlyZero()) Top = (FVector(1, 0, 0) - Out * Out.X).GetSafeNormal();
	return FTransform(FMatrix(Top, Out ^ Top, Out, Spot));
}

// typical nominal sizes per kind, centimeters
inline float HoldSize(const FString& Kind)
{
	return Kind == TEXT("jug") ? 22 : Kind == TEXT("crimp") ? 18 : Kind == TEXT("sloper") ? 28 : Kind == TEXT("pinch") ? 20 : Kind == TEXT("pocket") ? 18 : Kind == TEXT("volume") ? 55 : 16;
}
