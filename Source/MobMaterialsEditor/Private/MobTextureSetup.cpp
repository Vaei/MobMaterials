// Copyright (c) Jared Taylor

#include "MobTextureSetup.h"

#include "ContentBrowserModule.h"
#include "FileHelpers.h"
#include "IContentBrowserSingleton.h"
#include "ScopedTransaction.h"
#include "AssetRegistry/AssetData.h"
#include "Engine/Texture2D.h"
#include "Framework/Notifications/NotificationManager.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MobMaterialsEditor"

namespace
{
	enum class EMobTextureRole : uint8
	{
		None,
		Colour,
		Normal,
		Mask,
	};

	/** One role, and every suffix art arrives wearing for it. */
	struct FMobTextureRoleSuffixes
	{
		EMobTextureRole Role;
		TArray<FString> Endings;
	};

	const TArray<FMobTextureRoleSuffixes>& RoleSuffixes()
	{
		static const TArray<FMobTextureRoleSuffixes> Suffixes =
		{
			{ EMobTextureRole::Colour, { TEXT("_basecolor"), TEXT("_bc"), TEXT("_albedo"), TEXT("_diffuse"),
				TEXT("_color"), TEXT("_emissive") } },
			{ EMobTextureRole::Normal, { TEXT("_normal"), TEXT("_nrm"), TEXT("_n") } },
			{ EMobTextureRole::Mask, { TEXT("_hrc"), TEXT("_crm"), TEXT("_crt"), TEXT("_orm"), TEXT("_mra"),
				TEXT("_rma"), TEXT("_arm"), TEXT("_masks"), TEXT("_mask") } },
		};
		return Suffixes;
	}

	EMobTextureRole RoleFor(const FString& TextureName)
	{
		const FString Lower = TextureName.ToLower();
		for (const FMobTextureRoleSuffixes& Entry : RoleSuffixes())
		{
			const bool bMatches = Entry.Endings.ContainsByPredicate([&Lower](const FString& Ending)
			{
				return Lower.EndsWith(Ending);
			});

			if (bMatches)
			{
				return Entry.Role;
			}
		}
		return EMobTextureRole::None;
	}

	void GetSelectedTextureAssets(TArray<FAssetData>& Out)
	{
		TArray<FAssetData> Selected;
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
			.Get().GetSelectedAssets(Selected);

		for (const FAssetData& Asset : Selected)
		{
			if (Asset.IsInstanceOf(UTexture2D::StaticClass()))
			{
				Out.Add(Asset);
			}
		}
	}

	void ApplyRole(UTexture2D* Texture, EMobTextureRole Role, TextureAddress Address)
	{
		Texture->Modify();
		Texture->PreEditChange(nullptr);

		switch (Role)
		{
		case EMobTextureRole::Colour:
			Texture->SRGB = true;
			Texture->CompressionSettings = TC_Default;

			// Alpha is kept: the surface master can read it as an emissive mask.
			Texture->CompressionNoAlpha = false;
			Texture->LODGroup = TEXTUREGROUP_World;
			break;

		case EMobTextureRole::Normal:
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Normalmap;
			Texture->LODGroup = TEXTUREGROUP_WorldNormalMap;
			break;

		case EMobTextureRole::Mask:
			Texture->SRGB = false;
			Texture->CompressionSettings = TC_Masks;
			Texture->LODGroup = TEXTUREGROUP_WorldSpecular;
			break;

		default:
			break;
		}

		Texture->AddressX = Address;
		Texture->AddressY = Address;

		// Rebuilds the platform data, so the new compression and mips are what the editor shows.
		Texture->PostEditChange();
	}

	void NotifyTextureSetup(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 5.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	void SetupSelected(TextureAddress Address, const FText& Purpose)
	{
		TArray<FAssetData> Assets;
		GetSelectedTextureAssets(Assets);
		if (Assets.Num() == 0)
		{
			return;
		}

		const FScopedTransaction Transaction(LOCTEXT("TextureSetupTransaction", "Set Up Textures"));

		TArray<UPackage*> Touched;
		TArray<FString> Unclassified;
		for (const FAssetData& Asset : Assets)
		{
			UTexture2D* Texture = Cast<UTexture2D>(Asset.GetAsset());
			if (!Texture)
			{
				continue;
			}

			const EMobTextureRole Role = RoleFor(Texture->GetName());
			if (Role == EMobTextureRole::None)
			{
				Unclassified.Add(Texture->GetName());
				continue;
			}

			ApplyRole(Texture, Role, Address);
			Touched.AddUnique(Texture->GetPackage());
		}

		const bool bSaved = Touched.Num() == 0 || UEditorLoadingAndSavingUtils::SavePackages(Touched, false);

		FText Message = FText::Format(LOCTEXT("TextureSetupDone", "Set up {0} texture(s) for {1}."),
			FText::AsNumber(Touched.Num()), Purpose);

		if (!bSaved)
		{
			Message = FText::Format(LOCTEXT("TextureSetupUnsaved",
				"{0} Some could not be saved - check them out and save them, or the settings are lost on reload."),
				Message);
		}

		if (Unclassified.Num() > 0)
		{
			constexpr int32 MaxNamed = 6;
			TArray<FString> Named(Unclassified.GetData(), FMath::Min(Unclassified.Num(), MaxNamed));
			FString List = FString::Join(Named, TEXT(", "));
			if (Unclassified.Num() > MaxNamed)
			{
				List += FString::Printf(TEXT(" and %d more"), Unclassified.Num() - MaxNamed);
			}

			Message = FText::Format(LOCTEXT("TextureSetupUnclassified",
				"{0}\n{1} left untouched, no suffix says what they are: {2}"),
				Message, FText::AsNumber(Unclassified.Num()), FText::FromString(List));
		}

		NotifyTextureSetup(Message, bSaved && Touched.Num() > 0);
	}
}

void FMobTextureSetup::SetupSelectedForUniqueUVs()
{
	SetupSelected(TA_Clamp, LOCTEXT("TextureSetupUnique", "unique UVs (Clamp)"));
}

void FMobTextureSetup::SetupSelectedForTiling()
{
	SetupSelected(TA_Wrap, LOCTEXT("TextureSetupTiling", "tiling (Wrap)"));
}

bool FMobTextureSetup::CanSetupSelected()
{
	TArray<FAssetData> Assets;
	GetSelectedTextureAssets(Assets);
	return Assets.Num() > 0;
}

FText FMobTextureSetup::SetupReason()
{
	return CanSetupSelected() ? FText::GetEmpty()
		: LOCTEXT("TextureSetupNoSelection", "Select textures in the Content Browser first.");
}

#undef LOCTEXT_NAMESPACE
