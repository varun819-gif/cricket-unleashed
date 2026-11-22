using UnrealBuildTool;

public class CricketUnleashedEditorTarget : TargetRules
{
	public CricketUnleashedEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("CricketUnleashed");
	}
}
