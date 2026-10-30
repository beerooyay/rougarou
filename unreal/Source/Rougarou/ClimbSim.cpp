#include "ClimbSim.h"

#include "Dom/JsonObject.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

static const TCHAR* Names[] = { TEXT("right foot"), TEXT("left foot"), TEXT("right hand"), TEXT("left hand") };

bool FClimbSpec::Load(const FString& Path, FClimbSpec& Out)
{
	FString Text;
	TSharedPtr<FJsonObject> Root;
	if (!FFileHelper::LoadFileToString(Text, *Path) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text), Root) || !Root.IsValid())
		return false;
	Out.Gravity = Root->GetNumberField(TEXT("gravity"));
	Out.Mass = Root->GetNumberField(TEXT("mass"));
	Out.Stiffness = Root->GetNumberField(TEXT("stiffness"));
	Out.Damping = Root->GetNumberField(TEXT("damping"));
	Out.Shift = Root->GetNumberField(TEXT("shift"));
	Out.Reach = Root->GetNumberField(TEXT("reach"));
	Out.Routes.Reset();
	for (const auto& RouteValue : Root->GetArrayField(TEXT("routes")))
	{
		const auto& RouteObject = RouteValue->AsObject();
		FClimbRoute& Route = Out.Routes.AddDefaulted_GetRef();
		Route.Name = RouteObject->GetStringField(TEXT("name"));
		for (const auto& HoldValue : RouteObject->GetArrayField(TEXT("holds")))
		{
			const auto& Hold = HoldValue->AsObject();
			Route.Holds.Add({
				FVector2D(Hold->GetNumberField(TEXT("x")), Hold->GetNumberField(TEXT("y"))),
				Hold->GetStringField(TEXT("kind")),
				FVector2D(Hold->GetNumberField(TEXT("dx")), Hold->GetNumberField(TEXT("dy")))
			});
		}
	}
	return Out.Routes.Num() > 0;
}

const FClimbPanel& FClimbRoute::At(float Y) const
{
	for (const FClimbPanel& Panel : Panels)
		if (Y < Panel.Start + Panel.Length) return Panel;
	return Panels.Last();
}

FVector FClimbRoute::Map(FVector2D Point, float Depth) const
{
	const FClimbPanel& Panel = At(Point.Y);
	return Panel.Origin + Panel.Right * Point.X * 100 + Panel.Up * (Point.Y - Panel.Start) * 100 + Panel.Out * Depth;
}

FVector2D FClimbSim::Turn(FVector2D Offset) const
{
	return Offset.GetRotated(FMath::RadiansToDegrees(Angle));
}

FVector2D FClimbSim::Joint(int32 Limb) const
{
	return Position + Turn(Body.Seats[Limb]);
}

FVector2D FClimbSim::Rest(int32 Limb) const
{
	// where a relaxed contact sits relative to the body: hands overhead, feet below with the knee a little bent
	const float Side = Limb % 2 == 0 ? -1 : 1;
	return Body.Seats[Limb] + FVector2D(Side * 0.06f, Limb >= 2 ? Body.Lengths[Limb] : -Body.Lengths[Limb] * 1.6f);
}

float FClimbSim::Distance(int32 Limb, FVector2D Point) const
{
	// along the unrolled wall, plus how far the limb root sits off it
	const float Across = FVector2D::Distance(Joint(Limb), Point);
	return FMath::Sqrt(Across * Across + Body.Depths[Limb] * Body.Depths[Limb]);
}

FVector2D FClimbSim::Clamp(int32 Limb, FVector2D Point) const
{
	const FVector2D Root = Joint(Limb);
	FVector2D Result = Root + (Point - Root).GetClampedToMaxSize(Range(Limb));
	for (int32 Step = 0; Step < 80 && Distance(Limb, Result) > Range(Limb) - 0.001f; Step++)
		Result = Root + (Result - Root) * 0.98f;
	if (Limb < 2)
	{
		// a foot stops where the hip runs out of motion: about a high step up, a short way across the body
		const float Side = Limb % 2 == 0 ? -1 : 1;
		Result.Y = FMath::Min(Result.Y, Root.Y + Body.Lengths[Limb] * 0.55f);
		if (Side * (Result.X - Root.X) < -0.3f) Result.X = Root.X - Side * 0.3f;
	}
	return Result;
}

