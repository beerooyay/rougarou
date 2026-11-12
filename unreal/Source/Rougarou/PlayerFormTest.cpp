#include "PlayerForm.h"
#include "LocomotionAnim.h"
#include "Engine/SkeletalMesh.h"
#include "Components/SkeletalMeshComponent.h"
#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/FileHelper.h"
#include "HAL/FileManager.h"
#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlayerFormTest,"Rougarou.PlayerForm",EAutomationTestFlags::EditorContext|EAutomationTestFlags::EngineFilter)
bool FPlayerFormTest::RunTest(const FString& Parameters)
{
 USkeletalMesh* Asset=ULocomotionAnim::LoadPlayerMesh();if(!TestNotNull(TEXT("player mesh"),Asset))return false;
 USkeletalMeshComponent* Mesh=NewObject<USkeletalMeshComponent>();Mesh->SetSkeletalMesh(Asset);
 FString Baseline;FFileHelper::LoadFileToString(Baseline,*FHumanRig::DefaultProfilePath());
 FPlayerForm F;F.Begin(*Mesh);if(!TestTrue(TEXT("studio draft binds"),F.Open))return false;
 F.Defaults(*Mesh);F.Selected=Mesh->GetBoneIndex(TEXT("RightArm"));const int32 Left=Mesh->GetBoneIndex(TEXT("LeftArm"));
 TestEqual(TEXT("only anatomical joints are editable"),F.Draft.JointLimits().Num(),19);
 F.SetBound(*Mesh,0,true,65);TestEqual(TEXT("bend max edits actual solver bound"),F.Draft.JointLimits()[F.Selected].Forward,65.f);
 TestEqual(TEXT("mirroring copies anatomical limit"),F.Draft.JointLimits()[Left].Forward,65.f);
 F.Set(*Mesh,0,120);TestEqual(TEXT("locked preview stops at bound"),F.Get(0),65.f);
 const FHumanFrame* Frame=F.Draft.JointFrame(F.Selected);
 const FQuat Q=F.Draft.NeutralPose()[F.Selected].GetRotation().Inverse()*F.Draft.PreviewJoint(F.Selected,FVector(120,70,140),true);
 TestTrue(TEXT("studio uses the live solver's clamp"),FHumanRig::Violation(Q,*Frame,F.Draft.JointLimits()[F.Selected])<.02f);
 F.Mirror=false;F.SetBound(*Mesh,0,true,50);TestEqual(TEXT("mirror off preserves other side"),F.Draft.JointLimits()[Left].Forward,65.f);
 const FVector Offset(1,2,3);
 const FVector Reference=Asset->GetRefSkeleton().GetRefBonePose()[F.Selected].GetTranslation();
 F.MoveAnchor(*Mesh,Offset);
 F.Tick(*Mesh,.01f);
 TestTrue(TEXT("anchor preview moves smoothly toward the target"),FMath::IsNearlyEqual((F.Frozen[F.Selected].GetTranslation()-Reference).Size(),Offset.Size()*.22));
 const FString Text=F.Json(*Mesh);TestTrue(TEXT("export is a directly usable solver profile"),FHumanRig::ValidProfile(Text));
 TestFalse(TEXT("incomplete profile cannot erase joint constraints"),FHumanRig::ValidProfile(TEXT("{\"limits\":{}}")));
 F.Name=TEXT("automation_rig_")+FGuid::NewGuid().ToString();const auto Old=FPlayerForm::Versions();
 TestTrue(TEXT("save profile version"),F.Save(*Mesh));FString Saved;for(const auto& File:FPlayerForm::Versions())if(!Old.Contains(File)){Saved=File;break;}
 F.Defaults(*Mesh);TestTrue(TEXT("reload saved profile"),F.Load(*Mesh,Saved));TestEqual(TEXT("changed limit survives reload"),F.Draft.JointLimits()[F.Selected].Forward,50.f);
 const FString Active=FPaths::ProjectSavedDir()/TEXT("Automation")/(FGuid::NewGuid().ToString()+TEXT(".json"));
 FHumanRig Live,Gate;TestTrue(TEXT("apply profile"),F.Apply(*Mesh,Live,Gate,Active));
 TestTrue(TEXT("saved anchor survives loading"),F.AnchorOffsets.FindRef(F.Selected).Equals(Offset));
 TestTrue(TEXT("live IK uses the new anchor"),Live.NeutralPose()[F.Selected].GetTranslation().Equals(Reference+Offset));
 TestTrue(TEXT("reach gate uses the new anchor"),Gate.NeutralPose()[F.Selected].GetTranslation().Equals(Reference+Offset));
 TestEqual(TEXT("live IK receives changed limit"),Live.JointLimits()[F.Selected].Forward,50.f);
 TestEqual(TEXT("reach gate receives the same limit"),Gate.JointLimits()[F.Selected].Forward,50.f);
 FHumanRig Restarted;TestTrue(TEXT("applied profile reloads after restart"),Restarted.Initialize(Asset->GetRefSkeleton(),Active));
 TestEqual(TEXT("persisted constraint equals runtime constraint"),Restarted.JointLimits()[F.Selected].Forward,50.f);
 FString After;FFileHelper::LoadFileToString(After,*FHumanRig::DefaultProfilePath());TestEqual(TEXT("factory profile remains intact"),After,Baseline);
 TestFalse(TEXT("path traversal refused"),F.Load(*Mesh,TEXT("../outside.json")));
 const FString Invalid=TEXT("automation_invalid_")+FGuid::NewGuid().ToString()+TEXT(".json");FFileHelper::SaveStringToFile(TEXT("{}"),*(FPlayerForm::Folder()/Invalid));
 TestFalse(TEXT("invalid load is rejected"),F.Load(*Mesh,Invalid));TestEqual(TEXT("failed load preserves draft"),F.Draft.JointLimits()[F.Selected].Forward,50.f);
 IFileManager::Get().Delete(*Active);IFileManager::Get().Delete(*(FPlayerForm::Folder()/Invalid));if(!Saved.IsEmpty())IFileManager::Get().Delete(*(FPlayerForm::Folder()/Saved));
 return true;
}
#endif
