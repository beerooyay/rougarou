#include "ClimberActor.h"
#include "Misc/ScopeExit.h"

#include "Components/StaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rig.h"
#include "LocomotionAnim.h"
#include "GymActor.h"
#include "DrawDebugHelpers.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/Paths.h"

AClimberActor::AClimberActor()
{
	PrimaryActorTick.bCanEverTick = true;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Wall"));
	Walker = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Walker"));
	Walker->SetupAttachment(RootComponent);
	Walker->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Walker->SetVisibility(false);
	Walker->AddTickPrerequisiteActor(this);
}

void AClimberActor::BeginPlay()
{
	Super::BeginPlay();
	if (!FClimbSpec::Load(FPaths::ProjectContentDir() / TEXT("Data/routes.json"), Sim.Spec))
	{
		UE_LOG(LogTemp, Error, TEXT("rougarou: Content/Data/routes.json did not load; run shared/sync.py"));
		SetActorTickEnabled(false);
		return;
	}
	// routes.json supplies the body and physics constants; the problems are the ones set on the gym's walls
	Sim.Spec.Routes = AGymActor::Problems();
	// -route=N starts on a given problem
	FParse::Value(FCommandLine::Get(), TEXT("route="), Route);
	Sim.Route = FMath::Clamp(Route, 0, Sim.Spec.Routes.Num() - 1);
	Sim.Restart();
	USkeletalMesh* Mesh = FParse::Param(FCommandLine::Get(), TEXT("mannequin"))
		? nullptr : ULocomotionAnim::LoadPlayerMesh();
	Walker->SetVisibility(false);
	if (Mesh)
	{
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		const TCHAR* Names[] = { TEXT("Hips"), TEXT("Spine2"), TEXT("RightArm"), TEXT("LeftArm"), TEXT("RightForeArm"), TEXT("LeftForeArm"),
			TEXT("RightHand"), TEXT("LeftHand"), TEXT("RightUpLeg"), TEXT("LeftUpLeg"), TEXT("RightLeg"), TEXT("LeftLeg"),
			TEXT("RightFoot"), TEXT("LeftFoot"), TEXT("RightToeBase"), TEXT("LeftToeBase"), TEXT("RightHandMiddle1"), TEXT("LeftHandMiddle1") };
		Rigged = true;
		for (const TCHAR* Name : Names) Rigged &= Ref.FindBoneIndex(FName(Name)) != INDEX_NONE;
		if (Rigged) Rigged = HumanRig.Initialize(Ref,FHumanRig::ProfilePath());
		if (FParse::Param(FCommandLine::Get(), TEXT("nolimits"))) HumanRig.LimitsEnabled = false;
		if (Rigged && FParse::Param(FCommandLine::Get(), TEXT("bodylog")))
		{
			// the character's real proportions, to compare with what the sim assumes
			TArray<FTransform> Space = Ref.GetRefBonePose();
			for (int32 I = 0; I < Space.Num(); ++I) if (Ref.GetParentIndex(I) >= 0) Space[I] *= Space[Ref.GetParentIndex(I)];
			auto At = [&](const TCHAR* Name) { return Space[Ref.FindBoneIndex(FName(Name))].GetLocation(); };
			auto Gap2 = [&](const TCHAR* A, const TCHAR* B) { return FVector::Distance(At(A), At(B)); };
			UE_LOG(LogTemp, Display, TEXT("BODY thigh %.1f shin %.1f upperarm %.1f forearm %.1f hand %.1f | hips apart %.1f shoulders apart %.1f | hip to shoulder %.1f | head top %.1f"),
				Gap2(TEXT("LeftUpLeg"), TEXT("LeftLeg")), Gap2(TEXT("LeftLeg"), TEXT("LeftFoot")), Gap2(TEXT("LeftArm"), TEXT("LeftForeArm")), Gap2(TEXT("LeftForeArm"), TEXT("LeftHand")), Gap2(TEXT("LeftHand"), TEXT("LeftHandMiddle1")),
				Gap2(TEXT("LeftUpLeg"), TEXT("RightUpLeg")), Gap2(TEXT("LeftArm"), TEXT("RightArm")),
				FVector::Distance((At(TEXT("LeftUpLeg")) + At(TEXT("RightUpLeg"))) / 2, (At(TEXT("LeftArm")) + At(TEXT("RightArm"))) / 2), At(TEXT("Head")).Size());
		}
		if (Rigged) { Walker->SetSkeletalMesh(Mesh); Walker->SetAnimInstanceClass(ULocomotionAnim::StaticClass()); FPlayerForm::LoadGroundProfile(*Walker); }
		if (Rigged && GateRig.Initialize(Ref, FHumanRig::ProfilePath()))
		{
			GateRig.ColdIterations = 3;
			// pose the body as if the limb were already on the hold; refuse what no joint can reach, or what puts a knee or elbow in the wall
			Sim.Gate = [this](const FClimbSim& Now, int32 Limb, int32 Hold, float Across) -> FString
			{
				FClimbSim Try = Now;
				Try.Gate = nullptr;
				Try.Grips[Limb] = Hold; Try.Smeared[Limb] = false; Try.Latch[Limb] = 0;
				Try.Placement[Limb] = Across; Try.Tips[Limb] = Try.Spot(Limb); Try.Reaching = -1;
				GateRig.WarmFrom(HumanRig);
				const FHumanPose Pose = GateRig.Solve(FHumanGoals::FromSim(Try, Gap, 0, HipDepth, ChestDepth), 0);
				if (!Pose.Finite) return FString();
				if (Pose.Miss[Limb] > 8) return TEXT("the joints will not reach it from here");
				if (Pose.MinClearance < -1) return TEXT("that would put a knee or elbow in the wall");
				return FString();
			};
		}
	}
	Rebuild();
}

