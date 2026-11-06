#include "RougarouGame.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ClimberActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "GymActor.h"
#include "ClimbView.h"
#include "RougarouUI.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Framework/Application/IInputProcessor.h"

class FRougarouTrackpadInput : public IInputProcessor
{
 TWeakObjectPtr<ARougarouController> Owner;
public:
 explicit FRougarouTrackpadInput(ARougarouController* In):Owner(In){}
 void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
 bool HandleMouseWheelOrGestureEvent(FSlateApplication& App,const FPointerEvent&,const FPointerEvent* Gesture) override
 {
  return App.IsActive() && Owner.IsValid() && Gesture && Gesture->GetGestureType()==EGestureEvent::Scroll && Owner->LookFromTrackpad(Gesture->GetGestureDelta());
 }
 bool HandleMouseMoveEvent(FSlateApplication& App,const FPointerEvent& Event) override
 {
  return App.IsActive() && Owner.IsValid() && Event.IsAltDown() && Owner->LookFromTrackpad(Event.GetCursorDelta());
 }
};

bool ARougarouController::LookFromTrackpad(FVector2D D)
{
 if(MenuOpen || !TrackpadLook || !Climber)return false;
 const float X=FMath::Clamp(float(D.X),-100.f,100.f)*.003f*LookSensitivity;
 const float Y=FMath::Clamp(float(D.Y),-100.f,100.f)*.003f*LookSensitivity;
 if(ReferencePreview){ClimbOrbitYaw+=X;ClimbOrbitPitch=FMath::Clamp(ClimbOrbitPitch+Y,-.6f,.8f);}
 else if(FirstPerson){OpeningHold=-1;if(Mode==0){WalkYaw+=X;RoamPitch=FMath::Clamp(RoamPitch-Y,-1.4f,1.4f);}else{FirstYaw=FMath::Clamp(FirstYaw+X,-2.8f,2.8f);FirstPitch=FMath::Clamp(FirstPitch-Y,-1.4f,1.4f);}}
 else if(Mode==0){WalkYaw-=X;WalkPitch=FMath::Clamp(WalkPitch+Y,-.35f,.9f);}
 else{Wide=false;Elapsed=3;ClimbOrbitYaw=FMath::Clamp(ClimbOrbitYaw+X,-1.2f,1.2f);ClimbOrbitPitch=FMath::Clamp(ClimbOrbitPitch+Y,-.6f,.8f);}
 return true;
}

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
	// a soft sky fill with Lumen bounce, so shadowed cloth and skin read instead of going black
	ASkyLight* Sky = World->SpawnActor<ASkyLight>(FVector(600, 0, 300), FRotator::ZeroRotator);
	Sky->GetLightComponent()->SetMobility(EComponentMobility::Movable);
	Sky->GetLightComponent()->SourceType = SLS_CapturedScene;
	Sky->GetLightComponent()->bRealTimeCapture = true;
	Sky->GetLightComponent()->SetIntensity(0.6f);
	Sky->GetLightComponent()->SetLightColor(FLinearColor(0.95f, 0.97f, 1));
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
	FInputModeGameAndUI Input;
	Input.SetHideCursorDuringCapture(false);
	Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
	SetInputMode(Input);
	if (TActorIterator<AClimberActor> Found(GetWorld()); Found) Climber = *Found;
	// -route=N drops you straight on a problem (screenshots and tests); otherwise you spawn on the mats
	int32 Given;
	if (FParse::Value(FCommandLine::Get(), TEXT("route="), Given)) Mode = 1;
	// open on the whole gym, then swoop in to the climber
	View = GetWorld()->SpawnActor<ACameraActor>(AGymActor::Overview, (AGymActor::Focus - AGymActor::Overview).Rotation());
	View->GetCameraComponent()->SetFieldOfView(55);
	View->GetCameraComponent()->bConstrainAspectRatio=false;
	ConsoleCommand(TEXT("r.SetNearClipPlane 1"));
	// fixed exposure: auto exposure brightens the empty void and blows the holds out to white
	FPostProcessSettings& Look = View->GetCameraComponent()->PostProcessSettings;
	Look.bOverride_AutoExposureMethod = true;
	Look.AutoExposureMethod = AEM_Manual;
	Look.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
	Look.AutoExposureApplyPhysicalCameraExposure = false;
	Look.bOverride_AutoExposureBias = true;
	Look.AutoExposureBias = 0;
	Look.bOverride_MotionBlurAmount=true;Look.MotionBlurAmount=0;
	Look.bOverride_MotionBlurMax=true;Look.MotionBlurMax=0;
	GConfig->GetBool(TEXT("Camera"),TEXT("TrackpadLook"),TrackpadLook,SettingsFile());
	GConfig->GetFloat(TEXT("Camera"),TEXT("Look"),LookSensitivity,SettingsFile());
	GConfig->GetFloat(TEXT("Camera"),TEXT("Distance"),CameraDistance,SettingsFile());
	GConfig->GetBool(TEXT("Display"),TEXT("Gauges"),ShowGauges,SettingsFile());
	GConfig->GetBool(TEXT("Display"),TEXT("Fabric"),FabricMotion,SettingsFile());
 if(FParse::Param(FCommandLine::Get(),TEXT("nofabric")))FabricMotion=false;
	GConfig->GetBool(TEXT("Display"),TEXT("HighQuality"),HighQuality,SettingsFile());
	LookSensitivity=FMath::Clamp(LookSensitivity,.3f,2.5f);CameraDistance=FMath::Clamp(CameraDistance,.5f,1.8f);
	TArray<FString> Sent;GConfig->GetArray(TEXT("Progress"),TEXT("Sent"),Sent,SettingsFile());
	for(const FString& Name:Sent)Sends.Add(Name);
	ApplyQuality();
	Shooting = FParse::Param(FCommandLine::Get(), TEXT("shot"));
	Shots = FPaths::ProjectSavedDir();
	FParse::Value(FCommandLine::Get(), TEXT("shotdir="), Shots);
	SetViewTarget(View);
	MenuOpen=Mode==0 && !Shooting && !FParse::Param(FCommandLine::Get(),TEXT("motiontest")) && !FParse::Param(FCommandLine::Get(),TEXT("nomenu"));
	InMenuProof=FParse::Param(FCommandLine::Get(),TEXT("uishot"));
	MenuOpen|=InMenuProof;
	FirstPerson=FParse::Param(FCommandLine::Get(),TEXT("firstperson"));
	if(GEngine->GameViewport)
	{
		SAssignNew(Interface,SRougarouUI).Controller(this);
		GEngine->GameViewport->AddViewportWidgetContent(Interface.ToSharedRef(),20);
		TrackpadInput=MakeShared<FRougarouTrackpadInput>(this);
		FSlateApplication::Get().RegisterInputPreProcessor(TrackpadInput);
	}
	GEngine->ClearOnScreenDebugMessages();
	InputMode();
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
	const int32 Wanted = Hovered >= 0 ? Hovered : Nearest(Point), Touched = Nearest(Aim);
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
	if(!ReferenceStartupHandled && Climber->Walker->GetSkeletalMeshAsset()){ReferenceStartupHandled=true;if(FParse::Param(FCommandLine::Get(),TEXT("referencepreview")))ToggleReferencePreview();}
	FClimbSim& Sim = Climber->Sim;
	if(FParse::Param(FCommandLine::Get(),TEXT("flowtest")))VerifyFlow(Delta);
	if(WasInputKeyJustPressed(EKeys::M))ToggleMenu();
	if(WasInputKeyJustPressed(EKeys::Escape) && (Limb<0 || MenuOpen))
	{
		if(FirstPerson && !FreeCursor && !MenuOpen){FreeCursor=true;InputMode();}
		else ToggleMenu();
	}
	if(FirstPerson && FreeCursor && !MenuOpen && WasInputKeyJustPressed(EKeys::LeftMouseButton)){FreeCursor=false;InputMode();}
	Climber->Walking = ReferencePreview || Mode == 0;
	Climber->Paused=MenuOpen;Climber->Walker->bPauseAnims=MenuOpen;
	Climber->ClothEnabled=FabricMotion;
	if(MenuOpen)
	{
		if(InMenuProof)
		{
			Clock+=Delta;
			if(Clock>2 && !Taken){FScreenshotRequest::RequestScreenshot(Shots/TEXT("menu.png"),true,false);Taken=true;}
			if(Clock>3)ConsoleCommand(TEXT("quit"));
		}
		return;
	}
	if(ReferencePreview){if(WasInputKeyJustPressed(EKeys::J)){ToggleReferencePreview();return;}ReferenceCamera(Delta);return;}
	if(WasInputKeyJustPressed(EKeys::V))TogglePerspective();
	FirstBlend=FMath::FInterpConstantTo(FirstBlend,FirstPerson?1.f:0.f,Delta,1.f/.45f);
	Climber->FirstPersonAmount=FirstBlend;Climber->SetInside(FirstBlend>.5f);
	if(FirstPerson)
	{
		float DX=0,DY=0;if(!FreeCursor)GetInputMouseDelta(DX,DY);
		if(Mode==0){WalkYaw+=DX*.0025f*LookSensitivity;RoamPitch=FMath::Clamp(RoamPitch+DY*.002f*LookSensitivity,-1.4f,1.4f);}
		else{if(FMath::Abs(DX)+FMath::Abs(DY)>.001f)OpeningHold=-1;FirstYaw=FMath::Clamp(FirstYaw+DX*.0025f*LookSensitivity,-2.8f,2.8f);FirstPitch=FMath::Clamp(FirstPitch+DY*.002f*LookSensitivity,-1.4f,1.4f);FirstKeys();}
	}
	if (Mode == 0)
	{
		// free roam on the mats: mouse steers, wasd moves relative to the view, shift jogs
		float MX = 0, MY = 0;
		if (!FirstPerson && IsInputKeyDown(EKeys::RightMouseButton))
		{
			FVector2D Mouse;
			if (GetMousePosition(Mouse.X,Mouse.Y))
			{
				if (Looking) { MX = Mouse.X - LastMouse.X; MY = Mouse.Y - LastMouse.Y; }
				LastMouse=Mouse; Looking=true;
			}
		} else Looking=false;
		WalkYaw -= MX * 0.005f*LookSensitivity;
		WalkPitch = FMath::Clamp(WalkPitch + MY * 0.004f*LookSensitivity, -0.35f, 0.9f);
		const FVector F(FMath::Cos(WalkYaw), FMath::Sin(WalkYaw), 0), R(-F.Y, F.X, 0);
		FVector Move = FVector::ZeroVector;
		if (IsInputKeyDown(EKeys::W)) Move += F;
		if (IsInputKeyDown(EKeys::S)) Move -= F;
		if (IsInputKeyDown(EKeys::D)) Move += R;
		if (IsInputKeyDown(EKeys::A)) Move -= R;
		const bool MotionTest=FParse::Param(FCommandLine::Get(),TEXT("motiontest"));
		bool Run=IsInputKeyDown(EKeys::LeftShift);
		if(MotionTest)
		{
			Move=Elapsed<1 ? FVector::ZeroVector : Elapsed<5 ? FVector(0,-1,0) : Elapsed<6.5 ? FVector(1,0,0) : FVector::ZeroVector;
			Run=Elapsed>3 && Elapsed<6.5;
		}
		Locomotion.Step(Move.GetSafeNormal(), Run, Delta,
			[](FVector From,FVector Step) { return AGymActor::MoveWalker(From,Step); });
		WalkAt = Locomotion.At;
		Climber->Walk(WalkAt, Locomotion.Yaw, Locomotion.Velocity.Size2D());
		// the nearest problem's first start hold, so e can pull you onto it
		Near = -1;
		float Best = 300;
		for (int32 Index = 0; Index < Sim.Spec.Routes.Num(); Index++)
		{
			const float Gap = FVector::Dist2D(WalkAt, Sim.Spec.Routes[Index].Map(Sim.Spec.Routes[Index].Holds[0].Point, 0));
			if (Gap < Best) { Best = Gap; Near = Index; }
		}
		if (Near >= 0 && WasInputKeyJustPressed(EKeys::E)) Mount();
		Elapsed += Delta;
		if(MotionTest)
		{
			static const float Times[]={.8f,2.5f,4.05f,4.20f,4.35f,4.5f,5.6f,7.5f};
			static const TCHAR* Names[]={TEXT("idle"),TEXT("walk"),TEXT("run-cycle-1"),TEXT("run-cycle-2"),TEXT("run-cycle-3"),TEXT("run"),TEXT("turn"),TEXT("stop")};
			if(ProofFrame<UE_ARRAY_COUNT(Times) && Elapsed>Times[ProofFrame])
			{
				FScreenshotRequest::RequestScreenshot(Shots / FString::Printf(TEXT("%s.png"),Names[ProofFrame]),true,false);
				UE_LOG(LogTemp,Display,TEXT("motion proof %s: speed %.2f yaw %.2f position %s"),Names[ProofFrame],Locomotion.Velocity.Size2D(),Locomotion.Yaw,*WalkAt.ToString());
				ProofFrame++;
			}
			if(Elapsed>8)ConsoleCommand(TEXT("quit"));
		}
		if (Shooting && !MotionTest)
		{
			Clock += Delta;
			if (Clock > 3 && !Taken) { FScreenshotRequest::RequestScreenshot(Shots / TEXT("roam.png"), true, false); Taken = true; }
			if (Clock > 4) ConsoleCommand(TEXT("quit"));
		}
		const FVector Goal = MotionTest ? WalkAt + FVector(330,-300,150) : WalkAt - F * 420*CameraDistance + FVector(0, 0, 180 + WalkPitch * 220);
		const FVector Look = WalkAt + FVector(0, 0, 110 - WalkPitch * 60);
		const float Speed = MotionTest ? 20 : FVector::Distance(View->GetActorLocation(), Goal) > 300 ? 1.6f : 4;
		View->SetActorLocation(FMath::VInterpTo(View->GetActorLocation(), Goal, Delta, Speed));
		View->SetActorRotation(FMath::RInterpTo(View->GetActorRotation(), (Look - View->GetActorLocation()).Rotation(), Delta, Speed * 2));
		FirstCamera(Delta);
		Status();
		return;
	}
	FVector2D Point;
	bool Over = Cursor(Point);
	// a fall restarts the climb underneath us; whatever limb was lifted is gone with it
	if (Sim.Fallen) { const FString Why = Sim.Hint; Sim.Restart(); Sim.Hint = Why; }
	if (Sim.Falls != Falls) { Falls = Sim.Falls; Limb = -1; }
	// -shot -lift: scripted left-hand move, to check reaching and placing without a person at the mouse
	if (Shooting && FParse::Param(FCommandLine::Get(), TEXT("lift")))
	{
		int32 Test = 0;
		FParse::Value(FCommandLine::Get(), TEXT("limb="), Test);
		Test = FMath::Clamp(Test, 0, 3);
		// -lift: take the right foot off and reach it into a high step, to check the hip limit and the knee
		if (Clock > 1.5f && Clock < 2 && Limb < 0) Press(Tip(Test));
		if (Limb >= 0) { Over = true; Point = Sim.Joint(Test) + FVector2D(Test % 2 == 0 ? -0.25f : 0.25f, 0.9f); }
	}
	int32 Snap = -1;
	if (Over && Limb >= 0)
	{
		// the hold nearest the cursor is the one being aimed at; ask the body (reach, hip and skeleton) once when it changes and a few times a second after
		int32 Best = -1; float Closest = .25f;
		for (int32 Hold = 0; Hold < Sim.Holds().Num(); Hold++)
		{
			const float Gap = FVector2D::Distance(Sim.Holds()[Hold].Point, Point);
			if (Gap < Closest) { Closest = Gap; Best = Hold; }
		}
		if (Best != Hovered || FVector2D::Distance(HoverFrom, Sim.Position) > .04f)
		{
			Hovered = Best; HoverFrom = Sim.Position;
			HoverWhy = Best >= 0 ? Sim.Check(Limb, Best) : FString();
			HoverReady = Best >= 0 && HoverWhy.IsEmpty();
		}
		// a takeable hold pulls the hand in as the cursor nears it, so landing it does not take pixel aim
		FVector2D Wanted = Point;
		if (Hovered >= 0 && HoverReady)
		{
			const float Pull = FMath::SmoothStep(.16f, .03f, Closest);
			Wanted = FMath::Lerp(Point, Sim.Holds()[Hovered].Point, Pull);
			if (Pull > .3f) Snap = Hovered;
		}
		// the guide eases toward the cursor instead of teleporting with it, and the hand follows the guide until the limb runs out of length
		Guide = Guide.IsZero() ? Wanted : FMath::Lerp(Guide, Wanted, 1 - FMath::Exp(-22 * Delta));
		Aim = Sim.Clamp(Limb, Guide);
	}
	else if (Limb < 0) { Hovered = -1; HoverReady = false; HoverWhy.Reset(); Guide = FVector2D::ZeroVector; }
	// the reach target is where the cursor really is: past the fingertips, the body shifts and turns toward it
	Sim.Reaching = Limb;
	Sim.Aimed = Snap;
	Sim.Target = Over && Limb >= 0 ? Guide : Aim;
	Climber->Hovered = Limb >= 0 ? Hovered : -1;
	Climber->HoverReady = HoverReady;
	if (!FirstPerson && Over && WasInputKeyJustPressed(EKeys::LeftMouseButton)) Press(Point);
	if (Limb >= 0 && (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::RightMouseButton))) Cancel();
	if (Limb >= 0 && WasInputKeyJustPressed(EKeys::X)) Limb = -1;
	if (!FirstPerson && WasInputKeyJustPressed(EKeys::R)) Retry();
	if (WasInputKeyJustPressed(EKeys::J)) Dismount();
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
	Sim.Lean = (FirstPerson?LatchedLean:Lean) * 0.6f;
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
	Out = Behind.RotateAngleAxis(FMath::RadiansToDegrees(ClimbOrbitYaw),FVector::UpVector);
	const bool ContactShot = Shooting && FParse::Param(FCommandLine::Get(), TEXT("contactshot"));
	const FVector Goal = Overview ? AGymActor::Overview : Hips + Out * Face.Stand*CameraDistance * (ContactShot ? .65f : 1.f)
		+ Face.At(Sim.Position.Y).Right * (Face.Aside + (ContactShot ? 250.f : 0.f)) + FVector(0, 0, 30+FMath::Sin(ClimbOrbitPitch)*Face.Stand*CameraDistance);
	const FVector Look = Overview ? AGymActor::Focus : Hips + FVector(0, 0, 25);
	const float Speed = Overview || FVector::Distance(View->GetActorLocation(), Goal) > 300 ? 1.6f : 4;
	View->SetActorLocation(FMath::VInterpTo(View->GetActorLocation(), Goal, Delta, Speed));
	View->SetActorRotation(FMath::RInterpTo(View->GetActorRotation(), (Look - View->GetActorLocation()).Rotation(), Delta, Speed * 2));
	FirstCamera(Delta);
	Status();
	// -shot: save a frame to Saved/shot.png and quit, so changes can be checked without a person at the screen
	if (Shooting)
	{
		Clock += Delta;
		if (Clock > 2 && !Gym)
		{
			FScreenshotRequest::RequestScreenshot(Shots / TEXT("gym.png"), false, false);
			Gym = true;
		}
		if (Clock > 6.5f && !Taken)
		{
			FScreenshotRequest::RequestScreenshot(Shots / TEXT("shot.png"), true, false);
			Taken = true;
		}
		if (Clock > 8) ConsoleCommand(TEXT("quit"));
	}
}

