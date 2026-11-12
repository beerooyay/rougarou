#include "PlayerForm.h"
#include "LocomotionAnim.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonSerializer.h"
#include "Algo/Reverse.h"

FString FPlayerForm::Folder(){return FPaths::ProjectSavedDir()/TEXT("RigProfiles");}
bool FPlayerForm::Read(USkeletalMeshComponent& Mesh,const FString& Path)
{
 FString Text;TSharedPtr<FJsonObject> Data;
 if(!FFileHelper::LoadFileToString(Text,*Path)||!FHumanRig::ValidProfile(Text)||!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Data))return false;
 FHumanRig Candidate;if(!Candidate.Initialize(Mesh.GetSkeletalMeshAsset()->GetRefSkeleton(),Path))return false;
 Draft=MoveTemp(Candidate);Document=Data;Frozen=Draft.NeutralPose();Angles.Reset();AnchorOffsets.Reset();
 const TSharedPtr<FJsonObject>* Anchors=nullptr;
 if(Data->TryGetObjectField(TEXT("anchor_offsets_cm"),Anchors))
  for(const auto& Pair:(*Anchors)->Values)
  {
   const int32 Index=Mesh.GetBoneIndex(FName(*Pair.Key));
   const auto& XYZ=Pair.Value->AsArray();
   if(Draft.JointLimits().Contains(Index)) AnchorOffsets.Add(Index,FVector(XYZ[0]->AsNumber(),XYZ[1]->AsNumber(),XYZ[2]->AsNumber()));
  }
 IdleAngles.Reset();
 const TSharedPtr<FJsonObject>* Idle=nullptr;
 if(Data->TryGetObjectField(TEXT("idle_angles_deg"),Idle))
  for(const auto& Pair:(*Idle)->Values)
  {
   const int32 Index=Mesh.GetBoneIndex(FName(*Pair.Key));
   const TArray<TSharedPtr<FJsonValue>>* XYZ=nullptr;
   if(!Draft.JointLimits().Contains(Index)||!Pair.Value->TryGetArray(XYZ)||XYZ->Num()!=3) continue;
   FVector Value;
   if((*XYZ)[0]->TryGetNumber(Value.X)&&(*XYZ)[1]->TryGetNumber(Value.Y)&&(*XYZ)[2]->TryGetNumber(Value.Z)&&!Value.ContainsNaN())
    IdleAngles.Add(Index,Value.BoundToCube(180));
  }
 if(!Draft.JointLimits().Contains(Selected))Selected=Mesh.GetBoneIndex(TEXT("RightArm"));
 Clip=-1;Time=0;Playing=false;return true;
}
TMap<int32,FQuat> FPlayerForm::IdleRotations(const FHumanRig& Rig) const
{
 TMap<int32,FQuat> Rotations;
 for(const auto& Pair:IdleAngles)
  Rotations.Add(Pair.Key,(Rig.NeutralPose()[Pair.Key].GetRotation().Inverse()*Rig.PreviewJoint(Pair.Key,Pair.Value,false)).GetNormalized());
 return Rotations;
}
void FPlayerForm::ConfigureGround(USkeletalMeshComponent& Mesh,const FHumanRig& Rig) const
{
 if(auto* Anim=Cast<ULocomotionAnim>(Mesh.GetAnimInstance()))
 {
  Anim->GroundNeutral=Rig.NeutralPose();
  Anim->GroundLimits=Rig.JointLimits();
  Anim->GroundFrames.Reset();
  for(const auto& Pair:Rig.JointLimits())
   if(const auto* Frame=Rig.JointFrame(Pair.Key)) Anim->GroundFrames.Add(Pair.Key,*Frame);
  Anim->GroundAnchors=AnchorOffsets;
  Anim->GroundIdleRotations=IdleRotations(Rig);
 }
}
void FPlayerForm::LoadGroundProfile(USkeletalMeshComponent& Mesh)
{
 FPlayerForm Profile;
 if(Profile.Read(Mesh,FHumanRig::ProfilePath())) Profile.ConfigureGround(Mesh,Profile.Draft);
}
void FPlayerForm::Begin(USkeletalMeshComponent& Mesh)
{
 Open=Read(Mesh,FHumanRig::ProfilePath());if(Open)Document->TryGetStringField(TEXT("profile_name"),Name);
 Status=Open?TEXT("draft loaded · click a joint to tune its limits"):TEXT("could not load the rig profile");
}
void FPlayerForm::End(USkeletalMeshComponent& Mesh)
{
 Open=false;Playing=false;
 if(auto* Anim=Cast<ULocomotionAnim>(Mesh.GetAnimInstance()))Anim->FormOpen=false;
}
void FPlayerForm::Tick(USkeletalMeshComponent& Mesh,float Delta)
{
 if(!Open)return;
 const TArray<FTransform> Previous=Frozen;
 Frozen=Draft.NeutralPose();
 for(const auto& Pair:Angles)if(Frozen.IsValidIndex(Pair.Key))Frozen[Pair.Key].SetRotation(Draft.PreviewJoint(Pair.Key,Pair.Value,LockLimits));
 const auto& Ref=Mesh.GetSkeletalMeshAsset()->GetRefSkeleton().GetRefBonePose();
 for(const auto& Joint:Draft.JointLimits())
 {
  const int32 Index=Joint.Key;
  const FVector Target=Ref[Index].GetTranslation()+AnchorOffsets.FindRef(Index);
  Frozen[Index].SetTranslation(FMath::VInterpTo(Previous[Index].GetTranslation(),Target,Delta,22));
 }
 if(auto* Anim=Cast<ULocomotionAnim>(Mesh.GetAnimInstance()))
 {
  UAnimSequence* Sequence=Clip==6?Anim->LandingClip.Get():Anim->Clips.IsValidIndex(Clip)?Anim->Clips[Clip].Get():nullptr;
  if(Playing&&Sequence)Time=FMath::Fmod(Time+Delta,FMath::Max(.01f,Sequence->GetPlayLength()));
  if(Sequence)Time=FMath::Clamp(Time,0.f,Sequence->GetPlayLength());
  Anim->FormOpen=true;Anim->FormClip=Clip;Anim->FormTime=Time;Anim->FormFrozen=Frozen;Anim->FormNeutral=Draft.NeutralPose();Anim->FormLimits=Draft.JointLimits();Anim->FormClamp=LockLimits;
  Anim->FormIdleRotations=Clip==0?IdleRotations(Draft):TMap<int32,FQuat>();
  Anim->FormFrames.Reset();for(const auto& Pair:Draft.JointLimits())if(const auto* Frame=Draft.JointFrame(Pair.Key))Anim->FormFrames.Add(Pair.Key,*Frame);
 }
}
void FPlayerForm::MoveAnchor(USkeletalMeshComponent& Mesh,FVector Offset)
{
 if(Clip!=0) Clip=-1;
 Playing=false;
 AnchorOffsets.Add(Selected,Offset.GetClampedToMaxSize(8));
 const int32 Pair=Mirror?Other(Mesh):INDEX_NONE;
 if(Pair>=0)
 {
  const auto& Ref=Mesh.GetSkeletalMeshAsset()->GetRefSkeleton();
  TArray<FTransform> Component=Ref.GetRefBonePose();
  for(int32 Index=1;Index<Component.Num();++Index) Component[Index]*=Component[Ref.GetParentIndex(Index)];
  const int32 Left=Mesh.GetBoneIndex(TEXT("LeftArm")),Right=Mesh.GetBoneIndex(TEXT("RightArm"));
  const FVector Side=(Component[Left].GetLocation()-Component[Right].GetLocation()).GetSafeNormal();
  const FVector Delta=Component[Ref.GetParentIndex(Selected)].TransformVector(AnchorOffsets[Selected]);
  AnchorOffsets.Add(Pair,Component[Ref.GetParentIndex(Pair)].InverseTransformVector(FVector::VectorPlaneProject(Delta,Side)-Side*FVector::DotProduct(Delta,Side)).GetClampedToMaxSize(8));
 }
 Status=TEXT("anchor moved · apply to use on the wall");
}
float FPlayerForm::Get(int32 Axis) const{return (Clip==0?IdleAngles:Angles).FindRef(Selected)[Axis];}
void FPlayerForm::Set(USkeletalMeshComponent& Mesh,int32 Axis,float Value)
{
 if(!Draft.JointLimits().Contains(Selected)||Axis<0||Axis>2||!FMath::IsFinite(Value))return;
 if(Clip!=0) Clip=-1;
 Playing=false;
 auto& PoseAngles=Clip==0?IdleAngles:Angles;
 PoseAngles.FindOrAdd(Selected)[Axis]=FMath::Clamp(Value,LockLimits?Bound(Axis,false):-180.f,LockLimits?Bound(Axis,true):180.f);
 if(Mirror){const int32 Paired=Other(Mesh);if(Paired>=0)PoseAngles.FindOrAdd(Paired)=PoseAngles[Selected];}
}
float FPlayerForm::Bound(int32 Axis,bool Maximum) const
{
 const FHumanLimit* L=Draft.JointLimits().Find(Selected);if(!L)return 0;
 return Axis==0?(Maximum?L->Forward:-L->Back):Axis==1?(Maximum?L->Out:-L->In):(Maximum?L->TwistMax:L->TwistMin);
}
void FPlayerForm::SetBound(USkeletalMeshComponent& Mesh,int32 Axis,bool Maximum,float Value)
{
 const FHumanLimit* Old=Draft.JointLimits().Find(Selected);if(!Old||!FMath::IsFinite(Value))return;
 FHumanLimit L=*Old;Value=FMath::Clamp(Value,-180.f,180.f);
 if(Axis==0){if(Maximum)L.Forward=FMath::Max(0.f,Value);else L.Back=FMath::Max(0.f,-Value);}
 else if(Axis==1){if(Maximum)L.Out=FMath::Max(0.f,Value);else L.In=FMath::Max(0.f,-Value);}
 else {if(Maximum)L.TwistMax=FMath::Max(L.TwistMin,Value);else L.TwistMin=FMath::Min(L.TwistMax,Value);}
 Draft.SetJointLimit(Selected,L);if(LockLimits){auto& A=Angles.FindOrAdd(Selected);for(int32 I=0;I<3;++I)A[I]=FMath::Clamp(A[I],Bound(I,false),Bound(I,true));}if(Mirror)CopyOther(Mesh);
 Status=TEXT("draft changed · apply to update the climbing solver");
}
int32 FPlayerForm::Other(USkeletalMeshComponent& Mesh) const
{
 const FString N=Mesh.GetBoneName(Selected).ToString();const bool Left=N.StartsWith(TEXT("Left"));
 if(!Left&&!N.StartsWith(TEXT("Right")))return INDEX_NONE;
 const int32 I=Mesh.GetBoneIndex(*(FString(Left?TEXT("Right"):TEXT("Left"))+N.Mid(Left?4:5)));
 return Draft.JointLimits().Contains(I)?I:INDEX_NONE;
}
void FPlayerForm::CopyOther(USkeletalMeshComponent& Mesh)
{
 const int32 I=Other(Mesh);const FHumanLimit* L=Draft.JointLimits().Find(Selected);
 if(I>=0&&L){Draft.SetJointLimit(I,*L);Angles.FindOrAdd(I)=Angles.FindRef(Selected);IdleAngles.FindOrAdd(I)=IdleAngles.FindRef(Selected);}
}
void FPlayerForm::ResetJoint(USkeletalMeshComponent& Mesh)
{
 FHumanRig Base;if(Base.Initialize(Mesh.GetSkeletalMeshAsset()->GetRefSkeleton(),FHumanRig::DefaultProfilePath()))
 if(const auto* L=Base.JointLimits().Find(Selected)){Draft.SetJointLimit(Selected,*L);Angles.Remove(Selected);IdleAngles.Remove(Selected);AnchorOffsets.Remove(Selected);if(Mirror){CopyOther(Mesh);AnchorOffsets.Remove(Other(Mesh));}}
}
void FPlayerForm::Reset(){if(Clip==0){IdleAngles.Reset();Playing=false;Status=TEXT("idle adjustments cleared");return;}Angles.Reset();Clip=-1;Time=0;Playing=false;Status=TEXT("neutral pose · limits unchanged");}
bool FPlayerForm::Defaults(USkeletalMeshComponent& Mesh){const bool OK=Read(Mesh,FHumanRig::DefaultProfilePath());Status=OK?TEXT("human defaults loaded into draft"):TEXT("could not load defaults");return OK;}
FString FPlayerForm::Json(USkeletalMeshComponent& Mesh) const
{
 if(!Document.IsValid())return FString();
 // Clone so serializing a draft never mutates the source document.
 FString Text;FJsonSerializer::Serialize(Document.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));
 TSharedPtr<FJsonObject> Root;FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Text),Root);
 auto Limits=Root->GetObjectField(TEXT("limits"));
 for(const auto& Pair:Draft.BoneMap())
 {
  const int32 I=Mesh.GetBoneIndex(Pair.Value);const FHumanLimit* L=Draft.JointLimits().Find(I);if(!L)continue;
  auto J=MakeShared<FJsonObject>();J->SetNumberField(TEXT("fwd"),L->Forward);J->SetNumberField(TEXT("back"),L->Back);J->SetNumberField(TEXT("out"),L->Out);J->SetNumberField(TEXT("inn"),L->In);
  J->SetArrayField(TEXT("twist"),{MakeShared<FJsonValueNumber>(L->TwistMin),MakeShared<FJsonValueNumber>(L->TwistMax)});Limits->SetObjectField(Pair.Key.ToString(),J);
 }
 auto Anchors=MakeShared<FJsonObject>();
 for(const auto& Pair:AnchorOffsets)
  Anchors->SetArrayField(Mesh.GetBoneName(Pair.Key).ToString(),{MakeShared<FJsonValueNumber>(Pair.Value.X),MakeShared<FJsonValueNumber>(Pair.Value.Y),MakeShared<FJsonValueNumber>(Pair.Value.Z)});
 Root->SetObjectField(TEXT("anchor_offsets_cm"),Anchors);
 auto Idle=MakeShared<FJsonObject>();
 for(const auto& Pair:IdleAngles)
  Idle->SetArrayField(Mesh.GetBoneName(Pair.Key).ToString(),{MakeShared<FJsonValueNumber>(Pair.Value.X),MakeShared<FJsonValueNumber>(Pair.Value.Y),MakeShared<FJsonValueNumber>(Pair.Value.Z)});
 Root->SetObjectField(TEXT("idle_angles_deg"),Idle);
 Root->SetObjectField(TEXT("limits"),Limits);Root->SetStringField(TEXT("profile_name"),Name);Root->SetStringField(TEXT("character"),Mesh.GetSkeletalMeshAsset()->GetPathName());
 Text.Reset();FJsonSerializer::Serialize(Root.ToSharedRef(),TJsonWriterFactory<>::Create(&Text));return Text;
}
bool FPlayerForm::Save(USkeletalMeshComponent& Mesh)
{
 FString Safe;for(TCHAR C:Name)if(FChar::IsAlnum(C)||C=='-'||C=='_')Safe.AppendChar(C);else if(C==' ')Safe.AppendChar('_');if(Safe.IsEmpty())Safe=TEXT("rig");
 const FString File=Safe.Left(48)+TEXT("_")+FDateTime::Now().ToString(TEXT("%Y%m%d_%H%M%S"))+TEXT("_")+FGuid::NewGuid().ToString().Left(6)+TEXT(".json");
 const FString Text=Json(Mesh);if(!FHumanRig::ValidProfile(Text)){Status=TEXT("invalid profile · nothing saved");return false;}
 IFileManager::Get().MakeDirectory(*Folder(),true);const bool OK=FFileHelper::SaveStringToFile(Text,*(Folder()/File));Status=OK?TEXT("saved ")+File:TEXT("could not save profile");return OK;
}
TArray<FString> FPlayerForm::Versions(){TArray<FString> Out;IFileManager::Get().FindFiles(Out,*(Folder()/TEXT("*.json")),true,false);Out.Remove(TEXT("active.json"));Out.Sort();Algo::Reverse(Out);return Out;}
bool FPlayerForm::Load(USkeletalMeshComponent& Mesh,const FString& File)
{
 if(FPaths::GetCleanFilename(File)!=File)return false;
 const bool OK=Read(Mesh,Folder()/File);if(OK)Document->TryGetStringField(TEXT("profile_name"),Name);
 Status=OK?TEXT("loaded draft · apply to use on the wall"):TEXT("invalid rig profile · draft unchanged");return OK;
}
bool FPlayerForm::Apply(USkeletalMeshComponent& Mesh,FHumanRig& Live,FHumanRig& Gate,const FString& Destination)
{
 const FString Path=Destination.IsEmpty()?Folder()/TEXT("active.json"):Destination;
 const FString Text=Json(Mesh);if(!FHumanRig::ValidProfile(Text)){Status=TEXT("invalid profile · not applied");return false;}
 IFileManager::Get().MakeDirectory(*FPaths::GetPath(Path),true);
 const FString Temp=Path+TEXT(".pending");if(!FFileHelper::SaveStringToFile(Text,*Temp)){Status=TEXT("could not write profile");return false;}
 FHumanRig A,B;
 if(!A.Initialize(Mesh.GetSkeletalMeshAsset()->GetRefSkeleton(),Temp)||!B.Initialize(Mesh.GetSkeletalMeshAsset()->GetRefSkeleton(),Temp)){Status=TEXT("profile does not bind to this character");IFileManager::Get().Delete(*Temp);return false;}
 // Every applied tuning also gets a named version for later comparison.
 if(Destination.IsEmpty()&&!Save(Mesh)){IFileManager::Get().Delete(*Temp);return false;}
 if(!IFileManager::Get().Move(*Path,*Temp,true,true)){Status=TEXT("could not activate profile");return false;}
 B.ColdIterations=3;Live=MoveTemp(A);Gate=MoveTemp(B);ConfigureGround(Mesh,Live);Status=TEXT("applied · both climbing and reach checks use this profile");return true;
}
FVector FPlayerForm::AxisWorld(USkeletalMeshComponent& Mesh,int32 Axis) const
{
 const auto* F=Draft.JointFrame(Selected);if(!F)return FVector::ZeroVector;
 const FName Parent=Mesh.GetParentBone(Mesh.GetBoneName(Selected));
 const FQuat P=Parent.IsNone()?Mesh.GetComponentQuat():Mesh.GetSocketQuaternion(Parent);
 const FQuat Q=P*Draft.NeutralPose()[Selected].GetRotation();
 return Q.RotateVector(Axis==0?F->F:Axis==1?F->S:F->T*F->Side);
}
