// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

class AActor;

/**
 * Level helpers for the two placements a terrain needs constantly and neither the editor nor a
 * transform panel makes easy: putting something in the middle of the ground, and making a volume
 * cover exactly as much ground as there is.
 */
class MOBMASTERMATERIALEDITOR_API FMobLevelTools
{
public:
	/** Moves the selection to the middle of the nearest landscape and sits it on the surface. */
	static void SnapToLandscapeCentre();
	static bool CanSnapToLandscapeCentre();

	/** Centres and scales the selected box volume so it covers the nearest landscape exactly. */
	static void FitBoxToLandscape();
	static bool CanFitBoxToLandscape();

	/** Why the entry is greyed out, or empty when it is not. Shown as the tooltip's last line. */
	static FText SnapReason();
	static FText FitReason();
};
