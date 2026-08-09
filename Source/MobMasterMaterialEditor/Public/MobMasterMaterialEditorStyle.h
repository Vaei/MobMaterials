// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"

class FSlateStyleSet;

/** Slate brushes for the Mob editor UI, sourced from the plugin's Resources folder. */
class MOBMASTERMATERIALEDITOR_API FMobMasterMaterialEditorStyle
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
