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
		if (Rigged) { Walker->SetSkeletalMesh(Mesh); Walker->SetAnimInstanceClass(ULocomotionAnim::StaticClass()); }
		if (Rigged && GateRig.Initialize(Ref, FHumanRig::ProfilePath()))
		{
			GateRig.ColdIterations = 3;
			// pose the body as if the limb were already on the hold; refuse what no joint can reach, or what puts a knee or elbow in the wall
			Sim.Gate = [this](const FClimbSim& Now, int32 Limb, int32 Hold) -> FString
			{
				FClimbSim Try = Now;
				Try.Gate = nullptr;
				Try.Grips[Limb] = Hold; Try.Smeared[Limb] = false; Try.Latch[Limb] = 0;
				Try.Tips[Limb] = Now.Holds()[Hold].Point; Try.Reaching = -1;
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
	for (int32 L = 0; L < 2; L++) if (On(L)) { FootY += Sim.Tips[L].Y; Feet++; }
	FootY = Feet ? FootY / Feet : Sim.Position.Y - 1;
	const float Compress = FMath::Clamp(1 - (Sim.Position.Y - FootY) / .75f, 0.f, 1.f);
	float Hang = 0;
	for (int32 L = 2; L < 4; L++) if (On(L) && Sim.Grips[L] >= 0) Hang = FMath::Max(Hang, Sim.Distance(L, Sim.Tips[L]) / (Sim.Body.Lengths[L] * 2));
	const float Straight = FMath::Clamp((Hang - .75f) / .2f, 0.f, 1.f);
	const float Steep = FMath::Max(0.f, Sim.Spec.Routes[Sim.Route].Steep(Sim.Position.Y));
	HipDepth += (16 + 22 * Compress + 10 * Steep * Straight - HipDepth) * Ease;
	ChestDepth += (20 + 10 * Straight + 6 * Steep - ChestDepth) * Ease;
	const FHumanGoals Goals = FHumanGoals::FromSim(Sim, Gap, FirstPersonAmount, HipDepth, ChestDepth);
	FHumanPose Pose = HumanRig.Solve(Goals, GetWorld()->GetDeltaSeconds());
	Eye = Pose.Eye;
	Walker->SetWorldTransform(Pose.World);
	if (auto* Anim = Cast<ULocomotionAnim>(Walker->GetAnimInstance()))
	{
		Anim->Climbing = true;
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
		if (Selected >= 0 && Selected < 4 && Sim.Holds().IsValidIndex(Hovered))
		{
			// the hold being aimed at: a ring on the resin, green when the body can take it, red when it cannot
			const FClimbHold& H = Sim.Holds()[Hovered];
			const FVector N = H.Normal.IsNearlyZero() ? Sim.Spec.Routes[Sim.Route].At(H.Point.Y).Out : H.Normal;
			const FVector C = H.Radius > 0 ? H.Center + N * (H.Height + 1) : Place(H.Point, 4);
			FVector A, B; N.FindBestAxisVectors(A, B);
			DrawDebugCircle(GetWorld(), C, FMath::Max(H.Radius, 5.f) + 3, 40, HoverReady ? FColor(90, 230, 140) : FColor(240, 90, 80), false, -1, 0, 1.5f, A, B, false);
		}
		if (Selected >= 0 && Selected < 4)
			DrawDebugSphere(GetWorld(), Place(Sim.Tips[Selected], Sim.Arrived(Selected) ? 3 : Gap * .5f),
				12, 10, FColor(255, 230, 64), false, -1, 0, 2);
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
