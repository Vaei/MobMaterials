// Copyright (c) Jared Taylor

#include "MobUVScaleWindow.h"

#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "MaterialEditingLibrary.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "EngineUtils.h"
#include "Editor/EditorEngine.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobMasterMaterialEditor"

namespace
{
	const TCHAR* UVScaleSuffix = TEXT("_UVScale");

	void NotifyUVScale(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 5.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}
}

float FMobUVScaleWindow::QuadSizeCm(const ALandscapeProxy* Landscape)
{
	// A landscape's UV unit is one quad, and a quad is one unit of the actor's own scale.
	return Landscape ? FMath::Abs(Landscape->GetActorScale3D().X) : 0.f;
}

ALandscapeProxy* FMobUVScaleWindow::FindLandscape()
{
	if (!GEditor)
	{
		return nullptr;
	}

	for (FSelectionIterator It(*GEditor->GetSelectedActors()); It; ++It)
	{
		if (ALandscapeProxy* Selected = Cast<ALandscapeProxy>(*It))
		{
			return Selected;
		}
	}

	// Nothing selected: the level's only landscape is unambiguous, several is not.
	ALandscapeProxy* Found = nullptr;
	const UWorld* World = GEditor->GetEditorWorldContext().World();
	if (!World)
	{
		return nullptr;
	}

	for (TActorIterator<ALandscape> It(World); It; ++It)
	{
		if (Found)
		{
			return nullptr;
		}
		Found = *It;
	}
	return Found;
}

UMaterialInstanceConstant* FMobUVScaleWindow::FindInstance(const ALandscapeProxy* Landscape)
{
	TArray<FAssetData> Selected;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
		.Get().GetSelectedAssets(Selected);

	for (const FAssetData& Asset : Selected)
	{
		if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset()))
		{
			return Instance;
		}
	}

	return Landscape ? Cast<UMaterialInstanceConstant>(Landscape->GetLandscapeMaterial()) : nullptr;
}

TArray<FName> FMobUVScaleWindow::LayerNames(const UMaterialInstanceConstant* Instance)
{
	TArray<FName> Names;
	if (!Instance)
	{
		return Names;
	}

	TArray<FMaterialParameterInfo> Infos;
	TArray<FGuid> Guids;
	Instance->GetAllScalarParameterInfo(Infos, Guids);

	for (const FMaterialParameterInfo& Info : Infos)
	{
		const FString Name = Info.Name.ToString();
		if (Name.EndsWith(UVScaleSuffix))
		{
			Names.AddUnique(FName(*Name.LeftChop(FCString::Strlen(UVScaleSuffix))));
		}
	}

	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Names;
}

int32 FMobUVScaleWindow::Apply(const UMobUVScaleOptions& Options, FText& OutError)
{
	const float QuadCm = QuadSizeCm(Options.Landscape);
	if (QuadCm <= 0.f)
	{
		OutError = LOCTEXT("UVScaleNoLandscape", "Pick a landscape. Its scale is what decides the answer.");
		return 0;
	}

	UMaterialInstanceConstant* Instance = Options.MaterialInstance;
	if (!Instance)
	{
		OutError = LOCTEXT("UVScaleNoInstance", "Pick the material instance to write.");
		return 0;
	}

	const TArray<FName> Layers = LayerNames(Instance);
	if (Layers.Num() == 0)
	{
		OutError = LOCTEXT("UVScaleNoLayers",
			"That instance has no layer UVScale parameters, so it is not one of these masters.");
		return 0;
	}

	// An open material instance editor keeps showing what it read when it opened, so writing behind
	// it leaves the wrong numbers on screen and the next save there puts them back.
	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Instance);
	}

	const FScopedTransaction Transaction(LOCTEXT("UVScaleTransaction", "Fit UV Scale To Landscape"));
	Instance->Modify();

	int32 Written = 0;
	for (const FName& Layer : Layers)
	{
		const float* Override = Options.PerLayerTileSize.Find(Layer);
		const float TileM = FMath::Max(Override ? *Override : Options.TileSize, 0.01f);

		// UVScale is tiles per quad, so a tile spanning TileM metres is QuadCm / TileCm.
		const float Scale = QuadCm / (TileM * 100.f);

		Instance->SetScalarParameterValueEditorOnly(
			FMaterialParameterInfo(FName(*(Layer.ToString() + UVScaleSuffix))), Scale);
		++Written;
	}

	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);
	return Written;
}

FText FMobUVScaleWindow::Summary(const UMobUVScaleOptions& Options)
{
	const float QuadCm = QuadSizeCm(Options.Landscape);
	if (QuadCm <= 0.f)
	{
		return LOCTEXT("UVScaleSummaryNoLandscape",
			"Select a landscape in the level, or pick one above.");
	}

	const TArray<FName> Layers = LayerNames(Options.MaterialInstance);
	if (Layers.Num() == 0)
	{
		return FText::Format(LOCTEXT("UVScaleSummaryNoLayers",
			"One quad is {0} cm. Pick a material instance with layer UVScale parameters."),
			FText::AsNumber(QuadCm));
	}

	const float TileM = FMath::Max(Options.TileSize, 0.01f);
	return FText::Format(LOCTEXT("UVScaleSummary",
		"One quad is {0} cm, so a {1} m tile is UVScale {2}. Writing {3} layer(s)."),
		FText::AsNumber(QuadCm),
		FText::AsNumber(TileM),
		FText::AsNumber(QuadCm / (TileM * 100.f)),
		FText::AsNumber(Layers.Num()));
}

void FMobUVScaleWindow::Open()
{
	UMobUVScaleOptions* Options = GetMutableDefault<UMobUVScaleOptions>();
	Options->Landscape = FindLandscape();
	Options->MaterialInstance = FindInstance(Options->Landscape);

	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	const TSharedRef<IDetailsView> Details =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
			.CreateDetailView(Args);
	Details->SetObject(Options);

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("UVScaleWindowTitle", "Fit UV Scale To Landscape"))
		.ClientSize(FVector2D(560.f, 460.f))
		.SupportsMaximize(false)
		.SupportsMinimize(false);

	TWeakPtr<SWindow> WeakWindow = Window;

	Window->SetContent(
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.Padding(8.f)
		.AutoHeight()
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("UVScaleHelp",
				"A layer's UVScale is tiles per landscape quad, so what a tile measures on the ground "
				"depends on the landscape's scale. This works it out from the landscape itself and "
				"writes every layer at once."))
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		[
			Details
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Panel")))
			.Padding(8.f)
			[
				SNew(SVerticalBox)

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([] { return Summary(*GetDefault<UMobUVScaleOptions>()); })
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				[
					SNew(SHorizontalBox)

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("UVScaleApply", "Apply"))
						.OnClicked_Lambda([Options, WeakWindow]
						{
							FText Error;
							const int32 Written = Apply(*Options, Error);
							if (Written > 0)
							{
								Options->SaveConfig();
								NotifyUVScale(FText::Format(LOCTEXT("UVScaleDone",
									"Fitted {0} layer(s) to the landscape."), FText::AsNumber(Written)), true);

								if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
								{
									Pinned->RequestDestroyWindow();
								}
							}
							else
							{
								NotifyUVScale(Error, false);
							}
							return FReply::Handled();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("UVScaleCancel", "Cancel"))
						.OnClicked_Lambda([WeakWindow]
						{
							if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
							{
								Pinned->RequestDestroyWindow();
							}
							return FReply::Handled();
						})
					]
				]
			]
		]);

	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
