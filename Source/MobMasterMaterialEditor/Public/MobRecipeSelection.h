// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MobRecipeSelection.generated.h"

class UMobMaterialRecipe;

/**
 * Holds the generate window's current recipe.
 *
 * A UObject only so a details view can draw the asset picker; it is never saved. The recipe it
 * points at is edited directly, so there is no copy to load or write back.
 */
UCLASS(Transient)
class UMobRecipeSelection : public UObject
{
	GENERATED_BODY()

public:
	static UMobRecipeSelection* Get();

	/** The recipe to author. Its own properties are edited below the picker. */
	UPROPERTY(EditAnywhere, Category="Recipe")
	TObjectPtr<UMobMaterialRecipe> Recipe;
};
