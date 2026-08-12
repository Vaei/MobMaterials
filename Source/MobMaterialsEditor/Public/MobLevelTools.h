// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

class AActor;
class UMaterialInstanceConstant;
class UMaterialInterface;

/**
 * Level helpers for the two placements a terrain needs constantly and neither the editor nor a
 * transform panel makes easy: putting something in the middle of the ground, and making a volume
 * cover exactly as much ground as there is.
 */
class MOBMATERIALSEDITOR_API FMobLevelTools
{
public:
	/** Moves the selection to the middle of the nearest landscape and sits it on the surface. */
	static void SnapToLandscapeCentre();
	static bool CanSnapToLandscapeCentre();

	/** Centres and scales the selected box volume so it covers the nearest landscape exactly. */
	static void FitBoxToLandscape();
	static bool CanFitBoxToLandscape();

	/**
	 * Rebakes what the landscape reports underfoot.
	 *
	 * The physical material output is baked into the collision data rather than evaluated per
	 * trace, so regenerating the master changes what the material says and nothing about what a
	 * footstep hears until this has run.
	 */
	static void RebuildPhysicalMaterial();
	static bool CanRebuildPhysicalMaterial();

	/**
	 * Assigns the textures selected in the Content Browser to the landscape's material instance,
	 * matched by name.
	 *
	 * A texture goes to the layer whose name it carries and the slot its suffix names, so
	 * T_DryGrass_BaseColor lands on DryGrass_BC. All or nothing: anything that cannot be placed,
	 * or two textures wanting one slot, aborts before a single parameter is written.
	 */
	static void AssignSelectedTextures();
	static bool CanAssignSelectedTextures();

	/**
	 * Which layer slot a texture's name asks for - `_BC`, `_NRM`, `_HRC` - or empty for none.
	 *
	 * Matched on what the name ends in, so the layer it belongs to is a separate question and the
	 * layer editor, which already knows the layer from its tab, only has to ask this one.
	 */
	static FString SlotSuffixFor(const FString& TextureName);

	/**
	 * The material instance the open level's landscape renders with, and the master behind it.
	 *
	 * What the menu offers to open: the terrain in front of you is the thing being tuned, and
	 * finding its instance means selecting a proxy and reading a property off it.
	 */
	static UMaterialInstanceConstant* GetLandscapeInstance();
	static UMaterialInterface* GetLandscapeMaster();

	/** Why the entry is greyed out, or empty when it is not. Shown as the tooltip's last line. */
	static FText SnapReason();
	static FText FitReason();
	static FText RebuildReason();
	static FText AssignReason();
};
