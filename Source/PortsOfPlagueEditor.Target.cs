using UnrealBuildTool;

public class PortsOfPlagueEditorTarget : TargetRules
{
	public PortsOfPlagueEditorTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Editor;
		DefaultBuildSettings = BuildSettingsVersion.Latest;
		IncludeOrderVersion = EngineIncludeOrderVersion.Latest;
		ExtraModuleNames.AddRange(new string[] { "PortsEngine", "PortsOfPlague" });
	}
}
