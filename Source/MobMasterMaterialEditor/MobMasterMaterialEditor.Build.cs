// Copyright (c) Jared Taylor

using UnrealBuildTool;

public class MobMasterMaterialEditor : ModuleRules
{
	public MobMasterMaterialEditor(ReadOnlyTargetRules Target) : base(Target)
	{
		PCHUsage = ModuleRules.PCHUsageMode.UseExplicitOrSharedPCHs;

		PublicDependencyModuleNames.AddRange(
			new string[]
			{
				"Core",
				"DeveloperSettings",
			}
			);

		PrivateDependencyModuleNames.AddRange(
			new string[]
			{
				"CoreUObject",
				"Engine",
				"Slate",
				"SlateCore",
				"InputCore",
				"UnrealEd",
				"ToolMenus",
				"Projects",
				"Settings",
				"PythonScriptPlugin",
				"MobMasterMaterial",
				"PropertyEditor",
				"ContentBrowser",
				"AssetRegistry",
				"ImageCore",
				"Landscape",
				"MaterialEditor",
				"Json",
			}
			);
	}
}
