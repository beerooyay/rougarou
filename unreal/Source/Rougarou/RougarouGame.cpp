#include "RougarouGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ClimberActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GymActor.h"

ARougarouGameMode::ARougarouGameMode()
{
	DefaultPawnClass = nullptr;
	PlayerControllerClass = ARougarouController::StaticClass();
}

void ARougarouGameMode::StartPlay()
{
	// build the gym before play begins so the controller finds it
	UWorld* World = GetWorld();
	if (TActorIterator<AClimberActor>(World))
	{
		Super::StartPlay();
		return;
	}
	// one key light from over the camera's shoulder so hold shadows fall short and soft onto the wall
	ADirectionalLight* Key = World->SpawnActor<ADirectionalLight>(FVector::ZeroVector, FRotator(-38, 200, 0));
	UDirectionalLightComponent* Sun = Cast<UDirectionalLightComponent>(Key->GetLightComponent());
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetIntensity(3.5f);
	Sun->SetLightSourceAngle(6);
	APointLight* Fill = World->SpawnActor<APointLight>(FVector(420, -150, 260), FRotator::ZeroRotator);
	Fill->PointLightComponent->SetMobility(EComponentMobility::Movable);
	Fill->PointLightComponent->SetIntensity(9000);
	Fill->PointLightComponent->SetAttenuationRadius(2000);
	Fill->PointLightComponent->SetCastShadows(false);
	World->SpawnActor<AGymActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	World->SpawnActor<AClimberActor>(FVector::ZeroVector, FRotator::ZeroRotator);
	Super::StartPlay();
}

ARougarouController::ARougarouController()
{
	bShowMouseCursor = true;
	bEnableClickEvents = false;
}

void ARougarouController::BeginPlay()
{
	Super::BeginPlay();
	// never capture the mouse: the cursor aims limbs and stays free to leave the window
	FInputModeGameAndUI Mode;
	Mode.SetHideCursorDuringCapture(false);
	Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Mode);
	if (TActorIterator<AClimberActor> Found(GetWorld()); Found) Climber = *Found;
	// open on the whole gym, then swoop in to the climber
	View = GetWorld()->SpawnActor<ACameraActor>(AGymActor::Overview, (AGymActor::Focus - AGymActor::Overview).Rotation());
	View->GetCameraComponent()->SetFieldOfView(55);
	// fixed exposure: auto exposure brightens the empty void and blows the holds out to white
	FPostProcessSettings& Look = View->GetCameraComponent()->PostProcessSettings;
	Look.bOverride_AutoExposureMethod = true;
	Look.AutoExposureMethod = AEM_Manual;
	Look.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Look.AutoExposureApplyPhysicalCameraExposure = false;
	Look.bOverride_AutoExposureBias = true;
	Look.AutoExposureBias = 0;
	Shooting = FParse::Param(FCommandLine::Get(), TEXT("shot"));
	SetViewTarget(View);
}

bool ARougarouController::Cursor(FVector2D& Out) const
{
	FVector Origin, Direction;
	if (!Climber || !DeprojectMousePositionToWorld(Origin, Direction)) return false;
	// meet the problem's panels: the nearest one the ray hits within its stretch of the wall wins
	const FClimbRoute& Face = Climber->Sim.Spec.Routes[Climber->Sim.Route];
	float Nearest = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Face.Panels.Num(); Index++)
	{
		const FClimbPanel& Panel = Face.Panels[Index];
		const float Facing = FVector::DotProduct(Direction, Panel.Out);
		if (FMath::Abs(Facing) < 0.0001f) continue;
		const float Distance = FVector::DotProduct(Panel.Origin - Origin, Panel.Out) / Facing;
		if (Distance <= 0 || Distance >= Nearest) continue;
		const FVector Hit = Origin + Direction * Distance - Panel.Origin;
		const float Up = FVector::DotProduct(Hit, Panel.Up) / 100;
		const bool Below = Index == 0 || Up >= -0.05f, Above = Index + 1 == Face.Panels.Num() || Up <= Panel.Length + 0.05f;
		if (!Below || !Above) continue;
		Nearest = Distance;
		Out = FVector2D(FVector::DotProduct(Hit, Panel.Right) / 100, Panel.Start + Up);
	}
	return Nearest < TNumericLimits<float>::Max();
}

