#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonReader.h"
#include "Dom/JsonObject.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "LocomotionAnim.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "ClothingAsset.h"
#include "ChaosCloth/ChaosClothConfig.h"
#include "Misc/AutomationTest.h"
#include "Animation/AnimSequence.h"
#include "GarmentPhysics.h"

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FStablePlayerTest,"Rougarou.StablePlayer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FStablePlayerTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("stable player exists"),Mesh))return false;
 const bool Blaize=Mesh->GetPathName().Contains(TEXT("BlaizePlayer")) || Mesh->GetPathName().Contains(TEXT("Blaize2Player"));
 if(!Mesh->GetPathName().Contains(TEXT("CoreyPlayer")) && !Mesh->GetPathName().Contains(TEXT("PersonalPlayer")) && !Blaize)
 {
  const bool Experimental=FParse::Param(FCommandLine::Get(),TEXT("garmentcandidate")) || FParse::Param(FCommandLine::Get(),TEXT("modularplayer"));
  TestTrue(TEXT("only explicit experimental flags may bypass the stable default"),Experimental);
  return Experimental;
 }
 if(Mesh->GetPathName().Contains(TEXT("PersonalPlayer")) || Blaize)
  for(const TCHAR* Name:{TEXT("LeftSoleContact"),TEXT("RightSoleContact"),TEXT("PlayerEyes")})
   TestTrue(TEXT("personal player fitted landmark exists"),Mesh->GetRefSkeleton().FindBoneIndex(Name)>=0);
 TestFalse(TEXT("stable player has no Chaos cloth binding"),Mesh->HasActiveClothingAssets());
 if(!Blaize)for(int32 I=0;I<8;++I)TestTrue(TEXT("bounded garment control exists"),Mesh->GetRefSkeleton().FindBoneIndex(FGarmentPhysics::Bone(I))>=0);
 if(Blaize)for(int32 I=0;I<8;++I)TestEqual(TEXT("original outfit has no added garment deformation"),Mesh->GetRefSkeleton().FindBoneIndex(FGarmentPhysics::Bone(I)),INDEX_NONE);
 for(const TCHAR* Name:{TEXT("idle"),TEXT("walk"),TEXT("jog"),TEXT("run"),TEXT("start"),TEXT("stop")})
 {
  const UAnimSequence* Clip=ULocomotionAnim::LoadClip(*Mesh,Name);
  if(TestNotNull(Name,Clip))TestTrue(TEXT("clip uses stable skeleton"),Clip->GetSkeleton()==Mesh->GetSkeleton());
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPersonalGeometryTest,"Rougarou.PersonalGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPersonalGeometryTest::RunTest(const FString& Parameters)
{
 auto* Mesh=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Local/PersonalPlayer/climber.climber"),nullptr,LOAD_NoWarn);
 if(!Mesh){AddInfo(TEXT("Personal player not installed"));return true;}
 const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Bind;
 for(int32 I=0;I<Ref.GetNum();I++)Bind.Add(Ref.GetParentIndex(I)<0?Ref.GetRefBonePose()[I]:Ref.GetRefBonePose()[I]*Bind[Ref.GetParentIndex(I)]);
 TArray<FVector> Rubber;float Low=MAX_flt,High=-MAX_flt;
 for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections)
 {
  const FString Material=Mesh->GetMaterials()[Section.MaterialIndex].ImportedMaterialSlotName.ToString();
  for(const auto& V:Section.SoftVertices)
  {
   const FVector P(V.Position);Low=FMath::Min(Low,float(P.Z));High=FMath::Max(High,float(P.Z));
   if(Material==TEXT("Personal_Rubber"))Rubber.Add(P);
  }
 }
 TestTrue(TEXT("personal player is 5 feet 7 inches tall"),FMath::Abs(High-Low-170.18f)<.2f);
 for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})
 {
  const int32 Contact=Ref.FindBoneIndex(FName(FString(Side)+TEXT("SoleContact")));
  if(!TestTrue(TEXT("shoe surface marker exists"),Contact>=0))continue;
  const FVector Point=Bind[Contact].GetLocation();double Distance=MAX_dbl;
  for(const FVector& V:Rubber)Distance=FMath::Min(Distance,FVector::Distance(Point,V));
  TestTrue(TEXT("climbing marker lies on actual rubber geometry"),Distance<.1);
  TestTrue(TEXT("shoe marker is on the underside"),Point.Z<Low+.4f);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBlaizeGeometryTest,"Rougarou.BlaizeGeometry",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FBlaizeGeometryTest::RunTest(const FString& Parameters)
{
 const TCHAR* Package=FParse::Param(FCommandLine::Get(),TEXT("blaize2player")) ? TEXT("/Game/Local/Blaize2Player/climber.climber") : TEXT("/Game/Local/BlaizePlayer/climber.climber");
 auto* Mesh=LoadObject<USkeletalMesh>(nullptr,Package,nullptr,LOAD_NoWarn);
 if(!Mesh){AddInfo(TEXT("Blaize export not installed"));return true;}
 const auto& Ref=Mesh->GetRefSkeleton();TArray<FTransform> Bind;
 for(int32 I=0;I<Ref.GetNum();++I)Bind.Add(Ref.GetParentIndex(I)<0?Ref.GetRefBonePose()[I]:Ref.GetRefBonePose()[I]*Bind[Ref.GetParentIndex(I)]);
 float Low=MAX_flt,High=-MAX_flt;int32 Triangles=0;TArray<FVector> Points;
 for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections)
 {
  Triangles+=Section.NumTriangles;
  for(const auto& V:Section.SoftVertices)
  {
   const FVector P(V.Position);Points.Add(P);Low=FMath::Min(Low,float(P.Z));High=FMath::Max(High,float(P.Z));
  }
 }
 // A localized head/neck sculpt changes total height without resizing legs.
 double ExpectedHeight=180;
 if(FParse::Param(FCommandLine::Get(),TEXT("blaize2player")))
 {
  FString Text;TSharedPtr<FJsonObject> Fit;
  if(FFileHelper::LoadFileToString(Text,*(FPaths::ProjectContentDir()/TEXT("Data/PlayerFits/Blaize2Player.json")))
   && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Fit) && Fit.IsValid())
   Fit->TryGetNumberField(TEXT("expected_height_cm"),ExpectedHeight);
 }
 TestTrue(TEXT("import preserves the reviewed source-fit height"),FMath::Abs(High-Low-ExpectedHeight)<.02f);
 TestEqual(TEXT("all source triangles retained"),Triangles,49600);
 TestEqual(TEXT("original unified atlas material retained"),Mesh->GetMaterials().Num(),1);
 TestFalse(TEXT("no procedural cloth replaces the supplied skinning"),Mesh->HasActiveClothingAssets());
 for(const TCHAR* Side:{TEXT("Left"),TEXT("Right")})
 {
  const int32 Index=Ref.FindBoneIndex(FName(FString(Side)+TEXT("SoleContact")));
  if(!TestTrue(TEXT("measured sole landmark exists"),Index>=0))continue;
  const FVector Point=Bind[Index].GetLocation();double Distance=MAX_dbl;
  for(const auto& P:Points)Distance=FMath::Min(Distance,FVector::Distance(P,Point));
  TestTrue(TEXT("sole marker stays on the imported mesh"),Distance<.02);
  TestTrue(TEXT("sole marker is near the actual underside"),Point.Z<Low+1.f);
 }
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGarmentBodyTest,"Rougarou.GarmentBodyPreserved",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FGarmentBodyTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Original=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Local/ModularPlayer/climber.climber"));
 USkeletalMesh* Candidate=LoadObject<USkeletalMesh>(nullptr,TEXT("/Game/Local/ModularPlayerCandidate/climber.climber"),nullptr,LOAD_NoWarn);
 if(!Candidate){AddInfo(TEXT("No downloaded garment candidate installed"));return true;}
 if(!TestNotNull(TEXT("original player"),Original))return false;
 double RestDelta=0;
 const auto& A=Original->GetRefSkeleton();const auto& B=Candidate->GetRefSkeleton();
 for(int32 I=0;I<A.GetNum();++I)
 {
  const int32 J=B.FindBoneIndex(A.GetBoneName(I));if(!TestTrue(TEXT("body bone retained"),J>=0))return false;
  const auto& X=A.GetRefBonePose()[I];const auto& Y=B.GetRefBonePose()[J];
  RestDelta=FMath::Max(RestDelta,(X.GetTranslation()-Y.GetTranslation()).Length());
  RestDelta=FMath::Max(RestDelta,X.GetRotation().AngularDistance(Y.GetRotation()));
  RestDelta=FMath::Max(RestDelta,(X.GetScale3D()-Y.GetScale3D()).Length());
  const auto& XI=Original->GetRefBasesInvMatrix()[I];const auto& YI=Candidate->GetRefBasesInvMatrix()[J];
  for(int32 R=0;R<4;++R)for(int32 C=0;C<4;++C)RestDelta=FMath::Max(RestDelta,(double)FMath::Abs(XI.M[R][C]-YI.M[R][C]));
 }
 auto Signatures=[](USkeletalMesh* Mesh)
 {
  TMap<FString,TArray<FVector3f>> Result;const auto& Ref=Mesh->GetRefSkeleton();
  for(const auto& S:Mesh->GetImportedModel()->LODModels[0].Sections)
  {
   const FString Material=Mesh->GetMaterials()[S.MaterialIndex].ImportedMaterialSlotName.ToString();
   if(!Material.StartsWith(TEXT("Skin_")) && Material!=TEXT("Climbing_Shoes"))continue;
   for(const auto& V:S.SoftVertices)
   {
    TArray<FString> Weights;
    // Compare at 8-bit weight precision; tiny 16-bit rounding residues can
    // differ after FBX reimport without a meaningful skinning change.
    for(int32 K=0;K<MAX_TOTAL_INFLUENCES;++K)
    {
     const int32 Weight=FMath::RoundToInt(V.InfluenceWeights[K]/257.f);
     if(Weight)Weights.Add(FString::Printf(TEXT("%s:%d"),*Ref.GetBoneName(S.BoneMap[V.InfluenceBones[K]]).ToString(),Weight));
    }
    Weights.Sort();
    Result.FindOrAdd(FString::Printf(TEXT("%s:%.4f,%.4f:%s"),*Material,V.UVs[0].X,V.UVs[0].Y,*FString::Join(Weights,TEXT(",")))).Add(V.Position);
   }
  }
  return Result;
 };
 const auto Before=Signatures(Original),After=Signatures(Candidate);
 int32 Changed=0;double PositionDelta=0;
 // Runtime export may omit deeply covered thigh faces. Every retained
 // body vertex must still match the original geometry, UVs and weights.
 for(const auto& Pair:After)
 {
  const auto* Matches=Before.Find(Pair.Key);
  if(!Matches){if(Changed<3)AddInfo(TEXT("Missing body UV/weights ")+Pair.Key);Changed+=Pair.Value.Num();continue;}
  for(const auto& P:Pair.Value)
  {
   double Distance=MAX_dbl;for(const auto& Q:*Matches)Distance=FMath::Min(Distance,(double)(P-Q).Length());
   // FBX/mesh building welds near-coincident imported points. Source body
   // geometry is checked exactly in Blender; allow at most 0.5 mm here.
   PositionDelta=FMath::Max(PositionDelta,Distance);Changed+=Distance>.05;
  }
 }
 AddInfo(FString::Printf(TEXT("Body UV/weight groups %d / %d, changed signatures %d, max rest delta %.9f"),Before.Num(),After.Num(),Changed,RestDelta));
 AddInfo(FString::Printf(TEXT("Maximum preserved-body position delta %.9f cm"),PositionDelta));
 TestTrue(TEXT("native bind pose retained within import precision"),RestDelta<.005);
 TestTrue(TEXT("visible body remains present after covered-skin culling"),After.Num()>Before.Num()*.90);
 TestEqual(TEXT("body positions, atlas UVs and named skin weights retained"),Changed,0);
 return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FModularPlayerTest,"Rougarou.ModularPlayer",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FModularPlayerTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Mesh=ULocomotionAnim::LoadPlayerMesh();
 if(!TestNotNull(TEXT("assembled player"),Mesh))return false;
 if(!Mesh->GetPathName().Contains(TEXT("ModularPlayer")))
 {AddInfo(TEXT("Modular outfit is opt-in; stable player has its own validation"));return true;}
 const auto& Ref=Mesh->GetRefSkeleton();
 for(const TCHAR* Name:{TEXT("LeftSoleContact"),TEXT("RightSoleContact"),TEXT("PlayerEyes"),TEXT("LeftHandIndex3"),TEXT("RightHandPinky3")})
  TestTrue(FString::Printf(TEXT("%s landmark exists"),Name),Ref.FindBoneIndex(Name)>=0);
 TestEqual(TEXT("one shared outfit simulation"),Mesh->GetMeshClothingAssets().Num(),1);
 auto* Cloth=Cast<UClothingAssetCommon>(Mesh->GetMeshClothingAssets()[0]);
 if(!TestNotNull(TEXT("outfit clothing data"),Cloth))return false;
 auto* Config=Cloth->GetClothConfig<UChaosClothConfig>();
 if(!TestNotNull(TEXT("Chaos outfit configuration"),Config))return false;
 TestTrue(TEXT("garments collide with one another"),Config->bUseSelfCollisions);
 TestTrue(TEXT("fast movement uses continuous collision"),Config->bUseCCD);
 int32 Pinned=0,Free=0;
 for(const auto& Map:Cloth->LodData[0].PointWeightMaps)
  if(Map.CurrentTarget==(uint8)EWeightMapTargetCommon::MaxDistance)
   for(float Value:Map.Values){Pinned+=Value<.001f;Free+=Value>.01f;}
 TestTrue(TEXT("collar and waistband remain anchored"),Pinned>0);
 TestTrue(TEXT("fabric has independent movement"),Free>Pinned);
 TArray<FTransform> Bind=Ref.GetRefBonePose();
 for(int32 I=0;I<Bind.Num();++I)if(Ref.GetParentIndex(I)>=0)Bind[I]*=Bind[Ref.GetParentIndex(I)];
 struct FCapsule{FVector A,B;double Radius;};TArray<FCapsule> Capsules;
 TSet<FName> Bodies;
 for(const USkeletalBodySetup* Body:Mesh->GetPhysicsAsset()->SkeletalBodySetups)
 {
  TestFalse(TEXT("collision bodies have unique bone bindings"),Bodies.Contains(Body->BoneName));Bodies.Add(Body->BoneName);
  const int32 Bone=Ref.FindBoneIndex(Body->BoneName);if(Bone<0)continue;
  const FTransform& World=Bind[Bone];const double Scale=World.GetScale3D().X;
  for(const auto& Shape:Body->AggGeom.SphylElems)
  {
   const FVector Axis=Shape.Rotation.RotateVector(FVector::UpVector)*Shape.Length*.5;
   Capsules.Add({World.TransformPosition(Shape.Center-Axis),World.TransformPosition(Shape.Center+Axis),Shape.Radius*Scale});
  }
 }
 int32 Foundation=0,Outside=0,ShirtColors=0,ShortsColors=0,ProxyVertices=0,RenderFabricVertices=0;
 for(const auto& Section:Mesh->GetImportedModel()->LODModels[0].Sections)
 {
  const UMaterialInterface* Material=Mesh->GetMaterials()[Section.MaterialIndex].MaterialInterface;
  if(Material && Material->GetName().StartsWith(TEXT("Outfit_Proxy")))
  {
   ProxyVertices+=Section.SoftVertices.Num();
   TestTrue(TEXT("simulation proxy does not render"),Section.bDisabled);
   TestFalse(TEXT("simulation proxy does not cast a duplicate shadow"),Section.bCastShadow);
  }
  if(Material && Material->GetName().StartsWith(TEXT("Outfit_Fabric")))
   for(const auto& Vertex:Section.SoftVertices){++RenderFabricVertices;ShirtColors+=Vertex.UVs[1].X<.1f;ShortsColors+=Vertex.UVs[1].X>.9f;}
  if(!Material||!Material->GetName().StartsWith(TEXT("Skin_Underlap")))continue;
  for(const auto& Vertex:Section.SoftVertices)
  {
   // The generated foundation has no atlas UVs; limb overlap strips retain
   // the scan's UVs. Check the foundation against the same physical shapes.
   if(FMath::Abs(Vertex.UVs[0].X)>.00001f || (FMath::Abs(Vertex.UVs[0].Y)>.00001f && FMath::Abs(Vertex.UVs[0].Y-1)>.00001f))continue;
   Foundation++;double Distance=MAX_dbl;
   for(const auto& Shape:Capsules)
    Distance=FMath::Min(Distance,FVector::Dist(FVector(Vertex.Position),FMath::ClosestPointOnSegment(FVector(Vertex.Position),Shape.A,Shape.B))-Shape.Radius);
   Outside+=Distance>.05;
  }
 }
 AddInfo(FString::Printf(TEXT("Foundation %d vertices, %d outside %d collision capsules; outfit %d pinned / %d free"),Foundation,Outside,Capsules.Num(),Pinned,Free));
 TestTrue(TEXT("covered skin foundation is present"),Foundation>=300);
 TestEqual(TEXT("rendered foundation fits inside body collision"),Outside,0);
 TestTrue(TEXT("shirt and shorts retain distinct authored color regions"),ShirtColors>500 && ShortsColors>500);
 if(ProxyVertices)
 {
  TestTrue(TEXT("cloth solves on a coarser proxy"),Cloth->LodData[0].PhysicalMeshData.Vertices.Num()*2<RenderFabricVertices);
  AddInfo(FString::Printf(TEXT("Cloth proxy %d particles drives %d render fabric vertices"),Cloth->LodData[0].PhysicalMeshData.Vertices.Num(),RenderFabricVertices));
 }
 return true;
}
#endif
