#pragma once

#include "CoreMinimal.h"

// the climbing model on an unrolled wall, in meters: x across the wall, y measured up the wall's surface from the floor.
// a problem's wall is a stack of flat angled panels (a vertical base into a kicker, a steep wall onto a roof);
// gravity and peel come from whichever panel a body part is on. limbs are 0 right foot, 1 left foot, 2 right hand, 3 left hand.

struct FClimbHold
{
	FVector2D Point;
	FString Kind;
	FVector2D Pull;
};

// one flat panel of a problem's wall, placed in the gym in centimeters
struct FClimbPanel
{
	// meters up the unrolled wall where the panel starts, and how long it runs
	float Start = 0;
	float Length = 5;
	// the panel's bottom edge at x = 0, and its axes: across, up the panel, out toward the climber
	FVector Origin = FVector::ZeroVector;
	FVector Right = FVector(0, 1, 0);
	FVector Up = FVector(0, 0, 1);
	FVector Out = FVector(1, 0, 0);
};

struct FClimbRoute
{
	FString Name;
	TArray<FClimbHold> Holds;
	// bottom to top; never empty
	TArray<FClimbPanel> Panels = { FClimbPanel() };
	// where the camera stands, centimeters: back from the wall, and to the side along it (a cave needs to look down its tunnel)
	float Stand = 540;
	float Aside = 0;

	const FClimbPanel& At(float Y) const;
	// a point on the unrolled wall, Depth centimeters off its surface, in the gym
	FVector Map(FVector2D Point, float Depth = 0) const;
	// gravity along the panel as a fraction of g: (0, -1) on a vertical wall, shorter on slabs and overhangs
	FVector2D Down(float Y) const { return FVector2D(-At(Y).Right.Z, -At(Y).Up.Z); }
	// how much the panel looks at the floor: 0 vertical, positive overhanging (gravity peels you off), negative slab
	float Steep(float Y) const { return -At(Y).Out.Z; }
};

struct FClimbSpec
{
	float Gravity = 9.8f;
	float Mass = 65;
	float Stiffness = 720;
	float Damping = 70;
	float Shift = 850;
	float Reach = 1.35f;
	TArray<FClimbRoute> Routes;

	static bool Load(const FString& Path, FClimbSpec& Out);
};

// body proportions; measured from the 1.85m mixamo rig, to be replaced by the metahuman skeleton
struct FClimbBody
{
	FVector2D Seats[4] = { { -0.11f, -0.26f }, { 0.11f, -0.26f }, { -0.28f, 0.26f }, { 0.28f, 0.26f } };
	float Lengths[4] = { 0.46f, 0.46f, 0.27f, 0.27f };
	// how far each limb root sits off the wall, which costs reach
	float Depths[4] = { 0.1f, 0.1f, 0.14f, 0.14f };
	float Inertia = 5;
};

class FClimbSim
{
public:
	FClimbSpec Spec;
	FClimbBody Body;
	int32 Route = 0;

	FVector2D Position;
	FVector2D Velocity;
	float Angle = 0;
	float Spin = 0;
	FVector2D Lean = FVector2D::ZeroVector;
	int32 Grips[4] = { 0, 1, 2, 3 };
	bool Smeared[4] = {};
	FVector2D Posts[4];
	float Slips[4] = {};
	bool Won = false;
	int32 Falls = 0;
	// forearm fatigue per hand, 0 fresh to 1 fully pumped; feet stay 0
	float Pump[4] = {};
	// how long both hands have been matched on the finish hold
	float Topped = 0;
	// a lifted limb stretching toward a target shifts the hips that way and turns the torso to lead with that shoulder
	int32 Reaching = -1;
	FVector2D Target = FVector2D::ZeroVector;
	FString Hint;
	// where each hand and foot really is: it travels where it is sent at a human speed and only bears weight once it gets there
	FVector2D Tips[4];
	// how settled each grip is, 0 just caught to 1 locked in, negative while the limb is still on its way.
	// catching a hold at the deadpoint, with the body still, starts it high; catching it on the move starts it low
	float Latch[4] = { 1, 1, 1, 1 };
	// each contact's load over what it can hold; past 1 the fingers or rubber are sliding
	float Strain[4] = {};
	// the stance the hips are being moved toward, eased like muscles rather than snapped
	FVector2D Goal = FVector2D::ZeroVector;

	void Restart();
	void Step(float Delta);

	FVector2D Joint(int32 Limb) const;
	FVector2D Rest(int32 Limb) const;
	// straight limb plus what the shoulder blade or hip can add by reaching into the move
	float Range(int32 Limb) const { return FMath::Min(Spec.Reach, Body.Lengths[Limb] * 2 + (Limb >= 2 ? 0.08f : 0.04f)); }
	float Distance(int32 Limb, FVector2D Point) const;
	// the nearest point toward Point that the limb can actually touch, using the same measure as Grab
	FVector2D Clamp(int32 Limb, FVector2D Point) const;
	// where a free hand or foot hangs
	FVector2D Hang(int32 Limb) const;
	// where a limb is headed: its hold, its smear, the reach target while lifted, or a relaxed hang
	FVector2D Spot(int32 Limb) const;
	bool Arrived(int32 Limb) const { return Latch[Limb] >= 0; }
	// whether the hip can put a foot there: no higher than a high step, only a little across the body. hands are free
	bool Open(int32 Limb, FVector2D Point) const;
	const TArray<FClimbHold>& Holds() const { return Spec.Routes[Route].Holds; }

	bool Grab(int32 Limb, int32 Hold);
	bool Smear(int32 Limb, FVector2D Point);
	void Drop(int32 Limb);

	struct FState
	{
		FVector2D Position, Velocity, Lean;
		float Angle, Spin;
		int32 Grips[4];
		bool Smeared[4];
		FVector2D Posts[4];
		float Slips[4];
		float Pump[4];
		FVector2D Tips[4];
		float Latch[4];
		FVector2D Goal;
		float Topped;
		bool Won;
	};
	FState Save() const;
	void Load(const FState& State);

private:
	FVector2D Turn(FVector2D Offset) const;
};
