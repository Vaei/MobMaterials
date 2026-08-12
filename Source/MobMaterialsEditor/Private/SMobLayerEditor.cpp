// Copyright (c) Jared Taylor

#include "SMobLayerEditor.h"

#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "MaterialEditingLibrary.h"
#include "MobLevelTools.h"
#include "MobSimplifyWindow.h"
#include "AssetThumbnail.h"
#include "PropertyCustomizationHelpers.h"
#include "ScopedTransaction.h"
#include "Misc/MessageDialog.h"
#include "Engine/Texture.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialParameters.h"
#include "Styling/AppStyle.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Colors/SColorPicker.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobMaterialsEditor"

namespace
{
	/** The three a layer carries, in the order the content browser sorts them. */
	const TCHAR* LayerSlots[] = { TEXT("_BC"), TEXT("_HRC"), TEXT("_NRM") };

	const float LabelWidth = 190.f;

	void NotifyLayerEditor(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 4.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** Everything of one type the instance carries, with the metadata that says where it belongs. */
	TMap<FMaterialParameterInfo, FMaterialParameterMetadata> ParametersOfType(
		const UMaterialInstanceConstant* Instance, EMaterialParameterType Type)
	{
		TMap<FMaterialParameterInfo, FMaterialParameterMetadata> Out;
		if (Instance)
		{
			Instance->GetAllParametersOfType(Type, Out);
		}
		return Out;
	}

	/** The parameters of one type in a group, ordered the way the master asked for. */
	TArray<TPair<FName, FMaterialParameterMetadata>> GroupedParameters(
		const UMaterialInstanceConstant* Instance, EMaterialParameterType Type, FName Group)
	{
		TArray<TPair<FName, FMaterialParameterMetadata>> Out;
		for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair
			: ParametersOfType(Instance, Type))
		{
			if (Pair.Value.Group == Group)
			{
				Out.Emplace(Pair.Key.Name, Pair.Value);
			}
		}

		Out.Sort([](const TPair<FName, FMaterialParameterMetadata>& A,
			const TPair<FName, FMaterialParameterMetadata>& B)
		{
			return A.Value.SortPriority != B.Value.SortPriority
				? A.Value.SortPriority < B.Value.SortPriority
				: A.Key.LexicalLess(B.Key);
		});
		return Out;
	}

	/** A parameter name with its group prefix taken off, since the tab already says which layer. */
	FText ShortLabel(FName Parameter, FName Group)
	{
		FString Name = Parameter.ToString();
		const FString Prefix = Group.ToString() + TEXT("_");
		if (Name.StartsWith(Prefix))
		{
			Name.RightChopInline(Prefix.Len());
		}
		return FText::FromString(FName::NameToDisplayString(Name, false));
	}

	/** Live update without a recompile. Uniform values do not need the shader rebuilt. */
	void RefreshValues(UMaterialInstanceConstant* Instance)
	{
		if (Instance)
		{
			Instance->RecacheUniformExpressions(false);
		}
	}
}

void SMobLayerEditor::Construct(const FArguments& InArgs)
{
	ChildSlot
	[
		SNew(SVerticalBox)

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 8.f, 8.f, 4.f)
		[
			SNew(STextBlock)
			.Text_Lambda([this]
			{
				const UMaterialInstanceConstant* Current = Instance.Get();
				return Current ? FText::FromString(Current->GetName())
					: LOCTEXT("NoInstance", "No material instance");
			})
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
		]

		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8.f, 0.f, 8.f, 4.f)
		[
			SAssignNew(TabStrip, SWrapBox)
			.UseAllottedSize(true)
		]

		+ SVerticalBox::Slot()
		.FillHeight(1.f)
		.Padding(4.f)
		[
			SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush(TEXT("Brushes.Recessed")))
			.Padding(4.f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(Body, SVerticalBox)
				]
			]
		]
	];

	ThumbnailPool = MakeShared<FAssetThumbnailPool>(24);

	SetInstance(InArgs._Instance);
}

void SMobLayerEditor::SetInstance(UMaterialInstanceConstant* InInstance)
{
	Instance = InInstance;
	GatherTabs();
	ActiveTab = Tabs.Num() > 0 ? Tabs[0] : NAME_None;

	RebuildTabStrip();
	RebuildBody();
}

