// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MobMaterialsEditorUserSettings.generated.h"

/**
 * Per-developer editor preferences. Stored in EditorPerProjectUserSettings, so these are not
 * checked in and one dev's choice never lands on anyone else.
 */
UCLASS(Config=EditorPerProjectUserSettings, meta=(DisplayName="Mob Materials Editor"))
class MOBMATERIALSEDITOR_API UMobMaterialsEditorUserSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	/** Show the Mat dropdown in the level editor toolbar. */
	UPROPERTY(EditAnywhere, Config, Category="Toolbar")
	bool bShowToolbarMenu = true;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
