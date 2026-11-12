#include "RougarouGame.h"
#include "Kismet/GameplayStatics.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "ClimberActor.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "HAL/PlatformProcess.h"
#include "HAL/FileManager.h"
#include "DrawDebugHelpers.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/SkyLight.h"
#include "Components/SkyLightComponent.h"
#include "UnrealClient.h"
#include "Engine/Engine.h"
#include "Engine/SkeletalMesh.h"
#include "EngineUtils.h"
#include "GymActor.h"
#include "ClimbView.h"
#include "RougarouUI.h"
#include "Engine/GameViewportClient.h"
#include "Framework/Application/SlateApplication.h"
#include "Misc/ConfigCacheIni.h"
#include "Misc/Paths.h"
#include "Framework/Application/IInputProcessor.h"
#include <atomic>

// macOS keeps sending scroll events after the fingers lift (momentum). For a camera that reads as drift,
// so a local event monitor notes which kind is arriving and the look handler drops the coasting ones.
static std::atomic<bool> ScrollCoasting{false};
#if PLATFORM_MAC
#define FVector FVectorWorkaround
#import <AppKit/AppKit.h>
#undef FVector
static id ScrollMonitor=nil;
#endif

class FRougarouTrackpadInput : public IInputProcessor
{
 TWeakObjectPtr<ARougarouController> Owner;
public:
 explicit FRougarouTrackpadInput(ARougarouController* In):Owner(In){}
 void Tick(float,FSlateApplication&,TSharedRef<ICursor>) override {}
 bool HandleMouseWheelOrGestureEvent(FSlateApplication& App,const FPointerEvent&,const FPointerEvent* Gesture) override
 {
  return App.IsActive() && Owner.IsValid() && Gesture && Gesture->GetGestureType()==EGestureEvent::Scroll && Owner->LookFromTrackpad(Gesture->GetGestureDelta(),Gesture->IsDirectionInvertedFromDevice());
 }
 bool HandleMouseMoveEvent(FSlateApplication& App,const FPointerEvent& Event) override
 {
  if(!App.IsActive() || !Owner.IsValid()) return false;
  if(Owner->LookFromPointer(Event.GetCursorDelta())) return true;
  return Event.IsAltDown() && Owner->LookFromTrackpad(Event.GetCursorDelta(),true);
 }
};

bool ARougarouController::LookFromPointer(FVector2D Delta)
{
 if(MenuOpen || ReferencePreview || Mode!=0 || FreeCursor || !Climber || Delta.IsNearlyZero()) return false;
 const FVector2D Motion=Delta.ClampAxes(-150,150);
 const float Sensitivity=LookSensitivity*(InvertLook?-1.f:1.f);
 PendingLook+=FVector2D(Motion.X*.00085f,-Motion.Y*.0007f)*Sensitivity;
 return true;
}

// Fingers moving right turn the view right and fingers moving up tilt it up, in every mode and both views.
// The swipe is queued and fed to the camera over a few frames, so coarse gesture events still turn it evenly.
bool ARougarouController::LookFromTrackpad(FVector2D D,bool Inverted)
{
 if(MenuOpen || (!ReferencePreview && Mode==0) || !TrackpadLook || !Climber)return false;
 if(ScrollCoasting)return true;
 // with natural scrolling the delta already follows the fingers; otherwise it is the opposite
 const FVector2D Fingers=(Inverted?D:-D).ClampAxes(-100,100)*(InvertLook?-1.:1.);
 PendingLook+=FVector2D(Fingers.X,-Fingers.Y)*.001f*LookSensitivity;
 return true;
}

// Turn the view by Right and Up radians, whichever camera is live.
void ARougarouController::ApplyLook(FVector2D Turn)
{
 const float X=Turn.X,Y=Turn.Y;
 if(ReferencePreview){ClimbOrbitYaw+=X;ClimbOrbitPitch=FMath::Clamp(ClimbOrbitPitch-Y,-.6f,.8f);}
 else if(FirstPerson){OpeningHold=-1;if(Mode==0){WalkYaw+=X;RoamPitch=FMath::Clamp(RoamPitch+Y,-1.4f,1.4f);}else{FirstYaw=FMath::Clamp(FirstYaw+X,-2.8f,2.8f);FirstPitch=FMath::Clamp(FirstPitch+Y,-1.4f,1.4f);}}
 else if(Mode==0){WalkYaw+=X;WalkPitch=FMath::Clamp(WalkPitch-Y,-.35f,.9f);}
 else{Wide=false;Elapsed=FMath::Max(Elapsed,3.f);ClimbOrbitYaw=FMath::Clamp(ClimbOrbitYaw+X,-1.2f,1.2f);ClimbOrbitPitch=FMath::Clamp(ClimbOrbitPitch-Y,-.6f,.8f);}
}

