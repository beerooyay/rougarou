// Opt-in playback of the frozen, assisted MuJoCo reference. Never drives ClimbSim.
#include "RougarouGame.h"
#include "ClimberActor.h"
#include "LocomotionAnim.h"
#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/PointLight.h"
#include "Components/PointLightComponent.h"
#include "Camera/CameraActor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"
#include "UnrealClient.h"

static const FVector ReferenceOrigin(5000,0,0);
void ARougarouController::ToggleReferencePreview()
{
 if(!Climber || !Climber->Walker || !Climber->Walker->GetSkeletalMeshAsset())return;
 if(ReferencePreview)
 {
  ReferencePreview=false;
  for(AActor* A:ReferenceScene)if(A)A->Destroy();
  ReferenceScene.Reset();
  Climber->Walker->SetAnimationMode(EAnimationMode::AnimationBlueprint);
  Climber->Walker->SetAnimInstanceClass(ULocomotionAnim::StaticClass());
  Mode=0;Climber->Walking=true;Climber->Walk(WalkAt,WalkYaw,0);
  MenuOpen=false;InputMode();return;
 }
 auto* Clip=LoadObject<UAnimSequence>(nullptr,TEXT("/Game/Local/Blaize2ShoulderPlayer/reference_regrip.reference_regrip"));
 if(!Clip || Clip->GetSkeleton()!=Climber->Walker->GetSkeletalMeshAsset()->GetSkeleton())return;
 ReferencePreview=true;ReferenceElapsed=0;ReferenceShotTaken=false;FirstPerson=false;FirstBlend=0;Climber->SetInside(false);
 MenuOpen=false;Climber->Walking=true;Climber->Paused=false;Climber->Walker->bPauseAnims=false;
 ClimbOrbitYaw=0;ClimbOrbitPitch=0;
 Climber->Walker->SetWorldTransform(FTransform(FRotator(0,-90,0),ReferenceOrigin));
 Climber->Walker->PlayAnimation(Clip,true);
 Climber->Walker->PrestreamTextures(10.f,true);
 auto Box=[&](FVector At,FVector Size,FLinearColor Color)
 {
  auto* Actor=GetWorld()->SpawnActor<AStaticMeshActor>();ReferenceScene.Add(Actor);
  auto* C=Actor->GetStaticMeshComponent();C->SetMobility(EComponentMobility::Movable);
  C->SetStaticMesh(LoadObject<UStaticMesh>(nullptr,TEXT("/Engine/BasicShapes/Cube.Cube")));
  C->SetCollisionEnabled(ECollisionEnabled::NoCollision);Actor->SetActorLocation(ReferenceOrigin+At);Actor->SetActorScale3D(Size/100);
  auto* Mat=LoadObject<UMaterialInterface>(nullptr,TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
  auto* Dynamic=UMaterialInstanceDynamic::Create(Mat,Actor);Dynamic->SetVectorParameterValue(TEXT("Color"),Color);C->SetMaterial(0,Dynamic);
 };
 auto* Light=GetWorld()->SpawnActor<APointLight>(ReferenceOrigin+FVector(-220,0,260),FRotator::ZeroRotator);
 ReferenceScene.Add(Light);Light->PointLightComponent->SetMobility(EComponentMobility::Movable);Light->PointLightComponent->SetIntensity(7000);Light->PointLightComponent->SetAttenuationRadius(1000);Light->PointLightComponent->SetCastShadows(false);
 Box(FVector(10,0,180),FVector(20,400,360),FLinearColor(.2,.25,.12));
 FString Text;TSharedPtr<FJsonObject> Data;
 if(FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/TrainingReference/reference.json"))) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data))
 {
  for(const auto& Value:Data->GetArrayField(TEXT("holds")))
  {
   const auto& V=Value->AsArray();const FVector P(V[1]->AsNumber()*100,V[0]->AsNumber()*100,V[2]->AsNumber()*100);
   Box(P+FVector(0,0,-2.5),FVector(5,15,11),FLinearColor(.7,.5,.2));
   Box(P+FVector(-4.5,0,1.2),FVector(3.6,15,2.6),FLinearColor(.7,.5,.2));
  }
 }
 InputMode();ReferenceCamera(1);
}
void ARougarouController::ReferenceCamera(float Delta)
{
 ReferenceElapsed+=Delta;
 if(FParse::Param(FCommandLine::Get(),TEXT("referenceproof"))){if(ReferenceElapsed>6 && !ReferenceShotTaken){FScreenshotRequest::RequestScreenshot(FPaths::ProjectSavedDir()/TEXT("TrainingReference.png"),true,false);ReferenceShotTaken=true;}if(ReferenceElapsed>7)ConsoleCommand(TEXT("quit"));}
 const FVector Focus=ReferenceOrigin+FVector(-25,0,175);
 const FVector Offset=FVector(-340,0,0).RotateAngleAxis(FMath::RadiansToDegrees(ClimbOrbitYaw),FVector::UpVector)+FVector(0,0,ClimbOrbitPitch*200);
 View->SetActorLocation(FMath::VInterpTo(View->GetActorLocation(),Focus+Offset,Delta,8));
 View->SetActorRotation((Focus-View->GetActorLocation()).Rotation());
}
