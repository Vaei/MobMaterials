// Copyright (c) Jared Taylor

#include "MobMaterialsEditor.h"

#include "MobChannelRemapWindow.h"
#include "MobLevelTools.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MobMaterialsEditorStyle.h"
#include "MobSimplifyWindow.h"
#include "MobTrampleVolumeCustomization.h"
#include "SMobLayerEditor.h"
#include "MobMaterialsEditorUserSettings.h"
#include "MobUVScaleWindow.h"
#include "SMobGenerateWindow.h"

#include "ISettingsModule.h"
#include "PropertyEditorModule.h"
#include "MobTrampleVolume.h"

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
#include "Framework/Application/SlateApplication.h"
#include "ToolMenus.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobMaterialsEditor"

void FMobMaterialsEditorModule::StartupModule()
{
	FMobMaterialsEditorStyle::Register();

	FPropertyEditorModule& PropertyEditor =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	PropertyEditor.RegisterCustomClassLayout(AMobTrampleVolume::StaticClass()->GetFName(),
		FOnGetDetailCustomizationInstance::CreateStatic(&FMobTrampleVolumeCustomization::MakeInstance));

	if (UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::RegisterStartupCallback(FSimpleMulticastDelegate::FDelegate::CreateRaw(
			this, &FMobMaterialsEditorModule::RegisterMenus));
	}
}

void FMobMaterialsEditorModule::ShutdownModule()
{
	if (FPropertyEditorModule* PropertyEditor =
		FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		PropertyEditor->UnregisterCustomClassLayout(TEXT("MobTrampleVolume"));
	}

	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FMobMaterialsEditorStyle::Unregister();
}

void FMobMaterialsEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(this);

	UToolMenu* ToolBar = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.LevelEditorToolBar.PlayToolBar"));
	if (!ToolBar)
	{
		return;
	}

	FToolMenuEntry Entry = FToolMenuEntry::InitComboButton(
		TEXT("MatMenu"),
		FUIAction(
			FExecuteAction(),
			FCanExecuteAction(),
			FIsActionChecked(),
			FIsActionButtonVisible::CreateStatic(&FMobMaterialsEditorModule::IsToolbarMenuEnabled)),
		FOnGetContent::CreateRaw(this, &FMobMaterialsEditorModule::BuildMenu),
		LOCTEXT("MatToolbar", "Mat"),
		LOCTEXT("MatToolbarTip", "Master material tools"),
		FSlateIcon(FMobMaterialsEditorStyle::GetStyleSetName(),
			FMobMaterialsEditorStyle::GetMenuIconName())
	);

	// The style that gives a toolbar button its label beside the icon.
	Entry.StyleNameOverride = TEXT("CalloutToolbar");

	ToolBar->FindOrAddSection(TEXT("PlayGameExtensions")).AddEntry(Entry);
}

void FMobMaterialsEditorModule::FindRecipes(TArray<FAssetData>& OutRecipes)
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