// The third-person camera keeps up closely when it is near its spot, so turning does not swim, and glides when it has far to travel.
static float FollowSpeed(const FVector& From,const FVector& Goal)
{
 return FMath::Lerp(14.f,1.6f,FMath::SmoothStep(150.f,600.f,float(FVector::Distance(From,Goal))));
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
	GConfig->GetBool(TEXT("Camera"),TEXT("InvertLook"),InvertLook,SettingsFile());
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
#if PLATFORM_MAC
		if(!ScrollMonitor)ScrollMonitor=[NSEvent addLocalMonitorForEventsMatchingMask:NSEventMaskScrollWheel handler:^NSEvent*(NSEvent* Event){ScrollCoasting=[Event momentumPhase]!=NSEventPhaseNone;return Event;}];
#endif
	}
	GEngine->ClearOnScreenDebugMessages();
	InputMode();
}

bool ARougarouController::Cursor(FVector2D& Out, int32& Hold) const
{
	Hold = INDEX_NONE;
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
	if (Nearest == TNumericLimits<float>::Max()) return false;
	if (Limb < 0 || FirstPerson) return true;

	FVector ResinHit=FVector::ZeroVector;
	// The wall intersection guides the limb. The resin in front of it decides what can be grabbed.
	for (int32 Index = 0; Index < Face.Holds.Num(); ++Index)
	{
		const FClimbHold& Resin = Face.Holds[Index];
		if (!Resin.Surface.IsValid()) continue;
		const double Facing = FVector::DotProduct(Direction, Resin.Normal);
		if (Facing >= -0.0001) continue;
		const FVector FromHold = Origin - Resin.Center;
		const double CenterAlong = -FVector::DotProduct(FromHold, Direction);
		if ((FromHold + Direction * CenterAlong).SizeSquared() > FMath::Square(Resin.Radius + Resin.Height)) continue;

		const double OriginDepth = FVector::DotProduct(FromHold, Resin.Normal);
		const double Start = FMath::Max(0.0, (Resin.Height - OriginDepth) / Facing);
		const double End = FMath::Min(double(Nearest), (0.35 - OriginDepth) / Facing);
		if (Start > End) continue;
		for (int32 Step = 0; Step <= 16; ++Step)
		{
			const double At = FMath::Lerp(Start, End, Step / 16.0);
			if (Resin.Surface->Clear(Origin + Direction * At) > 0) continue;
			Hold = Index;
			ResinHit=Origin+Direction*At;
			Nearest = At;
			break;
		}
	}
	if(Hold>=0)
	{
		const auto& Resin=Face.Holds[Hold];
		const auto& Panel=Face.At(Resin.Point.Y);
		const FVector Offset=ResinHit-Resin.Center;
		Out=Resin.Point+FVector2D(Offset|Panel.Right,Offset|Panel.Up)/100;
	}
	return true;
}

FVector2D ARougarouController::Tip(int32 Index) const
{
	return Climber->Sim.Tips[Index];
}

void ARougarouController::Lift(int32 Index)
{
	FClimbSim& Sim = Climber->Sim;
	if (Sim.Fallen || Sim.Won || Index < 0 || Index > 3) return;
	Limb = Index;
	Previous = Sim.Grips[Limb];
	PreviousPlacement = Sim.Placement[Limb];
	Pressed = Sim.Smeared[Limb];
	Post = Sim.Posts[Limb];
	Aim = Tip(Limb);
	Guide = FVector2D::ZeroVector;
	Sim.Drop(Limb);
}

void ARougarouController::Press(FVector2D Point, int32 Hold)
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
		// lifting a limb takes it off the wall: the other three carry the weight from this moment
		const int32 Picked = Limb;
		Limb = -1;
		if (Picked >= 0) Lift(Picked);
		return;
	}
	// A missed grab leaves the limb selected. Only a click on blank wall can place a smear.
	if (Hold >= 0)
	{
		const auto& Resin=Sim.Holds()[Hold];
		const float Across=FVector2D::DotProduct(Point-Resin.Point,FClimbSim::GripSide(Resin))*100/FClimbSim::GripWidth(Resin);
		if (Sim.Grab(Limb, Hold, Across)) Limb = -1;
		else
		{
			HoverWhy = Sim.Hint;
			HoverReady = false;
		}
		return;
	}
	const FClimbPanel& Last = Sim.Spec.Routes[Sim.Route].Panels.Last();
	if (Point.Y >= 0 && Point.Y <= Last.Start + Last.Length && Sim.Smear(Limb, Point)) Limb = -1;
}

