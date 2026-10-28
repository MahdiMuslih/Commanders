using UnrealBuildTool;

public class Commanders : ModuleRules
{
	public Commanders(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicIncludePaths.Add(ModuleDirectory);

		PublicDependencyModuleNames.AddRange(new string[]
		{
			"Core",
			"CoreUObject",
			"Engine",
			"InputCore",
			"Json",
			"EngineSettings",
			"NetCore"
		});

		PrivateDependencyModuleNames.AddRange(new string[]
		{
			"Slate",
			"SlateCore",
			"RenderCore",
			"AudioMixer",
			"AudioExtensions"
		});
	}
}
