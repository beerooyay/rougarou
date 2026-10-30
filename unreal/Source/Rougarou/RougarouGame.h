#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "RougarouGame.generated.h"

class AClimberActor;
class ACameraActor;

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

private:
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
	float Elapsed = 0;
	FVector Behind = FVector(1, 0, 0);
	float Clock = 0;
	FVector2D Aim = FVector2D::ZeroVector;

	bool Cursor(FVector2D& Out) const;
	FVector2D Tip(int32 Index) const;
	void Press(FVector2D Point);
	void Cancel();
	void Status() const;
};
