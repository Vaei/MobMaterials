// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobFoliagePivots.generated.h"

class UStaticMesh;

/** Where a leaf is taken to be anchored. */
UENUM(BlueprintType)
enum class EMobPivotSource : uint8
{
	/**
	 * Where the leaf meets the rest of the mesh.
	 *
	 * The right answer for anything growing off a twig, and it works on a welded mesh precisely
	 * because the join is a shared vertex. Falls back to the lowest point when nothing else is
	 * within reach, which is what a leaf modelled free-floating gets.
	 */
	NearestOtherGeometry,

	/**
	 * The bottom of the leaf.
	 *
	 * For grass and anything else rooted in the ground, where every blade starts at the same height
	 * and the nearest other geometry would be the blade next to it.
	 */
	LowestPoint,
};

/** How a mesh's leaves get their pivots. */
USTRUCT(BlueprintType)
struct FMobFoliagePivotSettings
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivots")
	EMobPivotSource PivotSource = EMobPivotSource::NearestOtherGeometry;

	/**
	 * Only bakes sections whose material shades as two-sided foliage.
	 *
	 * That shading model is the one thing that separates leaves from bark without anybody naming
	 * anything, since the foliage master sets it and no other master does. Off bakes every section,
	 * which will hand a trunk a pivot it has no use for.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivots")
	bool bFoliageSectionsOnly = true;

	/**
	 * How far a leaf will look for the twig it grows from.
	 *
	 * Past this it is treated as free-floating and takes its lowest point instead. Too generous and
	 * a leaf anchors to whatever happened to be nearby.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivots", meta=(ClampMin="0.1", ForceUnits="cm"))
	float AttachSearchRadius = 12.f;

	/**
	 * How sharply the swing is weighted towards the tip.
	 *
	 * 1 is a straight ramp from the pivot. Above that the base holds stiffer and the movement piles
	 * into the far end of the leaf.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivots", meta=(ClampMin="0.1", ClampMax="8"))
	float StiffnessPower = 1.f;

	/**
	 * First of the two UV channels the pivot is written to.
	 *
	 * It takes this one and the next: XY in the first, Z and the stiffness in the second. Channel 0
	 * is the art's own coordinates and must not be used.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivots", meta=(ClampMin="1", ClampMax="6"))
	int32 PivotUVChannel = 1;

	/**
	 * Also writes the stiffness to vertex colour red and a per-leaf random to green.
	 *
	 * Red is what the foliage master already reads as the rustle weight, so this is what makes a
	 * baked mesh work without touching the material. Green is what the shell debug view draws, and
	 * it doubles as a per-leaf phase so no two leaves swing together.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivots")
	bool bWriteVertexColour = true;

	/**
	 * Counts the leaves and writes nothing.
	 *
	 * Worth doing first on any mesh you have not baked before. A wrong leaf count is the one failure
	 * that does not look like a failure afterwards, and a bake overwrites vertex colour.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Pivots")
	bool bDryRun = false;
};

/** What one bake did, for the caller to report. */
USTRUCT(BlueprintType)
struct FMobFoliagePivotResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	bool bSucceeded = false;

	/** How many leaves it found. One per UV shell. */
	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	int32 Shells = 0;

	/** How many of those had to fall back to their lowest point for want of anything to attach to. */
	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	int32 FreeFloating = 0;

	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	int32 SectionsBaked = 0;

	/** Whether the stiffness and per-leaf random reached vertex colour, read back after committing. */
	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	bool bColoursWritten = false;

	/** What vertex colour red came back as, over the baked sections. */
	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	float RedMin = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	float RedMax = 0.f;

	UPROPERTY(BlueprintReadOnly, Category="Pivots")
	FString Message;
};

/**
 * Gives every leaf on a mesh its own pivot, so foliage can swing about where it grows from rather
 * than about the whole plant's origin.
 *
 * Leaves are found as UV shells, not as separate pieces of geometry. A tree exported from anywhere
 * has its leaves welded to the twigs they hang off, so there is only ever one connected piece to
 * find; the leaf is still its own island in the texture, and that is what this reads.
 */
UCLASS()
class MOBMATERIALSEDITOR_API UMobFoliagePivots : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Bakes one mesh. Saving is the caller's business. */
	UFUNCTION(BlueprintCallable, Category="Mob|Foliage")
	static FMobFoliagePivotResult BakeFoliagePivots(UStaticMesh* Mesh,
		const FMobFoliagePivotSettings& Settings);

	/** Bakes whatever is selected in the content browser, and saves it. */
	static void BakeSelection();

	/** Whether anything selected is a static mesh. */
	static bool CanBakeSelection();
};