void SMobLayerEditor::GatherTabs()
{
	Tabs.Reset();

	const UMaterialInstanceConstant* Current = Instance.Get();
	if (!Current)
	{
		return;
	}

	// A group holding a UVScale is a paint layer. Everything else is a system, and the two want
	// different orders: layers as the terrain blends them, systems alphabetically.
	TSet<FName> Layers;
	TSet<FName> Systems;
	for (int32 Type = 0; Type < static_cast<int32>(NumMaterialParameterTypes); ++Type)
	{
		for (const TPair<FMaterialParameterInfo, FMaterialParameterMetadata>& Pair
			: ParametersOfType(Current, static_cast<EMaterialParameterType>(Type)))
		{
			if (Pair.Value.Group.IsNone())
			{
				continue;
			}

			const FString Name = Pair.Key.Name.ToString();
			if (Name == Pair.Value.Group.ToString() + TEXT("_UVScale"))
			{
				Layers.Add(Pair.Value.Group);
			}
			else
			{
				Systems.Add(Pair.Value.Group);
			}
		}
	}

	Systems = Systems.Difference(Layers);

	Tabs = Layers.Array();
	Tabs.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });

	TArray<FName> Rest = Systems.Array();
	Rest.Sort([](const FName& A, const FName& B) { return A.LexicalLess(B); });
	Tabs.Append(Rest);
}

void SMobLayerEditor::RebuildTabStrip()
{
	if (!TabStrip.IsValid())
	{
		return;
	}

	TabStrip->ClearChildren();

	for (const FName Tab : Tabs)
	{
		TabStrip->AddSlot()
		.Padding(0.f, 0.f, 4.f, 4.f)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), TEXT("PlacementBrowser.Tab"))
			.IsChecked_Lambda([this, Tab]
			{
				return ActiveTab == Tab ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Tab](ECheckBoxState)
			{
				ActiveTab = Tab;
				RebuildBody();
			})
			[
				SNew(STextBlock)
				.Margin(FMargin(10.f, 4.f))
				.Text(FText::FromName(Tab))
			]
		];
	}
}

bool SMobLayerEditor::IsOverridden(FName Parameter, EMaterialParameterType Type) const
{
	const UMaterialInstanceConstant* Current = Instance.Get();
	if (!Current)
	{
		return false;
	}

	// These arrays hold only what the instance overrides, so being in one is the whole question.
	auto Holds = [Parameter](const auto& Values)
	{
		return Values.ContainsByPredicate([Parameter](const auto& Value)
		{
			return Value.ParameterInfo.Name == Parameter;
		});
	};

	switch (Type)
	{
	case EMaterialParameterType::Scalar:		return Holds(Current->ScalarParameterValues);
	case EMaterialParameterType::Vector:		return Holds(Current->VectorParameterValues);
	case EMaterialParameterType::Texture:		return Holds(Current->TextureParameterValues);
	case EMaterialParameterType::StaticSwitch:	return Holds(Current->GetStaticParameters().StaticSwitchParameters);
	default:									return false;
	}
}

FReply SMobLayerEditor::OnResetParameter(FName Parameter, EMaterialParameterType Type)
{
	UMaterialInstanceConstant* Current = Instance.Get();
	if (!Current)
	{
		return FReply::Handled();
	}

	auto Named = [Parameter](const auto& Value) { return Value.ParameterInfo.Name == Parameter; };

	const FScopedTransaction Transaction(LOCTEXT("ResetParameter", "Reset Parameter"));
	Current->Modify();

	switch (Type)
	{
	case EMaterialParameterType::Scalar:	Current->ScalarParameterValues.RemoveAll(Named); break;
	case EMaterialParameterType::Vector:	Current->VectorParameterValues.RemoveAll(Named); break;
	case EMaterialParameterType::Texture:	Current->TextureParameterValues.RemoveAll(Named); break;
	case EMaterialParameterType::StaticSwitch:
		{
			FStaticParameterSet Statics = Current->GetStaticParameters();
			Statics.StaticSwitchParameters.RemoveAll(Named);
			Current->UpdateStaticPermutation(Statics);
			break;
		}
	default: break;
	}

	UMaterialEditingLibrary::UpdateMaterialInstance(Current);
	return FReply::Handled();
}

