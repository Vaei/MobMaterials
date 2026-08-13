// Copyright (c) Jared Taylor

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
class MOBMATERIALS_API UMobMaterialRecipe : public UDataAsset
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
	 * It also reads the blue channel of the CRM as transmission rather than metallic, since a plant
	 * is never metal and a thickness there is what separates a tip that glows from a stem that does
	 * not. Off until an instance ticks Transmission Map: art repacked from an ORM has metallic zero
	 * in that channel, which would put no light through the plant at all.
	 *
	 * This cannot be a switch on the standard master. Shading model, two-sidedness and blend mode
	 * are material properties rather than parameters, so foliage has to be a material of its own -
	 * which is the right shape anyway, since it wants different defaults the whole way down.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bFoliage = false;

	/**
	 * Adds accumulation: a covering that settles by which way a surface faces - snow on a ledge,
	 * dust on a shelf, ash on a sill - biased into crevices and broken up by noise.
	 *
	 * No extra samples. It reuses the cavity the layers already blended and the surface normal the
	 * material already has.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bAccumulation = true;

	/**
	 * Adds rain ripples on standing water: two scrolling normals gated by the puddle mask, so dry
	 * ground stays still.
	 *
	 * Two extra samples, and only where an instance turns them on. This is the difference between
	 * wetness that reads as weather and wetness that reads as a darker texture.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Surface",
		meta=(EditCondition="Kind == EMobMaterialKind::Surface", EditConditionHides))
	bool bRainRipples = true;

	/**
	 * Adds debug views to the landscape master: layer weights, cavity, wetness and height.
	 *
	 * Layer weights on terrain are the case that matters most - a wrong weight there looks like a
	 * texture choice rather than a mistake, and nothing downstream can reconstruct it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape",
		meta=(EditCondition="Kind == EMobMaterialKind::Landscape", EditConditionHides))
	bool bLandscapeDebugViews = true;

	/**
	 * Adds accumulation to the landscape master: snow, dust or ash settling by which way the ground
	 * faces, biased into the crevices and broken up by noise.
	 *
	 * No extra samples - it reuses the blended cavity and the surface normal. A paint layer is
	 * still the better answer where snow needs to drift in a particular place; this is for a fall
	 * that covers everything at once and can be driven from nothing but a number.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape",
		meta=(EditCondition="Kind == EMobMaterialKind::Landscape", EditConditionHides))
	bool bLandscapeAccumulation = true;

	/**
	 * Adds trample: a world-space record of what has been walked through, darkening and denting the
	 * ground where it has and taking the accumulation back off it.
	 *
	 * Three extra samples of one render target, sharing the clamp sampler. What writes the target is
	 * AMobTrampleVolume plus UMobTrampleSubsystem::AddTrample, so nothing happens until a volume is
	 * placed and something calls it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weather")
	bool bTrample = true;

	/**
	 * Parameter collection carrying the trample volume's position and size.
	 *
	 * The volume writes it at runtime, which is what lets one render target asset be read by a
	 * landscape material without anything in the chain being a dynamic material instance.
	 *
	 * Every master that should take prints from one volume has to name the same collection: a
	 * volume writes one, so terrain and the props standing on it reading different ones means only
	 * one of them gets prints. Leave it empty and generating uses the plugin's, then fills this in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weather",
		meta=(EditCondition="bTrample", EditConditionHides))
	TSoftObjectPtr<UMaterialParameterCollection> TrampleCollection;

	/**
	 * Samples the paint layers out of three texture arrays instead of three textures per layer.
	 *
	 * Texture count stops growing with layer count: eight layers is three texture objects rather
	 * than twenty-four, and the master carries one slice index per layer instead of three texture
	 * parameters. Worth it past about four layers, and the only way to go much beyond eight.
	 *
	 * The cost is that a slice cannot differ from its neighbours: every layer's textures must
	 * share one resolution and one format per channel, and swapping one layer's art means a
	 * repack rather than a parameter change. Pack Layer Arrays from the Mat menu does the packing
	 * and says which layer failed to match.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape",
		meta=(EditCondition="Kind == EMobMaterialKind::Landscape", EditConditionHides))
	bool bTextureArrayLayers = false;

	/**
	 * Folder searched for each layer's textures when packing.
	 *
	 * A layer matches a texture whose name contains the layer name and ends with a known channel
	 * suffix - _BC, _BaseColor, _basecolor for colour, _NRM, _Normal for normals, _HRC,
	 * _HeigRougAO for the mask pack. Searched recursively, so per-layer subfolders are fine.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Landscape", meta=(ContentDir,
		EditCondition="Kind == EMobMaterialKind::Landscape && bTextureArrayLayers", EditConditionHides))
	FDirectoryPath LayerTextureRoot;

	/**
	 * Parameter collection carrying the weather both masters read: wetness, and how much snow, dust
	 * or ash has fallen.
	 *
	 * Point every recipe at one collection and the whole world goes wet and gets covered together,
	 * which is the only way terrain and the props standing on it agree. Leave it empty and
	 * generating creates one beside the master, then fills this in.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Weather")
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
