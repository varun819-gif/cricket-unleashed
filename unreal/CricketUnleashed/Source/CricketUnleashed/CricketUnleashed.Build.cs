using UnrealBuildTool;

public class CricketUnleashed : ModuleRules
{
	public CricketUnleashed(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "CricketCore" });
	}
}
