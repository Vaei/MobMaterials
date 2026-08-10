// Copyright (c) Jared Taylor. All Rights Reserved

#include "MobMasterMaterialEditor.h"

#include "MobMasterMaterialEditorStyle.h"
#include "SMobGenerateWindow.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "IPythonScriptPlugin.h"
#include "MobMaterialRecipe.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobMasterMaterialEditor"

void FMobMasterMaterialEditorModule::StartupModule()
{
	FMobMasterMaterialEditorStyle::Register();

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FMobMasterMaterialEditorModule::RegisterMenus));
	}
}

void FMobMasterMaterialEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FMobMasterMaterialEditorStyle::Unregister();
}

void FMobMasterMaterialEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
	if (!ToolBar)
	{
		return;
	}

	FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
		TEXT("MobMenu"),
		FUIAction(),
		FOnGetContent::CreateRaw(this, &FMobMasterMaterialEditorModule::BuildMenu),
		LOCTEXT("MobToolbar", "Mob"),
		LOCTEXT("MobToolbarTip", "Master material tools"),
		FSlateIcon(FMobMasterMaterialEditorStyle::GetStyleSetName(),
			FMobMasterMaterialEditorStyle::GetMenuIconName())
	);

	// The style that gives a toolbar button its label beside the icon.
	Entry.StyleNameOverride = TEXT("CalloutToolbar");

	ToolBar->FindOrAddSection(TEXT("PlayGameExtensions")).AddEntry(Entry);
}

void FMobMasterMaterialEditorModule::FindRecipes(TArray<FAssetData>& OutRecipes)
{
	const FAssetRegistryModule& Registry =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));

	FARFilter Filter;
	Filter.ClassPaths.Add(UMobMaterialRecipe::StaticClass()->GetClassPathName());
	Filter.bRecursiveClasses = true;
	Registry.Get().GetAssets(Filter, OutRecipes);

	OutRecipes.Sort([](const FAssetData& A, const FAssetData& B)
	{
		return A.AssetName.LexicalLess(B.AssetName);
	});
}

TSharedRef<SWidget> FMobMasterMaterialEditorModule::BuildMenu()
{
	FMenuBuilder Menu(true, nullptr);

	Menu.BeginSection(TEXT("MobGenerate"), LOCTEXT("GenerateSection", "Generate"));
	Menu.AddMenuEntry(
		LOCTEXT("OpenWindow", "Generate Materials..."),
		LOCTEXT("OpenWindowTip",
			"Pick a recipe, edit it, and author the master it describes. A recipe is an asset, so a "
			"project can carry as many masters as it needs."),
		FSlateIcon(FMobMasterMaterialEditorStyle::GetStyleSetName(),
			FMobMasterMaterialEditorStyle::GetMenuIconName()),
		FUIAction(FExecuteAction::CreateStatic(&FMobMasterMaterialEditorModule::OpenWindow)));
	Menu.EndSection();

	TArray<FAssetData> Recipes;
	FindRecipes(Recipes);

	if (Recipes.Num() > 0)
	{
		Menu.BeginSection(TEXT("MobRecipes"), LOCTEXT("RecipesSection", "Recipes"));
		for (const FAssetData& Asset : Recipes)
		{
			const FSoftObjectPath Path = Asset.ToSoftObjectPath();
			Menu.AddMenuEntry(
				FText::FromName(Asset.AssetName),
				FText::Format(LOCTEXT("RecipeEntryTip", "Author the master {0} describes."),
					FText::FromName(Asset.AssetName)),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
				FUIAction(FExecuteAction::CreateStatic(
					&FMobMasterMaterialEditorModule::GenerateRecipe, Path)));
		}
		Menu.EndSection();
	}
	else
	{
		Menu.BeginSection(TEXT("MobNoRecipes"));
		Menu.AddWidget(
			SNew(STextBlock)
			.Text(LOCTEXT("NoRecipes",
				"No recipes yet.\nContent Browser, Miscellaneous, Data Asset, Mob Material Recipe."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Margin(FMargin(12.f, 4.f)),
			FText::GetEmpty());
		Menu.EndSection();
	}

	// Every distinct weather collection the surface recipes name. Scrubbing wetness is the one
	// thing done repeatedly while looking at a material, and the asset is otherwise buried.
	TArray<FSoftObjectPath> Collections;
	for (const FAssetData& Asset : Recipes)
	{
		if (const UMobMaterialRecipe* Recipe = Cast<UMobMaterialRecipe>(Asset.GetAsset());
			Recipe && Recipe->Kind == EMobMaterialKind::Surface && !Recipe->WeatherCollection.IsNull())
		{
			Collections.AddUnique(Recipe->WeatherCollection.ToSoftObjectPath());
		}
	}

	if (Collections.Num() > 0)
	{
		Menu.BeginSection(TEXT("MobWeather"), LOCTEXT("WeatherSection", "Weather"));
		for (const FSoftObjectPath& Path : Collections)
		{
			const FString Name = Path.GetAssetName();
			Menu.AddMenuEntry(
				FText::Format(LOCTEXT("OpenWeather", "Open {0}"), FText::FromString(Name)),
				LOCTEXT("OpenWeatherTip",
					"Opens the parameter collection carrying global wetness. Drag Wetness 0 to 1 and every "
					"instance with wetness on follows, live in the viewport."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Edit")),
				FUIAction(
					FExecuteAction::CreateStatic(
						&FMobMasterMaterialEditorModule::OpenWeatherCollection, Path),
					FCanExecuteAction::CreateStatic(
						&FMobMasterMaterialEditorModule::WeatherCollectionExists, Path)));
		}
		Menu.EndSection();
	}

	if (!IsPythonAvailable())
	{
		Menu.BeginSection(TEXT("MobPython"));
		Menu.AddWidget(
			SNew(STextBlock)
			.Text(LOCTEXT("NoPython",
				"Generating needs the Python Editor Script Plugin."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			.Margin(FMargin(12.f, 4.f)),
			FText::GetEmpty());
		Menu.EndSection();
	}

	return Menu.MakeWidget();
}

bool FMobMasterMaterialEditorModule::IsPythonAvailable()
{
	return IPythonScriptPlugin::Get() && IPythonScriptPlugin::Get()->IsPythonAvailable();
}

void FMobMasterMaterialEditorModule::OpenWindow()
{
	SMobGenerateWindow::Open();
}

void FMobMasterMaterialEditorModule::GenerateRecipe(FSoftObjectPath Path)
{
	SMobGenerateWindow::Generate(Cast<UMobMaterialRecipe>(Path.TryLoad()));
}

bool FMobMasterMaterialEditorModule::WeatherCollectionExists(FSoftObjectPath Path)
{
	// The collection is authored by the generator, so before a first run it will not be there yet.
	return FPackageName::DoesPackageExist(Path.GetLongPackageName());
}

void FMobMasterMaterialEditorModule::OpenWeatherCollection(FSoftObjectPath Path)
{
	if (UObject* Collection = Path.TryLoad(); Collection && GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Collection);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMobMasterMaterialEditorModule, MobMasterMaterialEditor)