bool FClimbSim::Open(int32 Limb, FVector2D Point) const
{
	if (Limb >= 2) return true;
	const float Side = Limb % 2 == 0 ? -1 : 1;
	const FVector2D Hip = Joint(Limb);
	return Point.Y - Hip.Y <= Body.Lengths[Limb] * 0.55f + 0.001f && Side * (Point.X - Hip.X) >= -0.3f - 0.001f;
}

FVector2D FClimbSim::Hang(int32 Limb) const
{
	const float Side = Limb % 2 == 0 ? -1 : 1;
	return Joint(Limb) + FVector2D(Side * 0.1f, Limb >= 2 ? -0.35f : -0.6f);
}

FVector2D FClimbSim::Spot(int32 Limb) const
{
	if (Grips[Limb] >= 0) return Holds()[Grips[Limb]].Point;
	if (Smeared[Limb]) return Posts[Limb];
	if (Limb == Reaching) return Clamp(Limb, Target);
	return Hang(Limb);
}

void FClimbSim::Restart()
{
	Position = FVector2D(0, 1.25f);
	Velocity = FVector2D::ZeroVector;
	Angle = 0;
	Spin = 0;
	Lean = FVector2D::ZeroVector;
	for (int32 Limb = 0; Limb < 4; Limb++)
	{
		Grips[Limb] = Limb;
		Smeared[Limb] = false;
		Slips[Limb] = 0;
		Pump[Limb] = 0;
		Latch[Limb] = 1;
		Strain[Limb] = 0;
		Tips[Limb] = Holds()[Limb].Point;
	}
	Won = false;
	Topped = 0;
	Reaching = -1;
	Goal = Position;
}

bool FClimbSim::Grab(int32 Limb, int32 Hold)
{
	if (!Holds().IsValidIndex(Hold)) return false;
	if (Distance(Limb, Holds()[Hold].Point) > Range(Limb))
	{
		Hint = FString::Printf(TEXT("%s cannot reach: shift your hips or move a foot"), Names[Limb]);
		return false;
	}
	if (!Open(Limb, Holds()[Hold].Point))
	{
		Hint = FString::Printf(TEXT("%s: the hip will not open that far"), Names[Limb]);
		return false;
	}
	Grips[Limb] = Hold;
	Smeared[Limb] = false;
	Slips[Limb] = 0;
	Latch[Limb] = -1;
	Hint = FString::Printf(TEXT("%s placed on %s"), Names[Limb], *Holds()[Hold].Kind);
	return true;
}

bool FClimbSim::Smear(int32 Limb, FVector2D Point)
{
	if (Distance(Limb, Point) > Range(Limb) || !Open(Limb, Point))
	{
		Hint = FString::Printf(TEXT("%s cannot reach the wall"), Names[Limb]);
		return false;
	}
	Grips[Limb] = -1;
	Smeared[Limb] = true;
	Posts[Limb] = Point;
	Slips[Limb] = 0;
	Latch[Limb] = -1;
	Hint = FString::Printf(TEXT("%s pressed on the wall"), Names[Limb]);
	return true;
}

void FClimbSim::Drop(int32 Limb)
{
	Grips[Limb] = -1;
	Smeared[Limb] = false;
	Latch[Limb] = 0;
	Strain[Limb] = 0;
}

