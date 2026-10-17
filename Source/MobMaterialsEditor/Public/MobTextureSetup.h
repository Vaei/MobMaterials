// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"

/**
 * Sets the import settings a texture needs for what its name says it is, on the Content Browser
 * selection.
 *
 * A texture is classified by its suffix: colour (_BaseColor, _BC, _Albedo, _Diffuse, _Color,
 * _Emissive) is sRGB with its alpha kept, normal (_Normal, _NRM, _N) is a normal map, and a mask
 * pack (_HRC, _CRM, _CRT, _ORM, _MRA, _RMA, _ARM, _Masks, _Mask) is linear masks. Anything else is
 * left untouched and named in the notification.
 *
 * The masters sample through shared Wrap samplers, so the address mode set here governs mip
 * generation and any material sampling from the texture asset's own sampler, not the masters.
 */
class MOBMATERIALSEDITOR_API FMobTextureSetup
{
public:
	/** Sets up the selection for a unique unwrap: address Clamp, so the mips do not bleed the far edge in. */
	static void SetupSelectedForUniqueUVs();

	/** Sets up the selection for tiling: address Wrap, so the mips filter across the seam. */
	static void SetupSelectedForTiling();

	static bool CanSetupSelected();

	/** Why the entries are greyed out, or empty when they are not. Shown as the tooltip's last line. */
	static FText SetupReason();
};
