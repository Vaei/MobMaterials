// Copyright (c) Jared Taylor

#include "MobSimplifyWindow.h"

#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "MaterialEditingLibrary.h"
#include "PropertyEditorModule.h"
#include "ScopedTransaction.h"
#include "Dom/JsonObject.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "Styling/AppStyle.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobMaterialsEditor"

namespace
{
	const TCHAR* UVScale = TEXT("_UVScale");

	/** The globals that draw over whatever layer is being looked at. */
	const TCHAR* OverlayAmounts[] = { TEXT("SlopeRock_Amount"), TEXT("Moss_Amount"), TEXT("Wetness_Amount") };

	/** Per layer grade, and what each reads as when it is doing nothing. */
	const TPair<const TCHAR*, float> GradeNeutral[] =
	{
		{ TEXT("_HueShift"), 0.f }, { TEXT("_Saturation"), 1.f }, { TEXT("_Value"), 1.f },
		{ TEXT("_Contrast"), 1.f }, { TEXT("_TintAmount"), 0.f }, { TEXT("_MacroValueAmount"), 0.f },
	};

	const TCHAR* TilingSwitches[] = { TEXT("_HexTiling"), TEXT("_DualScale") };
	const TCHAR* LayerTextures[] = { TEXT("_BC"), TEXT("_NRM"), TEXT("_HRC") };

	void NotifySimplify(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 5.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** PostEditChange alone leaves the static permutation alone, so a switch change never reaches a shader. */
	void Rebuild(UMaterialInstanceConstant& Instance)
	{
		UMaterialEditingLibrary::UpdateMaterialInstance(&Instance);
	}

	/** Records a value before it is written over, once. Later writes to the same name are ignored. */
	struct FSnapshot
	{
		TSharedPtr<FJsonObject> Scalars = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Switches = MakeShared<FJsonObject>();
		TSharedPtr<FJsonObject> Textures = MakeShared<FJsonObject>();

		void Scalar(UMaterialInstanceConstant& Instance, FName Name, float Value)
		{
			const FString Key = Name.ToString();
			float Existing = 0.f;
			if (!Scalars->HasField(Key) && Instance.GetScalarParameterValue(Name, Existing))
			{
				Scalars->SetNumberField(Key, Existing);
			}
			Instance.SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Name), Value);
		}

		void Switch(UMaterialInstanceConstant& Instance, FName Name, bool bValue)
		{
			const FString Key = Name.ToString();
			bool bExisting = false;
			FGuid Guid;
			if (!Switches->HasField(Key)
				&& Instance.GetStaticSwitchParameterValue(FHashedMaterialParameterInfo(Name), bExisting, Guid))
			{
				Switches->SetBoolField(Key, bExisting);
			}
			Instance.SetStaticSwitchParameterValueEditorOnly(FMaterialParameterInfo(Name), bValue);
		}

		void Texture(UMaterialInstanceConstant& Instance, FName Name, UTexture* Value)
		{
			const FString Key = Name.ToString();
			UTexture* Existing = nullptr;
			if (!Textures->HasField(Key) && Instance.GetTextureParameterValue(Name, Existing))
			{
				Textures->SetStringField(Key, Existing ? FSoftObjectPath(Existing).ToString() : FString());
			}
			Instance.SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Name), Value);
		}

		FString ToJson() const
		{
			const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
			Root->SetObjectField(TEXT("Scalars"), Scalars);
			Root->SetObjectField(TEXT("Switches"), Switches);
			Root->SetObjectField(TEXT("Textures"), Textures);

			FString Out;
			const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Out);
			FJsonSerializer::Serialize(Root, Writer);
			return Out;
		}
	};
}

TArray<FName> FMobSimplifyWindow::LayerNames(const UMaterialInstanceConstant* Instance)
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
		if (Name.EndsWith(UVScale))
		{
			Names.AddUnique(FName(*Name.LeftChop(FCString::Strlen(UVScale))));
		}
	}

	Names.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	return Names;
}

bool FMobSimplifyWindow::HasSnapshot(const UMaterialInstanceConstant* Instance)
{
	return Instance
		&& GetDefault<UMobSimplifyOptions>()->Snapshots.Contains(FSoftObjectPath(Instance).ToString());
}