FVector2D ARougarouController::Tip(int32 Index) const
{
	return Climber->Sim.Tips[Index];
}

void ARougarouController::Press(FVector2D Point)
{
	FClimbSim& Sim = Climber->Sim;
	if (Limb < 0)
	{
		// pick the hand or foot tip nearest the click
		float Closest = 0.3f;
		for (int32 Index = 0; Index < 4; Index++)
		{
			const float Gap = FVector2D::Distance(Tip(Index), Point);
			if (Gap < Closest) { Closest = Gap; Limb = Index; }
		}
		if (Limb < 0) return;
		// lifting a limb takes it off the wall: the other three carry the weight from this moment
		Previous = Sim.Grips[Limb];
		Pressed = Sim.Smeared[Limb];
		Post = Sim.Posts[Limb];
		Aim = Tip(Limb);
		Sim.Drop(Limb);
		return;
	}
	// commit whatever is under the hand or foot; a bad hold or a bare-wall press is allowed and physics decides if it sticks
	auto Nearest = [&](FVector2D Spot)
	{
		int32 Found = -1;
		float Closest = 0.15f;
		for (int32 Hold = 0; Hold < Sim.Holds().Num(); Hold++)
		{
			const float Gap = FVector2D::Distance(Sim.Holds()[Hold].Point, Spot);
			if (Gap < Closest) { Closest = Gap; Found = Hold; }
		}
		return Found;
	};
	// the hold under the cursor wins if the body can still get there by reaching; otherwise whatever the fingertips touch.
	// a hold just past the fingertips is caught by its near edge: a weak press there instead of nothing
	const int32 Wanted = Nearest(Point), Touched = Nearest(Aim);
	if (Wanted >= 0 && Sim.Grab(Limb, Wanted)) { Limb = -1; return; }
	if (Touched < 0 || !Sim.Grab(Limb, Touched)) Sim.Smear(Limb, Aim);
	Limb = -1;
}

void ARougarouController::Cancel()
{
	// put the limb back where it was if the body can still get it there, otherwise it hangs
	FClimbSim& Sim = Climber->Sim;
	if (Previous >= 0) Sim.Grab(Limb, Previous);
	else if (Pressed) Sim.Smear(Limb, Post);
	Limb = -1;
}

