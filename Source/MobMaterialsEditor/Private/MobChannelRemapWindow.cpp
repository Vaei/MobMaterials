// Copyright (c) Jared Taylor

#include "MobChannelRemapWindow.h"

#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "IDetailsView.h"
#include "ImageCore.h"
#include "PackageTools.h"
#include "PropertyEditorModule.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Misc/PackageName.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SWindow.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "MobMaterialsEditor"

namespace
{
	void NotifyRemap(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 5.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** The source's mip 0 as linear 8 bit BGRA, whatever it was authored as. */
	bool ReadImage(UTexture2D& Texture, FImage& OutImage, FText& OutError)
	{
		FImage Raw;
		if (!Texture.Source.IsValid() || !Texture.Source.GetMipImage(Raw, 0))
		{
			OutError = FText::Format(LOCTEXT("RemapNoSource",
				"{0} has no source art to read. A texture built without its source cannot be repacked."),
				FText::FromString(Texture.GetName()));
			return false;
		}

		// Linear throughout: a mask's numbers are the numbers, and an sRGB decode would bend them.
		Raw.CopyTo(OutImage, ERawImageFormat::BGRA8, EGammaSpace::Linear);
		return true;
	}

	uint8 ChannelOf(const FColor& Pixel, EMobTextureChannel Channel)
	{
		switch (Channel)
		{
		case EMobTextureChannel::G:	return Pixel.G;
		case EMobTextureChannel::B:	return Pixel.B;
		case EMobTextureChannel::A:	return Pixel.A;
		default:					return Pixel.R;
		}
	}

	/** Every distinct texture the three slots read, so sizes can be checked once. */
	void GatherSources(const UMobChannelRemapOptions& Options, TArray<UTexture2D*>& Out)
	{
		for (const FMobChannelSource* Slot : { &Options.Red, &Options.Green, &Options.Blue })
		{
			if (Slot->Texture)
			{
				Out.AddUnique(Slot->Texture.Get());
			}
		}
	}
}

void UMobChannelRemapOptions::ApplyPreset()
{
	auto Slot = [](UTexture2D* Texture, EMobTextureChannel Channel, float Constant)
	{
		FMobChannelSource Source;
		Source.Texture = Texture;
		Source.Channel = Channel;
		Source.Constant = Constant;
		return Source;
	};

	// Which of the incoming channels holds what, per layout.
	UTexture2D* Rough = nullptr;
	UTexture2D* Metal = nullptr;
	UTexture2D* Occlusion = nullptr;
	EMobTextureChannel RoughChannel = EMobTextureChannel::G;
	EMobTextureChannel MetalChannel = EMobTextureChannel::B;
	EMobTextureChannel OcclusionChannel = EMobTextureChannel::R;

	switch (Source)
	{
	case EMobRemapSource::ORM:
		Rough = Metal = Occlusion = PackedTexture;
		RoughChannel = EMobTextureChannel::G;
		MetalChannel = EMobTextureChannel::B;
		OcclusionChannel = EMobTextureChannel::R;
		break;

	case EMobRemapSource::MRAO:
		Rough = Metal = Occlusion = PackedTexture;
		RoughChannel = EMobTextureChannel::G;
		MetalChannel = EMobTextureChannel::R;
		OcclusionChannel = EMobTextureChannel::B;
		break;

	case EMobRemapSource::RMA:
		Rough = Metal = Occlusion = PackedTexture;
		RoughChannel = EMobTextureChannel::R;
		MetalChannel = EMobTextureChannel::G;
		OcclusionChannel = EMobTextureChannel::B;
		break;

	case EMobRemapSource::Separate:
		Rough = RoughnessTexture;
		Metal = MetallicTexture;
		Occlusion = OcclusionTexture;
		RoughChannel = EMobTextureChannel::R;
		MetalChannel = EMobTextureChannel::R;
		OcclusionChannel = EMobTextureChannel::R;
		break;

	default:
		return;
	}

	Green = Slot(Rough, RoughChannel, 0.5f);

	// Cavity is what these materials want and occlusion is what the art usually ships. They are not
	// the same signal, so it is wired but said out loud rather than quietly substituted.
	UTexture2D* Cavity = Source == EMobRemapSource::Separate ? CavityTexture.Get() : nullptr;
	EMobTextureChannel CavityChannel = EMobTextureChannel::R;

	bCavityFromOcclusion = false;
	if (!Cavity && Occlusion)
	{
		Cavity = Occlusion;
		CavityChannel = OcclusionChannel;
		bCavityFromOcclusion = true;
	}

	if (Target == EMobRemapTarget::HRC)
	{
		// No common packed layout carries height at all, so it stays a constant unless given a map.
		Red = Slot(Source == EMobRemapSource::Separate ? HeightTexture.Get() : nullptr,
			EMobTextureChannel::R, 0.5f);
		Blue = Slot(Cavity, CavityChannel, 1.f);
	}
	else
	{
		Red = Slot(Cavity, CavityChannel, 1.f);
		Blue = Slot(Metal, MetalChannel, 0.f);
	}
}

#if WITH_EDITOR
void UMobChannelRemapOptions::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	static const TSet<FName> Triggers =
	{
		GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, Source),
		GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, Target),
		GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, PackedTexture),
		GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, RoughnessTexture),
		GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, MetallicTexture),
		GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, HeightTexture),
		GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, CavityTexture),
	};

	if (Triggers.Contains(PropertyChangedEvent.GetPropertyName()))
	{
		ApplyPreset();
		return;
	}

	// Editing the cavity slot by hand makes the substitution yours, so stop describing it as ours.
	const FName Edited = PropertyChangedEvent.GetMemberPropertyName();
	const FName CavitySlot = Target == EMobRemapTarget::HRC
		? GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, Blue)
		: GET_MEMBER_NAME_CHECKED(UMobChannelRemapOptions, Red);

	if (Edited == CavitySlot)
	{
		bCavityFromOcclusion = false;
	}
}
#endif