void ARougarouController::Mount()
{
	FClimbSim& Sim = Climber->Sim;
	if (Near >= 0) Sim.Route = Near;
	Sim.Restart();
	Climber->Rebuild();
	Mode = 1;
	Climber->Walking = false;
	Locomotion.Velocity = FVector::ZeroVector;
	Limb = -1;
	Elapsed = 10;
	FirstYaw=0;FirstPitch=.2f;LatchedLean=FVector2D::ZeroVector;
	InputMode();
}

void ARougarouController::Dismount()
{
	FClimbSim& Sim = Climber->Sim;
	const FVector Hips = Climber->Place(Sim.Position, 0);
	const FVector Out = Sim.Spec.Routes[Sim.Route].Panels[0].Out;
	WalkAt = AGymActor::ClearWalker(Hips + Out * 100, Out);
	Locomotion.At = WalkAt;
	Locomotion.Velocity = FVector::ZeroVector;
	Mode = 0;
	Limb = -1;
	LatchedLean=FVector2D::ZeroVector;PickSide=-1;InputMode();
}

void ARougarouController::Status() const
{
	// UI reads state directly; avoid development debug text over the game.
	if(Climber->Sim.Won && !Sends.Contains(Climber->Sim.Spec.Routes[Climber->Sim.Route].Name))
	{
		auto* Self=const_cast<ARougarouController*>(this);
		Self->Sends.Add(Climber->Sim.Spec.Routes[Climber->Sim.Route].Name);Self->SaveSettings();
	}
}

