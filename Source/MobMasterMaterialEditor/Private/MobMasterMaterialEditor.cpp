// Copyright (c) Jared Taylor. All Rights Reserved

#include "MobMasterMaterialEditor.h"

#include "MobMasterMaterialEditorStyle.h"
#include "SMobGenerateWindow.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "IPythonScriptPlugin.h"
#include "MobMaterialRecipe.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
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

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMobMasterMaterialEditorModule, MobMasterMaterialEditor)
