// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "MobSimplifyWindow.generated.h"

class UMaterialInstanceConstant;

/**
 * What Simplify Material To Layer is set to. Config, so it opens where it was left.
 *
 * A landscape master is built to have everything on at once, which is the wrong thing to look at
 * while a single layer's art is being authored: slope rock, moss, wetness, the tiling break and the
 * other layers all move at the same time, and none of them can be told apart. This turns the
 * material down to one layer so what is on screen is that layer's textures and nothing else.
 */
UCLASS(Config=EditorPerProjectUserSettings)
class MOBMASTERMATERIALEDITOR_API UMobSimplifyOptions : public UObject
{
	GENERATED_BODY()

public:
	/** The instance to turn down. Filled from the Content Browser, or from the landscape's material. */
	UPROPERTY(EditAnywhere, Category="Source")
	TObjectPtr<UMaterialInstanceConstant> MaterialInstance = nullptr;

	/** The layer to look at. Every other layer is turned off or made to match it. */
	UPROPERTY(EditAnywhere, Category="Source")
	FName Layer = NAME_None;

	/**
	 * Point every other layer's textures at this one's, so the whole terrain shows it.
	 *
	 * Without this you only see the layer where it is painted, and unpainted ground still shows the
	 * base layer - which is exactly the confusion this is meant to remove when only one layer's art
	 * exists yet.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Simplify")
	bool bShowEverywhere = true;

	/** Turn off the tiling break on every layer. Two of the three tiers change what the art looks like. */
	UPROPERTY(EditAnywhere, Config, Category="Simplify")
	bool bDisableTilingBreak = true;

	/** Zero slope rock, moss and wetness, which otherwise cover the layer being looked at. */
	UPROPERTY(EditAnywhere, Config, Category="Simplify")
	bool bDisableOverlays = true;

	/** Neutralise hue, saturation, value, contrast and tint, so the texture reads as authored. */
	UPROPERTY(EditAnywhere, Config, Category="Simplify")
	bool bNeutraliseGrade = false;

	/**
	 * What each instance looked like before it was simplified, keyed by asset path.
	 *
	 * Restoring puts back what was there rather than what the master defaults to, because a layer
	 * that was already tuned should not lose that tuning to a debugging aid.
	 */
	UPROPERTY(Config)
	TMap<FString, FString> Snapshots;
};

/** Simplify Material To Layer: a details view over the options, Simplify, and Restore. */
class FMobSimplifyWindow
{
public:
	static void Open();

	/** Whether the given instance has something to put back. */
	static bool HasSnapshot(const UMaterialInstanceConstant* Instance);

private:
	static bool Simplify(UMobSimplifyOptions& Options, FText& OutError);
	static bool Restore(UMobSimplifyOptions& Options, FText& OutError);

	/** The layer names an instance carries, taken from its `<Layer>_UVScale` parameters. */
	static TArray<FName> LayerNames(const UMaterialInstanceConstant* Instance);

	/** One line saying what Simplify would do, shown under the details view. */
	static FText Summary(const UMobSimplifyOptions& Options);
};