void ARougarouController::Cancel()
{
	// put the limb back where it was if the body can still get it there, otherwise it hangs
	FClimbSim& Sim = Climber->Sim;
	if (Previous >= 0) Sim.Grab(Limb, Previous, PreviousPlacement);
	else if (Pressed) Sim.Smear(Limb, Post);
	Limb = -1;
}

void ARougarouController::PlayerTick(float Delta)
{
	Super::PlayerTick(Delta);
	if (!Climber) return;
	if(FParse::Param(FCommandLine::Get(),TEXT("formpreview")))
	{
		FormProofTime+=Delta;
		if(FormProofTime>2 && !FormProofOpened){FormProofOpened=true;StudioProofPosition=Climber->Sim.Position;TogglePlayerForm();}
		if(FParse::Param(FCommandLine::Get(),TEXT("studioflow")))
		{
			auto Check=[&](bool OK,const TCHAR* Label){if(OK){UE_LOG(LogTemp,Display,TEXT("STUDIO PASS: %s"),Label);}else {UE_LOG(LogTemp,Error,TEXT("STUDIO FAIL: %s"),Label);ConsoleCommand(TEXT("quit"));}};
			if(FormProofTime>4 && !FormProofShot)
			{
				Check(Climber->Form.Open&&Climber->Walker->GetComponentLocation().Z>9000,TEXT("character moved into separate studio"));
				Check(Climber->Sim.Position.Equals(StudioProofPosition,.0001),TEXT("wall simulation stays frozen"));
				Climber->Form.Set(*Climber->Walker,0,35);
				FScreenshotRequest::RequestScreenshot(Shots/TEXT("rig-studio.png"),true,false);FormProofShot=true;
			}
			if(FormProofTime>5 && !StudioProofApplied)
			{
				StudioProofApplied=true;const int32 Joint=Climber->Form.Selected;
				Climber->Form.SetBound(*Climber->Walker,0,true,120);
				StudioProofPath=FPaths::ProjectSavedDir()/TEXT("Automation")/(TEXT("studio_")+FGuid::NewGuid().ToString()+TEXT(".json"));
				ApplyPlayerForm(StudioProofPath);
				Check(!Climber->Form.Open&&!MenuOpen&&Climber->Walker->GetComponentLocation().Z<9000,TEXT("apply returns to the wall"));
				Check(Climber->HumanRig.JointLimits()[Joint].Forward==120&&Climber->GateRig.JointLimits()[Joint].Forward==120,TEXT("live rig and reach gate receive edited limit"));
				Check(Climber->Sim.Position.Equals(StudioProofPosition,.0001),TEXT("apply preserves climbing position"));
			}
			if(FormProofTime>7 && !StudioProofWallShot){FScreenshotRequest::RequestScreenshot(Shots/TEXT("rig-applied-wall.png"),true,false);StudioProofWallShot=true;}
			if(FormProofTime>8){IFileManager::Get().Delete(*StudioProofPath);ConsoleCommand(TEXT("quit"));}
		}
		if(FParse::Param(FCommandLine::Get(),TEXT("formshot")))
		{
			if(FormProofTime>4 && !FormProofShot){FScreenshotRequest::RequestScreenshot(Shots/TEXT("player-form.png"),true,false);FormProofShot=true;}
			if(FormProofTime>5)ConsoleCommand(TEXT("quit"));
		}
	}
	if(!ReferenceStartupHandled && Climber->Walker->GetSkeletalMeshAsset()){ReferenceStartupHandled=true;if(FParse::Param(FCommandLine::Get(),TEXT("referencepreview")))ToggleReferencePreview();}
	if(!GymActor) GymActor=Cast<AGymActor>(UGameplayStatics::GetActorOfClass(this,AGymActor::StaticClass()));
	FClimbSim& Sim = Climber->Sim;
	if(FParse::Param(FCommandLine::Get(),TEXT("flowtest")))VerifyFlow(Delta);
	if(WasInputKeyJustPressed(EKeys::M) && !Climber->Form.Open)ToggleMenu();
	if(WasInputKeyJustPressed(EKeys::Escape) && (Limb<0 || MenuOpen))
	{
		if((FirstPerson || (Mode==0 && !ReferencePreview)) && !FreeCursor && !MenuOpen){FreeCursor=true;InputMode();}
		else ToggleMenu();
	}
	if((FirstPerson || Mode==0) && FreeCursor && !MenuOpen && WasInputKeyJustPressed(EKeys::LeftMouseButton)){FreeCursor=false;InputMode();}
	Climber->Walking = ReferencePreview || Mode == 0;
	Climber->Paused=MenuOpen;Climber->Walker->bPauseAnims=MenuOpen && !Climber->Form.Open;
	if(Climber->Form.Open)
	{
		Climber->Form.Tick(*Climber->Walker,Delta);
		for(int32 I=-10;I<=10;++I)
		{
			DrawDebugLine(GetWorld(),FVector(I*25,-250,10000.0),FVector(I*25,250,10000.0),FColor(80,95,110),false,-1,0,.6);
			DrawDebugLine(GetWorld(),FVector(-250,I*25,10000.0),FVector(250,I*25,10000.0),FColor(80,95,110),false,-1,0,.6);
		}
	}
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
	if(!PendingLook.IsZero())
	{
		const FVector2D Turn=PendingLook.SizeSquared()<1e-9?PendingLook:PendingLook*(1-FMath::Exp(-30*Delta));
		PendingLook-=Turn;ApplyLook(Turn);
	}
	if(ReferencePreview){if(WasInputKeyJustPressed(EKeys::J)){ToggleReferencePreview();return;}ReferenceCamera(Delta);return;}
	if(WasInputKeyJustPressed(EKeys::V))TogglePerspective();
	FirstBlend=FMath::FInterpConstantTo(FirstBlend,FirstPerson?1.f:0.f,Delta,1.f/.45f);
	Climber->FirstPersonAmount=FirstBlend;Climber->SetInside(FirstBlend>.5f);
	if(FirstPerson && Mode!=0)
	{
		float DX=0,DY=0;
		if(!FreeCursor) GetInputMouseDelta(DX,DY);
		if(FMath::Abs(DX)+FMath::Abs(DY)>.001f)
		{
			const float Sensitivity=LookSensitivity*(InvertLook?-1.f:1.f);
			ApplyLook(FVector2D(DX*.00085f,DY*.0007f)*Sensitivity);
		}
	}
	if (Mode == 0)
	{
		const FVector F(FMath::Cos(WalkYaw), FMath::Sin(WalkYaw), 0), R(-F.Y, F.X, 0);
		FVector Move = FVector::ZeroVector;
		if (IsInputKeyDown(EKeys::W)) Move += F;
		if (IsInputKeyDown(EKeys::S)) Move -= F;
		if (IsInputKeyDown(EKeys::D)) Move += R;
		if (IsInputKeyDown(EKeys::A)) Move -= R;
		const bool MotionTest=FParse::Param(FCommandLine::Get(),TEXT("motiontest"));
		bool Run=IsInputKeyDown(EKeys::LeftShift)||IsInputKeyDown(EKeys::RightShift);
		if(MotionTest)
		{
			Move=Elapsed<1 ? FVector::ZeroVector : Elapsed<5 ? FVector(0,-1,0) : Elapsed<6.5 ? FVector(1,0,0) : FVector::ZeroVector;
			Run=Elapsed>3 && Elapsed<6.5;
		}
		Locomotion.Step(Move.GetSafeNormal(), Run, Delta,
			[](FVector From,FVector Step) { return AGymActor::MoveWalker(From,Step); });
		WalkAt = Locomotion.At;
		Climber->Walk(WalkAt, Locomotion.Yaw, Locomotion.Velocity.Size2D());
		PadLanding=false;
		if(GymActor && Climber->Walker->GetSkeletalMeshAsset())
		{
			const float Speed=Locomotion.Velocity.Size2D();
			for(const FName Foot:{FName(TEXT("LeftToeBase")),FName(TEXT("RightToeBase"))})
			{
				if(Climber->Walker->GetBoneIndex(Foot)==INDEX_NONE) continue;
				const FVector Toe=Climber->Walker->GetBoneLocation(Foot);
				const float Contact=1-FMath::SmoothStep(35.f,43.f,float(Toe.Z));
				GymActor->PressPad(Toe,Contact*FMath::Lerp(.28f,1.25f,FMath::Clamp(Speed/380.f,0.f,1.f)),false);
			}
		}
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
		const float Speed = MotionTest ? 20 : FollowSpeed(View->GetActorLocation(), Goal);
		View->SetActorLocation(FMath::VInterpTo(View->GetActorLocation(), Goal, Delta, Speed));
		View->SetActorRotation(FMath::RInterpTo(View->GetActorRotation(), (Look - View->GetActorLocation()).Rotation(), Delta, Speed * 2));
		FirstCamera(Delta);
		Status();
		return;
	}
	FVector2D Point;
	int32 CursorHold = INDEX_NONE;
	bool Over = Cursor(Point, CursorHold);
	// a fall restarts the climb underneath us; whatever limb was lifted is gone with it
	// a fall plays out onto the mats first; then the climb restarts underneath us
	if(!Sim.Fallen) PadLanding=false;
	if (Sim.Fallen)
	{
		if(!PadLanding && GymActor && Climber->FallTime>=0 && Climber->FallTime>=Climber->FallAir)
		{
			PadLanding=true;
			GymActor->PressPad(Climber->FallSpot,FMath::Clamp(1.4f+Climber->FallDrop*.012f,1.4f,3.f),true);
		}
		Limb = -1;
		// the fall plays out and the climber gets up; from there you are on the mats, free to walk back to the start
		if (Climber->FallDone() && !Shooting)
		{
			Near = Sim.Route;
			WalkAt = Climber->FallSpot; Locomotion.At = WalkAt; Locomotion.Velocity = FVector::ZeroVector;
			Locomotion.Yaw = WalkYaw = Climber->FallYaw;
			Sim.Restart();
			Mode = 0; SmoothLean = FVector2D::ZeroVector;
			InputMode();
			return;
		}
	}
	if (Sim.Falls != Falls) { Falls = Sim.Falls; Limb = -1; }
	// -shot -lift: scripted left-hand move, to check reaching and placing without a person at the mouse
	if (Shooting && FParse::Param(FCommandLine::Get(), TEXT("lift")))
	{
		int32 Test = 0;
		FParse::Value(FCommandLine::Get(), TEXT("limb="), Test);
		Test = FMath::Clamp(Test, 0, 3);
		// -lift: take the right foot off and reach it into a high step, to check the hip limit and the knee
		if (Clock > 1.5f && Clock < 2 && Limb < 0) Press(Tip(Test));
		if (Limb >= 0)
		{
			Over = true;
			const float Side = Test % 2 == 0 ? -1.f : 1.f;
			// -sweep: carry the limb slowly up from a hang to a high step and across, to watch the joints the whole way
			Point = Sim.Joint(Test) + (FParse::Param(FCommandLine::Get(), TEXT("sweep"))
				? FVector2D(Side * (.05f + .3f * (.5f - .5f * FMath::Cos(Clock * 1.1f))), -.6f + .75f * (.5f - .5f * FMath::Cos(Clock * .7f)))
				: FVector2D(Side * .25f, .9f));
		}
	}
	int32 Snap = -1;
	if (Over && Limb >= 0)
	{
		const int32 Best = CursorHold;
		float Across=0;
		if(Best>=0)
		{
			const auto& Resin=Sim.Holds()[Best];
			Across=FMath::Clamp(float(FVector2D::DotProduct(Point-Resin.Point,FClimbSim::GripSide(Resin))*100/FClimbSim::GripWidth(Resin)),-1.f,1.f);
		}
		// ask again a few times a second: the hips keep moving, and what was out of reach may not be any more
		HoverClock += Delta;
		if (Best != Hovered || HoverClock > .25f)
		{
			HoverClock = 0;
			Hovered = Best;
			HoverWhy = Best >= 0 ? Sim.Check(Limb, Best, Across) : FString();
			HoverReady = Best >= 0 && HoverWhy.IsEmpty();
		}
		// a takeable hold pulls the hand in as the cursor nears it, so landing it does not take pixel aim
		FVector2D Wanted = Point;
		if (Hovered >= 0 && HoverReady)
		{
			Wanted = FClimbSim::GripPoint(Sim.Holds()[Hovered],Across);
			Snap = Hovered;
		}
		// the guide eases toward the cursor instead of teleporting with it, and the hand follows the guide until the limb runs out of length
		Guide = Guide.IsZero() ? Wanted : FMath::Lerp(Guide, Wanted, 1 - FMath::Exp(-15 * Delta));
		Aim = Sim.Clamp(Limb, Guide);
	}
	else if (Limb < 0) { Hovered = -1; HoverReady = false; HoverWhy.Reset(); Guide = FVector2D::ZeroVector; }
	// the reach target is where the cursor really is: past the fingertips, the body shifts and turns toward it
	Sim.Reaching = Limb;
	Sim.Aimed = Snap;
	Sim.Target = Over && Limb >= 0 ? Guide : Aim;
	Climber->Hovered = Limb >= 0 ? Hovered : -1;
	Climber->HoverReady = HoverReady;
	if (!FirstPerson && Over && WasInputKeyJustPressed(EKeys::LeftMouseButton)) Press(Point, CursorHold);
	// q and e are the hands, z and c the feet, laid out like the body seen from behind. from outside they pick a limb up
	// (again puts it back); through your own eyes they send it to the hold you are looking at
	static const FKey LimbKeys[4] = { EKeys::C, EKeys::Z, EKeys::E, EKeys::Q };
	for (int32 Index = 0; Index < 4; Index++)
	{
		if (!WasInputKeyJustPressed(LimbKeys[Index])) continue;
		if (FirstPerson) { SendLimb(Index); break; }
		const bool Same = Limb == Index;
		if (Limb >= 0) Cancel();
		if (!Same) Lift(Index);
		break;
	}
	if (Limb >= 0 && (WasInputKeyJustPressed(EKeys::Escape) || WasInputKeyJustPressed(EKeys::RightMouseButton))) Cancel();
	if (Limb >= 0 && WasInputKeyJustPressed(EKeys::X)) Limb = -1;
	if (WasInputKeyJustPressed(EKeys::R)) Retry();
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
	// the push comes on and lets go like a muscle, not a switch
	SmoothLean = FMath::Lerp(SmoothLean, Lean, 1 - FMath::Exp(-9 * Delta));
	Sim.Lean = SmoothLean * 0.6f;
	Climber->Selected = Limb;
	// g flips between the gym overview and the climb; the opening shot holds the overview for a few seconds
	Elapsed += Delta;
	if (WasInputKeyJustPressed(EKeys::G)) Wide = !Wide;
	const bool Overview = Wide || Elapsed < 3;
	const FVector Hips = Sim.Fallen ? Climber->BodyAt : Climber->Place(Sim.Position, 0);
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
	const float Speed = Overview ? 1.6f : FollowSpeed(View->GetActorLocation(), Goal);
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
		// -fall: let go with both hands and capture the fall mid-air and on the mats
		if (FParse::Param(FCommandLine::Get(), TEXT("fall")))
		{
			static int32 FallStage = 0;
			if (FallStage == 0 && Clock > 4) { Climber->Sim.Drop(2); Climber->Sim.Drop(3); FallStage = 1; }
			if (FallStage == 1 && Climber->FallTime > Climber->FallAir * .7f) { FScreenshotRequest::RequestScreenshot(Shots / TEXT("fall-air.png"), true, false); FallStage = 2; }
			if (FallStage == 2 && Climber->FallTime > Climber->FallAir + .45f) { FScreenshotRequest::RequestScreenshot(Shots / TEXT("fall-land.png"), true, false); FallStage = 3; }
			if (FallStage == 3 && Climber->FallTime > Climber->FallAir + 1.0f) { FScreenshotRequest::RequestScreenshot(Shots / TEXT("fall-ground.png"), true, false); FallStage = 4; }
			if (FallStage == 4 && Climber->FallDone()) { FScreenshotRequest::RequestScreenshot(Shots / TEXT("fall-up.png"), true, false); FallStage = 5; }
		}
		if (Clock > (FParse::Param(FCommandLine::Get(), TEXT("fall")) ? 3.5f : 6.5f) && !Taken)
		{
			FScreenshotRequest::RequestScreenshot(Shots / TEXT("shot.png"), true, false);
			Taken = true;
		}
		if (Clock > (FParse::Param(FCommandLine::Get(), TEXT("fall")) ? 9.f : 8.f)) ConsoleCommand(TEXT("quit"));
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
#if PLATFORM_MAC
	if(ScrollMonitor){[NSEvent removeMonitor:ScrollMonitor];ScrollMonitor=nil;}
#endif
	ScrollCoasting=false;
	if(TrackpadInput.IsValid() && FSlateApplication::IsInitialized())FSlateApplication::Get().UnregisterInputPreProcessor(TrackpadInput);
	TrackpadInput.Reset();Interface.Reset();Super::EndPlay(Reason);
}
FString ARougarouController::SettingsFile() const{return FPaths::ProjectSavedDir()/(FParse::Param(FCommandLine::Get(),TEXT("flowtest"))?TEXT("PolishProof/TestPlayer.ini"):TEXT("Player.ini"));}
void ARougarouController::SaveSettings()
{
	GConfig->SetBool(TEXT("Camera"),TEXT("TrackpadLook"),TrackpadLook,SettingsFile());
	GConfig->SetBool(TEXT("Camera"),TEXT("InvertLook"),InvertLook,SettingsFile());
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
	const bool Capture=!MenuOpen && !FreeCursor && (FirstPerson || (Mode==0 && !ReferencePreview));
	bShowMouseCursor=!Capture;
	if(Capture)
	{
		FInputModeGameOnly Input;Input.SetConsumeCaptureMouseDown(false);SetInputMode(Input);
	}
	else
	{
		FInputModeGameAndUI Input;Input.SetHideCursorDuringCapture(false);Input.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);SetInputMode(Input);
	}
	if(!MenuOpen)FSlateApplication::Get().SetAllUserFocusToGameViewport();
}
void ARougarouController::ToggleMenu(){if(Climber && Climber->Form.Open){TogglePlayerForm();return;}MenuOpen=!MenuOpen;PendingLook=FVector2D::ZeroVector;FreeCursor=false;if(MenuOpen && Interface.IsValid())Interface->Refresh();InputMode();}
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
void ARougarouController::Retry(){Climber->Sim.Restart();Limb=-1;SmoothLean=FVector2D::ZeroVector;}
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

	if(FirstPerson)return AimedHold>=0?TEXT("hold in sight: q e for a hand, z c for a foot"):TEXT("look at a hold to reach it");
	if(Limb>=0)
	{
		if(Hovered>=0)return HoverReady?TEXT("click to take it"):HoverWhy;
		return TEXT("click a hold to grab, or reachable bare wall to press");
	}
	return TEXT("pick up a hand or foot");
}
FString ARougarouController::Keys() const
{
	if(ReferencePreview)return TEXT("two-finger swipe / Option + move to orbit   j back to gym   m menu");
	if(Mode==0)return TEXT("wasd move   shift sprint   pointer look   v view   m menu");
	return FirstPerson?TEXT("q e hands   z c feet   wasd hips   r retry   j drop   esc cursor   v view"):TEXT("q e hands   z c feet   wasd hips   x let go   r retry   j drop   v view");
}
void ARougarouController::SendLimb(int32 Index)
{
	PickSide=-1;OpeningHold=-1;if(Mode!=1)return;
	if(AimedHold<0){Climber->Sim.Hint=TEXT("look at a hold first");return;}
	Limb=-1;Climber->Sim.Grab(Index,AimedHold);
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
 case 3:if(FlowTime>2){VisitProblem(0,false);Check(!MenuOpen&&Mode==0&&!AGymActor::WalkerBlocked(WalkAt),TEXT("walk there finds clear floor"));Check(!bShowMouseCursor && !LookFromTrackpad(FVector2D(30,20)),TEXT("roam captures the pointer and ignores scroll look"));PendingLook=FVector2D::ZeroVector;Check(LookFromPointer(FVector2D(30,20))&&PendingLook.X>0&&PendingLook.Y<0,TEXT("ordinary pointer motion queues roam camera yaw and pitch"));PendingLook=FVector2D::ZeroVector;FlowStage++;}break;
 case 4:if(FlowTime>2.8){VisitProblem(0,true);Check(Mode==1&&Climber->Sim.Route==0,TEXT("climb it mounts selected problem"));FlowStage++;}break;
 case 5:if(FlowTime>4){TogglePerspective();Check(FirstPerson,TEXT("first person toggles on"));FlowStage++;}break;
 case 6:if(FlowTime>5){Check(FirstBlend>.99&&!bShowMouseCursor,TEXT("first person finishes transition and captures mouse"));Check(!View->GetActorLocation().ContainsNaN(),TEXT("eye camera stays finite"));Check(AimedHold>=0 && Climber->Sim.Holds()[AimedHold].Point.Y>FMath::Max(Climber->Sim.Tips[2].Y,Climber->Sim.Tips[3].Y),TEXT("opening view targets the next hand hold"));UE_LOG(LogTemp,Display,TEXT("FP PROOF eye=%s cached=%s forward=%s up=%s"),*View->GetActorLocation().ToString(),*Climber->Eye.ToString(),*View->GetActorForwardVector().ToString(),*FirstUp.ToString());Shot(TEXT("first-person.png"));FlowStage++;}break;
 case 7:if(FlowTime>5.4){ToggleMenu();PausePosition=Climber->Sim.Position;FlowStage++;}break;
 case 8:if(FlowTime>6){Check(Climber->Sim.Position.Equals(PausePosition,.0001),TEXT("menu pauses the climbing sim"));ToggleMenu();TogglePerspective();Dismount();Check(Mode==0,TEXT("drop returns to free roam"));FlowStage++;}break;
 case 9:if(FlowTime>7){Shot(TEXT("player.png"));FlowStage++;}break;
 case 10:if(FlowTime>7.5){UE_LOG(LogTemp,Display,TEXT("FLOW COMPLETE"));ConsoleCommand(TEXT("quit"));FlowStage++;}break;
 }
}