FString FMobChannelRemapWindow::TargetSuffix(EMobRemapTarget Target)
{
	switch (Target)
	{
	case EMobRemapTarget::HRC:	return TEXT("HRC");
	case EMobRemapTarget::CRM:	return TEXT("CRM");
	default:					return TEXT("Packed");
	}
}

bool FMobChannelRemapWindow::IsIdentityCopy(const UMobChannelRemapOptions& Options, FText& OutReason)
{
	const FMobChannelSource& R = Options.Red;
	const FMobChannelSource& G = Options.Green;
	const FMobChannelSource& B = Options.Blue;

	const bool bSameTexture = R.Texture && R.Texture == G.Texture && G.Texture == B.Texture;
	const bool bInPlace = R.Channel == EMobTextureChannel::R
		&& G.Channel == EMobTextureChannel::G
		&& B.Channel == EMobTextureChannel::B;
	const bool bUntouched = !R.bInvert && !G.bInvert && !B.bInvert;

	if (!bSameTexture || !bInPlace || !bUntouched)
	{
		return false;
	}

	OutReason = FText::Format(LOCTEXT("RemapIdentity",
		"Every channel reads its own channel of {0}, so this would write a second, identical asset "
		"and change nothing. ORM is already laid out as CRM: occlusion sits where cavity does, and "
		"roughness and metallic are where they belong. Use {0} as it is - set its compression to "
		"Masks with sRGB off if it is not already - or point a channel somewhere else."),
		FText::FromString(R.Texture->GetName()));
	return true;
}

FText FMobChannelRemapWindow::Warning(const UMobChannelRemapOptions& Options)
{
	FText Reason;
	if (IsIdentityCopy(Options, Reason))
	{
		return Reason;
	}

	if (Options.bCavityFromOcclusion)
	{
		return LOCTEXT("RemapOcclusionAsCavity",
			"Ambient occlusion is going into the cavity channel. They are not the same signal: "
			"occlusion is broad contact shading, cavity is the crevice detail that multiplies base "
			"colour and specular. These materials have no AO input on purpose, because the renderer "
			"supplies its own and a baked one on top darkens twice - so the result will read heavier "
			"than a real cavity map. It is the best that art without one can do; author a cavity map "
			"if the surface matters.");
	}

	return FText::GetEmpty();
}

