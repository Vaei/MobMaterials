// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SVerticalBox;
class UMaterialInstanceConstant;

/**
 * A material instance editor that stays where you left it.
 *
 * The stock one rebuilds its whole details tree on every change, which re-expands every group and
 * throws you back to the top - unusable for the one thing these masters are actually edited for,
 * which is nudging a number and looking at the result. This lays each group out once and writes
 * values straight through, so nothing moves under the cursor.
 *
 * Groups become tabs, so a layer is a tab and everything belonging to it is on it.
 */
class MOBMATERIALSEDITOR_API SMobLayerEditor : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMobLayerEditor) {}
		SLATE_ARGUMENT(UMaterialInstanceConstant*, Instance)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Opens the window, on the Content Browser selection or the open level's landscape. */
	static void Open();

private:
	/** Points the editor at an instance and rebuilds the tabs for it. */
	void SetInstance(UMaterialInstanceConstant* InInstance);

	/** The groups the instance's parameters carry, layers first and in the order they blend. */
	void GatherTabs();

	/** Lays out one group. Called when the tab changes, and never while a value is being edited. */
	void RebuildBody();

	/** Fills the strip in place. Nesting a wrap box inside one gives every tab its own row. */
	void RebuildTabStrip();
	TSharedRef<SWidget> BuildTextureRow(FName Parameter);
	TSharedRef<SWidget> BuildScalarRow(FName Parameter, const struct FMaterialParameterMetadata& Meta);
	TSharedRef<SWidget> BuildVectorRow(FName Parameter);
	TSharedRef<SWidget> BuildSwitchRow(FName Parameter);

	/** Puts the three selected textures into this tab's layer, by what their names end in. */
	FReply OnAssignSelected();

	/** Drops everything on this tab back to the parent's values. */
	FReply OnResetTab();

	/**
	 * The arrow that puts one parameter back, sat to the left of its input rather than off at the
	 * far right where the stock editor hides it behind whatever the row is wide.
	 */
	TSharedRef<SWidget> BuildResetButton(FName Parameter, EMaterialParameterType Type);

	/** Whether the instance carries its own value for a parameter, rather than the parent's. */
	bool IsOverridden(FName Parameter, EMaterialParameterType Type) const;

	FReply OnResetParameter(FName Parameter, EMaterialParameterType Type);
	bool CanAssignSelected() const;

	TWeakObjectPtr<UMaterialInstanceConstant> Instance;

	/** Group names, in tab order. */
	TArray<FName> Tabs;

	/** Which group is laid out below. */
	FName ActiveTab;

	/** Whether the active tab is one of the paint layers, which is what gets a texture block. */
	bool bActiveTabIsLayer = false;

	/** Where the texture rows draw their thumbnails. Shared, so twelve rows are not twelve pools. */
	TSharedPtr<class FAssetThumbnailPool> ThumbnailPool;

	TSharedPtr<SVerticalBox> Body;
	TSharedPtr<class SWrapBox> TabStrip;
};
