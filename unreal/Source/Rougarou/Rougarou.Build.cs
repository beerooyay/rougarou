using UnrealBuildTool;

public class Rougarou : ModuleRules
{
	public Rougarou(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Json", "ProceduralMeshComponent", "Slate", "SlateCore" });
		PublicDependencyModuleNames.AddRange(new[] { "ClothingSystemRuntimeCommon", "ChaosCloth" });
		if (Target.bBuildEditor) PrivateDependencyModuleNames.AddRange(new[] { "ClothingSystemEditor", "ClothingSystemEditorInterface", "UnrealEd" });
	}
}
