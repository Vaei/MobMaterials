// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "IDetailCustomization.h"

class IPropertyHandle;

/**
 * Puts Trample at the top of the volume's details, and says so when the settings there mean every
 * print is kept forever.
 *
 * Everything anyone opens this actor to change is in that category, and by default it sorts in
 * among Transform, Rendering and the rest of an actor's inherited furniture.
 */
class FMobTrampleVolumeCustomization : public IDetailCustomization
{
public:
	static TSharedRef<IDetailCustomization> MakeInstance();

	virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override;

private:
	TSharedPtr<IPropertyHandle> HoldSecondsHandle;
	TSharedPtr<IPropertyHandle> MaxMarksHandle;

	/** Whether the hold is set such that no print ever retires. */
	bool NeverFades() const;

	EVisibility WarningVisibility() const;
	FText WarningText() const;
};
