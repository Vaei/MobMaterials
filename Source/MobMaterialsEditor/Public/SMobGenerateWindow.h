// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class IDetailsView;
class UMobMaterialRecipe;

/**
 * A recipe picker over a details view of the recipe itself, plus Generate.
 *
 * A details view rather than hand-built Slate, so asset pickers, array editing and
 * reset-to-default all come for free and any field added to the recipe later needs no widget work.
 */
class SMobGenerateWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SMobGenerateWindow) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Opens the window, or focuses it if it is already up. Selects Recipe when one is given. */
	static void Open(UMobMaterialRecipe* Recipe = nullptr);

	/** Authors whatever the recipe asks for. Reports what it did, or why it could not. */
	static bool Generate(UMobMaterialRecipe* Recipe);

	/** Builds a level demonstrating a surface recipe's master, one feature per object. */
	static bool CreateTestLevel(UMobMaterialRecipe* Recipe);

private:
	FReply OnGenerateClicked();
	FReply OnTestLevelClicked();
	bool CanGenerate() const;
	bool CanCreateTestLevel() const;
	FText GetStatusText() const;

	void SetRecipe(UMobMaterialRecipe* Recipe);

	/** Runs one of the Python generators against a recipe. */
	static bool RunGenerator(UMobMaterialRecipe* Recipe, const TCHAR* Module, const TCHAR* Function,
		const FText& DoneMessage);

	TSharedPtr<IDetailsView> PickerView;
	TSharedPtr<IDetailsView> RecipeView;

	static TWeakPtr<SWindow> WindowInstance;
	static TWeakPtr<SMobGenerateWindow> Instance;
};