void ARougarouController::TogglePlayerForm()
{
 if(!Climber || !Climber->Walker->GetSkeletalMeshAsset())return;
 if(Climber->Form.Open)
 {
  Climber->Form.End(*Climber->Walker);Climber->SetInside(FirstBlend>.5f);
  Climber->Walker->SetWorldTransform(FormCharacterTransform);Climber->Walker->ForceClothNextUpdateTeleportAndReset();
  if(FormStage){FormStage->Destroy();FormStage=nullptr;}
  View->SetActorTransform(FormCamera);
 }
 else
 {
  FormCamera=View->GetActorTransform();FormCharacterTransform=Climber->Walker->GetComponentTransform();
  Climber->Form.Begin(*Climber->Walker);if(!Climber->Form.Open)return;
  MenuOpen=true;Climber->Paused=true;Climber->SetInside(false);
  const FVector Origin(0,0,10000);
  Climber->Walker->SetWorldTransform(FTransform(FQuat::Identity,Origin));
  Climber->Walker->SetVisibility(true);Climber->Walker->bPauseAnims=false;
  FormStage=GetWorld()->SpawnActor<AActor>();
  USceneComponent* Root=NewObject<USceneComponent>(FormStage);FormStage->SetRootComponent(Root);Root->RegisterComponent();FormStage->SetActorLocation(Origin);
  auto* Mat=UMaterialInstanceDynamic::Create(LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial")),FormStage);
  Mat->SetVectorParameterValue(TEXT("Color"),FLinearColor(.025,.03,.035));
  auto Box=[&](FVector At,FVector Scale)
  {
   auto* Part=NewObject<UStaticMeshComponent>(FormStage);Part->SetupAttachment(Root);
   Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));Part->SetMaterial(0,Mat);
   Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);Part->SetRelativeLocation(At);Part->SetRelativeScale3D(Scale);Part->RegisterComponent();
  };
  Box(FVector(0,0,-3),FVector(12,12,.04));
  Box(FVector(0,600,300),FVector(12,.04,6));Box(FVector(0,-600,300),FVector(12,.04,6));
  Box(FVector(600,0,300),FVector(.04,12,6));Box(FVector(-600,0,300),FVector(.04,12,6));
  const auto& Ref=Climber->Walker->GetSkeletalMeshAsset()->GetRefSkeleton();
  TArray<FTransform> Space=Climber->Form.Draft.NeutralPose();
  for(int32 I=0;I<Space.Num();++I)if(Ref.GetParentIndex(I)>=0)Space[I]*=Space[Ref.GetParentIndex(I)];
  const FVector Front=FVector::VectorPlaneProject(Space[Ref.FindBoneIndex(TEXT("LeftToeBase"))].GetLocation()-Space[Ref.FindBoneIndex(TEXT("LeftFoot"))].GetLocation(),FVector::UpVector).GetSafeNormal();
  const FVector Side=FVector::CrossProduct(Front,FVector::UpVector);
  for(int32 I=0;I<3;++I)
  {
   auto* Light=NewObject<UPointLightComponent>(FormStage);Light->SetupAttachment(Root);
   Light->SetRelativeLocation(Front*(I==2?-130:180)+Side*(I==0?-160:160)+FVector(0,0,240));
   Light->SetIntensity(I==0?5500:3000);Light->SetAttenuationRadius(900);Light->SetLightColor(I==1?FLinearColor(.8,.88,1):FLinearColor(1,.95,.88));Light->RegisterComponent();
  }
  const FVector Center=Origin+FVector(0,0,92);
  View->SetActorLocation(Center+Front*330+FVector(0,0,10));View->SetActorRotation((Center-View->GetActorLocation()).Rotation());
 }
 if(Interface.IsValid())Interface->Refresh();InputMode();
}
void ARougarouController::ApplyPlayerForm(const FString& Destination)
{
 if(!Climber||!Climber->Form.Open)return;
 if(!Climber->Form.Apply(*Climber->Walker,Climber->HumanRig,Climber->GateRig,Destination)){if(Interface.IsValid())Interface->Refresh();return;}
 TogglePlayerForm();MenuOpen=false;InputMode();
}
void ARougarouController::ShowPlayerFormFiles(){IFileManager::Get().MakeDirectory(*FPlayerForm::Folder(),true);FPlatformProcess::ExploreFolder(*FPaths::ConvertRelativePathToFull(FPlayerForm::Folder()));}
void ARougarouController::OrbitPlayerForm(float Yaw,float Pitch,float Zoom)
{
 if(!Climber || !Climber->Form.Open)return;
 const FVector Center=Climber->Walker->GetBoneLocation(TEXT("Spine"));
 FVector Offset=View->GetActorLocation()-Center;
 Offset=Offset.RotateAngleAxis(Yaw,FVector::UpVector).RotateAngleAxis(Pitch,View->GetActorRightVector());
 Offset=Offset.GetSafeNormal()*FMath::Clamp(float(Offset.Size()+Zoom),90.f,650.f);
 View->SetActorLocation(Center+Offset);View->SetActorRotation((-Offset).Rotation());
}
