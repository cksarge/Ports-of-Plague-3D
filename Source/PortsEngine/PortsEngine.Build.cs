using UnrealBuildTool;

// The game's data and rules. No rendering, input or UI code belongs here,
// mirroring src/engine/ in the web version.
public class PortsEngine : ModuleRules
{
	public PortsEngine(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core" });
	}
}
