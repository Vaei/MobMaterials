// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobLayerArrayLibrary.generated.h"

class UTexture2D;
class UTexture2DArray;

/**
 * Packing loose layer textures into a texture array.
 *
 * The array's slice list and the call that rebuilds it from that list are editor-only C++ with no
 * scripting exposure, so this is the one step of array authoring the generators cannot do
 * themselves.
 */
UCLASS()
class MOBMASTERMATERIALEDITOR_API UMobLayerArrayLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Creates or rebuilds a texture array from Slices, in the order given.
	 *
	 * Rebuilds in place when the asset already exists, so a material pointing at it keeps
	 * pointing at it and a repack does not orphan anything. Format and colour space are taken
	 * from the first slice, which is what makes one call work for colour, normals and masks
	 * alike.
	 *
	 * Every slice must match the first in dimensions and format. Returns null if any does not,
	 * having logged which.
	 */
	UFUNCTION(BlueprintCallable, Category="Mob")
	static UTexture2DArray* PackLayerArray(const FString& PackagePath, const TArray<UTexture2D*>& Slices);
};
