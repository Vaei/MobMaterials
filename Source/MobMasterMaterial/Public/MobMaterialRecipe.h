// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Materials/MaterialParameterCollection.h"
#include "MobMaterialRecipe.generated.h"

UENUM(BlueprintType)
enum class EMobMaterialKind : uint8
{
	/** Terrain. Paint layers, tiling break, slope rock, moss. */
	Landscape,

	/** Props, environment pieces and buildings. Three layers, triplanar, vertex paint. */
	Surface,
};

/** One landscape paint layer, in blend order. */
USTRUCT(BlueprintType)
struct FMobLandscapeLayer
{
	GENERATED_BODY()

	/** Layer name. This is what the landscape editor paints and what the layer info asset is named after. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer")
	FName Name = NAME_None;

	/**
	 * Physical surface this layer walks like, as named in the project's PhysicalSurfaces list.
	 * Only used when Build Project Outputs is on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer")
	FName PhysicalSurface = TEXT("SurfaceType1");

	/** Physical material asset name to create or reuse. Only used when Build Project Outputs is on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Layer")
	FName PhysicalMaterial = NAME_None;
};

/**
 * Everything needed to author one master material, and to author it again the same way later.
 *
 * A recipe is an asset rather than a project setting so a project can carry as many masters as it
 * needs - one landscape per biome, a surface master per art style - each regenerated on its own.
 *
 * Editor-only: the generators read it when they build the material, and the built assets carry the
 * result. Nothing here is read at runtime.
 */
UCLASS(BlueprintType)
class MOBMASTERMATERIAL_API UMobMaterialRecipe : public UDataAsset
{
	GENERATED_BODY()

public:
	/** Which master to author. The rest of this asset is read according to it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe")
	EMobMaterialKind Kind = EMobMaterialKind::Surface;

	/** Content path the master and its instances are written to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe", meta=(ContentDir))
	FDirectoryPath OutputPath;

	/** Base name for the authored assets: "MobLandscape" gives M_MobLandscape and MI_MobLandscape. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Recipe")
	FString AssetName = TEXT("MobSurface");

	/**
	 * Paint layers, in blend order. The first is the base: it holds whatever weight the painted
	 * layers leave behind, so untouched ground has something to blend against.
	 *
	 * Layer count is built into the material graph, so changing this list needs a regenerate.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape",
		meta=(EditCondition="Kind == EMobMaterialKind::Landscape", EditConditionHides))
	TArray<FMobLandscapeLayer> Layers;

	/**
	 * Adds the runtime virtual texture, physical material and grass outputs to the landscape master.
	 *
	 * These reference assets a game owns rather than the plugin, so point Output Path somewhere in
	 * /Game before turning this on, or the material ends up referencing packages that do not exist
	 * anywhere else.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape|Project Integration",
		meta=(EditCondition="Kind == EMobMaterialKind::Landscape", EditConditionHides))
	bool bBuildProjectOutputs = false;

	/** Where the two runtime virtual textures are created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape|Project Integration", meta=(ContentDir,
		EditCondition="Kind == EMobMaterialKind::Landscape && bBuildProjectOutputs", EditConditionHides))
	FDirectoryPath RuntimeVirtualTexturePath;

	/** Where footstep physical materials are created or found. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape|Project Integration", meta=(ContentDir,
		EditCondition="Kind == EMobMaterialKind::Landscape && bBuildProjectOutputs", EditConditionHides))
	FDirectoryPath PhysicalMaterialPath;

	/** Where the landscape's layer info assets live. These are created from Landscape mode, not here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape|Project Integration", meta=(ContentDir,
		EditCondition="Kind == EMobMaterialKind::Landscape && bBuildProjectOutputs", EditConditionHides))
	FDirectoryPath LayerInfoPath;

	/** Where landscape grass type assets are created. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape|Project Integration", meta=(ContentDir,
		EditCondition="Kind == EMobMaterialKind::Landscape && bBuildProjectOutputs", EditConditionHides))
	FDirectoryPath GrassTypePath;

	/**
	 * Adds a detail normal: one fine tiling normal laid over the surface and faded out with
	 * distance, which is most of what separates a base layer from a finished one up close.
	 *
	 * One extra texture sample for the whole material, and only when an instance turns it on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bDetailMaps = true;

	/**
	 * Adds a distance tiling break: a second, incommensurate tiling of the same texture crossfaded
	 * in with distance, so a large tiled surface stops reading as a grid from across the room.
	 *
	 * Three extra samples per layer that uses it, and only on the mesh-UV path - triplanar already
	 * samples three ways and breaking that too would be nine taps to solve a repeat the projection
	 * has largely hidden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bDistanceTilingBreak = true;

	/**
	 * Adds parallax: shifting the UV along the view direction so a flat surface reads as though it
	 * has depth. Two modes per layer - a single-step offset, and a raymarched occlusion variant.
	 *
	 * The cheap mode costs one extra height tap. Occlusion costs that plus a raymarch of up to
	 * Steps samples per pixel, and is the only thing here that spends a sampler slot, since a loop
	 * cannot be expressed as graph taps. Neither changes the silhouette.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bParallax = false;

	/**
	 * Reads per-instance tint, roughness and wetness from custom primitive data.
	 *
	 * The cheapest per-instance variation there is: no new material instance, no new shader
	 * permutation, and it can be driven from Blueprint at runtime. Indices are fixed - 0,1,2 tint,
	 * 3 roughness offset, 4 wetness offset - because they are a contract with whatever sets them.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bPrimitiveData = true;

	/**
	 * Adds debug views: layer weights, cavity, normal, wetness, height and vertex colour, sent to
	 * emissive with the base colour blacked out so the value is seen rather than the value times
	 * whatever the light was doing.
	 *
	 * A blend you cannot see is a blend you cannot fix. Costs nothing until an instance asks.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bDebugViews = true;

	/**
	 * Authors a foliage master instead of a standard one: masked, two-sided, two-sided-foliage
	 * shading, subsurface colour, and wind on world position offset.
	 *
	 * This cannot be a switch on the standard master. Shading model, two-sidedness and blend mode
	 * are material properties rather than parameters, so foliage has to be a material of its own -
	 * which is the right shape anyway, since it wants different defaults the whole way down.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bFoliage = false;

	/**
	 * Parameter collection carrying the global wetness the surface master reads.
	 *
	 * Point several recipes at one collection and their materials go wet together. Leave it empty
	 * and generating creates one beside the master, then fills this in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	TSoftObjectPtr<UMaterialParameterCollection> WeatherCollection;

	/** Whether there is enough here to author anything. */
	UFUNCTION(BlueprintCallable, Category="Recipe")
	bool IsUsable() const
	{
		return !AssetName.IsEmpty()
			&& !OutputPath.Path.IsEmpty()
			&& (Kind != EMobMaterialKind::Landscape || Layers.Num() > 0);
	}
};