UTexture2D* FMobChannelRemapWindow::Remap(const UMobChannelRemapOptions& Options, FText& OutError)
{
	TArray<UTexture2D*> Sources;
	GatherSources(Options, Sources);

	if (Sources.Num() == 0)
	{
		OutError = LOCTEXT("RemapNoTextures", "Give at least one channel a texture to read.");
		return nullptr;
	}

	if (IsIdentityCopy(Options, OutError))
	{
		return nullptr;
	}

	// Every slot indexes the same pixel, so a mismatch has no correct answer.
	TMap<UTexture2D*, FImage> Images;
	int32 SizeX = 0;
	int32 SizeY = 0;

	for (UTexture2D* Texture : Sources)
	{
		FImage Image;
		if (!ReadImage(*Texture, Image, OutError))
		{
			return nullptr;
		}

		if (SizeX == 0)
		{
			SizeX = Image.SizeX;
			SizeY = Image.SizeY;
		}
		else if (Image.SizeX != SizeX || Image.SizeY != SizeY)
		{
			OutError = FText::Format(LOCTEXT("RemapSizeMismatch",
				"{0} is {1}x{2} where the others are {3}x{4}. Resize it first: packing reads the same "
				"pixel out of each source."),
				FText::FromString(Texture->GetName()),
				FText::AsNumber(Image.SizeX), FText::AsNumber(Image.SizeY),
				FText::AsNumber(SizeX), FText::AsNumber(SizeY));
			return nullptr;
		}

		Images.Add(Texture, MoveTemp(Image));
	}

	FString Name = Options.OutputName;
	if (Name.IsEmpty())
	{
		// The first source's name with a recognised map suffix swapped for this one.
		Name = Sources[0]->GetName();
		for (const TCHAR* Suffix : { TEXT("_ORM"), TEXT("_MRAO"), TEXT("_RMA"), TEXT("_Roughness"),
			TEXT("_Metallic"), TEXT("_Height"), TEXT("_Cavity"), TEXT("_AO"), TEXT("_OcclusionRoughnessMetallic") })
		{
			if (Name.EndsWith(Suffix))
			{
				Name.LeftChopInline(FCString::Strlen(Suffix));
				break;
			}
		}
		Name += TEXT("_") + TargetSuffix(Options.Target);
	}

	const FString Folder = Options.OutputPath.Path.IsEmpty() ? TEXT("/Game") : Options.OutputPath.Path;
	const FString PackageName = Folder / Name;

	UPackage* Package = CreatePackage(*PackageName);
	if (!Package)
	{
		OutError = LOCTEXT("RemapNoPackage", "Could not create the package. Check the output path.");
		return nullptr;
	}
	Package->FullyLoad();

	// Overwritten in place when it already exists, so every material pointing at it keeps working.
	UTexture2D* Result = FindObject<UTexture2D>(Package, *Name);
	if (!Result)
	{
		Result = NewObject<UTexture2D>(Package, *Name, RF_Public | RF_Standalone);
	}
	if (!Result)
	{
		OutError = LOCTEXT("RemapNoTexture", "Could not create the texture asset.");
		return nullptr;
	}

	TArray64<uint8> Packed;
	Packed.SetNumUninitialized(static_cast<int64>(SizeX) * SizeY * 4);

	const FMobChannelSource* Slots[3] = { &Options.Blue, &Options.Green, &Options.Red };

	for (int64 Pixel = 0; Pixel < static_cast<int64>(SizeX) * SizeY; ++Pixel)
	{
		// BGRA on the wire, so the slots are walked blue first.
		for (int32 Component = 0; Component < 3; ++Component)
		{
			const FMobChannelSource& Slot = *Slots[Component];

			uint8 Value;
			if (const FImage* Image = Slot.Texture ? Images.Find(Slot.Texture.Get()) : nullptr)
			{
				Value = ChannelOf(Image->AsBGRA8()[Pixel], Slot.Channel);
			}
			else
			{
				Value = static_cast<uint8>(FMath::Clamp(Slot.Constant, 0.f, 1.f) * 255.f + 0.5f);
			}

			Packed[Pixel * 4 + Component] = Slot.bInvert ? 255 - Value : Value;
		}
		Packed[Pixel * 4 + 3] = 255;
	}

	Result->Source.Init(SizeX, SizeY, 1, 1, TSF_BGRA8, Packed.GetData());

	// A mask holds numbers, not colour: no sRGB curve, and a compression that keeps the channels apart.
	Result->CompressionSettings = TC_Masks;
	Result->SRGB = false;
	Result->LODGroup = TEXTUREGROUP_World;
	Result->PostEditChange();
	Result->MarkPackageDirty();

	FAssetRegistryModule::AssetCreated(Result);
	UEditorLoadingAndSavingUtils::SavePackages({ Package }, false);
	return Result;
}