TSharedRef<SWidget> SMobLayerEditor::BuildResetButton(FName Parameter, EMaterialParameterType Type)
{
	// Hidden rather than collapsed, so a row with nothing to reset still lines up with the rest.
	return SNew(SBox)
		.WidthOverride(20.f)
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.ButtonStyle(FAppStyle::Get(), TEXT("SimpleButton"))
			.ContentPadding(0.f)
			.ToolTipText(LOCTEXT("ResetOne", "Put this back to the parent's value"))
			.Visibility_Lambda([this, Parameter, Type]
			{
				return IsOverridden(Parameter, Type) ? EVisibility::Visible : EVisibility::Hidden;
			})
			.OnClicked(this, &SMobLayerEditor::OnResetParameter, Parameter, Type)
			[
				SNew(SImage)
				.Image(FAppStyle::GetBrush(TEXT("PropertyWindow.DiffersFromDefault")))
				.ColorAndOpacity(FSlateColor::UseForeground())
			]
		];
}

TSharedRef<SWidget> SMobLayerEditor::BuildTextureRow(FName Parameter)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(LabelWidth)
			[
				SNew(STextBlock).Text(ShortLabel(Parameter, ActiveTab))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			BuildResetButton(Parameter, EMaterialParameterType::Texture)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		[
			SNew(SObjectPropertyEntryBox)
			.AllowedClass(UTexture::StaticClass())
			.DisplayThumbnail(true)
			.ThumbnailPool(ThumbnailPool)
			.ObjectPath_Lambda([this, Parameter]
			{
				UMaterialInstanceConstant* Current = Instance.Get();
				UTexture* Texture = nullptr;
				if (Current)
				{
					Current->GetTextureParameterValue(Parameter, Texture);
				}
				return Texture ? Texture->GetPathName() : FString();
			})
			.OnObjectChanged_Lambda([this, Parameter](const FAssetData& Asset)
			{
				UMaterialInstanceConstant* Current = Instance.Get();
				if (!Current)
				{
					return;
				}

				const FScopedTransaction Transaction(LOCTEXT("SetTexture", "Set Layer Texture"));
				Current->Modify();
				Current->SetTextureParameterValueEditorOnly(
					FMaterialParameterInfo(Parameter), Cast<UTexture>(Asset.GetAsset()));
				UMaterialEditingLibrary::UpdateMaterialInstance(Current);
			})
		];
}

TSharedRef<SWidget> SMobLayerEditor::BuildScalarRow(FName Parameter, const FMaterialParameterMetadata& Meta)
{
	// A range the master asked for is the range the slider gets; without one, a spin box that
	// keeps going, because a UV scale and a hue shift do not share sensible bounds.
	const bool bHasRange = Meta.ScalarMax > Meta.ScalarMin;
	const float Min = bHasRange ? Meta.ScalarMin : 0.f;
	const float Max = bHasRange ? Meta.ScalarMax : 0.f;

	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(LabelWidth)
			[
				SNew(STextBlock)
				.Text(ShortLabel(Parameter, ActiveTab))
				.ToolTipText(FText::FromString(Meta.Description))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			BuildResetButton(Parameter, EMaterialParameterType::Scalar)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(SSpinBox<float>)
			.MinValue(bHasRange ? TOptional<float>(Min) : TOptional<float>())
			.MaxValue(bHasRange ? TOptional<float>(Max) : TOptional<float>())
			.MinSliderValue(bHasRange ? TOptional<float>(Min) : TOptional<float>(0.f))
			.MaxSliderValue(bHasRange ? TOptional<float>(Max) : TOptional<float>(1.f))
			.Delta(0.f)
			.Value_Lambda([this, Parameter]
			{
				UMaterialInstanceConstant* Current = Instance.Get();
				float Value = 0.f;
				if (Current)
				{
					Current->GetScalarParameterValue(Parameter, Value);
				}
				return Value;
			})
			.OnValueChanged_Lambda([this, Parameter](float Value)
			{
				// Scrubbing writes straight through and refreshes the uniforms, so the viewport
				// follows the cursor without the panel being rebuilt under it.
				if (UMaterialInstanceConstant* Current = Instance.Get())
				{
					Current->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Parameter), Value);
					RefreshValues(Current);
				}
			})
			.OnValueCommitted_Lambda([this, Parameter](float Value, ETextCommit::Type)
			{
				if (UMaterialInstanceConstant* Current = Instance.Get())
				{
					const FScopedTransaction Transaction(LOCTEXT("SetScalar", "Set Parameter"));
					Current->Modify();
					Current->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(Parameter), Value);
					RefreshValues(Current);
				}
			})
		];
}

