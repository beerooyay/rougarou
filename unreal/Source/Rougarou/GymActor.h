#pragma once

#include "ClimbSim.h"
#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HoldShapes.h"
#include "GymActor.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;
class UProceduralMeshComponent;

// the rougarou rock lounge: a steel warehouse lined with gym walls built from flat angled plywood-and-black panels,
// a massive faceted arch boulder with a cave through it, black mats, led strips and a lounge loft.
// every boulder problem is set straight onto the gym's own panels, tape-colored like a real gym.
// unreal axes as everywhere here: x runs from the back wall into the room, y across, z up, centimeters.
UCLASS()
class ROUGAROU_API AGymActor : public AActor
{
	GENERATED_BODY()

public:
	AGymActor();

	// where the wide establishing shot sits and looks
	static const FVector Overview;
	static const FVector Focus;

	// every boulder problem in the gym, each on its own stack of panels
	static TArray<FClimbRoute> Problems();
	// the tape color of problem Index
	static FLinearColor Tape(int32 Index);
	static bool WalkerBlocked(FVector At);
	static FVector MoveWalker(FVector From, FVector Step);
	static FVector ClearWalker(FVector At, FVector Direction);

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Panels;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> HoldMesh;

	UPROPERTY()
	TMap<FName, TObjectPtr<UInstancedStaticMeshComponent>> Batches;

	// wall panels: 0 plywood, 1 black
	FMeshSink Boards[2];
	// holds by palette color, plus the dark of pocket holes
	TArray<FMeshSink> Paint;
	FMeshSink Shadow;
	TArray<FLinearColor> Palette;
	FRandomStream Dice;

	UMaterialInstanceDynamic* Coat(FLinearColor Color);
	UMaterialInstanceDynamic* Finish(const TCHAR* Path, FLinearColor Color, float Grain, float Roughness);
	UInstancedStaticMeshComponent* Batch(FName Name, const TCHAR* Mesh, FLinearColor Color, bool Shadows = true);
	void Block(FVector Center, FVector Size, FLinearColor Color, bool Shadows = true);
	int32 Swatch(FLinearColor Color);

	// a faceted panel over a surface. Flat keeps problem areas smooth and bare; Show says which side of a facet faces climbers
	void Facets(TFunctionRef<FVector(float, float)> Surface, int32 Columns, int32 Rows, float Bump, float Dark, float Crowd,
		TFunctionRef<bool(float, float)> Flat, TFunctionRef<bool(FVector, FVector)> Show);
	void Sprinkle(FVector A, FVector B, FVector C, FVector Normal, float Crowd);
	void Set(const FClimbRoute& Problem, int32 Index);

	void Shell();
	void Walls();
	void Arch();
	void Loft();
	void Garage();
};