void FClimbSim::Step(float Delta)
{
	const FClimbRoute& Face = Spec.Routes[Route];
	const FVector2D Weight2D = Face.Down(Position.Y) * Spec.Gravity * Spec.Mass;
	FVector2D Force = Lean * Spec.Shift + Weight2D;
	// hands and feet travel to where they are sent: quick but not instant, easing in at the end like a placed hand.
	// a limb sent to a hold only bears weight once it gets there, and how settled the catch is depends on how still the body is
	for (int32 Limb = 0; Limb < 4; Limb++)
	{
		const FVector2D Destination = Spot(Limb);
		const FVector2D Gap = Destination - Tips[Limb];
		const float Speed = (Limb >= 2 ? 3.2f : 2.4f) + Velocity.Size();
		Tips[Limb] += (Gap * FMath::Min(1.f, Delta * 14)).GetClampedToMaxSize(Speed * Delta);
		const FVector2D Root = Joint(Limb);
		Tips[Limb] = Root + (Tips[Limb] - Root).GetClampedToMaxSize(Range(Limb) * 1.15f);
		if (Latch[Limb] < 0 && FVector2D::Distance(Tips[Limb], Destination) < 0.025f)
		{
			Tips[Limb] = Destination;
			// the deadpoint: a hold caught with the hips at rest locks right in, one caught mid-swing has to be fought for
			const float Still = FMath::Clamp(1 - (Velocity.Size() - 0.15f) / 0.9f, 0.3f, 1.f);
			Latch[Limb] = Still;
			if (Limb >= 2 && Grips[Limb] >= 0) Hint = Still > 0.9f ? FString::Printf(TEXT("%s latched"), Names[Limb]) : FString::Printf(TEXT("%s caught it moving: hold on"), Names[Limb]);
		}
		else if (Latch[Limb] >= 0 && (Grips[Limb] >= 0 || Smeared[Limb])) Latch[Limb] = FMath::Min(1.f, Latch[Limb] + Delta * 3);
	}
	// reaching past a comfortable stretch: shift the hips toward the target and turn the torso so that shoulder leads.
	// the shift is a new stance the remaining limbs must carry, so it only goes as far as they can support.
	// a limb still travelling to its hold keeps reaching for it
	int32 Mover = Reaching >= 0 && Grips[Reaching] < 0 && !Smeared[Reaching] ? Reaching : -1;
	FVector2D Toward = Target;
	for (int32 Limb = 0; Limb < 4 && Mover < 0; Limb++)
		if (Latch[Limb] < 0) { Mover = Limb; Toward = Spot(Limb); }
	FVector2D Reach = FVector2D::ZeroVector;
	float Twist = 0;
	if (Mover >= 0)
	{
		const float Excess = FVector2D::Distance(Joint(Mover), Toward) - Range(Mover) * 0.85f;
		if (Excess > 0) Reach = (Toward - Joint(Mover)).GetSafeNormal() * FMath::Min(Excess, 0.35f);
		const FVector2D Seat = Turn(Body.Seats[Mover]);
		const FVector2D Lead = Toward - Position;
		const float Swing = FMath::Atan2(Seat.X * Lead.Y - Seat.Y * Lead.X, FVector2D::DotProduct(Seat, Lead));
		if (Mover >= 2) Twist = FMath::Clamp(Swing * 0.35f, -0.5f, 0.5f);
	}
	// core tension squares the torso back up (or into the reaching twist); one-sided contact can still overpower it and barn-door
	float Torque = FMath::Clamp(-(Angle - Twist) * 180 - Spin * 25, -90.f, 90.f);
	// hands that end up below the shoulder stop the legs from standing the hips up past them
	float Stretch = 0;
	for (int32 Limb = 2; Limb < 4; Limb++)
		if (Grips[Limb] >= 0 && Arrived(Limb) && Holds()[Grips[Limb]].Point.Y < Joint(Limb).Y)
			Stretch = FMath::Max(Stretch, Distance(Limb, Holds()[Grips[Limb]].Point) / (Body.Lengths[Limb] * 2));

	struct FContact
	{
		int32 Limb;
		FVector2D Point;
		const FClimbHold* Hold;
		bool Press;
		float Capacity;
		FVector2D Applied;
	};
	TArray<FContact, TInlineAllocator<4>> Contacts;
	FVector2D Stance = FVector2D::ZeroVector;
	float Weight = 0;
	for (int32 Limb = 0; Limb < 4; Limb++)
	{
		const bool Press = Grips[Limb] < 0 && Smeared[Limb];
		if ((Grips[Limb] < 0 && !Press) || !Arrived(Limb)) { Strain[Limb] = 0; continue; }
		const FClimbHold* Hold = Press ? nullptr : &Holds()[Grips[Limb]];
		const FVector2D Point = Press ? Posts[Limb] : Hold->Point;
		// legs are far stronger than arms; pump saps the arms; a smear only carries a fraction
		const float Strength = (Limb < 2 ? 1100 : 700 * (1 - 0.4f * Pump[Limb])) * (Press ? 0.35f : 1) * (0.5f + 0.5f * Latch[Limb]);
		Contacts.Add({ Limb, Point, Hold, Press, Strength, FVector2D::ZeroVector });
		// the stance the contacts hold the hips in; feet count double because the body stands on them
		const float Vote = Limb < 2 ? 2 : 1;
		Stance += (Point - Turn(Rest(Limb))) * Vote;
		Weight += Vote;
	}

	// what one limb can actually deliver of a requested force on the body
	auto Allow = [&](const FContact& Contact, FVector2D Request)
	{
		const FVector2D Away = (Joint(Contact.Limb) - Contact.Point).GetSafeNormal();
		const float Extension = Distance(Contact.Limb, Contact.Point) / (Body.Lengths[Contact.Limb] * 2);
		const float Push = FVector2D::DotProduct(Request, Away);
		if (Contact.Limb >= 2)
		{
			// hands only pull, and a hand below its shoulder only steadies until the arm runs out of length
			if (Push > 0) Request -= Away * Push;
			if (Contact.Point.Y < Joint(Contact.Limb).Y && Request.Y < 0)
				Request.Y *= FMath::Lerp(0.15f, 1.f, FMath::Clamp((Extension - 0.9f) / 0.1f, 0.f, 1.f));
		}
		else if (Push > 0)
		{
			// legs push until straight, and never stand the hips up past a gripping hand
			Request -= Away * Push * FMath::Clamp(FMath::Max(Extension - 0.9f, Stretch - 0.6f) / 0.15f, 0.f, 1.f);
		}
		else
		{
			// a foot can barely pull; toe and heel hooks come later
			Request -= Away * Push * 0.8f;
		}
		return Request;
	};

	if (Weight > 0)
	{
		// muscles hold the hips at the stance and carry the body's weight; the limbs on the wall share the work by strength,
		// and whatever one limb cannot deliver passes to the others. only when they all run out does the body sag.
		// the hips ease toward the stance at a human pace instead of snapping, which keeps the load on the fingers smooth
		const FVector2D Wanted = Stance / Weight + Reach;
		Goal += ((Wanted - Goal) * FMath::Min(1.f, Delta * 6)).GetClampedToMaxSize(1.2f * Delta);
		FVector2D Needed = (Goal - Position) * 2400 - Velocity * 520 - Weight2D;
		for (int32 Pass = 0; Pass < 4 && Needed.Size() > 1; Pass++)
		{
			float Open = 0;
			for (const FContact& Contact : Contacts) Open += FMath::Max(0.f, Contact.Capacity - Contact.Applied.Size());
			if (Open <= 1) break;
			FVector2D Given = FVector2D::ZeroVector;
			for (FContact& Contact : Contacts)
			{
				const float Headroom = FMath::Max(0.f, Contact.Capacity - Contact.Applied.Size());
				if (Headroom <= 0) continue;
				const FVector2D Total = (Contact.Applied + Allow(Contact, Needed * (Headroom / Open))).GetClampedToMaxSize(Contact.Capacity);
				Given += Total - Contact.Applied;
				Contact.Applied = Total;
			}
			Needed -= Given;
			if (Given.Size() < 1) break;
		}
	}

	for (const FContact& Contact : Contacts)
	{
		const int32 Limb = Contact.Limb;
		Force += Contact.Applied;
		const FVector2D Seat = Turn(Body.Seats[Limb]);
		Torque += Seat.X * Contact.Applied.Y - Seat.Y * Contact.Applied.X;
		const FVector2D Load = -Contact.Applied;
		const FVector2D Direction = Contact.Press ? FVector2D(0, -1) : Contact.Hold->Pull;
		const FString Kind = Contact.Press ? TEXT("smear") : Contact.Hold->Kind;
		float Matchup = Load.Size() > 0.001f ? FMath::Max(0.f, FVector2D::DotProduct(Load.GetSafeNormal(), Direction)) : 1;
		// a pinch is squeezed from both sides, so it holds in most directions; a pocket only works pulled straight
		if (Kind == TEXT("pinch")) Matchup = FMath::Max(Matchup, 0.6f);
		if (Kind == TEXT("pocket")) Matchup = Matchup * Matchup;
		const float Grip = Kind == TEXT("smear") ? 260 : Kind == TEXT("jug") ? 1000 : Kind == TEXT("crimp") ? 600 : Kind == TEXT("pinch") ? 650 : Kind == TEXT("pocket") ? 560 : 480;
		// steep faces try to peel the body off: holds need more grip and feet cut loose; slabs press the feet in
		const float Steep = Face.Steep(Contact.Point.Y);
		const float Peel = Steep > 0 ? 1 - Steep * (Limb < 2 ? 0.7f : 0.45f) : 1 - Steep * (Limb < 2 ? 0.3f : 0);
		// a fresh catch has not settled yet: the fingers are still finding the hold
		const float Limit = Grip * (0.5f + 0.5f * Matchup) * (Limb < 2 ? 1.45f : Contact.Press ? 0.55f : 1) * (1 - 0.5f * Pump[Limb]) * Peel * (0.45f + 0.55f * Latch[Limb]);
		const float Effort = Load.Size() / FMath::Max(Limit, 1.f);
		Strain[Limb] = Effort;
		if (Limb >= 2 && !Contact.Press)
		{
			// hard holds pump the forearm, a bent lock-off far faster than hanging off a straight arm; a relaxed hang slowly recovers
			const float Bend = FMath::Clamp((0.95f - Distance(Limb, Contact.Point) / (Body.Lengths[Limb] * 2)) / 0.4f, 0.f, 1.f);
			Pump[Limb] = FMath::Clamp(Pump[Limb] + Delta * (Effort > 0.3f ? (Effort - 0.3f) * 0.12f * (0.7f + 0.9f * Bend) : -0.03f), 0.f, 1.f);
		}
		// sliding is gradual: a brief spike is held, a sustained overload creeps off, a big one rips straight off
		Slips[Limb] = Effort > 1 ? Slips[Limb] + Delta * (Effort - 1) * 5 : FMath::Max(0.f, Slips[Limb] - Delta * 1.5f);
		if (Slips[Limb] <= 0.25f && Distance(Limb, Contact.Point) <= Range(Limb) * 1.15f) continue;
		Drop(Limb);
		Slips[Limb] = 0;
		Hint = FString::Printf(TEXT("%s slipped: adjust your weight"), Names[Limb]);
	}

	// a hand off the wall shakes out
	for (int32 Limb = 2; Limb < 4; Limb++)
		if (Grips[Limb] < 0) Pump[Limb] = FMath::Max(0.f, Pump[Limb] - Delta * 0.2f);
	Velocity += Force / Spec.Mass * Delta;
	Velocity /= 1 + Delta * 0.55f;
	Spin += Torque / Body.Inertia * Delta;
	Spin /= 1 + Delta * 6;
	Position += Velocity * Delta;
	Angle += Spin * Delta;
	if (Position.Y < 0.62f)
	{
		Restart();
		Falls++;
		Hint = TEXT("fell: try moving a foot before reaching");
	}
	const int32 Top = Holds().Num() - 1;
	// a send is both hands matched on the finish hold, held under control
	Topped = Grips[2] == Top && Grips[3] == Top && Arrived(2) && Arrived(3) ? Topped + Delta : 0;
	if (Topped > 0.5f) Won = true;
}

