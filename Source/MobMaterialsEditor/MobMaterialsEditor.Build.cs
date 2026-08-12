// Copyright (c) Jared Taylor

using UnrealBuildTool;

public class MobMaterialsEditor : ModuleRules
{
	public MobMaterialsEditor(ReadOnlyTargetRules Target) : base(Target)
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
				"MobMaterials",
				"PropertyEditor",
				"ContentBrowser",
				"AssetRegistry",
				"ImageCore",
				"Landscape",
				"RHI",
				"RenderCore",
				"MaterialEditor",
				"Json",
				"AppFramework",
			}
			);
	}
}