TSharedRef<SWidget> SMobLayerEditor::BuildVectorRow(FName Parameter)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(LabelWidth)
			[
				SNew(STextBlock).Text(ShortLabel(Parameter, ActiveTab))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			BuildResetButton(Parameter, EMaterialParameterType::Vector)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(SColorBlock)
			.Color_Lambda([this, Parameter]
			{
				UMaterialInstanceConstant* Current = Instance.Get();
				FLinearColor Colour = FLinearColor::Black;
				if (Current)
				{
					Current->GetVectorParameterValue(Parameter, Colour);
				}
				return Colour;
			})
			.ShowBackgroundForAlpha(false)
			.Size(FVector2D(70.f, 18.f))
			.OnMouseButtonDown_Lambda([this, Parameter](const FGeometry&, const FPointerEvent& Event)
			{
				if (Event.GetEffectingButton() != EKeys::LeftMouseButton)
				{
					return FReply::Unhandled();
				}

				UMaterialInstanceConstant* Current = Instance.Get();
				if (!Current)
				{
					return FReply::Unhandled();
				}

				FLinearColor Colour = FLinearColor::Black;
				Current->GetVectorParameterValue(Parameter, Colour);

				FColorPickerArgs Args;
				Args.bIsModal = false;
				Args.bUseAlpha = false;
				Args.InitialColor = Colour;
				Args.OnColorCommitted = FOnLinearColorValueChanged::CreateLambda(
					[this, Parameter](FLinearColor NewColour)
					{
						if (UMaterialInstanceConstant* Target = Instance.Get())
						{
							Target->SetVectorParameterValueEditorOnly(
								FMaterialParameterInfo(Parameter), NewColour);
							RefreshValues(Target);
						}
					});
				OpenColorPicker(Args);
				return FReply::Handled();
			})
		];
}

TSharedRef<SWidget> SMobLayerEditor::BuildSwitchRow(FName Parameter)
{
	return SNew(SHorizontalBox)

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(LabelWidth)
			[
				SNew(STextBlock).Text(ShortLabel(Parameter, ActiveTab))
			]
		]

		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.f, 0.f, 4.f, 0.f)
		[
			BuildResetButton(Parameter, EMaterialParameterType::StaticSwitch)
		]

		+ SHorizontalBox::Slot()
		.FillWidth(1.f)
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.ToolTipText(LOCTEXT("SwitchTip",
				"A static switch. Changing it recompiles this instance's shaders, so it is not a "
				"thing to scrub."))
			.IsChecked_Lambda([this, Parameter]
			{
				UMaterialInstanceConstant* Current = Instance.Get();
				bool bValue = false;
				FGuid Guid;
				if (Current)
				{
					Current->GetStaticSwitchParameterValue(
						FHashedMaterialParameterInfo(Parameter), bValue, Guid);
				}
				return bValue ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
			})
			.OnCheckStateChanged_Lambda([this, Parameter](ECheckBoxState State)
			{
				if (UMaterialInstanceConstant* Current = Instance.Get())
				{
					const FScopedTransaction Transaction(LOCTEXT("SetSwitch", "Set Switch"));
					Current->Modify();
					Current->SetStaticSwitchParameterValueEditorOnly(
						FMaterialParameterInfo(Parameter), State == ECheckBoxState::Checked);
					UMaterialEditingLibrary::UpdateMaterialInstance(Current);
				}
			})
		];
}