TSharedRef<SWidget> FMobMaterialsEditorModule::BuildMenu()
{
	FMenuBuilder Menu(true, nullptr);

	Menu.BeginSection(TEXT("MobGenerate"), LOCTEXT("GenerateSection", "Generate"));
	Menu.AddMenuEntry(
		LOCTEXT("OpenWindow", "Generate Materials..."),
		LOCTEXT("OpenWindowTip",
			"Pick a recipe, edit it, and author the master it describes. A recipe is an asset, so a "
			"project can carry as many masters as it needs."),
		FSlateIcon(FMobMaterialsEditorStyle::GetStyleSetName(),
			FMobMaterialsEditorStyle::GetMenuIconName()),
		FUIAction(FExecuteAction::CreateStatic(&FMobMaterialsEditorModule::OpenWindow)));
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
					&FMobMaterialsEditorModule::GenerateRecipe, Path)));
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
		if (!Recipe->WeatherCollection.IsNull())
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
					&FMobMaterialsEditorModule::PackLayerArrays, Path)));
		}
		Menu.EndSection();
	}

	if (UMaterialInstanceConstant* Instance = FMobLevelTools::GetLandscapeInstance())
	{
		Menu.BeginSection(TEXT("MobLandscapeMaterial"), LOCTEXT("LandscapeMaterialSection", "This Landscape"));
		Menu.AddMenuEntry(
			FText::Format(LOCTEXT("OpenLandscapeMI", "Open {0}"),
				FText::FromString(Instance->GetName())),
			LOCTEXT("OpenLandscapeMITip",
				"Opens the material instance the landscape in this level renders with. Every layer's "
				"textures and every dial live here, and finding it otherwise means selecting a proxy "
				"and reading a property off it."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.MaterialInstanceConstant")),
			FUIAction(FExecuteAction::CreateStatic(
				&FMobMaterialsEditorModule::OpenLandscapeInstance)));

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
						&FMobMaterialsEditorModule::OpenWeatherCollection, Path),
					FCanExecuteAction::CreateStatic(
						&FMobMaterialsEditorModule::WeatherCollectionExists, Path)));
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
			FUIAction(FExecuteAction::CreateStatic(&FMobMaterialsEditorModule::VerifyAll)));

		Menu.AddMenuEntry(
			LOCTEXT("Report", "Report Cost"),
			LOCTEXT("ReportTip",
				"Distinct shader permutations the instances add up to, and texture held resident per "
				"master. Both otherwise surface at cook time."),
			FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Statistics")),
			FUIAction(FExecuteAction::CreateStatic(&FMobMaterialsEditorModule::ReportAll)));
		Menu.EndSection();
	}

	Menu.BeginSection(TEXT("MobTextures"), LOCTEXT("TexturesSection", "Textures"));
	Menu.AddMenuEntry(
		LOCTEXT("LayerEditor", "Edit Layers..."),
		LOCTEXT("LayerEditorTip",
			"A material instance editor that stays where you left it. Groups become tabs, so a layer "
			"is a tab, and a value written while scrubbing does not rebuild the panel under the "
			"cursor. Opens on the Content Browser selection, or the open level's landscape."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("LevelEditor.Tabs.Layers")),
		FUIAction(FExecuteAction::CreateStatic(&SMobLayerEditor::Open)));

	Menu.AddMenuEntry(
		LOCTEXT("FitUVScale", "Fit UV Scale To Landscape..."),
		LOCTEXT("FitUVScaleTip",
			"Works out every layer's UVScale from the landscape's own quad size, so a tile measures what "
			"you asked for on the ground. A landscape that has been resized no longer has the quads the "
			"defaults assume, and every layer then tiles wrongly at once."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Landscape")),
		FUIAction(FExecuteAction::CreateStatic(&FMobUVScaleWindow::Open)));

	Menu.AddMenuEntry(
		LOCTEXT("Simplify", "Simplify Material To Layer..."),
		LOCTEXT("SimplifyTip",
			"Turns a landscape material down to one layer so what is on screen is that layer's art and "
			"nothing else - no other layers, no tiling break, no slope rock, moss or wetness. Everything "
			"it changes is recorded, and Restore in the same window puts it all back. Reset takes those "
			"same parameters to the parent's values when there is no recording left to put back."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Filter")),
		FUIAction(FExecuteAction::CreateStatic(&FMobSimplifyWindow::Open)));

	Menu.AddMenuEntry(
		LOCTEXT("RemapChannels", "Remap Texture Channels..."),
		LOCTEXT("RemapChannelsTip",
			"Repacks incoming art into the HRC a landscape layer reads or the CRM a surface reads. "
			"Common layouts are presets; anything else is three slots you set yourself."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
		FUIAction(FExecuteAction::CreateStatic(&FMobChannelRemapWindow::Open)));
	Menu.EndSection();

	// Greyed out entries say why in their own tooltip: a disabled entry with the same text as an
	// enabled one only tells you it is disabled, which is the part already visible.
	auto Reason = [](const FText& Tip, FText (*Why)()) -> TAttribute<FText>
	{
		return TAttribute<FText>::CreateLambda([Tip, Why]
		{
			const FText Blocked = Why();
			return Blocked.IsEmpty() ? Tip
				: FText::Format(LOCTEXT("LevelToolBlocked", "{0}\n\n{1}"), Tip, Blocked);
		});
	};

	Menu.BeginSection(TEXT("MobLevel"), LOCTEXT("LevelSection", "Level"));
	Menu.AddMenuEntry(
		LOCTEXT("SnapToLandscape", "Snap Selected Actor To Landscape Centre"),
		Reason(LOCTEXT("SnapToLandscapeTip",
			"Moves the selection to the middle of the nearest landscape and sits it on the surface, "
			"which is where a test mesh wants to be and is otherwise three numbers to work out by hand."),
			&FMobLevelTools::SnapReason),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Transform")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobLevelTools::SnapToLandscapeCentre),
			FCanExecuteAction::CreateStatic(&FMobLevelTools::CanSnapToLandscapeCentre)));

	Menu.AddMenuEntry(
		LOCTEXT("FitBoxToLandscape", "Fit Selected Box Volume To Landscape"),
		Reason(LOCTEXT("FitBoxToLandscapeTip",
			"Centres and scales the selected volume so it covers the nearest landscape exactly, "
			"proxies included. Its rotation is cleared: a turned box cannot be scaled to cover an "
			"axis aligned one."),
			&FMobLevelTools::FitReason),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Volume")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobLevelTools::FitBoxToLandscape),
			FCanExecuteAction::CreateStatic(&FMobLevelTools::CanFitBoxToLandscape)));

	Menu.AddMenuEntry(
		LOCTEXT("WireRVT", "Wire Landscape Runtime Virtual Textures"),
		Reason(LOCTEXT("WireRVTTip",
			"Points the landscape and every proxy at the runtime virtual textures its recipe names, "
			"and fits a volume to each. Until this has run the textures are never written, and a "
			"material with Use RVT on samples black: unlit black ground that no layer's art can "
			"change."),
			&FMobMaterialsEditorModule::WireRVTReason),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.RuntimeVirtualTexture")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobMaterialsEditorModule::WireLandscapeRVT),
			FCanExecuteAction::CreateStatic(&FMobMaterialsEditorModule::CanWireLandscapeRVT)));

	Menu.AddMenuEntry(
		LOCTEXT("RebuildPhysMat", "Rebake Landscape Physical Materials"),
		Reason(LOCTEXT("RebuildPhysMatTip",
			"Rebakes which physical material the ground reports underfoot. The material's physical "
			"material output is baked into collision data rather than read per trace, so a "
			"regenerated master changes nothing about what a footstep hears until this runs."),
			&FMobLevelTools::RebuildReason),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Refresh")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobLevelTools::RebuildPhysicalMaterial),
			FCanExecuteAction::CreateStatic(&FMobLevelTools::CanRebuildPhysicalMaterial)));

	Menu.AddMenuEntry(
		LOCTEXT("AssignTextures", "Assign Selected Textures To Landscape"),
		Reason(LOCTEXT("AssignTexturesTip",
			"Assigns the textures selected in the Content Browser to the landscape's material "
			"instance, matched by name: T_DryGrass_BaseColor lands on DryGrass_BC. All or nothing, "
			"so a set with one name it cannot place changes nothing."),
			&FMobLevelTools::AssignReason),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("ClassIcon.Texture2D")),
		FUIAction(
			FExecuteAction::CreateStatic(&FMobLevelTools::AssignSelectedTextures),
			FCanExecuteAction::CreateStatic(&FMobLevelTools::CanAssignSelectedTextures)));
	Menu.EndSection();

	Menu.BeginSection(TEXT("MobSettings"), LOCTEXT("SettingsSection", "Settings"));
	Menu.AddMenuEntry(
		LOCTEXT("EditorSettings", "Editor Preferences"),
		LOCTEXT("EditorSettingsTip", "Per-developer settings for this plugin. Not checked in."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Toolbar.Settings")),
		FUIAction(FExecuteAction::CreateStatic(&FMobMaterialsEditorModule::OpenSettings)));

	Menu.AddMenuEntry(
		LOCTEXT("HideMenu", "Hide This Menu"),
		LOCTEXT("HideMenuTip",
			"Removes the Mat button from your toolbar. Turn it back on under Editor Preferences, Plugins, "
			"Mob Materials Editor."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), TEXT("Icons.Visibility")),
		FUIAction(FExecuteAction::CreateStatic(&FMobMaterialsEditorModule::HideToolbarMenu)));
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

