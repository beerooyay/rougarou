#if WITH_EDITOR
#include "CoreMinimal.h"
#include "HAL/IConsoleManager.h"
#include "Engine/SkeletalMesh.h"
#include "ClothingAssetFactory.h"
#include "ClothingAsset.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "UObject/SavePackage.h"
#include "Misc/PackageName.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"

static bool BuildModularCollision(UPhysicsAsset* Physics,const FReferenceSkeleton& Ref,const TArray<FTransform>& Space,double RootScale)
{
 FString Text;TSharedPtr<FJsonObject> Data;
 if(!FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/modular-player-collision.json"))) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data))
 {UE_LOG(LogTemp,Error,TEXT("Missing modular body collision definition; rebuild the player"));return false;}
 auto Vector=[](const TSharedPtr<FJsonObject>& Object,const TCHAR* Field)
 {
  const auto& A=Object->GetArrayField(Field);return FVector(A[0]->AsNumber(),A[1]->AsNumber(),A[2]->AsNumber());
 };
 const FVector Origin=Vector(Data,TEXT("origin")),Side=Vector(Data,TEXT("side")),Front=Vector(Data,TEXT("front")),Up=Vector(Data,TEXT("up"));
 auto At=[&](const TCHAR* Bone){return Space[Ref.FindBoneIndex(Bone)].GetLocation();};
 const FVector Hip=At(TEXT("Hips")),Across=(At(TEXT("LeftArm"))-At(TEXT("RightArm"))).GetSafeNormal();
 const FVector Vertical=FVector::VectorPlaneProject(At(TEXT("Head"))-Hip,Across).GetSafeNormal();
 // Use the actual toes for forward. FBX changes coordinate handedness, so a
 // cross product here can mirror all front/back collider offsets.
 const FVector Toes=At(TEXT("LeftToeBase"))-At(TEXT("LeftFoot"))+At(TEXT("RightToeBase"))-At(TEXT("RightFoot"));
 const FVector Forward=FVector::VectorPlaneProject(FVector::VectorPlaneProject(Toes,Vertical),Across).GetSafeNormal();
 auto Point=[&](FVector P){P-=Origin;return Hip+100*(Across*FVector::DotProduct(P,Side)+Forward*FVector::DotProduct(P,Front)+Vertical*FVector::DotProduct(P,Up));};
 TMap<FName,USkeletalBodySetup*> Bodies;
 for(const auto& Entry:Data->GetArrayField(TEXT("capsules")))
 {
  const auto ShapeData=Entry->AsObject();const FName Name(*ShapeData->GetStringField(TEXT("bone")));
  const int32 Index=Ref.FindBoneIndex(Name);if(Index<0){UE_LOG(LogTemp,Error,TEXT("Missing collision bone %s"),*Name.ToString());return false;}
  USkeletalBodySetup*& Body=Bodies.FindOrAdd(Name);
  if(!Body){Body=NewObject<USkeletalBodySetup>(Physics);Body->BoneName=Name;Physics->SkeletalBodySetups.Add(Body);}
  const FVector A=Space[Index].InverseTransformPosition(Point(Vector(ShapeData,TEXT("a")))),B=Space[Index].InverseTransformPosition(Point(Vector(ShapeData,TEXT("b"))));
  FKSphylElem Shape;Shape.Center=(A+B)*.5;Shape.Length=(B-A).Size();Shape.Radius=ShapeData->GetNumberField(TEXT("radius"))*100/RootScale;
  if(Shape.Length>UE_SMALL_NUMBER)Shape.Rotation=FQuat::FindBetweenNormals(FVector::UpVector,(B-A).GetSafeNormal()).Rotator();
  Body->AggGeom.SphylElems.Add(Shape);
 }
 return true;
}

static void BuildPlayerClothing(const TArray<FString>& Args)
{
 const FString Asset=Args.IsEmpty()?TEXT("/Game/Local/CoreyCloth/climber.climber"):Args[0];
 USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,*Asset);
 if(!Mesh){UE_LOG(LogTemp,Error,TEXT("Cloth setup: missing mesh"));return;}
 const bool Modular=Asset.Contains(TEXT("ModularPlayer"));
 const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
 TArray<FTransform> Space=Ref.GetRefBonePose();
 for(int32 I=0;I<Space.Num();++I)if(Ref.GetParentIndex(I)>=0)Space[I]*=Space[Ref.GetParentIndex(I)];
 UPhysicsAsset* Physics=Mesh->GetPhysicsAsset();
 if(!Physics)Physics=NewObject<UPhysicsAsset>(Mesh,TEXT("GarmentCollision"),RF_Public|RF_Transactional);
 Physics->SkeletalBodySetups.Reset();
 // Chaos scales physics primitives by the FBX root scale when it creates them.
 const double RootScale=FMath::Max(UE_SMALL_NUMBER,FMath::Abs(Ref.GetRefBonePose()[0].GetScale3D().X));
 auto Capsule=[&](const TCHAR* Start,const TCHAR* End,float Radius)->USkeletalBodySetup*
 {
  const int32 A=Ref.FindBoneIndex(Start),B=Ref.FindBoneIndex(End);
  if(A<0||B<0)return nullptr;
  USkeletalBodySetup* Body=NewObject<USkeletalBodySetup>(Physics);
  Body->BoneName=Start;
  const FVector Tip=Space[A].InverseTransformPosition(Space[B].GetLocation());
  FKSphylElem Shape;Shape.Center=Tip*.5;Shape.Radius=Radius/RootScale;Shape.Length=FMath::Max(0.,Tip.Size()-Shape.Radius);
  Shape.Rotation=FQuat::FindBetweenNormals(FVector::UpVector,Tip.GetSafeNormal()).Rotator();
  Body->AggGeom.SphylElems.Add(Shape);Physics->SkeletalBodySetups.Add(Body);
  return Body;
 };
 if(Modular)
 {
  if(!BuildModularCollision(Physics,Ref,Space,RootScale))return;
 }
 else
 {
  Capsule(TEXT("Hips"),TEXT("Spine2"),12);
  Capsule(TEXT("Spine2"),TEXT("Neck"),12.5f);
  Capsule(TEXT("Neck"),TEXT("Head"),5.5f);
  for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})
  {
   FString S(Side);
   Capsule(*(S+TEXT("UpLeg")),*(S+TEXT("Leg")),9.1f);
   Capsule(*(S+TEXT("Leg")),*(S+TEXT("Foot")),5.5f);
   Capsule(*(S+TEXT("ForeArm")),*(S+TEXT("Hand")),4.2f);
   if(USkeletalBodySetup* Upper=Capsule(*(S+TEXT("Arm")),*(S+TEXT("ForeArm")),5.3f))
   {
    FKSphereElem Deltoid;Deltoid.Center=FVector::ZeroVector;Deltoid.Radius=7.2f/RootScale;
    Upper->AggGeom.SphereElems.Add(Deltoid);
   }
  }
 }
 Physics->UpdateBodySetupIndexMap();Mesh->SetPhysicsAsset(Physics);
 UE_LOG(LogTemp,Display,TEXT("Garment collider FBX root scale %.3f"),RootScale);
 auto* Factory=NewObject<UClothingAssetFactory>();
 TArray<int32> Sections;
 int32 ProxySection=INDEX_NONE;
 const auto& LOD=Mesh->GetImportedModel()->LODModels[0];
 for(int32 I=0;I<LOD.Sections.Num();++I)
 {
  const auto& Material=Mesh->GetMaterials()[LOD.Sections[I].MaterialIndex];
  const FString Name=Material.MaterialInterface?Material.MaterialInterface->GetName():TEXT("");
  if(Name.Contains(TEXT("Outfit_Proxy")))ProxySection=I;
  if(Name.Contains(TEXT("Ivory_Cotton"))||Name.Contains(TEXT("Forest_Green"))||Name.Contains(TEXT("Outfit_Fabric")))Sections.Add(I);
 }
 // A reimported mesh keeps its old clothing assets while its sections lose their cloth data, so start clean.
 {
  FScopedSkeletalMeshPostEditChange ClearMesh(Mesh);
  for(UClothingAssetBase* Stale:Mesh->GetMeshClothingAssets())
  {
   if(!Stale)continue;
   Stale->UnbindFromSkeletalMesh(Mesh);
   // Move the old asset out of the package so a fresh one is built from the current section geometry under the same name.
   Stale->Rename(*MakeUniqueObjectName(GetTransientPackage(),Stale->GetClass(),TEXT("StaleCloth")).ToString(),GetTransientPackage(),REN_DontCreateRedirectors|REN_NonTransactional);
   Stale->ClearFlags(RF_Public|RF_Standalone);
  }
  Mesh->GetMeshClothingAssets().Reset();
 }
 for(int32 Section:Sections)
 {
  auto& Model=Mesh->GetImportedModel()->LODModels[0];
  if(Model.Sections[Section].HasClothingData())
  {
   if(auto* Bound=Cast<UClothingAssetCommon>(Mesh->GetSectionClothingAsset(0,Section)))Bound->PhysicsAsset=Physics;
   continue;
  }
  FScopedSkeletalMeshPostEditChange UpdateMesh(Mesh);
  const bool Shirt=Mesh->GetMaterials()[Mesh->GetImportedModel()->LODModels[0].Sections[Section].MaterialIndex].MaterialInterface->GetName().Contains(TEXT("Ivory"));
  const int32 SimulationSection=ProxySection>=0?ProxySection:Section;
  FSkeletalMeshClothBuildParams Params;Params.LodIndex=0;Params.SourceSection=SimulationSection;Params.bRemoveFromMesh=false;
  Params.AssetName=Modular?TEXT("OutfitSimulation"):Shirt?TEXT("CottonSimulation"):TEXT("ShortsSimulation");Params.PhysicsAsset=Physics;
  UClothingAssetCommon* Cloth=FindObject<UClothingAssetCommon>(Mesh,*Params.AssetName);
  if(!Cloth)
  {
   Cloth=Cast<UClothingAssetCommon>(Factory->CreateFromSkeletalMesh(Mesh,Params));
   if(!Cloth){UE_LOG(LogTemp,Error,TEXT("Cloth setup failed section %d"),Section);return;}
   Mesh->AddClothingAsset(Cloth);
  }
  if(!Mesh->GetMeshClothingAssets().Contains(Cloth))Mesh->AddClothingAsset(Cloth);
  Cloth->PhysicsAsset=Physics;
  auto& Data=Cloth->LodData[0];
  Data.bSmoothTransition=true;
  Data.bUseMultipleInfluences=ProxySection>=0;
  if(ProxySection>=0)Data.SkinningKernelRadius=2.5f;
  Data.PointWeightMaps.Reset();
  auto& Mask=Data.PointWeightMaps.AddDefaulted_GetRef();
  Mask.Initialize(Data.PhysicalMeshData.Vertices.Num());Mask.Name=TEXT("FreeFabric");Mask.CurrentTarget=(uint8)EWeightMapTargetCommon::MaxDistance;Mask.bEnabled=true;
  const float ShoulderZ=Space[Ref.FindBoneIndex(TEXT("LeftArm"))].GetLocation().Z;
  const FVector Across=(Space[Ref.FindBoneIndex(TEXT("LeftArm"))].GetLocation()-Space[Ref.FindBoneIndex(TEXT("RightArm"))].GetLocation()).GetSafeNormal();
  const float HipZ=Space[Ref.FindBoneIndex(TEXT("Hips"))].GetLocation().Z;
  TMap<FIntVector,float> Painted;
  auto Key=[](const FVector3f& Point){return FIntVector(FMath::RoundToInt(Point.X*1000),FMath::RoundToInt(Point.Y*1000),FMath::RoundToInt(Point.Z*1000));};
  if(Modular)for(const FSoftSkinVertex& Vertex:Model.Sections[SimulationSection].SoftVertices)
  {
   const FIntVector K=Key(Vertex.Position);
   const float Mobility=Vertex.Color.A/255.f;
   if(float* Old=Painted.Find(K))*Old=FMath::Min(*Old,Mobility);else Painted.Add(K,Mobility);
  }
  int32 Free=0;
  for(int32 I=0;I<Mask.Values.Num();++I)
  {
   const FVector3f P=Data.PhysicalMeshData.Vertices[I];
   // Pin the shoulder yoke/neck and waistband; release the sleeves and lower panels.
   const float Side=FMath::Abs(FVector::DotProduct(FVector(P),Across));
   const float Sleeve=FMath::Clamp((Side-21)/12,0.f,1.f);
   const float Hem=FMath::Clamp((ShoulderZ-13-P.Z)/22,0.f,1.f);
   const float Cuff=FMath::Clamp((HipZ-5-P.Z)/17,0.f,1.f);
   if(Modular)
   {
    const float* Mobility=Painted.Find(Key(P));
    if(!Mobility){UE_LOG(LogTemp,Error,TEXT("Missing modular cloth paint at vertex %d"),I);return;}
    Mask.Values[I]=*Mobility*3.5f;
   }
   else Mask.Values[I]=Shirt?FMath::Lerp(Hem*5.f,Sleeve*3.5f,Sleeve):Cuff*3.f;
   Free+=Mask.Values[I]>0;
  }
  Cloth->ApplyParameterMasks(true);
  UChaosClothConfig* Config=Cloth->GetClothConfig<UChaosClothConfig>();
  if(!Config){Config=NewObject<UChaosClothConfig>(Cloth);Cloth->ClothConfigs.Add(Config->GetClass()->GetFName(),Config);}
  Config->BendingStiffnessWeighted=Modular?FChaosClothWeightedValue{.35f,.35f}:FChaosClothWeightedValue{.45f,.45f};Config->EdgeStiffnessWeighted={.95f,.95f};
  Config->bUseBendingElements=true;
  Config->DampingCoefficient=Modular?.15f:.22f;Config->CollisionThickness=Modular?.25f:.4f;Config->FrictionCoefficient=.35f;
  Config->AnimDriveStiffness=Modular?FChaosClothWeightedValue{.15f,.15f}:FChaosClothWeightedValue{.25f,.25f};
  if(Modular)
  {
   Config->MassMode=EClothMassMode::Density;Config->Density=.2f;Config->MinPerParticleMass=.000001f;
   Config->bUseSelfCollisions=true;Config->SelfCollisionThickness=.18f;Config->SelfCollisionFriction=.25f;
   Config->bUseSelfIntersections=true;Config->bUseCCD=true;
  }
  Cloth->InvalidateAllCachedData();
  if(!Cloth->BindToSkeletalMesh(Mesh,0,Section,0)){UE_LOG(LogTemp,Error,TEXT("Cloth bind failed"));return;}
  // Derived mesh builds restore bindings from source section data.
  const auto& BoundSection=Model.Sections[Section];
  auto& UserData=Model.UserSectionsData.FindOrAdd(BoundSection.OriginalDataSectionIndex);
  UserData.CorrespondClothAssetIndex=BoundSection.CorrespondClothAssetIndex;
  UserData.ClothingData=BoundSection.ClothingData;
  if(ProxySection>=0)
  {
   auto& Proxy=Model.Sections[ProxySection];Proxy.bDisabled=true;Proxy.bCastShadow=false;
   auto& ProxyData=Model.UserSectionsData.FindOrAdd(Proxy.OriginalDataSectionIndex);
   ProxyData.bDisabled=true;ProxyData.bCastShadow=false;
  }
  UE_LOG(LogTemp,Display,TEXT("CLOTH READY %s: %d particles, %d free"),*Params.AssetName,Mask.Values.Num(),Free);
 }
 Mesh->PostEditChange();Mesh->MarkPackageDirty();
 FSavePackageArgs SaveArgs;SaveArgs.TopLevelFlags=RF_Public|RF_Standalone;SaveArgs.SaveFlags=SAVE_NoError;
 const FString Filename=FPackageName::LongPackageNameToFilename(Mesh->GetOutermost()->GetName(),FPackageName::GetAssetPackageExtension());
 if(!UPackage::SavePackage(Mesh->GetOutermost(),Mesh,*Filename,SaveArgs))UE_LOG(LogTemp,Error,TEXT("Cloth save failed"));
}
static FAutoConsoleCommand BuildClothingCommand(TEXT("rougarou.BuildClothing"),TEXT("Build player garment physics assets; optional mesh asset path"),FConsoleCommandWithArgsDelegate::CreateStatic(&BuildPlayerClothing));
#endif