void ARougarouController::EndPlay(const EEndPlayReason::Type Reason)
{
	if(Interface.IsValid() && GEngine && GEngine->GameViewport)GEngine->GameViewport->RemoveViewportWidgetContent(Interface.ToSharedRef());
	if(TrackpadInput.IsValid() && FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(TrackpadInput);
	TrackpadInput.Reset();Interface.Reset();Super::EndPlay(Reason);
}
FString ARougarouController::SettingsFile() const{return FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("flowtest"))?TEXT("PolishProof/TestPlayer.ini"):TEXT("Player.ini"));}
void ARougarouController::SaveSettings()
{
	GConfig->SetBool(TEXT("Camera"),TEXT("TrackpadLook"),TrackpadLook,SettingsFile());
	GConfig->SetFloat(TEXT("Camera"),TEXT("Look"),LookSensitivity,SettingsFile());
	GConfig->SetFloat(TEXT("Camera"),TEXT("Distance"),CameraDistance,SettingsFile());
	GConfig->SetBool(TEXT("Display"),TEXT("Gauges"),ShowGauges,SettingsFile());
	GConfig->SetBool(TEXT("Display"),TEXT("Fabric"),FabricMotion,SettingsFile());
	GConfig->SetBool(TEXT("Display"),TEXT("HighQuality"),HighQuality,SettingsFile());
	GConfig->SetArray(TEXT("Progress"),TEXT("Sent"),Sends.Array(),SettingsFile());GConfig->Flush(false,SettingsFile());
}
void ARougarouController::ApplyQuality()
{
	ConsoleCommand(TEXT("r.AntiAliasingMethod 4"));
	// the procedural gym is one big non-nanite mesh; the VSM overflow note is noise on screen
	ConsoleCommand(TEXT("r.Shadow.Virtual.AllowScreenOverflowMessages 0"));
	ConsoleCommand(HighQuality?TEXT("r.ScreenPercentage 100"):TEXT("r.ScreenPercentage 75"));
	ConsoleCommand(TEXT("r.MotionBlurQuality 0"));ConsoleCommand(TEXT("r.Tonemapper.Sharpen 0.35"));
}
void ARougarouController::InputMode()
{
	Looking=false;
	bShowMouseCursor=MenuOpen || !FirstPerson || FreeCursor;
	if(!MenuOpen && FirstPerson && !FreeCursor)
	{
		FInputModeGameOnly Input;Input.SetConsumeCaptureMouseDown(false);SetInputMode(Input);
	}
	else
	{
		FInputModeGameAndUI Input;Input.SetHideCursorDuringCapture(false);Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Input);
	}
	if(!MenuOpen)FSlateApplication::Get().SetAllUserFocusToGameViewport();
}
void ARougarouController::ToggleMenu(){MenuOpen=!MenuOpen;FreeCursor=false;if(MenuOpen && Interface.IsValid())Interface->Refresh();InputMode();}
void ARougarouController::TogglePerspective()
{
	if(ReferencePreview)return;
	FirstPerson=!FirstPerson;OpeningHold=-1;OpeningTime=0;PickSide=-1;LatchedLean=FVector2D::ZeroVector;
	if(FirstPerson){FirstYaw=0;FirstPitch=.2f;}
	if(Mode==0 && FirstPerson)RoamPitch=.05f;
	if(Mode==1 && FirstPerson)
	{
		const FClimbRoute& Route=Climber->Sim.Spec.Routes[Climber->Sim.Route];
		const FClimbPanel& Panel=Route.At(Climber->Sim.Position.Y);
		FirstForward=-Panel.Out;FirstUp=Panel.Up;
		int32 Next=FMath::Max(Climber->Sim.Grips[2],0);
		const float Hands=FMath::Max(Climber->Sim.Tips[2].Y,Climber->Sim.Tips[3].Y);
		float NextHeight=MAX_flt;
		for(int32 I=0;I<Route.Holds.Num();I++)
			if(Route.Holds[I].Point.Y>Hands+.05f && Route.Holds[I].Point.Y<NextHeight){Next=I;NextHeight=Route.Holds[I].Point.Y;}
		OpeningHold=Next;OpeningTime=.8f;
		const FVector Eye=FClimbView::ClearEye(Route,Climber->Eye+FirstForward*12+FirstUp*2,32);
		const FVector Direction=(Route.Map(Route.Holds[Next].Point,6)-Eye).GetSafeNormal();
		FirstYaw=FMath::Atan2(FVector::DotProduct(Direction,FirstUp^FirstForward),FVector::DotProduct(Direction,FirstForward));
		FirstPitch=FMath::Asin(FMath::Clamp(FVector::DotProduct(Direction,FirstUp),-1.,1.));
		SmoothedEye=Eye;
	}
	InputMode();
}
void ARougarouController::Retry(){Climber->Sim.Restart();Limb=-1;LatchedLean=FVector2D::ZeroVector;}
void ARougarouController::VisitProblem(int32 Index,bool Climb)
{
	if(!Climber || !Climber->Sim.Spec.Routes.IsValidIndex(Index))return;
	if(ReferencePreview)ToggleReferencePreview();
	const FClimbRoute& Route=Climber->Sim.Spec.Routes[Index];
	const FVector Middle=(Route.Map(Route.Holds[2].Point)+Route.Map(Route.Holds[3].Point))/2;
	FVector Out=Route.Panels[0].Out;Out.Z=0;Out.Normalize();
	WalkAt=AGymActor::ClearWalker(Middle+Out*75,Out);Locomotion.At=WalkAt;Locomotion.Velocity=FVector::ZeroVector;
	WalkYaw=Locomotion.Yaw=FMath::Atan2(-Out.Y,-Out.X);Near=Index;Mode=0;Limb=-1;
	MenuOpen=false;if(Climb)Mount();InputMode();
}
FString ARougarouController::Title() const
{
	if(ReferencePreview)return TEXT("release / regrip reference");
	if(!Climber)return TEXT("rougarou");
	if(Mode==0)return Near>=0?Climber->Sim.Spec.Routes[Near].Name:TEXT("the rock lounge");
	return Climber->Sim.Spec.Routes[Climber->Sim.Route].Name;
}
FString ARougarouController::SidePrompt() const{return PickSide<0?TEXT(""):FString(PickSide==0?TEXT("right"):TEXT("left"))+TEXT(" side / a arm, l leg");}
FString ARougarouController::Hint() const
{
	if(ReferencePreview)return TEXT("recorded physics · 90% harness · not live training");
	if(Mode==0)return Near>=0?TEXT("ready when you are"):TEXT("walk the gym. find your next problem.");
	if(Climber->Sim.Won)return TEXT("sent. nice work.");
	if(FirstPerson && PickSide>=0)return SidePrompt();
	if(FirstPerson)return AimedHold>=0?TEXT("hold in sight / choose a hand or foot"):TEXT("look at a hold to reach it");
	if(Limb>=0)
	{
		if(Hovered>=0)return HoverReady?TEXT("click to take it"):HoverWhy;
		return TEXT("reach for a hold, then click");
	}
	return TEXT("choose a hand or foot");
}
FString ARougarouController::Keys() const
{
	if(ReferencePreview)return TEXT("two-finger swipe / Option + move to orbit   j back to gym   m menu");
	if(Mode==0)return FirstPerson?TEXT("wasd move   shift run   e climb   v view   m menu"):TEXT("wasd move   shift run   two-finger swipe look   v view   m menu");
	return FirstPerson?TEXT("r/l then a arm or l leg   arrows hips   space center   esc cursor   v view"):TEXT("wasd hips   x release   r retry   j drop   v view   m menu");
}
void ARougarouController::SendLimb(int32 Index)
{
	PickSide=-1;OpeningHold=-1;if(Mode!=1)return;
	if(AimedHold<0){Climber->Sim.Hint=TEXT("look at a hold first");return;}
	Limb=-1;Climber->Sim.Grab(Index,AimedHold);
}
void ARougarouController::FirstKeys()
{
	const double Now=FPlatformTime::Seconds();if(Now-PickTime>1.6)PickSide=-1;
	// Resolve a pending leg before L can begin a new left-side pick.
	if(PickSide>=0 && (WasInputKeyJustPressed(EKeys::A)||WasInputKeyJustPressed(EKeys::L)))
		SendLimb(WasInputKeyJustPressed(EKeys::A)?(PickSide==0?2:3):(PickSide==0?0:1));
	else if(WasInputKeyJustPressed(EKeys::R)||WasInputKeyJustPressed(EKeys::L))
	{PickSide=WasInputKeyJustPressed(EKeys::R)?0:1;PickTime=Now;}
	if(WasInputKeyJustPressed(EKeys::One))SendLimb(2);
	if(WasInputKeyJustPressed(EKeys::Two))SendLimb(3);
	if(WasInputKeyJustPressed(EKeys::Three))SendLimb(0);
	if(WasInputKeyJustPressed(EKeys::Four))SendLimb(1);
	if(WasInputKeyJustPressed(EKeys::Left))LatchedLean.X=LatchedLean.X==1?0:1;
	if(WasInputKeyJustPressed(EKeys::Right))LatchedLean.X=LatchedLean.X==-1?0:-1;
	if(WasInputKeyJustPressed(EKeys::Up))LatchedLean.Y=LatchedLean.Y==1?0:1;
	if(WasInputKeyJustPressed(EKeys::Down))LatchedLean.Y=LatchedLean.Y==-1?0:-1;
	if(WasInputKeyJustPressed(EKeys::SpaceBar))LatchedLean=FVector2D::ZeroVector;
}
void ARougarouController::FirstCamera(float Delta)
{
	AimedHold=-1;
	const float T=FirstBlend*FirstBlend*(3-2*FirstBlend);
	int32 Width=1440,Height=900;GetViewportSize(Width,Height);
	const float FirstFov=FMath::RadiansToDegrees(2*FMath::Atan(FMath::Tan(FMath::DegreesToRadians(40.f))*float(Width)/FMath::Max(Height,1)));
	View->GetCameraComponent()->SetFieldOfView(FMath::Lerp(55.f,FirstFov,T));
	if(T<=0)return;
	FVector Eye=Climber->Eye,Direction,Up=FVector::UpVector;
	if(Mode==0)Direction=FVector(FMath::Cos(WalkYaw)*FMath::Cos(RoamPitch),FMath::Sin(WalkYaw)*FMath::Cos(RoamPitch),FMath::Sin(RoamPitch));
	else
	{
		const FClimbRoute& Route=Climber->Sim.Spec.Routes[Climber->Sim.Route];
		const FClimbPanel& Panel=Route.At(Climber->Sim.Position.Y);
		FirstForward=FMath::VInterpTo(FirstForward,-Panel.Out,Delta,6).GetSafeNormal();
		FirstUp=FMath::VInterpTo(FirstUp,Panel.Up,Delta,6).GetSafeNormal();
		Up=FVector::VectorPlaneProject(FirstUp,FirstForward).GetSafeNormal();
		const FVector Right=Up^FirstForward;
		Direction=FirstForward*(FMath::Cos(FirstPitch)*FMath::Cos(FirstYaw))+Right*(FMath::Cos(FirstPitch)*FMath::Sin(FirstYaw))+Up*FMath::Sin(FirstPitch);
		Eye=FClimbView::ClearEye(Route,Eye+FirstForward*12+Up*2,32);
	}
	SmoothedEye=SmoothedEye.IsNearlyZero()?Eye:FMath::VInterpTo(SmoothedEye,Eye,Delta,30);
	if(Mode==1)SmoothedEye=FClimbView::ClearEye(Climber->Sim.Spec.Routes[Climber->Sim.Route],SmoothedEye,32);
	View->SetActorLocation(FMath::Lerp(View->GetActorLocation(),SmoothedEye,T));
	// Preserve the selected opening sight line while changing camera/rig pose.
	// Any mouse look cancels this brief transition anchor immediately.
	if(FirstPerson && Mode==1 && OpeningHold>=0)
	{
		const FClimbRoute& Route=Climber->Sim.Spec.Routes[Climber->Sim.Route];
		Direction=(Route.Map(Route.Holds[OpeningHold].Point,6)-View->GetActorLocation()).GetSafeNormal();
		FirstYaw=FMath::Atan2(FVector::DotProduct(Direction,Up^FirstForward),FVector::DotProduct(Direction,FirstForward));
		FirstPitch=FMath::Asin(FMath::Clamp(FVector::DotProduct(Direction,Up),-1.,1.));
		OpeningTime-=Delta;if(OpeningTime<=0)OpeningHold=-1;
	}

	View->SetActorRotation(FQuat::Slerp(View->GetActorQuat(),FRotationMatrix::MakeFromXZ(Direction,Up).ToQuat(),T));
	if(Mode==1 && FirstBlend>.9f)AimedHold=FClimbView::HoldInSight(Climber->Sim.Spec.Routes[Climber->Sim.Route],View->GetActorLocation(),View->GetActorForwardVector());
}