bool FMobMaterialsEditorModule::IsPythonAvailable()
{
	return IPythonScriptPlugin::Get() && IPythonScriptPlugin::Get()->IsPythonAvailable();
}

void FMobMaterialsEditorModule::OpenWindow()
{
	SMobGenerateWindow::Open();
}

void FMobMaterialsEditorModule::GenerateRecipe(FSoftObjectPath Path)
{
	SMobGenerateWindow::Generate(Cast<UMobMaterialRecipe>(Path.TryLoad()));
}

UMobMaterialRecipe* FMobMaterialsEditorModule::GetLandscapeRecipe()
{
	const UMaterialInterface* Master = FMobLevelTools::GetLandscapeMaster();
	if (!Master)
	{
		return nullptr;
	}

	const FString MasterName = Master->GetName();

	TArray<FAssetData> Recipes;
	FindRecipes(Recipes);

	for (const FAssetData& Asset : Recipes)
	{
		UMobMaterialRecipe* Recipe = Cast<UMobMaterialRecipe>(Asset.GetAsset());
		if (Recipe && Recipe->Kind == EMobMaterialKind::Landscape
			&& MasterName == TEXT("M_") + Recipe->AssetName)
		{
			return Recipe;
		}
	}
	return nullptr;
}

bool FMobMaterialsEditorModule::CanWireLandscapeRVT()
{
	return IsPythonAvailable() && GetLandscapeRecipe() != nullptr;
}