FText FMobChannelRemapWindow::Summary(const UMobChannelRemapOptions& Options)
{
	TArray<UTexture2D*> Sources;
	GatherSources(Options, Sources);

	if (Sources.Num() == 0)
	{
		return LOCTEXT("RemapSummaryEmpty",
			"Every channel is a constant. Give at least one of them a texture.");
	}

	auto Describe = [](const FMobChannelSource& Slot)
	{
		if (!Slot.Texture)
		{
			return FString::Printf(TEXT("%.2f"), Slot.Constant);
		}
		const TCHAR* Channel = TEXT("R");
		switch (Slot.Channel)
		{
		case EMobTextureChannel::G: Channel = TEXT("G"); break;
		case EMobTextureChannel::B: Channel = TEXT("B"); break;
		case EMobTextureChannel::A: Channel = TEXT("A"); break;
		default: break;
		}
		return FString::Printf(TEXT("%s%s.%s"), Slot.bInvert ? TEXT("1-") : TEXT(""),
			*Slot.Texture->GetName(), Channel);
	};

	return FText::Format(LOCTEXT("RemapSummary", "R = {0}\nG = {1}\nB = {2}"),
		FText::FromString(Describe(Options.Red)),
		FText::FromString(Describe(Options.Green)),
		FText::FromString(Describe(Options.Blue)));
}

void FMobChannelRemapWindow::Open()
{
	UMobChannelRemapOptions* Options = GetMutableDefault<UMobChannelRemapOptions>();

	// A texture picked in the Content Browser is almost always the one being repacked.
	TArray<FAssetData> Selected;
	FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
		.Get().GetSelectedAssets(Selected);

	for (const FAssetData& Asset : Selected)
	{
		if (UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset()))
		{
			Options->PackedTexture = Texture;
			Options->ApplyPreset();
			break;
		}
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
		.Title(LOCTEXT("RemapWindowTitle", "Remap Texture Channels"))
		.ClientSize(FVector2D(620.f, 720.f))
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
			.Text(LOCTEXT("RemapHelp",
				"Packs three channels into one mask texture. Pick how the incoming art is laid out and "
				"the three slots fill themselves; change any of them afterwards for a layout not listed. "
				"The result is written linear, as Masks, and overwrites an existing asset of the same "
				"name in place."))
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
					SNew(SHorizontalBox)
					.Visibility_Lambda([]
					{
						return Warning(*GetDefault<UMobChannelRemapOptions>()).IsEmpty()
							? EVisibility::Collapsed : EVisibility::Visible;
					})

					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Top)
					.Padding(0.f, 0.f, 6.f, 0.f)
					[
						SNew(SImage).Image(FAppStyle::GetBrush(TEXT("Icons.Warning")))
					]

					+ SHorizontalBox::Slot()
					.FillWidth(1.f)
					[
						SNew(STextBlock)
						.AutoWrapText(true)
						.Text_Lambda([] { return Warning(*GetDefault<UMobChannelRemapOptions>()); })
					]
				]

				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.f, 0.f, 0.f, 8.f)
				[
					SNew(STextBlock)
					.AutoWrapText(true)
					.ColorAndOpacity(FSlateColor::UseSubduedForeground())
					.Text_Lambda([] { return Summary(*GetDefault<UMobChannelRemapOptions>()); })
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
						.Text(LOCTEXT("RemapApply", "Remap"))
						.OnClicked_Lambda([Options, WeakWindow]
						{
							FText Error;
							if (const UTexture2D* Result = Remap(*Options, Error))
							{
								Options->SaveConfig();
								NotifyRemap(FText::Format(LOCTEXT("RemapDone", "Packed {0}."),
									FText::FromString(Result->GetName())), true);

								if (const TSharedPtr<SWindow> Pinned = WeakWindow.Pin())
								{
									Pinned->RequestDestroyWindow();
								}
							}
							else
							{
								NotifyRemap(Error, false);
							}
							return FReply::Handled();
						})
					]

					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SButton)
						.Text(LOCTEXT("RemapCancel", "Cancel"))
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