void SMobLayerEditor::RebuildBody()
{
	if (!Body.IsValid())
	{
		return;
	}

	Body->ClearChildren();

	UMaterialInstanceConstant* Current = Instance.Get();
	if (!Current || ActiveTab.IsNone())
	{
		Body->AddSlot().Padding(8.f).AutoHeight()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("PickInstance",
				"Select a material instance in the Content Browser, or open a level with a landscape."))
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
		return;
	}

	const TArray<TPair<FName, FMaterialParameterMetadata>> Textures =
		GroupedParameters(Current, EMaterialParameterType::Texture, ActiveTab);
	bActiveTabIsLayer = Textures.Num() > 0;

	auto AddRow = [this](TSharedRef<SWidget> Row)
	{
		Body->AddSlot().AutoHeight().Padding(6.f, 3.f)[Row];
	};

	auto AddHeading = [this](const FText& Text)
	{
		Body->AddSlot().AutoHeight().Padding(6.f, 10.f, 6.f, 2.f)
		[
			SNew(STextBlock)
			.Text(Text)
			.ColorAndOpacity(FSlateColor::UseSubduedForeground())
		];
	};

	if (Textures.Num() > 0)
	{
		AddHeading(LOCTEXT("TexturesHeading", "TEXTURES"));
		for (const TPair<FName, FMaterialParameterMetadata>& Entry : Textures)
		{
			AddRow(BuildTextureRow(Entry.Key));
		}

		Body->AddSlot().AutoHeight().Padding(6.f, 6.f)
		[
			SNew(SHorizontalBox)

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("SimplifyToLayer", "Simplify To This Layer"))
				.ToolTipText(LOCTEXT("SimplifyToLayerTip",
					"Turns the material down to this layer so what is on screen is its art and "
					"nothing else. Opens with this instance and this layer already chosen."))
				.OnClicked_Lambda([this]
				{
					FMobSimplifyWindow::OpenFor(Instance.Get(), ActiveTab);
					return FReply::Handled();
				})
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.f, 0.f, 6.f, 0.f)
			[
				SNew(SButton)
				.Text(LOCTEXT("ResetLayer", "Reset To Default"))
				.ToolTipText(LOCTEXT("ResetLayerTip",
					"Drops this instance's own value for everything on this tab, so each reads as the "
					"parent gives it. Textures included."))
				.OnClicked(this, &SMobLayerEditor::OnResetTab)
			]

			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
			SNew(SButton)
			.Text(LOCTEXT("AssignSelected", "Assign Selected Textures"))
			.ToolTipText(LOCTEXT("AssignSelectedTip",
				"Puts the textures selected in the Content Browser into this layer, each into the "
				"slot its name ends in. All or nothing: a name it cannot place assigns none of them."))
			.IsEnabled(this, &SMobLayerEditor::CanAssignSelected)
			.OnClicked(this, &SMobLayerEditor::OnAssignSelected)
			]
		];
	}

	const TArray<TPair<FName, FMaterialParameterMetadata>> Scalars =
		GroupedParameters(Current, EMaterialParameterType::Scalar, ActiveTab);
	if (Scalars.Num() > 0)
	{
		AddHeading(LOCTEXT("ValuesHeading", "VALUES"));
		for (const TPair<FName, FMaterialParameterMetadata>& Entry : Scalars)
		{
			AddRow(BuildScalarRow(Entry.Key, Entry.Value));
		}
	}

	const TArray<TPair<FName, FMaterialParameterMetadata>> Vectors =
		GroupedParameters(Current, EMaterialParameterType::Vector, ActiveTab);
	if (Vectors.Num() > 0)
	{
		AddHeading(LOCTEXT("ColoursHeading", "COLOURS"));
		for (const TPair<FName, FMaterialParameterMetadata>& Entry : Vectors)
		{
			AddRow(BuildVectorRow(Entry.Key));
		}
	}

	const TArray<TPair<FName, FMaterialParameterMetadata>> Switches =
		GroupedParameters(Current, EMaterialParameterType::StaticSwitch, ActiveTab);
	if (Switches.Num() > 0)
	{
		AddHeading(LOCTEXT("SwitchesHeading", "SWITCHES - EACH CHANGE RECOMPILES"));
		for (const TPair<FName, FMaterialParameterMetadata>& Entry : Switches)
		{
			AddRow(BuildSwitchRow(Entry.Key));
		}
	}
}

FReply SMobLayerEditor::OnResetTab()
{
	UMaterialInstanceConstant* Current = Instance.Get();
	if (!Current || ActiveTab.IsNone())
	{
		return FReply::Handled();
	}

	if (FMessageDialog::Open(EAppMsgType::YesNo, FText::Format(LOCTEXT("ResetTabConfirm",
		"Reset everything on {0} back to the parent's values? This instance's own textures and "
		"numbers for that tab are dropped."),
		FText::FromName(ActiveTab))) != EAppReturnType::Yes)
	{
		return FReply::Handled();
	}

	// Everything named in this group, whatever its type, since the tab is what is being reset.
	TSet<FName> Names;
	for (int32 Type = 0; Type < static_cast<int32>(NumMaterialParameterTypes); ++Type)
	{
		for (const TPair<FName, FMaterialParameterMetadata>& Entry :
			GroupedParameters(Current, static_cast<EMaterialParameterType>(Type), ActiveTab))
		{
			Names.Add(Entry.Key);
		}
	}

	auto Named = [&Names](const auto& Value) { return Names.Contains(Value.ParameterInfo.Name); };

	const FScopedTransaction Transaction(LOCTEXT("ResetTabTransaction", "Reset Layer"));
	Current->Modify();

	int32 Cleared = Current->ScalarParameterValues.RemoveAll(Named);
	Cleared += Current->VectorParameterValues.RemoveAll(Named);
	Cleared += Current->TextureParameterValues.RemoveAll(Named);

	FStaticParameterSet Statics = Current->GetStaticParameters();
	Cleared += Statics.StaticSwitchParameters.RemoveAll(Named);
	Current->UpdateStaticPermutation(Statics);

	UMaterialEditingLibrary::UpdateMaterialInstance(Current);

	NotifyLayerEditor(Cleared > 0
		? FText::Format(LOCTEXT("ResetTabDone", "Reset {0} value(s) on {1}."),
			FText::AsNumber(Cleared), FText::FromName(ActiveTab))
		: FText::Format(LOCTEXT("ResetTabNothing", "{0} already reads as its parent does."),
			FText::FromName(ActiveTab)), Cleared > 0);

	return FReply::Handled();
}

