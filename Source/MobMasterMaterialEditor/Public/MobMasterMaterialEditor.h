// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "AssetRegistry/AssetData.h"
#include "Modules/ModuleManager.h"
#include "UObject/SoftObjectPath.h"

class SWidget;

class FMobMasterMaterialEditorModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

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

	/** Runs a snippet against the plugin's Python directory. */
	static bool RunPython(const FString& Snippet, const FText& DoneMessage);

	/** Opens a weather collection so its wetness can be scrubbed without hunting for the asset. */
	static void OpenWeatherCollection(FSoftObjectPath Path);
	static bool WeatherCollectionExists(FSoftObjectPath Path);

	/** Python is only needed to generate; a material already authored works without it. */
	static bool IsPythonAvailable();
};
