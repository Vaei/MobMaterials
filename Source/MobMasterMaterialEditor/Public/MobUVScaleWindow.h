// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MobUVScaleWindow.generated.h"

class ALandscapeProxy;
class UMaterialInstanceConstant;

/**
 * What Fit UV Scale To Landscape is set to. Config, so it opens where it was left.
 *
 * A layer's UVScale is tiles per landscape UV unit, and one UV unit is one quad, so what a tile
 * measures in the world depends on the landscape's own scale. A landscape that was resampled or
 * resized no longer has the 100 unit quads the defaults assume, and every layer on it then tiles
 * at the wrong size at once - which reads as a blurred or moire mess rather than as a wrong number.
 */
UCLASS(Config=EditorPerProjectUserSettings)
class MOBMASTERMATERIALEDITOR_API UMobUVScaleOptions : public UObject
{
	GENERATED_BODY()

public:
	/** The landscape whose quad size sets the answer. Filled from the selection when the window opens. */
	UPROPERTY(EditAnywhere, Category="Source")
	TObjectPtr<ALandscapeProxy> Landscape = nullptr;

	/** The instance to write. Filled from the Content Browser, or from the landscape's own material. */
	UPROPERTY(EditAnywhere, Category="Source")
	TObjectPtr<UMaterialInstanceConstant> MaterialInstance = nullptr;

	/** How far one repeat of a layer's texture should span, in metres. */
	UPROPERTY(EditAnywhere, Config, Category="Tiling", meta=(UIMin="0.25", UIMax="16", ClampMin="0.01"))
	float TileSize = 4.f;

	/**
	 * Layers that want a different tile size from the one above, in metres.
	 *
	 * Gravel wants a tighter repeat than a grass field does. A layer named here is the only thing
	 * that departs from Tile Size; everything else follows it.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Tiling")
	TMap<FName, float> PerLayerTileSize;
};

/** Fit UV Scale To Landscape: a details view over the options, and Apply. */
class FMobUVScaleWindow
{
public:
	static void Open();

private:
	/** Writes every layer's UVScale on the instance. Returns how many it wrote. */
	static int32 Apply(const UMobUVScaleOptions& Options, FText& OutError);

	/** World size of one landscape quad, in centimetres, or 0 if there is no landscape. */
	static float QuadSizeCm(const ALandscapeProxy* Landscape);

	/** The layer names an instance carries, taken from its `<Layer>_UVScale` parameters. */
	static TArray<FName> LayerNames(const UMaterialInstanceConstant* Instance);

	/** Selected landscape, or the only one in the level. */
	static ALandscapeProxy* FindLandscape();

	/** Content Browser selection, or whatever the landscape is rendering with. */
	static UMaterialInstanceConstant* FindInstance(const ALandscapeProxy* Landscape);

	/** One line saying what Apply would do, shown under the details view. */
	static FText Summary(const UMobUVScaleOptions& Options);
};
