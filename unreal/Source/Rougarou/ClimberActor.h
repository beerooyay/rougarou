#pragma once

#include "ClimbSim.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ClimberActor.generated.h"

// drives the climbing sim at 60 hz and draws the wall, holds and limbs until the metahuman rig takes over.
// unreal axes: x is depth off the wall, y is across (sim x), z is up (sim y); sim meters become centimeters.
UCLASS()
class ROUGAROU_API AClimberActor : public AActor
{
	GENERATED_BODY()

public:
	AClimberActor();

	UPROPERTY(EditAnywhere, Category = "Climb")
	int32 Route = 0;

	// hips ride this far off the wall face, like a real climber
	UPROPERTY(EditAnywhere, Category = "Climb")
	float Gap = 30;

	UFUNCTION(BlueprintCallable, Category = "Climb")
	bool Grab(int32 Limb, int32 Hold) { return Sim.Grab(Limb, Hold); }

	UFUNCTION(BlueprintCallable, Category = "Climb")
	void SetLean(FVector2D Lean) { Sim.Lean = Lean; }

	UFUNCTION(BlueprintPure, Category = "Climb")
	FVector Place(FVector2D Point, float Depth = 0) const;

	// the limb the player is moving, drawn with a highlight
	int32 Selected = -1;

	// respawn the wall and hold meshes for the current route
	void Rebuild();

	FClimbSim Sim;

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float Delta) override;

private:
	float Banked = 0;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Parts;

	UStaticMeshComponent* Shape(const TCHAR* Mesh, FVector Location, FVector Scale, FLinearColor Color);
	// a jointed mannequin in blaize's colors: tee, olive shorts, yellow shoes. the metahuman replaces it
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Figure;

	// hands and feet: per hand a wrist and two-segment fingers and thumb, per foot an ankle
	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Extras;
	float Clock = 0;

	void Dress();
	void Orient(UStaticMeshComponent* Part, FVector Center, FVector Along, FVector Out, FVector Size) const;
	// the hand's fingers shaped to the grip: Over is the way the fingers point, Out away from the wall, Thumb toward the thumb side
	void Fingers(int32 Hand, FVector Palm, FVector Over, FVector Out, FVector Thumb, const FString& Grip) const;
	void Segment(UStaticMeshComponent* Part, FVector Start, FVector End, float Radius) const;
	void Draw();
};
