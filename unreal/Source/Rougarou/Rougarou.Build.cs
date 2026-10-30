using UnrealBuildTool;

public class Rougarou : ModuleRules
{
	public Rougarou(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new[] { "Core", "CoreUObject", "Engine", "InputCore", "EnhancedInput", "Json", "ProceduralMeshComponent" });
	}
}
