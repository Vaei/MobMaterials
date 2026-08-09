// Copyright (c) Jared Taylor. All Rights Reserved

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
			}
			);
	}
}