bool FMobSimplifyWindow::Simplify(UMobSimplifyOptions& Options, FText& OutError)
{
	UMaterialInstanceConstant* Instance = Options.MaterialInstance;
	if (!Instance)
	{
		OutError = LOCTEXT("SimplifyNoInstance", "Pick the material instance to turn down.");
		return false;
	}

	const TArray<FName> Layers = LayerNames(Instance);
	if (Layers.Num() == 0)
	{
		OutError = LOCTEXT("SimplifyNoLayers",
			"That instance has no layers, so it is not one of these landscape masters.");
		return false;
	}

	if (!Layers.Contains(Options.Layer))
	{
		OutError = LOCTEXT("SimplifyNoLayer", "Pick a layer that instance actually carries.");
		return false;
	}

	const FString Key = FSoftObjectPath(Instance).ToString();
	if (Options.Snapshots.Contains(Key))
	{
		OutError = LOCTEXT("SimplifyAlready",
			"That instance is already simplified. Restore it first, or the state it came from is lost.");
		return false;
	}

	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Instance);
	}

	const FScopedTransaction Transaction(LOCTEXT("SimplifyTransaction", "Simplify Material To Layer"));
	Instance->Modify();

	FSnapshot Snapshot;
	const FString Chosen = Options.Layer.ToString();

	for (const FName& Layer : Layers)
	{
		const FString Name = Layer.ToString();
		const bool bIsChosen = Layer == Options.Layer;

		// Weight: the chosen layer at full, everything else contributing nothing.
		Snapshot.Scalar(*Instance, FName(*(Name + TEXT("_Amount"))), bIsChosen ? 1.f : 0.f);
		Snapshot.Scalar(*Instance, FName(*(Name + TEXT("_SlopeAmount"))), 0.f);
		Snapshot.Scalar(*Instance, FName(*(Name + TEXT("_AltitudeAmount"))), 0.f);

		if (Options.bDisableTilingBreak)
		{
			for (const TCHAR* Suffix : TilingSwitches)
			{
				Snapshot.Switch(*Instance, FName(*(Name + Suffix)), false);
			}
		}

		if (Options.bNeutraliseGrade)
		{
			for (const TPair<const TCHAR*, float>& Grade : GradeNeutral)
			{
				Snapshot.Scalar(*Instance, FName(*(Name + Grade.Key)), Grade.Value);
			}
		}

		// Every layer wearing the chosen one's textures is what makes unpainted ground show it too.
		if (Options.bShowEverywhere && !bIsChosen)
		{
			for (const TCHAR* Suffix : LayerTextures)
			{
				UTexture* Source = nullptr;
				Instance->GetTextureParameterValue(FName(*(Chosen + Suffix)), Source);
				Snapshot.Texture(*Instance, FName(*(Name + Suffix)), Source);
			}

			// And wearing its tiling, or the same art reads at a different size per layer.
			float Scale = 0.25f;
			Instance->GetScalarParameterValue(FName(*(Chosen + UVScale)), Scale);
			Snapshot.Scalar(*Instance, FName(*(Name + UVScale)), Scale);
		}
	}

	if (Options.bDisableOverlays)
	{
		for (const TCHAR* Name : OverlayAmounts)
		{
			Snapshot.Scalar(*Instance, FName(Name), 0.f);
		}
	}

	Options.Snapshots.Add(Key, Snapshot.ToJson());
	Options.SaveConfig();

	Rebuild(*Instance);
	return true;
}

bool FMobSimplifyWindow::Restore(UMobSimplifyOptions& Options, FText& OutError)
{
	UMaterialInstanceConstant* Instance = Options.MaterialInstance;
	if (!Instance)
	{
		OutError = LOCTEXT("RestoreNoInstance", "Pick the material instance to put back.");
		return false;
	}

	const FString Key = FSoftObjectPath(Instance).ToString();
	const FString* Json = Options.Snapshots.Find(Key);
	if (!Json)
	{
		OutError = LOCTEXT("RestoreNoSnapshot",
			"Nothing recorded for that instance, so there is nothing to put back.");
		return false;
	}

	TSharedPtr<FJsonObject> Root;
	const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(*Json);
	if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
	{
		OutError = LOCTEXT("RestoreBadSnapshot", "The recorded state could not be read.");
		return false;
	}

	if (GEditor)
	{
		GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->CloseAllEditorsForAsset(Instance);
	}

	const FScopedTransaction Transaction(LOCTEXT("RestoreTransaction", "Restore Material"));
	Instance->Modify();

	if (const TSharedPtr<FJsonObject>* Scalars; Root->TryGetObjectField(TEXT("Scalars"), Scalars))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Scalars)->Values)
		{
			Instance->SetScalarParameterValueEditorOnly(
				FMaterialParameterInfo(FName(*Pair.Key)), Pair.Value->AsNumber());
		}
	}

	if (const TSharedPtr<FJsonObject>* Switches; Root->TryGetObjectField(TEXT("Switches"), Switches))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Switches)->Values)
		{
			Instance->SetStaticSwitchParameterValueEditorOnly(
				FMaterialParameterInfo(FName(*Pair.Key)), Pair.Value->AsBool());
		}
	}

	if (const TSharedPtr<FJsonObject>* Textures; Root->TryGetObjectField(TEXT("Textures"), Textures))
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*Textures)->Values)
		{
			const FString Path = Pair.Value->AsString();
			UTexture* Texture = Path.IsEmpty() ? nullptr : Cast<UTexture>(FSoftObjectPath(Path).TryLoad());
			Instance->SetTextureParameterValueEditorOnly(
				FMaterialParameterInfo(FName(*Pair.Key)), Texture);
		}
	}

	Options.Snapshots.Remove(Key);
	Options.SaveConfig();

	Rebuild(*Instance);
	return true;
}