void ARougarouController::PlayerTick(float Delta)
{
	Super::PlayerTick(Delta);
	if (!Climber) return;
	FClimbSim& Sim = Climber->Sim;
	FVector2D Point;
	bool Over = Cursor(Point);
	// a fall restarts the climb underneath us; whatever limb was lifted is gone with it
	if (Sim.Fallen) { const FString Why = Sim.Hint; Sim.Restart(); Sim.Hint = Why; }
	if (Sim.Falls != Falls) { Falls = Sim.Falls; Limb = -1; }
	// -shot -lift: scripted left-hand move, to check reaching and placing without a person at the mouse
	if (Shooting && FParse::Param(FCommandLine::Get(), TEXT("lift")))
	{
		// -lift: take the right foot off and reach it into a high step, to check the hip limit and the knee
		if (Clock > 1.5f && Clock < 2 && Limb < 0) Press(Tip(0));
		if (Limb >= 0) { Over = true; Point = Sim.Joint(0) + FVector2D(-0.25f, 0.9f); }
	}
	if (Over && Limb >= 0)
	{
		// the lifted hand or foot follows the cursor until the limb runs out of length
		Aim = Sim.Clamp(Limb, Point);
	}
	// the reach target is where the cursor really is: past the fingertips, the body shifts and turns toward it
	Sim.Reaching = Limb;
	Sim.Target = Over && Limb >= 0 ? Point : Aim;
	if (Over && WasInputKeyJustPressed(EKeys::LeftMouseButton)) Press(Point);
	if (Limb >= 0 && (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::RightMouseButton))) Cancel();
	if (Limb >= 0 && WasInputKeyJustPressed(EKeys::X)) Limb = -1;
	if (WasInputKeyJustPressed(EKeys::R)) { Sim.Restart(); Limb = -1; }
	const int32 Count = Sim.Spec.Routes.Num();
	if (WasInputKeyJustPressed(EKeys::RightBracket) || WasInputKeyJustPressed(EKeys::LeftBracket))
	{
		Sim.Route = (Sim.Route + (WasInputKeyJustPressed(EKeys::RightBracket) ? 1 : Count - 1)) % Count;
		Sim.Restart();
		Climber->Rebuild();
		Limb = -1;
	}
	// wasd shifts the hips; the camera sits behind the climber, so screen right is the climber's right (sim -x)
	FVector2D Lean(0, 0);
	if (IsInputKeyDown(EKeys::A)) Lean.X += 1;
	if (IsInputKeyDown(EKeys::D)) Lean.X -= 1;
	if (IsInputKeyDown(EKeys::W)) Lean.Y += 1;
	if (IsInputKeyDown(EKeys::S)) Lean.Y -= 1;
	Sim.Lean = Lean * 0.6f;
	Climber->Selected = Limb;
	// g flips between the gym overview and the climb; the opening shot holds the overview for a few seconds
	Elapsed += Delta;
	if (WasInputKeyJustPressed(EKeys::G)) Wide = !Wide;
	const bool Overview = Wide || Elapsed < 3;
	const FVector Hips = Climber->Place(Sim.Position, 0);
	// stand back from the face, level with the climber, so overhangs never drive the camera into the floor
	const FClimbRoute& Face = Sim.Spec.Routes[Sim.Route];
	FVector Out = Face.At(Sim.Position.Y).Out;
	Out.Z = 0;
	// under a roof the panel faces the floor; keep the last good direction
	if (Out.Size() > 0.3f) Behind = Out.GetSafeNormal();
	Out = Behind;
	const FVector Goal = Overview ? AGymActor::Overview : Hips + Out * Face.Stand + Face.At(Sim.Position.Y).Right * Face.Aside + FVector(0, 0, 30);
	const FVector Look = Overview ? AGymActor::Focus : Hips + FVector(0, 0, 25);
	const float Speed = Overview || FVector::Distance(View->GetActorLocation(), Goal) > 300 ? 1.6f : 4;
	View->SetActorLocation(FMath::VInterpTo(View->GetActorLocation(), Goal, Delta, Speed));
	View->SetActorRotation(FMath::RInterpTo(View->GetActorRotation(), (Look - View->GetActorLocation()).Rotation(), Delta, Speed * 2));
	Status();
	// -shot: save a frame to Saved/shot.png and quit, so changes can be checked without a person at the screen
	if (Shooting)
	{
		Clock += Delta;
		if (Clock > 2 && !Gym)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("gym.png"), false, false);
			Gym = true;
		}
		if (Clock > 6.5f && !Taken)
		{
			FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir() / TEXT("shot.png"), false, false);
			Taken = true;
		}
		if (Clock > 8) ConsoleCommand(TEXT("quit"));
	}
}

void ARougarouController::Status() const
{
	static const TCHAR* Names[] = { TEXT("right foot"), TEXT("left foot"), TEXT("right hand"), TEXT("left hand") };
	const FClimbSim& Sim = Climber->Sim;
	const FString Line = FString::Printf(TEXT("rougarou / %s%s\n%s  |  move the mouse to reach  |  click to place it on whatever it touches\nwasd shifts hips  |  esc puts it back  |  x lets it hang  |  r retry  |  [ ] route  |  g gym view\n%s"),
		*Sim.Spec.Routes[Sim.Route].Name, Sim.Won ? TEXT("  /  SENT!") : FMath::Max(Sim.Strain[2], Sim.Strain[3]) > 0.9f ? TEXT("  /  fingers sliding") : TEXT(""),
		Limb >= 0 ? *FString::Printf(TEXT("%s off the wall"), Names[Limb]) : TEXT("click a hand or foot"), *Sim.Hint);
		auto Bar = [](float Value) { FString Out; for (int32 Cell = 0; Cell < 10; Cell++) Out += Cell < FMath::RoundToInt(Value * 10) ? TEXT("#") : TEXT("."); return Out; };
	const FString Pump = FString::Printf(TEXT("\npump  left [%s]  right [%s]%s"), *Bar(Sim.Pump[3]), *Bar(Sim.Pump[2]), Sim.Topped > 0 && !Sim.Won ? TEXT("  matched the top, hold it...") : TEXT(""));
	GEngine->AddOnScreenDebugMessage(1, 0, FColor::White, Line + Pump, false, FVector2D(1.4f, 1.4f));
}
