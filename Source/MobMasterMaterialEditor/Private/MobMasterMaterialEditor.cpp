// Copyright (c) Jared Taylor. All Rights Reserved

#include "MobMasterMaterialEditor.h"

#include "MobMasterMaterialEditorStyle.h"
#include "MobMasterMaterialEditorUserSettings.h"
#include "SMobGenerateWindow.h"

#include "ISettingsModule.h"

#include "AssetRegistry/ARFilter.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "IPythonScriptPlugin.h"
#include "MobMaterialRecipe.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "Widgets/Notifications/SNotificationList.h"
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
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateStatic(&FMobMasterMaterialEditorModule::IsToolbarMenuEnabled)),
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
	TArray<FAssetData> ArrayRecipes;
	for (const FAssetData& Asset : Recipes)
	{
		const UMobMaterialRecipe* Recipe = Cast<UMobMaterialRecipe>(Asset.GetAsset());
		if (!Recipe)
		{
			continue;
		}
		if (Recipe->Kind == EMobMaterialKind::Surface && !Recipe->WeatherCollection.IsNull())
		{
			Collections.AddUnique(Recipe->WeatherCollection.ToSoftObjectPath());
		}
		if (Recipe->Kind == EMobMaterialKind::Landscape && Recipe->bTextureArrayLayers)
		{
			ArrayRecipes.Add(Asset);
		}
	}

	if (ArrayRecipes.Num() > 0)
	{
		Menu.BeginSection(TEXT("MobArrays"), LOCTEXT("ArraysSection", "Layer Arrays"));
		for (const FAssetData& Asset : ArrayRecipes)
		{
			const FSoftObjectPath Path = Asset.ToSoftObjectPath();
			Menu.AddMenuEntry(
				FText::Format(LOCTEXT("PackArrays", "Pack Layers for {0}"),
					FText::FromName(Asset.AssetName)),
				LOCTEXT("PackArraysTip",
					"Finds each layer's textures under the recipe's Layer Texture Root and packs them into "
					"one array per channel. Run this before generating, and again whenever a layer's art "
					"changes."),
				FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2DArray")),
				FUIAction(FExecuteAction::CreateStatic(
					&FMobMasterMaterialEditorModule::PackLayerArrays, Path)));
		}
		Menu.EndSection();
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

	if (Recipes.Num() > 0)
	{
		Menu.BeginSection(TEXT("MobTools"), LOCTEXT("ToolsSection", "Check"));
		Menu.AddMenuEntry(
			LOCTEXT("Verify", "Verify Contract"),
			LOCTEXT("VerifyTip",
				"Builds a scratch instance per feature and asserts what the documentation claims: what each "
				"one costs in taps and samplers, that ambient occlusion is left alone, that the custom "
				"primitive data indices have not moved."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Check")),
			FUIAction(FExecuteAction::CreateStatic(&FMobMasterMaterialEditorModule::VerifyAll)));

		Menu.AddMenuEntry(
			LOCTEXT("Report", "Report Cost"),
			LOCTEXT("ReportTip",
				"Distinct shader permutations the instances add up to, and texture held resident per "
				"master. Both otherwise surface at cook time."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Statistics")),
			FUIAction(FExecuteAction::CreateStatic(&FMobMasterMaterialEditorModule::ReportAll)));
		Menu.EndSection();
	}

	Menu.BeginSection(TEXT("MobSettings"), LOCTEXT("SettingsSection", "Settings"));
	Menu.AddMenuEntry(
		LOCTEXT("EditorSettings", "Editor Preferences"),
		LOCTEXT("EditorSettingsTip", "Per-developer settings for this plugin. Not checked in."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Toolbar.Settings")),
		FUIAction(FExecuteAction::CreateStatic(&FMobMasterMaterialEditorModule::OpenSettings)));

	Menu.AddMenuEntry(
		LOCTEXT("HideMenu", "Hide This Menu"),
		LOCTEXT("HideMenuTip",
			"Removes the Mob button from your toolbar. Turn it back on under Editor Preferences, Plugins, "
			"Mob Master Material Editor."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visibility")),
		FUIAction(FExecuteAction::CreateStatic(&FMobMasterMaterialEditorModule::HideToolbarMenu)));
	Menu.EndSection();

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

bool FMobMasterMaterialEditorModule::RunPython(const FString& Snippet, const FText& DoneMessage)
{
	if (!IsPythonAvailable())
	{
		return false;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobMasterMaterial"));
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString ScriptDir = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()), TEXT("Python")).Replace(TEXT("\\"), TEXT("/"));

	const FString Command = FString::Printf(
		TEXT("import sys\n")
		TEXT("p = r'%s'\n")
		TEXT("sys.path.append(p) if p not in sys.path else None\n")
		TEXT("%s\n"), *ScriptDir, *Snippet);

	const bool bOk = IPythonScriptPlugin::Get()->ExecPythonCommand(*Command);

	FNotificationInfo Info(bOk ? DoneMessage
		: LOCTEXT("PythonFailed", "Mob: failed. See the Output Log."));
	Info.ExpireDuration = bOk ? 4.f : 8.f;
	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bOk ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
	return bOk;
}

void FMobMasterMaterialEditorModule::VerifyAll()
{
	TArray<FAssetData> Recipes;
	FindRecipes(Recipes);

	FString Snippet = TEXT("import importlib, mob_verify; importlib.reload(mob_verify)\n");
	for (const FAssetData& Asset : Recipes)
	{
		Snippet += FString::Printf(TEXT("mob_verify.run(r'%s')\n"),
			*Asset.ToSoftObjectPath().ToString());
	}

	RunPython(Snippet, LOCTEXT("VerifyDone", "Mob: verification finished. See the Output Log."));
}

void FMobMasterMaterialEditorModule::ReportAll()
{
	RunPython(TEXT("import importlib, mob_report; importlib.reload(mob_report); mob_report.run_all()"),
		LOCTEXT("ReportDone", "Mob: report written to the Output Log."));
}

void FMobMasterMaterialEditorModule::PackLayerArrays(FSoftObjectPath Path)
{
	const FString Snippet = FString::Printf(
		TEXT("import importlib, mob_arrays; importlib.reload(mob_arrays); mob_arrays.pack(r'%s')"),
		*Path.ToString());

	RunPython(Snippet, LOCTEXT("PackDone", "Mob: layer arrays packed. See the Output Log."));
}

bool FMobMasterMaterialEditorModule::IsToolbarMenuEnabled()
{
	return GetDefault<UMobMasterMaterialEditorUserSettings>()->bShowToolbarMenu;
}

void FMobMasterMaterialEditorModule::HideToolbarMenu()
{
	UMobMasterMaterialEditorUserSettings* Settings =
		GetMutableDefault<UMobMasterMaterialEditorUserSettings>();
	Settings->bShowToolbarMenu = false;
	Settings->SaveConfig();
}

void FMobMasterMaterialEditorModule::OpenSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
	{
		const UMobMasterMaterialEditorUserSettings* Settings =
			GetDefault<UMobMasterMaterialEditorUserSettings>();
		SettingsModule->ShowViewer(Settings->GetContainerName(), Settings->GetCategoryName(),
			Settings->GetSectionName());
	}
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
