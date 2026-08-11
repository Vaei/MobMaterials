// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;

/** Slate brushes for the MobMaterials editor UI, sourced from the plugin's Resources folder. */
class MOBMATERIALSEDITOR_API FMobMaterialsEditorStyle
{
public:
	static void Register();
	static void Unregister();

	static FName GetStyleSetName();

	/** The plugin icon, sized for a toolbar entry. */
	static FName GetMenuIconName() { return TEXT("Mob.MenuIcon"); }

private:
	static TSharedPtr<FSlateStyleSet> StyleSet;
};
