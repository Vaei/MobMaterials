// Copyright (c) Jared Taylor

#include "SMobGenerateWindow.h"
#include "MobLevelTools.h"

#include "IDetailsView.h"
#include "IPythonScriptPlugin.h"
#include "MobMaterialRecipe.h"
#include "MobRecipeSelection.h"
#include "PropertyEditorModule.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/MessageDialog.h"
#include "Misc/Paths.h"
#include "Styling/AppStyle.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobGenerateWindow"

TWeakPtr<SWindow> SMobGenerateWindow::WindowInstance;
TWeakPtr<SMobGenerateWindow> SMobGenerateWindow::Instance;

UMobRecipeSelection* UMobRecipeSelection::Get()
{
	static TStrongObjectPtr<UMobRecipeSelection> Selection(NewObject<UMobRecipeSelection>());
	return Selection.Get();
}

namespace
{
	void Notify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 4.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

void SMobGenerateWindow::Construct(const FArguments& InArgs)
{
	FPropertyEditorModule& PropertyModule =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));

	FDetailsViewArgs PickerArgs;
	PickerArgs.bShowOptions = false;
	PickerArgs.bHideSelectionTip = true;
	PickerArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	PickerView = PropertyModule.CreateDetailView(PickerArgs);
	PickerView->SetObject(UMobRecipeSelection::Get());

	FDetailsViewArgs RecipeArgs;
	RecipeArgs.bAllowSearch = true;
	RecipeArgs.bShowOptions = false;
	RecipeArgs.bHideSelectionTip = true;
	RecipeArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	RecipeView = PropertyModule.CreateDetailView(RecipeArgs);
	RecipeView->SetObject(UMobRecipeSelection::Get()->Recipe);

	// The picker writes straight onto the selection object, so the recipe view has to follow it.
	PickerView->OnFinishedChangingProperties().AddLambda(
		[this](const FPropertyChangedEvent&)
		{
			if (RecipeView.IsValid())
			{
				RecipeView->SetObject(UMobRecipeSelection::Get()->Recipe, true);
			}
		});

	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			PickerView.ToSharedRef()
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			RecipeView.ToSharedRef()
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
			.Padding(8.f)
			[
				SNew(SVerticalBox)

				// Generating runs on the game thread and can take minutes on a big landscape, with
				// the editor unresponsive throughout. Said here rather than discovered.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(SImage)
						.Image(FAppStyle::GetBrush(TEXT("Icons.WarningWithColor")))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(LOCTEXT("BuildTimeWarning",
							"The editor locks up while this runs. A surface master takes a few seconds; a "
							"landscape with a dozen layers rebuilds several hundred nodes and recompiles, "
							"which can take minutes. It is working, not hung - let it finish."))
						.ColorAndOpacity(FSlateColor::UseSubduedForeground())
						.AutoWrapText(true)
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(this, &SMobGenerateWindow::GetStatusText)
						.AutoWrapText(true)
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("TestLevel", "Test Level"))
					.ToolTipText(LOCTEXT("TestLevelTip",
						"Builds a level demonstrating this master, one feature per object, using the "
						"placeholder textures and separating the layers by tint. Opens it, unsaved work in "
						"the current level is discarded."))
					.IsEnabled(this, &SMobGenerateWindow::CanCreateTestLevel)
					.OnClicked(this, &SMobGenerateWindow::OnTestLevelClicked)
				]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("Generate", "Generate"))
					.ToolTipText(LOCTEXT("GenerateTip",
						"Authors the master, its functions and its instances. Existing assets are rebuilt in "
						"place, so instances keep their references and their parameter values."))
					.IsEnabled(this, &SMobGenerateWindow::CanGenerate)
					.OnClicked(this, &SMobGenerateWindow::OnGenerateClicked)
					]
				]
			]
		]
	];
}

void SMobGenerateWindow::SetRecipe(UMobMaterialRecipe* Recipe)
{
	UMobRecipeSelection::Get()->Recipe = Recipe;
	if (PickerView.IsValid())
	{
		PickerView->ForceRefresh();
	}
	if (RecipeView.IsValid())
	{
		RecipeView->SetObject(Recipe, true);
	}
}

void SMobGenerateWindow::Open(UMobMaterialRecipe* Recipe)
{
	if (Recipe)
	{
		UMobRecipeSelection::Get()->Recipe = Recipe;
	}

	if (const TSharedPtr<SWindow> Existing = WindowInstance.Pin())
	{
		if (const TSharedPtr<SMobGenerateWindow> Content = Instance.Pin(); Content.IsValid() && Recipe)
		{
			Content->SetRecipe(Recipe);
		}
		Existing->BringToFront();
		return;
	}

	const TSharedRef<SMobGenerateWindow> Content = SNew(SMobGenerateWindow);

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("WindowTitle", "Mob Materials"))
		.ClientSize(FVector2D(560.f, 720.f))
		.SupportsMaximize(false)
		[
			Content
		];

	FSlateApplication::Get().AddWindow(Window);
	WindowInstance = Window;
	Instance = Content;
}

bool SMobGenerateWindow::CanGenerate() const
{
	const UMobMaterialRecipe* Recipe = UMobRecipeSelection::Get()->Recipe;
	return Recipe && Recipe->IsUsable() && IPythonScriptPlugin::Get()
		&& IPythonScriptPlugin::Get()->IsPythonAvailable();
}

