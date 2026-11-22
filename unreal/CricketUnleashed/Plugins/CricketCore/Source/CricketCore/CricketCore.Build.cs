// Cricket Core plugin: our engine-independent C++ (rules, DRS, ball physics,
// scorecards, recording) compiled inside Unreal.
// The core sources are COPIED into Private/Core and Public/cricket by
// tools/unreal/sync_core.ps1 (the repository's core/ folder stays the master).
using UnrealBuildTool;
using System.IO;

public class CricketCore : ModuleRules
{
	public CricketCore(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		// Same settings our Linux build checks on every commit
		// (cricket_unreal_compat_check): no exceptions, no RTTI.
		bEnableExceptions = false;
		bUseRTTI = false;
		// The core .cpp files each have private helpers in anonymous
		// namespaces; merging them into one "unity" file could clash.
		bUseUnity = false;
		// Core headers include each other as "cricket/....h".
		PublicIncludePaths.Add(Path.Combine(ModuleDirectory, "Public"));
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "Json" });
		// MSVC: /fp:precise is the default and does not fuse multiply-adds, which
		// the deterministic ball physics needs (see docs/BALL_PHYSICS.md section 4).
	}
}
