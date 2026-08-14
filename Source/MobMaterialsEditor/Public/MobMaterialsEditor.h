// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"

class SWidget;

class FMobMaterialsEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	/**
	 * The recipe that authored the master the open level's landscape renders with, matched by name.
	 *
	 * Which runtime virtual textures the terrain writes into is the recipe's answer, so wiring the
	 * level needs the recipe rather than the material the level points at.
	 */
	static class UMobMaterialRecipe* GetLandscapeRecipe();

private:
	void RegisterMenus();
	TSharedRef<SWidget> BuildMenu();

	/** Every recipe asset in the project, sorted by name. */
	static void FindRecipes(TArray<FAssetData>& OutRecipes);

	static void OpenWindow();
	static void GenerateRecipe(FSoftObjectPath Path);

	/** Asserts the contract every recipe's master claims to meet. */
	static void VerifyAll();

	/** What the project is spending: shader permutations, and texture held resident. */
	static void ReportAll();

	/** Packs one recipe's layer textures into the arrays its master samples. */
	static void PackLayerArrays(FSoftObjectPath Path);

	/** Points the landscape at its runtime virtual textures and fits a volume to each. */
	static void WireLandscapeRVT();
	static bool CanWireLandscapeRVT();
	static FText WireRVTReason();

	/** Runs a snippet against the plugin's Python directory. */
	static bool RunPython(const FString& Snippet, const FText& DoneMessage);

	/** Whether the toolbar button is shown. Per developer, not checked in. */
	static bool IsToolbarMenuEnabled();
	static void HideToolbarMenu();
	static void OpenSettings();

	/** Opens a weather collection so its wetness can be scrubbed without hunting for the asset. */
	static void OpenWeatherCollection(FSoftObjectPath Path);

	/** Opens whatever is at a path in its own editor. */
	static void OpenAsset(FSoftObjectPath Path);

	/** The landscape's instance, in the layer editor, or the stock one when shift is held. */
	static void OpenLandscapeInstance();
	static bool WeatherCollectionExists(FSoftObjectPath Path);

	/** Python is only needed to generate; a material already authored works without it. */
	static bool IsPythonAvailable();
};
