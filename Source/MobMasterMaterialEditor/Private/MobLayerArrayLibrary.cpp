// Copyright (c) Jared Taylor. All Rights Reserved

#include "MobLayerArrayLibrary.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

DEFINE_LOG_CATEGORY_STATIC(LogMobLayerArray, Log, All);

UTexture2DArray* UMobLayerArrayLibrary::PackLayerArray(const FString& PackagePath, const TArray<UTexture2D*>& Slices)
{
	if (Slices.Num() == 0)
	{
		UE_LOG(LogMobLayerArray, Error, TEXT("%s: no slices given"), *PackagePath);
		return nullptr;
	}

	for (int32 Index = 0; Index < Slices.Num(); ++Index)
	{
		if (!Slices[Index])
		{
			UE_LOG(LogMobLayerArray, Error, TEXT("%s: slice %d is missing"), *PackagePath, Index);
			return nullptr;
		}
	}

	const FString AssetName = FPackageName::GetShortName(PackagePath);

	UPackage* Package = CreatePackage(*PackagePath);
	if (!Package)
	{
		UE_LOG(LogMobLayerArray, Error, TEXT("%s: could not create package"), *PackagePath);
		return nullptr;
	}
	Package->FullyLoad();

	UTexture2DArray* Array = FindObject<UTexture2DArray>(Package, *AssetName);
	const bool bCreated = Array == nullptr;
	if (bCreated)
	{
		Array = NewObject<UTexture2DArray>(Package, *AssetName, RF_Public | RF_Standalone);
		if (!Array)
		{
			UE_LOG(LogMobLayerArray, Error, TEXT("%s: could not create array"), *PackagePath);
			return nullptr;
		}
	}

	Array->Modify();

	// Taken from the first slice so one call serves colour, normals and masks. Setting them before
	// the rebuild is what makes the built platform data come out in the right format.
	const UTexture2D* First = Slices[0];
	Array->SRGB = First->SRGB;
	Array->CompressionSettings = First->CompressionSettings;
	Array->MipGenSettings = First->MipGenSettings;
	Array->Filter = First->Filter;
	Array->LODGroup = First->LODGroup;
	Array->NeverStream = First->NeverStream;

	Array->SourceTextures.Empty(Slices.Num());
	for (UTexture2D* Slice : Slices)
	{
		Array->SourceTextures.Add(Slice);
	}

	if (!Array->CheckArrayTexturesCompatibility())
	{
		UE_LOG(LogMobLayerArray, Error,
			TEXT("%s: slices are not compatible - every one must match the first in size and format. ")
			TEXT("First is %s at %dx%d."),
			*PackagePath, *First->GetName(), First->Source.GetSizeX(), First->Source.GetSizeY());
		Array->SourceTextures.Empty();
		return nullptr;
	}

	if (!Array->UpdateSourceFromSourceTextures(bCreated))
	{
		UE_LOG(LogMobLayerArray, Error, TEXT("%s: rebuild from slices failed"), *PackagePath);
		return nullptr;
	}

	Array->PostEditChange();
	Package->MarkPackageDirty();

	if (bCreated)
	{
		FAssetRegistryModule::AssetCreated(Array);
	}

	UE_LOG(LogMobLayerArray, Log, TEXT("%s: %d slice(s) at %dx%d"),
		*PackagePath, Slices.Num(), Array->Source.GetSizeX(), Array->Source.GetSizeY());

	return Array;
}
