#pragma once

#include "CoreMinimal.h"
#include "Locomotion.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "RougarouGame.generated.h"

class AClimberActor;
class ACameraActor;
class SRougarouUI;
class IInputProcessor;

// builds the debug gym on any map: lights, wall, holds, climber and a camera that follows the hips
UCLASS()
class ROUGAROU_API ARougarouGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARougarouGameMode();
	virtual void StartPlay() override;
};

// mouse and keyboard climbing: click a hand or foot, aim, click a hold or bare wall
UCLASS()
class ROUGAROU_API ARougarouController : public APlayerController
{
	GENERATED_BODY()

public:
	ARougarouController();

	UPROPERTY()
	TObjectPtr<AClimberActor> Climber;

	UPROPERTY()
	TObjectPtr<ACameraActor> View;

	virtual void BeginPlay() override;
	virtual void PlayerTick(float Delta) override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	bool MenuOpen=true, FirstPerson=false, ShowGauges=true, FabricMotion=true, HighQuality=true;
	float LookSensitivity=1, CameraDistance=1, FirstBlend=0;
	int32 AimedHold=-1, OpeningHold=-1;
	float OpeningTime=0;
	TSet<FString> Sends;
	bool ReferencePreview=false;
	void ToggleReferencePreview();
	bool TrackpadLook=true;
	bool LookFromTrackpad(FVector2D Delta);
	void ToggleMenu();
	void TogglePerspective();
	void VisitProblem(int32 Index,bool Climb);
	void Retry();
	void DropToFloor(){Dismount();}
	void PullOn(){Mount();}
	void SendLimb(int32 Index);
	void SaveSettings();
	void ApplyQuality();
	FString Title() const;
	FString Hint() const;
	FString Keys() const;
	bool IsClimbing() const{return Mode==1;}
	int32 NearestProblem() const{return Near;}
	int32 CurrentLimb() const{return Limb;}
	FString SidePrompt() const;


private:
	TSharedPtr<SRougarouUI> Interface;
	TSharedPtr<IInputProcessor> TrackpadInput;
	float ClimbOrbitYaw=0, ClimbOrbitPitch=0;
	UPROPERTY() TArray<TObjectPtr<AActor>> ReferenceScene;
	void ReferenceCamera(float Delta);
	bool ReferenceStartupHandled=false;
	float ReferenceElapsed=0;
	bool ReferenceShotTaken=false;
	void InputMode();
	void FirstCamera(float Delta);
	void FirstKeys();
	void VerifyFlow(float Delta);
	int32 FlowStage=0;
	float FlowTime=0;
	FVector2D PausePosition=FVector2D::ZeroVector;
	bool InMenuProof=false, FreeCursor=false;
	int32 PickSide=-1;
	double PickTime=0;
	float FirstYaw=0, FirstPitch=.2f, RoamPitch=.05f;
	FVector FirstForward=FVector(-1,0,0), FirstUp=FVector::UpVector, SmoothedEye=FVector::ZeroVector;
	FVector2D LatchedLean=FVector2D::ZeroVector;
	FString SettingsFile() const;
	int32 Limb = -1;
	// where the lifted limb was, for putting it back
	int32 Previous = -1;
	bool Pressed = false;
	FVector2D Post = FVector2D::ZeroVector;
	int32 Falls = 0;
	bool Shooting = false;
	bool Taken = false;
	bool Gym = false;
	bool Wide = false;
	FString Shots;
	float Elapsed = 0;
	FVector Behind = FVector(1, 0, 0);
	float Clock = 0;
	FVector2D Aim = FVector2D::ZeroVector;

	bool Cursor(FVector2D& Out) const;
	FVector2D Tip(int32 Index) const;
	void Press(FVector2D Point);
	void Cancel();
	void Status() const;

	// 0 free roam on the mats, 1 climbing: e mounts the nearest problem, j drops back down
	int32 Mode = 0;
	FLocomotion Locomotion;
	FVector WalkAt = FVector(450, 0, 28);
	float WalkYaw = PI, WalkPitch = 0.3f;
	int32 Near = -1;
	int32 ProofFrame = 0;
	bool Looking = false;
	FVector2D LastMouse = FVector2D::ZeroVector;
	void Mount();
	void Dismount();
};
