// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "MobDebugView.generated.h"

/**
 * What the debug view draws in place of the shaded result.
 *
 * The master materials expose this as the DebugMode scalar. It is an enum rather than a number so
 * the instance editor names the views instead of asking for an index nobody can remember; the
 * scalar parameter's Enumeration points here.
 */
UENUM(BlueprintType)
enum class EMobDebugView : uint8
{
	/** Nothing. Black. */
	Off = 0,

	/** The first three layers' blend weights as red, green and blue. */
	LayerWeights = 1 UMETA(DisplayName = "Layer Weights"),

	/** Blended cavity, which is also what height blending reads. */
	Cavity = 2,

	/** Blended normal, in tangent space, as a normal map reads. */
	Normal = 3,

	/** Where the surface counts as wet, in red. */
	Wetness = 4,

	/** Blended height. Flat grey here means nothing to blend along. */
	Height = 5,

	/** Vertex colour as painted. */
	VertexColour = 6 UMETA(DisplayName = "Vertex Colour"),

	/** Where the ground has been walked through, in green. Black wherever no volume covers it. */
	Trample = 7,

	/** Where snow, dust or ash has settled, in blue. */
	Accumulation = 8,

	/**
	 * How much light the surface lets through, from the blue channel of the CRM. Foliage only:
	 * black everywhere else, where that channel is metallic instead.
	 */
	Transmission = 9,

	/**
	 * Every leaf in its own colour, from the per-leaf random in vertex colour green.
	 *
	 * The first thing to look at after baking pivots, and the only way to see whether the leaves
	 * were separated correctly. A canopy that comes out in broad bands rather than leaf-sized
	 * patches means the shells are wrong, and everything downstream of that is quietly mush.
	 */
	FoliageShells = 10 UMETA(DisplayName = "Foliage Shells"),

	/**
	 * How freely each part of a leaf swings, from vertex colour red. Black at the stem, white at
	 * the tip.
	 */
	FoliageStiffness = 11 UMETA(DisplayName = "Foliage Stiffness"),
};
