using UnrealBuildTool;

public class CricketUnleashedTarget : TargetRules
{
	public CricketUnleashedTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.Add("CricketUnleashed");
	}
}