FClimbSim::FState FClimbSim::Save() const
{
	FState State{ Position, Velocity, Lean, Angle, Spin };
	FMemory::Memcpy(State.Grips, Grips, sizeof(Grips));
	FMemory::Memcpy(State.Smeared, Smeared, sizeof(Smeared));
	FMemory::Memcpy(State.Posts, Posts, sizeof(Posts));
	FMemory::Memcpy(State.Slips, Slips, sizeof(Slips));
	FMemory::Memcpy(State.Pump, Pump, sizeof(Pump));
	FMemory::Memcpy(State.Tips, Tips, sizeof(Tips));
	FMemory::Memcpy(State.Latch, Latch, sizeof(Latch));
	State.Goal = Goal;
	State.Topped = Topped;
	State.Won = Won;
	return State;
}

void FClimbSim::Load(const FState& State)
{
	Position = State.Position;
	Velocity = State.Velocity;
	Lean = State.Lean;
	Angle = State.Angle;
	Spin = State.Spin;
	FMemory::Memcpy(Grips, State.Grips, sizeof(Grips));
	FMemory::Memcpy(Smeared, State.Smeared, sizeof(Smeared));
	FMemory::Memcpy(Posts, State.Posts, sizeof(Posts));
	FMemory::Memcpy(Slips, State.Slips, sizeof(Slips));
	FMemory::Memcpy(Pump, State.Pump, sizeof(Pump));
	FMemory::Memcpy(Tips, State.Tips, sizeof(Tips));
	FMemory::Memcpy(Latch, State.Latch, sizeof(Latch));
	Goal = State.Goal;
	Topped = State.Topped;
	Won = State.Won;
}
