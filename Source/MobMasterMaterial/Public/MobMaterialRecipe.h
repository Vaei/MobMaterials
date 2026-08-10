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