FText SMobGenerateWindow::GetStatusText() const
{
	const UMobMaterialRecipe* Recipe = UMobRecipeSelection::Get()->Recipe;
	if (!Recipe)
	{
		return LOCTEXT("NoRecipe", "Pick a recipe, or make one: Content Browser, Miscellaneous, Data Asset, Mob Material Recipe.");
	}
	if (!IPythonScriptPlugin::Get() || !IPythonScriptPlugin::Get()->IsPythonAvailable())
	{
		return LOCTEXT("NoPython", "Generating needs the Python Editor Script Plugin.");
	}
	if (Recipe->AssetName.IsEmpty() || Recipe->OutputPath.Path.IsEmpty())
	{
		return LOCTEXT("NeedNameAndPath", "Set an output path and an asset name.");
	}
	if (Recipe->Kind == EMobMaterialKind::Landscape && Recipe->Layers.Num() == 0)
	{
		return LOCTEXT("NeedLayers", "A landscape recipe needs at least one paint layer.");
	}

	return FText::Format(LOCTEXT("WillWrite", "Writes M_{0} to {1}"),
		FText::FromString(Recipe->AssetName), FText::FromString(Recipe->OutputPath.Path));
}

FReply SMobGenerateWindow::OnGenerateClicked()
{
	Generate(UMobRecipeSelection::Get()->Recipe);
	return FReply::Handled();
}

FReply SMobGenerateWindow::OnTestLevelClicked()
{
	CreateTestLevel(UMobRecipeSelection::Get()->Recipe);
	return FReply::Handled();
}

bool SMobGenerateWindow::CanCreateTestLevel() const
{
	const UMobMaterialRecipe* Recipe = UMobRecipeSelection::Get()->Recipe;
	return CanGenerate() && Recipe && Recipe->Kind == EMobMaterialKind::Surface;
}

bool SMobGenerateWindow::RunGenerator(UMobMaterialRecipe* Recipe, const TCHAR* Module,
	const TCHAR* Function, const FText& DoneMessage)
{
	if (!Recipe)
	{
		Notify(LOCTEXT("GenNoRecipe", "Mat: no recipe selected."), false);
		return false;
	}

	if (!IPythonScriptPlugin::Get() || !IPythonScriptPlugin::Get()->IsPythonAvailable())
	{
		Notify(LOCTEXT("GenNoPython", "Mat: enable the Python Editor Script Plugin to generate."), false);
		return false;
	}

	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobMaterials"));
	if (!Plugin.IsValid())
	{
		Notify(LOCTEXT("GenNoPlugin", "Mat: could not locate the plugin directory."), false);
		return false;
	}

	const FString ScriptDir = FPaths::Combine(
		FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir()), TEXT("Python")).Replace(TEXT("\\"), TEXT("/"));

	// importlib.reload so an edited generator is picked up without restarting the editor.
	const FString Command = FString::Printf(
		TEXT("import sys, importlib\n")
		TEXT("p = r'%s'\n")
		TEXT("sys.path.append(p) if p not in sys.path else None\n")
		TEXT("import %s as m; importlib.reload(m); m.%s(r'%s')\n"),
		*ScriptDir, Module, Function, *Recipe->GetPathName());

	const bool bOk = IPythonScriptPlugin::Get()->ExecPythonCommand(*Command);
	if (bOk)
	{
		Notify(DoneMessage, true);
	}
	else
	{
		// The Python error itself goes to the Output Log, which is where the detail is.
		Notify(LOCTEXT("GenFailed", "Mat: failed. See the Output Log."), false);
	}
	return bOk;
}

bool SMobGenerateWindow::Generate(UMobMaterialRecipe* Recipe)
{
	if (!Recipe)
	{
		Notify(LOCTEXT("GenNoRecipe2", "Mat: no recipe selected."), false);
		return false;
	}

	const TCHAR* Module = Recipe->Kind == EMobMaterialKind::Landscape
		? TEXT("author_landscape") : TEXT("author_surface");

	const bool bBuilt = RunGenerator(Recipe, Module, TEXT("build_all"),
		FText::Format(LOCTEXT("GenDone", "Mat: authored M_{0}."), FText::FromString(Recipe->AssetName)));

	// What the ground reports underfoot is baked into collision data, not read per trace, so a
	// regenerated master says something new and every footstep keeps hearing the old answer until
	// this runs. It is the step that gets forgotten, so it is not a step.
	if (bBuilt && Recipe->Kind == EMobMaterialKind::Landscape
		&& FMobLevelTools::CanRebuildPhysicalMaterial())
	{
		FMobLevelTools::RebuildPhysicalMaterial();
	}

	return bBuilt;
}

bool SMobGenerateWindow::CreateTestLevel(UMobMaterialRecipe* Recipe)
{
	if (!Recipe || Recipe->Kind != EMobMaterialKind::Surface)
	{
		Notify(LOCTEXT("TestNeedsSurface", "Mat: test levels are built from a surface recipe."), false);
		return false;
	}

	// It opens a new level, so give the chance to keep whatever is loaded.
	const EAppReturnType::Type Answer = FMessageDialog::Open(EAppMsgType::OkCancel,
		LOCTEXT("TestLevelConfirm",
			"This opens a new level. Unsaved work in the current one is lost.\n\nContinue?"));
	if (Answer != EAppReturnType::Ok)
	{
		return false;
	}

	return RunGenerator(Recipe, TEXT("author_test_level"), TEXT("build"),
		FText::Format(LOCTEXT("TestDone", "Mat: test level built for M_{0}."),
			FText::FromString(Recipe->AssetName)));
}

#undef LOCTEXT_NAMESPACE
