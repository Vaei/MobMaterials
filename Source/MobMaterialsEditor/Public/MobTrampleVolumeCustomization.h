// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

/**
 * Puts Trample at the top of the volume's details.
 *
 * Everything anyone opens this actor to change is in that category, and by default it sorts in
 * among Transform, Rendering and the rest of an actor's inherited furniture.
 */
class FMobTrampleVolumeCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;
};