FText FMobMaterialsEditorModule::WireRVTReason()
{
	if (!IsPythonAvailable())
	{
		return LOCTEXT("WireNoPython", "This needs the Python Editor Script Plugin.");
	}

	const UMaterialInterface* Master = FMobLevelTools::GetLandscapeMaster();
	if (!Master)
	{
		return LOCTEXT("WireNoLandscape", "There is no landscape in this level.");
	}

	if (!GetLandscapeRecipe())
	{
		return FText::Format(LOCTEXT("WireNoRecipe",
			"No landscape recipe authored {0}, so there is nothing to say which runtime virtual "
			"textures it writes."), FText::FromString(Master->GetName()));
	}

	return FText::GetEmpty();
}

void FMobMaterialsEditorModule::WireLandscapeRVT()
{
	SMobGenerateWindow::WireRVT(GetLandscapeRecipe());
}

bool FMobMaterialsEditorModule::RunPython(const FString& Snippet, const FText& DoneMessage)
{
	if (!IsPythonAvailable())
	{
		return false;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobMaterials"));
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
		: LOCTEXT("PythonFailed", "Mat: failed. See the Output Log."));
	Info.ExpireDuration = bOk ? 4.f : 8.f;
	if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
	{
		Item->SetCompletionState(bOk ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
	}
	return bOk;
}

void FMobMaterialsEditorModule::VerifyAll()
{
	TArray<FAssetData> Recipes;
	FindRecipes(Recipes);

	FString Snippet = TEXT("import importlib, mob_verify; importlib.reload(mob_verify)\n");
	for (const FAssetData& Asset : Recipes)
	{
		Snippet += FString::Printf(TEXT("mob_verify.run(r'%s')\n"),
			*Asset.ToSoftObjectPath().ToString());
	}

	RunPython(Snippet, LOCTEXT("VerifyDone", "Mat: verification finished. See the Output Log."));
}

void FMobMaterialsEditorModule::ReportAll()
{
	RunPython(TEXT("import importlib, mob_report; importlib.reload(mob_report); mob_report.run_all()"),
		LOCTEXT("ReportDone", "Mat: report written to the Output Log."));
}

void FMobMaterialsEditorModule::PackLayerArrays(FSoftObjectPath Path)
{
	const FString Snippet = FString::Printf(
		TEXT("import importlib, mob_arrays; importlib.reload(mob_arrays); mob_arrays.pack(r'%s')"),
		*Path.ToString());

	RunPython(Snippet, LOCTEXT("PackDone", "Mat: layer arrays packed. See the Output Log."));
}

bool FMobMaterialsEditorModule::IsToolbarMenuEnabled()
{
	return GetDefault<UMobMaterialsEditorUserSettings>()->bShowToolbarMenu;
}

void FMobMaterialsEditorModule::HideToolbarMenu()
{
	UMobMaterialsEditorUserSettings* Settings =
		GetMutableDefault<UMobMaterialsEditorUserSettings>();
	Settings->bShowToolbarMenu = false;
	Settings->SaveConfig();
}

void FMobMaterialsEditorModule::OpenSettings()
{
	if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>(TEXT("Settings")))
	{
		const UMobMaterialsEditorUserSettings* Settings =
			GetDefault<UMobMaterialsEditorUserSettings>();
		SettingsModule->ShowViewer(Settings->GetContainerName(), Settings->GetCategoryName(),
			Settings->GetSectionName());
	}
}

bool FMobMaterialsEditorModule::WeatherCollectionExists(FSoftObjectPath Path)
{
	// The collection is authored by the generator, so before a first run it will not be there yet.
	return FPackageName::DoesPackageExist(Path.GetLongPackageName());
}

void FMobMaterialsEditorModule::OpenAsset(FSoftObjectPath Path)
{
	OpenWeatherCollection(Path);
}

void FMobMaterialsEditorModule::OpenLandscapeInstance()
{
	// The stock editor rebuilds its whole tree on every change, so it is the exception rather than
	// the route: shift asks for it, everything else gets the one that stays put.
	if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
	{
		if (UMaterialInstanceConstant* Instance = FMobLevelTools::GetLandscapeInstance())
		{
			OpenAsset(FSoftObjectPath(Instance));
		}
		return;
	}

	SMobLayerEditor::Open();
}

void FMobMaterialsEditorModule::OpenWeatherCollection(FSoftObjectPath Path)
{
	if (UObject* Collection = Path.TryLoad(); Collection && GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Collection);
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMobMaterialsEditorModule, MobMaterialsEditor)
