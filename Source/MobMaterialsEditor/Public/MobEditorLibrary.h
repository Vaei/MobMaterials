// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "MobEditorLibrary.generated.h"

/** The level tools, reachable from Python and Blueprint as well as from the Mat menu. */
UCLASS()
class MOBMATERIALSEDITOR_API UMobEditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * Rebakes what the landscape reports underfoot, then returns how many proxies had work to do.
	 *
	 * The physical material output is baked into collision data rather than evaluated per trace, so
	 * a regenerated master changes what the material says and nothing about what a footstep hears
	 * until this runs.
	 */
	UFUNCTION(BlueprintCallable, Category="Mob|Landscape")
	static int32 RebuildLandscapePhysicalMaterial();

	/**
	 * The same, but driven to completion before returning rather than over the following frames.
	 *
	 * The bake is a render readback finished on a later tick, which a commandlet never has, so this
	 * flushes the renderer between passes. Returns how many components still read as outdated.
	 */
	UFUNCTION(BlueprintCallable, Category="Mob|Landscape")
	static int32 RebuildLandscapePhysicalMaterialSync(int32 MaxPasses = 8);

	/** How many landscape components are still holding a stale answer. Zero means the bake is current. */
	UFUNCTION(BlueprintPure, Category="Mob|Landscape")
	static int32 GetOutdatedPhysicalMaterialComponentCount();

	/**
	 * Takes the editor out of the mobile preview, and says whether it had to.
	 *
	 * The landscape refuses to bake its physical materials below SM5, and this project drops into
	 * the mobile preview on every map load, so anything that needs a bake has to leave first.
	 */
	UFUNCTION(BlueprintCallable, Category="Mob|Landscape")
	static bool LeaveMobilePreview();

	/** Puts the preview back, for whoever left it to bake. */
	UFUNCTION(BlueprintCallable, Category="Mob|Landscape")
	static void RestoreMobilePreview();
};