bool SMobLayerEditor::CanAssignSelected() const
{
	if (!bActiveTabIsLayer || !Instance.IsValid())
	{
		return false;
	}

	TArray<FAssetData> Selected;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
		.Get().GetSelectedAssets(Selected);

	return Selected.ContainsByPredicate([](const FAssetData& Asset)
	{
		return Asset.IsInstanceOf(UTexture::StaticClass());
	});
}

FReply SMobLayerEditor::OnAssignSelected()
{
	UMaterialInstanceConstant* Current = Instance.Get();
	if (!Current)
	{
		return FReply::Handled();
	}

	TArray<FAssetData> Selected;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
		.Get().GetSelectedAssets(Selected);

	// Resolved before anything is written, so a set with one name it cannot place leaves the layer
	// as it was rather than half assigned.
	TMap<FName, UTexture*> Resolved;
	for (const FAssetData& Asset : Selected)
	{
		UTexture* Texture = Cast<UTexture>(Asset.GetAsset());
		if (!Texture)
		{
			continue;
		}

		const FString Slot = FMobLevelTools::SlotSuffixFor(Texture->GetName());
		if (Slot.IsEmpty())
		{
			NotifyLayerEditor(FText::Format(LOCTEXT("AssignUnknownSlot",
				"{0} does not end in a channel this understands: base colour, normal or HRC."),
				FText::FromString(Texture->GetName())), false);
			return FReply::Handled();
		}

		const FName Parameter(*(ActiveTab.ToString() + Slot));
		if (UTexture** Clash = Resolved.Find(Parameter))
		{
			NotifyLayerEditor(FText::Format(LOCTEXT("AssignSlotClash", "{0} and {1} both want {2}."),
				FText::FromString((*Clash)->GetName()), FText::FromString(Texture->GetName()),
				FText::FromName(Parameter)), false);
			return FReply::Handled();
		}

		Resolved.Add(Parameter, Texture);
	}

	if (Resolved.Num() == 0)
	{
		NotifyLayerEditor(LOCTEXT("AssignNothing", "No textures selected in the Content Browser."), false);
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("AssignTransaction", "Assign Layer Textures"));
	Current->Modify();
	for (const TPair<FName, UTexture*>& Pair : Resolved)
	{
		Current->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Pair.Key), Pair.Value);
	}
	UMaterialEditingLibrary::UpdateMaterialInstance(Current);

	NotifyLayerEditor(FText::Format(LOCTEXT("AssignedToLayer", "Assigned {0} texture(s) to {1}."),
		FText::AsNumber(Resolved.Num()), FText::FromName(ActiveTab)), true);
	return FReply::Handled();
}

void SMobLayerEditor::Open()
{
	UMaterialInstanceConstant* Target = nullptr;

	TArray<FAssetData> Selected;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
		.Get().GetSelectedAssets(Selected);
	for (const FAssetData& Asset : Selected)
	{
		if (UMaterialInstanceConstant* Found = Cast<UMaterialInstanceConstant>(Asset.GetAsset()))
		{
			Target = Found;
			break;
		}
	}

	// Nothing chosen means the terrain in front of you, which is what it is nearly always for.
	if (!Target)
	{
		Target = FMobLevelTools::GetLandscapeInstance();
	}

	const TSharedRef<SWindow> Window = SNew(SWindow)
		.Title(LOCTEXT("LayerEditorTitle", "Layers"))
		.ClientSize(FVector2D(620.f, 780.f))
		.SupportsMaximize(false);

	Window->SetContent(SNew(SMobLayerEditor).Instance(Target));
	FSlateApplication::Get().AddWindow(Window);
}

#undef LOCTEXT_NAMESPACE