FText FMobSimplifyWindow::Summary(const UMobSimplifyOptions& Options)
{
	if (!Options.MaterialInstance)
	{
		return LOCTEXT("SimplifySummaryNone", "Select a landscape material instance.");
	}

	if (HasSnapshot(Options.MaterialInstance))
	{
		return LOCTEXT("SimplifySummarySimplified",
			"This instance is simplified. Restore puts back exactly what it held before.");
	}

	const TArray<FName> Layers = LayerNames(Options.MaterialInstance);
	return FText::Format(LOCTEXT("SimplifySummary",
		"{0} layer(s). Everything not named is turned off, and what is turned off is recorded so it "
		"can be put back."), FText::AsNumber(Layers.Num()));
}

void FMobSimplifyWindow::Open()
{
	UMobSimplifyOptions* Options = GetMutableDefault<UMobSimplifyOptions>();

	TArray<FAssetData> Selected;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
		.Get().GetSelectedAssets(Selected);

	for (const FAssetData& Asset : Selected)
	{
		if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(Asset.GetAsset()))
		{
			Options->MaterialInstance = Instance;
			break;
		}
	}

	if (const TArray<FName> Layers = LayerNames(Options->MaterialInstance);
		Layers.Num() > 0 && !Layers.Contains(Options->Layer))
	{
		Options->Layer = Layers[0];
	}

	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.bHideSelectionTip = true;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;

	const TSharedRef<IDetailsView> Details =
		FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"))
			.CreateDetailView(Args);
	Details->SetObject(Options);

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("SimplifyWindowTitle", "Simplify Material To Layer"))
		.ClientSize(FVector2D(560.f, 480.f))
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
			.Text(LOCTEXT("SimplifyHelp",
				"Turns the material down to one layer, so what is on screen is that layer's art and "
				"nothing else. Everything it changes is recorded first, and Restore puts back exactly "
				"what was there rather than the master's defaults."))
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
					.Text_Lambda([] { return Summary(*GetDefault<UMobSimplifyOptions>()); })
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
						.Text(LOCTEXT("SimplifyApply", "Simplify"))
						.IsEnabled_Lambda([Options]
						{
							return !HasSnapshot(Options->MaterialInstance);
						})
						.OnClicked_Lambda([Options, WeakWindow]
						{
							FText Error;
							if (Simplify(*Options, Error))
							{
								NotifySimplify(FText::Format(LOCTEXT("SimplifyDone",
									"Simplified to {0}."), FText::FromName(Options->Layer)), true);

								if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
								{
									Pinned->RequestDestroyWindow();
								}
							}
							else
							{
								NotifySimplify(Error, false);
							}
							return FReply::Handled();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(0.f, 0.f, 8.f, 0.f)
					[
						SNew(SButton)
						.Text(LOCTEXT("SimplifyRestore", "Restore"))
						.IsEnabled_Lambda([Options]
						{
							return HasSnapshot(Options->MaterialInstance);
						})
						.OnClicked_Lambda([Options, WeakWindow]
						{
							FText Error;
							if (Restore(*Options, Error))
							{
								NotifySimplify(LOCTEXT("RestoreDone", "Material restored."), true);

								if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
								{
									Pinned->RequestDestroyWindow();
								}
							}
							else
							{
								NotifySimplify(Error, false);
							}
							return FReply::Handled();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("SimplifyCancel", "Cancel"))
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
