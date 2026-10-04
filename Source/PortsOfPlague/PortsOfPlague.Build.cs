using UnrealBuildTool;

// The 3D map, camera, screens and sound.
public class PortsOfPlague : ModuleRules
{
	public PortsOfPlague(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;
		PublicDependencyModuleNames.AddRange(new string[] { "Core", "CoreUObject", "Engine", "InputCore", "PortsEngine" });
		PrivateDependencyModuleNames.AddRange(new string[] { "Slate", "SlateCore", "ApplicationCore", "Json", "ProceduralMeshComponent", "GeometryCore" });
	}
}