UStaticMeshComponent* AClimberActor::Shape(const TCHAR* Mesh, FVector Location, FVector Scale, FLinearColor Color)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, Mesh));
	Part->SetupAttachment(RootComponent);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(Scale);
	Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	UMaterialInstanceDynamic* Paint = Part->CreateDynamicMaterialInstance(0, LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")));
	if (Paint) Paint->SetVectorParameterValue(TEXT("Color"), Color);
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

void AClimberActor::Rebuild()
{
	HumanRig.Reset();
	Sag = SagSpeed = 0; Gaze = FVector::ZeroVector;
	HipDepth = 22; ChestDepth = 22; FallTime = -1;
	for (int32 L = 0; L < 4; L++) { PrevLatch[L] = 1; PrevHeld[L] = true; FromHold[L] = false; }
	Banked = 0;
	HasLast = false;
	if (Rigged)
	{
		Walker->ForceClothNextUpdateTeleportAndReset();
	}
	else if (Parts.IsEmpty())
	{
		Dress();
	}
}

void AClimberActor::Dress()
{
	const TCHAR* Rod = TEXT("/Engine/BasicShapes/Cylinder.Cylinder");
	const TCHAR* Ball = TEXT("/Engine/BasicShapes/Sphere.Sphere");
	const FLinearColor Skin(0.78f, 0.55f, 0.42f), Tee(0.88f, 0.85f, 0.76f), Shorts(0.24f, 0.29f, 0.17f), Shoe(0.9f, 0.72f, 0.08f);
	Figure.Reset();
	// 0 torso, 1 head, 2 shoulders, 3 pelvis, then per limb: upper, lower, joint, end
	Figure.Add(Shape(Rod, FVector::ZeroVector, FVector::OneVector, Tee));
	Figure.Add(Shape(Ball, FVector::ZeroVector, FVector(0.2f, 0.19f, 0.24f), Skin));
	Figure.Add(Shape(Rod, FVector::ZeroVector, FVector::OneVector, Tee));
	Figure.Add(Shape(Rod, FVector::ZeroVector, FVector::OneVector, Shorts));
	for (int32 Limb = 0; Limb < 4; Limb++)
	{
		const bool Arm = Limb >= 2;
		Figure.Add(Shape(Rod, FVector::ZeroVector, FVector::OneVector, Arm ? Tee : Shorts));
		Figure.Add(Shape(Rod, FVector::ZeroVector, FVector::OneVector, Skin));
		Figure.Add(Shape(Ball, FVector::ZeroVector, FVector(Arm ? 0.1f : 0.13f), Skin));
		Figure.Add(Shape(Ball, FVector::ZeroVector, Arm ? FVector(0.1f, 0.08f, 0.11f) : FVector(0.22f, 0.1f, 0.1f), Arm ? Skin : Shoe));
	}
	// 20 neck
	Figure.Add(Shape(Rod, FVector::ZeroVector, FVector::OneVector, Skin));
	// per hand: a wrist, then index to pinky and the thumb, two segments each. per foot: an ankle
	Extras.Reset();
	for (int32 Hand = 0; Hand < 2; Hand++)
	{
		Extras.Add(Shape(Ball, FVector::ZeroVector, FVector(0.06f), Skin));
		for (int32 Bone = 0; Bone < 10; Bone++) Extras.Add(Shape(Rod, FVector::ZeroVector, FVector::OneVector, Skin));
	}
	for (int32 Foot = 0; Foot < 2; Foot++) Extras.Add(Shape(Ball, FVector::ZeroVector, FVector(0.085f), Skin));
}

void AClimberActor::Orient(UStaticMeshComponent* Part, FVector Center, FVector Along, FVector Out, FVector Size) const
{
	// a basic sphere stretched to Size centimeters, its length along Along and its thickness along Out
	const FVector X = Along.GetSafeNormal();
	const FVector Z = (Out - X * FVector::DotProduct(Out, X)).GetSafeNormal();
	Part->SetRelativeLocation(Center);
	Part->SetRelativeRotation(FMatrix(X, Z ^ X, Z, FVector::ZeroVector).Rotator());
	Part->SetRelativeScale3D(Size / 100);
}

void AClimberActor::Fingers(int32 Hand, FVector Palm, FVector Over, FVector Out, FVector Thumb, const FString& Grip) const
{
	// how each finger bends for the grip: the first bone's direction, then the second's, as (over, out) weights
	FVector2D First(0.9f, 0.15f), Second(0.6f, -0.5f);
	float Spread = 1;
	if (Grip == TEXT("jug") || Grip == TEXT("pinch")) { First = { 1, 0.1f }; Second = { -0.25f, -1 }; }
	else if (Grip == TEXT("crimp")) { First = { 0.45f, 0.9f }; Second = { 0.3f, -1 }; Spread = 0.85f; }
	else if (Grip == TEXT("sloper")) { First = { 1, -0.25f }; Second = { 0.85f, -0.5f }; Spread = 1.25f; }
	else if (Grip == TEXT("smear")) { First = { 1, -0.05f }; Second = { 1, -0.1f }; Spread = 1.2f; }
	const int32 Base = Hand * 11 + 1;
	const float Lengths[4] = { 4.4f, 4.9f, 4.6f, 3.7f };
	for (int32 Finger = 0; Finger < 4; Finger++)
	{
		// index nearest the thumb, out to the pinky
		const FVector Knuckle = Palm + Over * 4.2f + Thumb * (2.7f - Finger * 1.8f) * Spread - Out * 0.5f;
		FVector2D One = First, Two = Second;
		// a pocket takes the two middle fingers; the others curl out of the way
		if (Grip == TEXT("pocket")) { const bool Used = Finger == 1 || Finger == 2; One = Used ? FVector2D(0.6f, -0.8f) : FVector2D(0.4f, 0.6f); Two = Used ? FVector2D(0.5f, -0.9f) : FVector2D(-0.8f, -0.4f); }
		const FVector Middle = Knuckle + (Over * One.X + Out * One.Y).GetSafeNormal() * Lengths[Finger] * 0.55f;
		const FVector Tip = Middle + (Over * Two.X + Out * Two.Y).GetSafeNormal() * Lengths[Finger] * 0.45f;
		Segment(Extras[Base + Finger * 2], Knuckle, Middle, 1.05f);
		Segment(Extras[Base + Finger * 2 + 1], Middle, Tip, 0.95f);
	}
	// the thumb: wrapped over the index on a crimp, opposed across the hold on a pinch, alongside otherwise
	const FVector Root = Palm + Thumb * 3.6f - Over * 1.5f;
	FVector One = Over * 0.6f + Thumb * 0.7f, Two = Over * 0.8f - Thumb * 0.2f;
	if (Grip == TEXT("crimp")) { One = Over * 0.7f + Out * 0.6f; Two = -Thumb * 0.9f + Over * 0.3f; }
	else if (Grip == TEXT("pinch")) { One = Over * 0.4f + Thumb * 0.5f - Out * 0.7f; Two = Over * 0.6f - Thumb * 0.3f - Out * 0.6f; }
	const FVector Middle = Root + One.GetSafeNormal() * 3.4f;
	Segment(Extras[Base + 8], Root, Middle, 1.3f);
	Segment(Extras[Base + 9], Middle, Middle + Two.GetSafeNormal() * 2.9f, 1.1f);
}

void AClimberActor::Segment(UStaticMeshComponent* Part, FVector Start, FVector End, float Radius) const
{
	// the basic cylinder is 100cm tall and 100cm wide along z
	const FVector Along = End - Start;
	Part->SetRelativeLocation((Start + End) / 2);
	Part->SetRelativeRotation(FRotationMatrix::MakeFromZ(Along.GetSafeNormal()).Rotator());
	Part->SetRelativeScale3D(FVector(Radius / 50, Radius / 50, Along.Size() / 100));
}

FVector AClimberActor::Place(FVector2D Point, float Depth) const
{
	return Sim.Spec.Routes[Sim.Route].Map(Point, Depth);
}

void AClimberActor::SetInside(bool Inside)
{
	if (!Rigged || HeadHidden == Inside) return;
	HeadHidden = Inside;
	if(Inside) Walker->HideBoneByName(TEXT("Head"),EPhysBodyOp::PBO_None);
	else Walker->UnHideBoneByName(TEXT("Head"));
	Walker->SetCastShadow(!Inside);
}

void AClimberActor::Walk(FVector At, float Yaw, float Speed)
{
	if (!Rigged || !Walker) return;
	Walker->SetVisibility(true);
	Walker->SetWorldLocationAndRotation(At, FRotator(0, FMath::RadiansToDegrees(Yaw) - 90, 0));
	Eye=Walker->GetBoneLocation(TEXT("Head"))+FVector::UpVector*12;
 const auto& Ref=Walker->GetSkeletalMeshAsset()->GetRefSkeleton();
 const int32 Eyes=Ref.FindBoneIndex(TEXT("PlayerEyes"));
 if(Eyes>=0)
 {
  const FTransform Head=Walker->GetSocketTransform(TEXT("Head"),RTS_World);
  const FVector Offset=Ref.GetRefBonePose()[Eyes].GetTranslation()*Ref.GetRefBonePose()[0].GetScale3D().X;
  Eye=Head.GetLocation()+Head.GetRotation().RotateVector(Offset);
 }
	if (ULocomotionAnim* Anim = Cast<ULocomotionAnim>(Walker->GetAnimInstance()))
	{
		Anim->Climbing=false;
        Anim->GroundSpeed = Speed;
		Anim->ClothEnabled=ClothEnabled;
	}
}

void AClimberActor::Pose()
{
	// long legs keep the hips in close and a high step pushes them out; straight arms on steep ground let the chest hang back
	const float Ease = FMath::Min(1.f, GetWorld()->GetDeltaSeconds() * 5);
	auto On = [&](int32 L) { return (Sim.Grips[L] >= 0 || Sim.Smeared[L]) && Sim.Arrived(L); };
	float FootY = 0; int32 Feet = 0;
	for (int32 L = 0; L < 2; L++) if (On(L) && (Sim.Grips[L] >= 0 || Sim.Loads[L] > 30)) { FootY += Sim.Tips[L].Y; Feet++; }
	FootY = Feet ? FootY / Feet : Sim.Position.Y - 1;
	const float Compress = FMath::Clamp(1 - (Sim.Position.Y - FootY) / .75f, 0.f, 1.f);
	float Hang = 0;
	for (int32 L = 2; L < 4; L++) if (On(L) && Sim.Grips[L] >= 0) Hang = FMath::Max(Hang, Sim.Distance(L, Sim.Tips[L]) / (Sim.Body.Lengths[L] * 2));
	const float Straight = FMath::Clamp((Hang - .75f) / .2f, 0.f, 1.f);
	// With both hands around shoulder height or higher and a foot supporting,
	// settle back into the stance while the arms still have length to give.
	float SettleBack = 0;
	if (Feet > 0 && On(2) && On(3) && Sim.Grips[2] >= 0 && Sim.Grips[3] >= 0)
	{
		const float Above = FMath::Min(Sim.Tips[2].Y - Sim.Joint(2).Y, Sim.Tips[3].Y - Sim.Joint(3).Y);
		SettleBack = FMath::SmoothStep(-.12f, .12f, Above) * (1 - FMath::SmoothStep(.74f, .95f, Hang));
	}
	const float Steep = FMath::Max(0.f, Sim.Spec.Routes[Sim.Route].Steep(Sim.Position.Y));
	// the body swings out when the feet cut loose, and peels away from the wall as it starts to tip off
	const float Loose = Feet == 0 ? 1.f : 0.f, Peel = FMath::Clamp(Sim.Tipping / .35f, 0.f, 1.f);
	// on three points the body settles to suit: a hand off and it leans in over its feet, a foot off and it hangs back off straight arms
	const float HandOff = !On(2) || !On(3) ? 1.f : 0.f, FootOff = Feet == 1 ? 1.f : 0.f;
	// a foot brought up near hip height runs the hip out of room: the pelvis comes away from the wall to make it
	float HighStep = 0;
	for (int32 L = 0; L < 2; L++)
	{
		if (!On(L) && Sim.Grips[L] < 0 && !Sim.Smeared[L] && Sim.Reaching != L) continue;
		HighStep = FMath::Max(HighStep, FMath::SmoothStep(-.5f, -.1f, float(Sim.Tips[L].Y - Sim.Joint(L).Y)));
	}
	const float HandSupport = float(On(2) && Sim.Grips[2] >= 0) + float(On(3) && Sim.Grips[3] >= 0);
	const float StepRise = 12.f * HighStep * (.35f + .325f * HandSupport);
	HipDepth += (42 + 22 * Compress + 10 * Steep * Straight + 23 * Loose + 30 * Peel - 5 * HandOff + 5 * FootOff + 29 * HighStep + 20 * SettleBack - HipDepth) * Ease;
	// the chest hangs back off the wall as far as leaves the more stretched arm nearly straight: hands close in give long
	// easy arms, hands far apart or high keep the chest in and the arms working
	float Lean = 34;
	for (int32 L = 2; L < 4; L++)
	{
		if (!On(L) || Sim.Grips[L] < 0) continue;
		const float Long = Sim.Body.Lengths[L] * 2 * .9f, Across = FVector2D::Distance(Sim.Joint(L), Sim.Tips[L]);
		Lean = FMath::Min(Lean, FMath::Sqrt(FMath::Max(0.f, Long * Long - Across * Across)) * 100);
	}
	ChestDepth += (FMath::Clamp(Lean, 18.f, 34.f) + 6 * Steep + 8 * Loose + 25 * Peel - 4 * HandOff + 3 * FootOff + 10 * SettleBack - ChestDepth) * FMath::Min(1.f, GetWorld()->GetDeltaSeconds() * 3);
	FHumanGoals Goals = FHumanGoals::FromSim(Sim, Gap, FirstPersonAmount, HipDepth, ChestDepth);
	Goals.RelaxedSupport = Sim.Fallen ? 0.f : SettleBack * FMath::SmoothStep(.65f, .9f, float(Goals.PanelUp[0].Z));
	if (!Sim.Fallen)
	{
		const float Dt = FMath::Min(GetWorld()->GetDeltaSeconds(), .05f);
		const FClimbRoute& Face = Sim.Spec.Routes[Sim.Route];
		const FClimbPanel& Frame = Face.At(Sim.Position.Y);
		for (int32 L = 0; L < 4; L++)
		{
			const bool Travelling = Sim.Latch[L] < 0;
			if (Travelling && PrevLatch[L] >= 0) { TravelFrom[L] = Sim.Tips[L]; FromHold[L] = PrevHeld[L]; }
			// a caught hold takes weight: the body sags onto it and recovers, further when it was caught on the move
			if (!Travelling && PrevLatch[L] < 0 && Goals.Held[L]) SagSpeed -= (L >= 2 ? 34 : 14) * (1 + 2 * (1 - Sim.Latch[L]));
			if (Travelling)
			{
				// a hand or foot peels off its hold, carries clear of the wall and comes back in to land, instead of sliding along the surface
				float Lift = FMath::SmoothStep(0.f, .1f, float(FVector2D::Distance(Sim.Spot(L), Sim.Tips[L])));
				if (FromHold[L]) Lift = FMath::Min(Lift, FMath::SmoothStep(0.f, .08f, float(FVector2D::Distance(TravelFrom[L], Sim.Tips[L]))));
				Goals.Ends[L] += Goals.Out[L] * (Lift - 1) * (Gap * .5f - 4);
				if (L >= 2 && Lift > 0)
				{
					float Clearance = 0;
					for (const auto& Dome : Goals.Domes)
					{
						if (!Dome.Surface.IsValid() || FVector::DotProduct(Dome.N, Goals.Out[L]) < .5) continue;
						const FVector Local = Dome.Surface->Frame.InverseTransformPosition(Goals.Ends[L]);
						if (Local.X < Dome.Surface->Min.X || Local.X > Dome.Surface->Max.X
							|| Local.Y < Dome.Surface->Min.Y || Local.Y > Dome.Surface->Max.Y) continue;
						Clearance = FMath::Max(Clearance, FMath::Clamp(6.f - Dome.Surface->Clear(Goals.Ends[L]), 0.f, 20.f));
					}
					Goals.Ends[L] += Goals.Out[L] * Lift * Clearance;
				}
			}
			PrevLatch[L] = Sim.Latch[L]; PrevHeld[L] = Goals.Held[L];
		}
		SagSpeed += (-180 * Sag - 22 * SagSpeed) * Dt;
		Sag = FMath::Clamp(Sag + SagSpeed * Dt, -6.f, 3.f);
		for (int32 L = 0; L < 4; L++)
		{
			Goals.Roots[L] += Frame.Up * ((L < 2 ? Sag + StepRise : .8f * Sag + .45f * StepRise) - 20.f * SettleBack * (1 - HighStep));
		}
		// the eyes follow the work: the hand or foot being moved and where it is going; otherwise the wall just above,
		// drifting the way the hips are travelling. in first person the camera already does the looking
		FVector Want = -Frame.Out;
		if (FirstPersonAmount < .5f && !Eye.IsZero())
		{
			int32 Mover = Selected >= 0 && Selected < 4 ? Selected : -1;
			for (int32 L = 0; L < 4 && Mover < 0; L++) if (Sim.Latch[L] < 0) Mover = L;
			const FVector2D Point = Mover >= 0 ? FMath::Lerp(Sim.Tips[Mover], Sim.Spot(Mover), .4f)
				: Sim.Position + FVector2D(0, .8f) + (Sim.Velocity * .5f + Sim.Lean * .25f).GetClampedToMaxSize(.35f);
			Want = (Want * .25f + (Face.Map(Point, 6) - Eye).GetSafeNormal() * .75f).GetSafeNormal();
		}
		Gaze = Gaze.IsNearlyZero() ? Want : FMath::VInterpTo(Gaze, Want, Dt, 5).GetSafeNormal();
		Goals.Look = Gaze;
		FallTime = -1; FallFrom = Goals; BodyAt = (Goals.Roots[0] + Goals.Roots[1]) / 2;
	}
	else
	{
		// off the wall. a short controlled drop lands on its feet and soaks it up; a longer one lands feet first, sits back and
		// rolls over the back; coming off sideways, tipping, or out of a roof lands flat. then the climber gets up where they landed
		const FVector Up = FVector::UpVector;
		FVector Out = FallFrom.Out[0].GetSafeNormal2D();
		if (Out.IsNearlyZero()) Out = FVector(1, 0, 0);
		const FVector P0 = (FallFrom.Roots[0] + FallFrom.Roots[1]) / 2, C0 = (FallFrom.Roots[2] + FallFrom.Roots[3]) / 2;
		FVector S = FallFrom.Roots[3] - FallFrom.Roots[2];
		S -= Out * FVector::DotProduct(S, Out); S.Z = 0; S = S.GetSafeNormal();
		if (S.IsNearlyZero()) S = FVector::CrossProduct(Up, Out);
		const float HipHalf = FVector::Distance(FallFrom.Roots[0], FallFrom.Roots[1]) / 2, ShoulderHalf = FVector::Distance(FallFrom.Roots[2], FallFrom.Roots[3]) / 2;
		const float Torso = FVector::Distance(P0, C0), Stand = Sim.Body.Lengths[0] * 186;
		auto Ramp = [](float A, float B, float X) { return FMath::SmoothStep(A, B, X); };
		if (FallTime < 0)
		{
			FallTime = 0;
			FallSpot = AGymActor::ClearWalker(P0 + Out * 60, Out);
			FallDrop = P0.Z - (FallSpot.Z + Stand);
			const bool Wild = FMath::Abs(Sim.Angle) > .9f || Sim.Tipping > .3f || Sim.Spec.Routes[Sim.Route].Steep(Sim.Position.Y) > .45f;
			FallKind = Wild ? (FallDrop > 40 ? 2 : 1) : FallDrop < 110 ? 0 : 1;
			FParse::Value(FCommandLine::Get(), TEXT("fallkind="), FallKind);
			FallKind = FMath::Clamp(FallKind, 0, 2);
			const float Touch = FallSpot.Z + (FallKind == 2 ? 14.f : Stand * .92f);
			FallAir = FMath::Sqrt(FMath::Max(float(P0.Z - Touch), 4.f) / 490);
			FallLength = FallAir + (FallKind == 0 ? 1.1f : FallKind == 1 ? 2.5f : 2.4f);
			FallYaw = FMath::Atan2(-Out.Y, -Out.X);
		}
		else FallTime += GetWorld()->GetDeltaSeconds() * 1.12f;
		const float T = FallTime, G = T - FallAir, Fly = FMath::Clamp(T / FallAir, 0.f, 1.f), Mat = FallSpot.Z;
		const float Touch = Mat + (FallKind == 2 ? 14.f : Stand * .92f);
		// Theta tips the trunk back from upright toward lying head-away-from-the-wall; Back is how far the hips sit behind the feet;
		// Tuck lifts the feet off the mat with the knees up; Rise is how far along standing back up
		float Height = 0, Theta = 0, Back = FallKind == 2 ? 45.f : 0.f, Tuck = 0, Rise = 0;
		if (G < 0)
		{
			Height = (P0.Z < Touch ? FMath::Lerp(float(P0.Z), Touch, Fly) : FMath::Max(Touch, float(P0.Z) - 490 * T * T)) - Mat;
			if (FallKind == 2) { Theta = 1.4f * Ramp(0, 1, Fly); Tuck = Ramp(.3f, 1, Fly); }
		}
		else if (FallKind == 0)
		{
			const float Dip = FMath::Clamp(FallDrop / 260, .12f, .4f);
			Rise = Ramp(.3f, 1, G);
			Height = Stand * FMath::Lerp(FMath::Lerp(.92f, .92f - Dip, Ramp(0.f, .22f, G)), .97f, Rise);
		}
		else if (FallKind == 1)
		{
			const float Sit = Ramp(.18f, .55f, G), Squat = Ramp(1.3f, 1.8f, G);
			Rise = Ramp(1.8f, 2.4f, G);
			Theta = 1.75f * Ramp(.2f, .75f, G) - 1.15f * Ramp(.8f, 1.3f, G) - .6f * Squat;
			Height = FMath::Lerp(FMath::Lerp(FMath::Lerp(FMath::Lerp(Stand * .92f, Stand * .45f, Ramp(0, .22f, G)), 14.f, Sit), Stand * .5f, Squat), Stand * .97f, Rise);
			Back = 45 * Sit * (1 - Squat);
			Tuck = Ramp(.9f, 1.4f, Theta);
		}
		else
		{
			const float Squat = Ramp(1.2f, 1.7f, G);
			Rise = Ramp(1.7f, 2.3f, G);
			Theta = 1.4f + .17f * Ramp(0, .1f, G) - .97f * Ramp(.6f, 1.1f, G) - .6f * Squat;
			Height = FMath::Lerp(FMath::Lerp(14.f, Stand * .5f, Squat), Stand * .97f, Rise);
			Back = 45 * (1 - Squat);
			Tuck = 1 - Ramp(.5f, 1, G);
		}
		const float Lying = Ramp(.7f, 1.3f, Theta);
		const FVector Stance(FallSpot.X, FallSpot.Y, Mat);
		const FVector Landing = Stance + Out * Back;
		const FVector Pelvis(FMath::Lerp(P0.X, Landing.X, Fly), FMath::Lerp(P0.Y, Landing.Y, Fly), Mat + Height);
		const FVector Trunk = Up * FMath::Cos(Theta) + Out * FMath::Sin(Theta), Front = -Out * FMath::Cos(Theta) + Up * FMath::Sin(Theta);
		const FVector Chest = Pelvis + Trunk * Torso;
		// limbs let go and the body leaves its wall pose over the first quarter second instead of snapping
		const float Free = Ramp(0, .28f, T);
		const FVector Carried = Pelvis - P0;
		Goals = FallFrom;
		Goals.Domes.Reset();
		for (int32 L = 0; L < 4; L++)
		{
			const float Side = L % 2 == 1 ? 1.f : -1.f;
			const FVector Root = (L < 2 ? Pelvis + S * Side * HipHalf : Chest + S * Side * ShoulderHalf);
			FVector End, Pole, Along;
			if (L < 2)
			{
				const FVector Planted = Stance + S * Side * 13 + Up * 9;
				FVector Under = Pelvis - Up * Stand * .86f + S * Side * 11 - Out * 4;
				Under.Z = FMath::Max(Under.Z, Mat + 9);
				const FVector Down = G >= 0 ? Planted : FMath::Lerp(Under, Planted, Ramp(.6f, 1, Fly));
				End = FMath::Lerp(Down, Pelvis - Out * 38 + Up * 22 + S * Side * 12, Tuck);
				Along = FMath::Lerp((-Out * 14 - Up * 9).GetSafeNormal(), (Up - Out * .3f).GetSafeNormal(), Tuck).GetSafeNormal();
				Pole = (FMath::Lerp(-Out, Up, Tuck) + S * Side * .3f).GetSafeNormal();
			}
			else
			{
				// arms out for balance in the air, braced forward on landing, pulled in across the chest for a roll, down at the sides to finish
				End = FallKind == 2 && G < 0 ? Root + Front * 12 + Trunk * 8 + S * Side * 38 : Root + Front * 32 + Trunk * 4 + S * Side * 16;
				if (G >= 0) End = FMath::Lerp(End, Root + Front * 34 - Trunk * 14 + S * Side * 8, Ramp(0, .15f, G));
				End = FMath::Lerp(End, Root + Front * 20 - Trunk * 24 - S * Side * 4, Lying);
				End = FMath::Lerp(End, Root - Trunk * 52 + Front * 8 + S * Side * 7, Rise);
				Along = (End - Root).GetSafeNormal();
				Pole = (-Front * .6f - Trunk * .5f + S * Side * .5f).GetSafeNormal();
				Goals.Out[L] = -Front;
			}
			Goals.Roots[L] = FMath::Lerp(FallFrom.Roots[L] + Carried, Root, Free);
			Goals.Ends[L] = FMath::Lerp(FallFrom.Ends[L] + Carried, End, Free);
			Goals.Poles[L] = FMath::Lerp(FallFrom.Poles[L], Pole, Free).GetSafeNormal();
			Goals.Along[L] = FMath::Lerp(FallFrom.Along[L], Along, Free).GetSafeNormal();
			Goals.PanelUp[L] = Up;
			Goals.Held[L] = false; Goals.Weight[L] = 0; Goals.Want[L] = 0; Goals.Grips[L] = TEXT("free");
		}
		Goals.ShoeSurfaces[0].Reset(); Goals.ShoeSurfaces[1].Reset(); Goals.HandSurfaces[0].Reset(); Goals.HandSurfaces[1].Reset();
		Goals.Look = (Front - Trunk * .15f).GetSafeNormal();
		BodyAt = Pelvis;
	}
	FHumanPose Pose = HumanRig.Solve(Goals, GetWorld()->GetDeltaSeconds());
	Eye = Pose.Eye;
	if (FParse::Param(FCommandLine::Get(), TEXT("contactlog")))
	{
		UE_LOG(LogTemp, Display, TEXT("CONTACT feet %.3f %.3f hands %.3f %.3f effort %.3f shift %.3f %.3f %.3f"),
			Pose.Miss[0], Pose.Miss[1], Pose.Miss[2], Pose.Miss[3], Pose.SupportEffort, Pose.Shift.X, Pose.Shift.Y, Pose.Shift.Z);
	}
	// -shakelog: one line a frame of where the sim and the rig put the body, for measuring jitter
	// the elbows (or with -kneelog the knees) as they were actually drawn last frame, after the animation's own smoothing
	if (FParse::Param(FCommandLine::Get(), TEXT("shakelog")))
	{
		const bool Knees = FParse::Param(FCommandLine::Get(), TEXT("kneelog"));
		const FVector Shown[2] = { Walker->GetBoneLocation(Knees ? TEXT("RightLeg") : TEXT("RightForeArm")), Walker->GetBoneLocation(Knees ? TEXT("LeftLeg") : TEXT("LeftForeArm")) };
		UE_LOG(LogTemp, Display, TEXT("SHAKE %.4f sim %.5f %.5f %.5f shift %.3f %.3f %.3f tilt %.4f %.4f eye %.3f %.3f %.3f rh %.3f %.3f %.3f lh %.3f %.3f %.3f"),
			GetWorld()->GetTimeSeconds(), Sim.Position.X, Sim.Position.Y, Sim.Angle, Pose.Shift.X, Pose.Shift.Y, Pose.Shift.Z, Pose.PelvisPitch, Pose.PelvisYaw,
			Pose.Eye.X, Pose.Eye.Y, Pose.Eye.Z, Shown[0].X, Shown[0].Y, Shown[0].Z, Shown[1].X, Shown[1].Y, Shown[1].Z);
	}
	Walker->SetWorldTransform(Pose.World);
	if (auto* Anim = Cast<ULocomotionAnim>(Walker->GetAnimInstance()))
	{
		Anim->Climbing = true;
		Anim->LandingTime = Sim.Fallen && FallKind == 0 ? FallTime - FallAir : -10.f;
		Anim->ClimbingPose = MoveTemp(Pose.Local);
		Anim->ClothEnabled = ClothEnabled;
	}

}

void AClimberActor::Tick(float Delta)
{
	Super::Tick(Delta);
	const bool SuspendCloth = Paused;
	if (Rigged && Walker->GetSkeletalMeshAsset()->HasActiveClothingAssets())
	{
		Walker->bDisableClothSimulation = !ClothEnabled;
		Walker->ClothBlendWeight = ClothEnabled ? 1.f : 0.f;
		if (SuspendCloth != Walker->IsClothingSimulationSuspended())
		{
			if (SuspendCloth) Walker->SuspendClothingSimulation();
			else Walker->ResumeClothingSimulation();
		}
	}
	if (Paused) return;
	if (Walking) return;
	Walker->SetVisibility(Rigged);
	// fixed 60 hz steps so the feel matches the unity and godot prototypes
	Banked = FMath::Min(Banked + Delta, 0.25f);
	FVector2D FromPosition = Sim.Position, FromTips[4];
	float FromAngle = Sim.Angle;
	bool Stepped = false;
	while (Banked >= 1.f / 60)
	{
		FromPosition = Sim.Position;
		FromAngle = Sim.Angle;
		for (int32 L = 0; L < 4; L++) FromTips[L] = Sim.Tips[L];
		Sim.Step(1.f / 60);
		Banked -= 1.f / 60;
		Stepped = true;
	}
	// draw between the last two steps so motion stays even when the display runs faster than the sim
	if (Stepped) { LastPosition = FromPosition; LastAngle = FromAngle; for (int32 L = 0; L < 4; L++) LastTips[L] = FromTips[L]; HasLast = true; }
	const FVector2D NowPosition = Sim.Position, NowTips[4] = {Sim.Tips[0], Sim.Tips[1], Sim.Tips[2], Sim.Tips[3]};
	const float NowAngle = Sim.Angle;
	// a restart or big jump teleports, so it is drawn as is
	const bool Blend = HasLast && !Sim.Fallen && FVector2D::Distance(LastPosition, NowPosition) < .5f;
	if (Blend)
	{
		const float T = Banked * 60;
		Sim.Position = FMath::Lerp(LastPosition, NowPosition, T);
		Sim.Angle = FMath::Lerp(LastAngle, NowAngle, T);
		for (int32 L = 0; L < 4; L++) Sim.Tips[L] = FMath::Lerp(LastTips[L], NowTips[L], T);
	}
	ON_SCOPE_EXIT { Sim.Position = NowPosition; Sim.Angle = NowAngle; for (int32 L = 0; L < 4; L++) Sim.Tips[L] = NowTips[L]; };
	if (Rigged)
	{
		Pose();
	}
	else Draw();
}

void AClimberActor::Draw()
{
	const UWorld* World = GetWorld();
	if (Figure.Num() < 21 || Extras.Num() < 24) return;
	Clock += World->GetDeltaSeconds();
	// everything maps through the problem's panels into gym space; the hips' panel gives the body its sense of out, across and up
	const FClimbRoute& Face = Sim.Spec.Routes[Sim.Route];
	auto At = [&](FVector2D Point, float Depth) { return Face.Map(Point, Depth); };
	const FClimbPanel& Frame = Face.At(Sim.Position.Y);
	FVector Roots[4];
	for (int32 Limb = 0; Limb < 4; Limb++) Roots[Limb] = At(Sim.Joint(Limb), Gap - 8 + FirstPersonAmount*(Limb<2?30:26));
	const FVector Pelvis = (Roots[0] + Roots[1]) / 2;
	const FVector Chest = (Roots[2] + Roots[3]) / 2;
	const FVector Up = (Chest - Pelvis).GetSafeNormal();
	Segment(Figure[0], Pelvis + Up * 4, Chest + Up * 6, 17);
	Figure[1]->SetRelativeLocation(Chest + Up * 24 - Frame.Out * 3);
	Segment(Figure[20], Chest + Up * 4, Chest + Up * 14 - Frame.Out * 2, 6);
	Figure[1]->SetRelativeRotation(FRotationMatrix::MakeFromZ(Up).Rotator());
	Segment(Figure[2], Roots[2], Roots[3], 8);
	Segment(Figure[3], Roots[0] - (Roots[1] - Roots[0]) * 0.3f, Roots[1] + (Roots[1] - Roots[0]) * 0.3f, 13);
	for (int32 Limb = 0; Limb < 4; Limb++)
	{
		const bool Arm = Limb >= 2;
		const float Side = Limb % 2 == 0 ? -1 : 1;
		const bool Held = (Sim.Grips[Limb] >= 0 || Sim.Smeared[Limb]) && Sim.Arrived(Limb);
		const FVector2D Tip = Sim.Tips[Limb];
		const FClimbPanel& Panel = Face.At(Tip.Y);
		const FVector Root = Roots[Limb];
		// the hand or foot's own frame first, so the limb ends at the wrist or ankle instead of the fingertips or toes
		FVector End, Contact = At(Tip, Held ? 3 : Gap * 0.5f);
		FVector Over = Panel.Up, Thumb = Panel.Right * -Side;
		FString Grip = TEXT("free");
		if (Arm)
		{
			if (Sim.Grips[Limb] >= 0)
			{
				const FClimbHold& Hold = Sim.Holds()[Sim.Grips[Limb]];
				Grip = Hold.Kind;
				// fingers point over the lip, away from the way the hold is pulled; a pinch is gripped from below its side
				if (Grip != TEXT("pinch")) Over = (Panel.Right * -Hold.Pull.X + Panel.Up * -Hold.Pull.Y).GetSafeNormal();
				if (!Held) Grip = TEXT("free");
			}
			else if (Sim.Smeared[Limb] && Held) Grip = TEXT("smear");
			else Over = (Contact - Root).GetSafeNormal();
			Thumb = (Thumb - Over * FVector::DotProduct(Thumb, Over) - Panel.Out * FVector::DotProduct(Thumb, Panel.Out)).GetSafeNormal();
			if (Thumb.IsNearlyZero()) Thumb = (Panel.Out ^ Over).GetSafeNormal() * -Side;
			// a hand near its limit trembles
			const float Shake = FMath::Clamp((Sim.Strain[Limb] - 0.85f) * 4, 0.f, 1.f) * FMath::Sin(Clock * 70 + Limb) * 0.6f;
			const FVector Palm = Contact - Over * 3.5f + Panel.Out * (Grip == TEXT("crimp") ? 5.5f : 3.8f) + Thumb * Shake;
			End = Palm - Over * 5.5f + Panel.Out * 0.8f;
			Orient(Figure[4 + Limb * 4 + 3], Palm, Over, Panel.Out, FVector(9.5f, 8.5f, 3.2f));
			Extras[(Limb - 2) * 11]->SetRelativeLocation(End);
			Fingers(Limb - 2, Palm, Over, Panel.Out, Thumb, Grip);
		}
		else
		{
			// a climbing shoe: toe on the hold, heel out from the wall and a little low, the ankle above the heel
			const FVector Toe = Contact;
			const FVector Ankle = Toe + Panel.Out * 13 + Panel.Up * 3;
			const FVector Heel = Toe + Panel.Out * 19 - Panel.Up * 1.5f;
			End = Ankle;
			Orient(Figure[4 + Limb * 4 + 3], (Toe + Heel) / 2 + Panel.Up * 2, Toe - Heel, Panel.Up, FVector(25, 9.5f, 9));
			Extras[22 + Limb]->SetRelativeLocation(Ankle);
		}
		// two-bone bend at the sim's real limb length: elbows drop down and out, knees splay out to the side.
		// knees lead upward: a foot below the hip splays the knee out (frog), a high step folds it up toward the chest
		const float Length = Sim.Body.Lengths[Limb] * 100;
		const FVector Toward = End - Root;
		const float Reach = FMath::Min(Toward.Size(), Length * 2 - 0.1f);
		const FVector Direction = Toward.GetSafeNormal();
		const FVector Lean = Arm ? FVector(0.6f, Side * 0.5f, -0.6f) : FVector(0.35f, Side * 0.3f, 1.f);
		const FVector Hint = Frame.Out * Lean.X + Frame.Right * Lean.Y + Frame.Up * Lean.Z;
		const FVector Pole = (Hint - Direction * FVector::DotProduct(Hint, Direction)).GetSafeNormal();
		const FVector Bend = Root + Direction * Reach / 2 + Pole * FMath::Sqrt(FMath::Max(0.f, Length * Length - Reach * Reach / 4));
		const FVector Joint = Root + Direction * Reach;
		const int32 First = 4 + Limb * 4;
		Segment(Figure[First], Root, Bend, Arm ? 6 : 9);
		Segment(Figure[First + 1], Bend, Joint, Arm ? 4.5f : 6.5f);
		Figure[First + 2]->SetRelativeLocation(Bend);
		if (Limb == Selected) DrawDebugSphere(World, Contact, 12, 10, FColor(255, 230, 64), false, -1, 0, 2);
	}
}
