using UnrealBuildTool;

public class PortsOfPlagueTarget : TargetRules
{
	public PortsOfPlagueTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "PortsEngine", "PortsOfPlague" });
	}
}