// A reproducible native smoke test for the actual controller, UI and camera.
// It uses isolated preferences and does not synthesize OS input.
void ARougarouController::VerifyFlow(float Delta)
{
 FlowTime+=Delta;
 auto Check=[&](bool Pass,const TCHAR* What)
 {
  if(!Pass){UE_LOG(LogTemp,Error,TEXT("FLOW FAIL: %s"),What);ConsoleCommand(TEXT("quit"));}
  else UE_LOG(LogTemp,Display,TEXT("FLOW PASS: %s"),What);
 };
 auto Shot=[&](const TCHAR* Name){FScreenshotRequest::RequestScreenshot(Shots/FString(Name),true,false);};
 switch(FlowStage)
 {
 case 0:if(FlowTime>.8){Check(MenuOpen,TEXT("menu opens on launch"));Check(!LookFromTrackpad(FVector2D(30,20)),TEXT("menu leaves trackpad scrolling unconsumed"));Shot(TEXT("menu.png"));FlowStage++;}break;
 case 1:if(FlowTime>1.2){Interface->SelectTab(1);Shot(TEXT("settings.png"));FlowStage++;}break;
 case 2:if(FlowTime>1.6){Interface->SelectTab(2);Shot(TEXT("help.png"));FlowStage++;}break;
 case 3:if(FlowTime>2){VisitProblem(0,false);Check(!MenuOpen&&Mode==0&&!AGymActor::WalkerBlocked(WalkAt),TEXT("walk there finds clear floor"));const float Yaw=WalkYaw;Check(LookFromTrackpad(FVector2D(30,20)) && WalkYaw!=Yaw,TEXT("trackpad gesture rotates walking camera"));TrackpadLook=false;Check(!LookFromTrackpad(FVector2D(30,20)),TEXT("trackpad setting disables gesture"));TrackpadLook=true;FlowStage++;}break;
 case 4:if(FlowTime>2.8){VisitProblem(0,true);Check(Mode==1&&Climber->Sim.Route==0,TEXT("climb it mounts selected problem"));FlowStage++;}break;
 case 5:if(FlowTime>4){TogglePerspective();Check(FirstPerson,TEXT("first person toggles on"));FlowStage++;}break;
 case 6:if(FlowTime>5){Check(FirstBlend>.99&&!bShowMouseCursor,TEXT("first person finishes transition and captures mouse"));Check(!View->GetActorLocation().ContainsNaN(),TEXT("eye camera stays finite"));Check(AimedHold>=0 && Climber->Sim.Holds()[AimedHold].Point.Y>FMath::Max(Climber->Sim.Tips[2].Y,Climber->Sim.Tips[3].Y),TEXT("opening view targets the next hand hold"));UE_LOG(LogTemp,Display,TEXT("FP PROOF eye=%s cached=%s forward=%s up=%s"),*View->GetActorLocation().ToString(),*Climber->Eye.ToString(),*View->GetActorForwardVector().ToString(),*FirstUp.ToString());Shot(TEXT("first-person.png"));FlowStage++;}break;
 case 7:if(FlowTime>5.4){ToggleMenu();PausePosition=Climber->Sim.Position;FlowStage++;}break;
 case 8:if(FlowTime>6){Check(Climber->Sim.Position.Equals(PausePosition,.0001),TEXT("menu pauses the climbing sim"));ToggleMenu();TogglePerspective();Dismount();Check(Mode==0,TEXT("drop returns to free roam"));FlowStage++;}break;
 case 9:if(FlowTime>7){Shot(TEXT("player.png"));FlowStage++;}break;
 case 10:if(FlowTime>7.5){UE_LOG(LogTemp,Display,TEXT("FLOW COMPLETE"));ConsoleCommand(TEXT("quit"));FlowStage++;}break;
 }
}
