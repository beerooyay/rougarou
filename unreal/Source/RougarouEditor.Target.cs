using UnrealBuildTool;

public class RougarouEditorTarget : TargetRules
{
	public RougarouEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("Rougarou");
	}
}
