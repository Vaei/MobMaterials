// Copyright (c) Jared Taylor

#include "MobLevelTools.h"

#include "MobEditorLibrary.h"
#include "Editor.h"
#include "Containers/Ticker.h"
#include "FileHelpers.h"
#include "ContentBrowserModule.h"
#include "IContentBrowserSingleton.h"
#include "MaterialEditingLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Engine/Texture.h"
#include "EngineUtils.h"
#include "Landscape.h"
#include "LandscapeProxy.h"
#include "ScopedTransaction.h"
#include "Selection.h"
#include "Components/BoxComponent.h"
#include "Components/BrushComponent.h"
#include "Framework/Notifications/NotificationManager.h"
#include "GameFramework/Volume.h"
#include "Widgets/Notifications/SNotificationList.h"

#define LOCTEXT_NAMESPACE "MobMaterialsEditor"

namespace
{
	/** Anything a scale can be made to cover a landscape with: a brush volume, or a box component. */
	bool IsBoxVolume(const AActor* Actor)
	{
		if (!Actor)
		{
			return false;
		}

		if (Actor->IsA<AVolume>())
		{
			return true;
		}

		return Actor->FindComponentByClass<UBoxComponent>() != nullptr
			|| Actor->FindComponentByClass<UBrushComponent>() != nullptr;
	}

	/**
	 * The box an actor describes, in its own local space, at scale one.
	 *
	 * Measured off the shape components alone with the actor transform divided out. The actor's world
	 * bounds cannot stand in for this: sprites, arrows and meshes land in them as well, and a shape
	 * component pads its bounds by a unit so its wireframe draws.
	 */
	bool LocalBoxOf(const AActor* Actor, FBox& Out)
	{
		Out = FBox(ForceInit);
		if (!Actor)
		{
			return false;
		}

		const FTransform WorldToActor = Actor->GetActorTransform().Inverse();
		for (const UActorComponent* Component : Actor->GetComponents())
		{
			const USceneComponent* Scene = Cast<USceneComponent>(Component);
			if (!Scene || !Scene->IsRegistered())
			{
				continue;
			}

			const FTransform Local = Scene->GetComponentTransform() * WorldToActor;
			if (const UBoxComponent* Box = Cast<UBoxComponent>(Scene))
			{
				const FVector Extent = Box->GetUnscaledBoxExtent();
				Out += FBox(-Extent, Extent).TransformBy(Local);
			}
			else if (const UBrushComponent* Brush = Cast<UBrushComponent>(Scene))
			{
				Out += Brush->CalcBounds(Local).GetBox();
			}
		}

		return Out.IsValid != 0;
	}

	void GetSelectedActors(TArray<AActor*>& Out)
	{
		if (!GEditor)
		{
			return;
		}

		for (FSelectionIterator It(GEditor->GetSelectedActorIterator()); It; ++It)
		{
			if (AActor* Actor = Cast<AActor>(*It))
			{
				Out.Add(Actor);
			}
		}
	}

	/** The selection with landscapes taken out. Snapping a landscape to itself is not a thing. */
	void GetSelectedNonLandscapeActors(TArray<AActor*>& Out)
	{
		TArray<AActor*> Selected;
		GetSelectedActors(Selected);

		for (AActor* Actor : Selected)
		{
			if (!Actor->IsA<ALandscapeProxy>())
			{
				Out.Add(Actor);
			}
		}
	}

	/** One landscape, gathered from every proxy that belongs to it. */
	struct FLandscapeTarget
	{
		TArray<ALandscapeProxy*> Proxies;
		FBox Bounds = FBox(ForceInit);
	};

	/**
	 * The landscape nearest a point, as one box.
	 *
	 * Bounds are unioned across proxies rather than read off the actor: a world partitioned or
	 * streamed landscape is many proxies, and any one of them covers a fraction of the ground.
	 */
	bool FindLandscape(const UWorld* World, const FVector& Near, FLandscapeTarget& Out)
	{
		if (!World)
		{
			return false;
		}

		TMap<const AActor*, FLandscapeTarget> ByLandscape;
		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			ALandscapeProxy* Proxy = *It;
			const AActor* Key = Proxy->GetLandscapeActor();
			FLandscapeTarget& Target = ByLandscape.FindOrAdd(Key ? Key : Proxy);
			Target.Proxies.Add(Proxy);
			Target.Bounds += Proxy->GetComponentsBoundingBox(true);
		}

		double Best = TNumericLimits<double>::Max();
		for (const TPair<const AActor*, FLandscapeTarget>& Pair : ByLandscape)
		{
			if (!Pair.Value.Bounds.IsValid)
			{
				continue;
			}

			if (const double Distance = FVector::DistSquared(Pair.Value.Bounds.GetCenter(), Near);
				Distance < Best)
			{
				Best = Distance;
				Out = Pair.Value;
			}
		}

		return Out.Bounds.IsValid != 0;
	}

	/** The ground height at a point, from whichever proxy covers it. */
	TOptional<float> HeightAt(const FLandscapeTarget& Target, const FVector& Location)
	{
		for (ALandscapeProxy* Proxy : Target.Proxies)
		{
			if (const TOptional<float> Height = Proxy->GetHeightAtLocation(Location);
				Height.IsSet())
			{
				return Height;
			}
		}
		return TOptional<float>();
	}

	void NotifyLevelTool(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = bSuccess ? 5.f : 8.f;
		if (const TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	bool WorldHasLandscape()
	{
		const UWorld* World = EditorWorld();
		if (!World)
		{
			return false;
		}

		TActorIterator<ALandscapeProxy> It(World);
		return !!It;
	}
}

namespace
{
	/** The three slots a layer carries, and every suffix art arrives wearing for each. */
	struct FMobTextureSlot
	{
		const TCHAR* Suffix;
		TArray<FString> Endings;
	};

	TArray<FMobTextureSlot> TextureSlots()
	{
		return {
			{ TEXT("_BC"),  { TEXT("_bc"), TEXT("_basecolor"), TEXT("_albedo"), TEXT("_diffuse"), TEXT("_d") } },
			{ TEXT("_NRM"), { TEXT("_nrm"), TEXT("_normal"), TEXT("_n") } },
			{ TEXT("_HRC"), { TEXT("_hrc"), TEXT("_heigrougao"), TEXT("_masks"), TEXT("_mask"), TEXT("_h") } },
		};
	}

	/** The landscape material instance the open level actually renders with. */
	UMaterialInstanceConstant* LandscapeInstance()
	{
		const UWorld* World = EditorWorld();
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
		{
			if (UMaterialInstanceConstant* Instance = Cast<UMaterialInstanceConstant>(It->GetLandscapeMaterial()))
			{
				return Instance;
			}
		}
		return nullptr;
	}

	/** The layers an instance carries, taken from its `<Layer>_UVScale` parameters. */
	TArray<FString> InstanceLayers(const UMaterialInstanceConstant* Instance)
	{
		TArray<FString> Layers;
		if (!Instance)
		{
			return Layers;
		}

		TArray<FMaterialParameterInfo> Infos;
		TArray<FGuid> Guids;
		Instance->GetAllScalarParameterInfo(Infos, Guids);

		for (const FMaterialParameterInfo& Info : Infos)
		{
			const FString Name = Info.Name.ToString();
			if (Name.EndsWith(TEXT("_UVScale")))
			{
				Layers.AddUnique(Name.LeftChop(8));
			}
		}

		// Longest first, or DryGrass art lands on Grass: every layer name that is the tail of
		// another one would win on whichever happened to be tested first.
		Layers.Sort([](const FString& A, const FString& B) { return A.Len() > B.Len(); });
		return Layers;
	}

	void SelectedTextures(TArray<UTexture*>& Out)
	{
		TArray<FAssetData> Selected;
		FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser"))
			.Get().GetSelectedAssets(Selected);

		for (const FAssetData& Asset : Selected)
		{
			if (UTexture* Texture = Cast<UTexture>(Asset.GetAsset()))
			{
				Out.Add(Texture);
			}
		}
	}
}

UMaterialInstanceConstant* FMobLevelTools::GetLandscapeInstance()
{
	return LandscapeInstance();
}

UMaterialInterface* FMobLevelTools::GetLandscapeMaster()
{
	const UMaterialInstanceConstant* Instance = LandscapeInstance();
	return Instance ? Instance->Parent : nullptr;
}

FString FMobLevelTools::SlotSuffixFor(const FString& TextureName)
{
	const FString Lower = TextureName.ToLower();
	for (const FMobTextureSlot& Slot : TextureSlots())
	{
		const bool bMatches = Slot.Endings.ContainsByPredicate([&Lower](const FString& Ending)
		{
			return Lower.EndsWith(Ending);
		});

		if (bMatches)
		{
			return Slot.Suffix;
		}
	}
	return FString();
}

bool FMobLevelTools::CanAssignSelectedTextures()
{
	TArray<UTexture*> Textures;
	SelectedTextures(Textures);
	return Textures.Num() > 0 && LandscapeInstance() != nullptr;
}

FText FMobLevelTools::AssignReason()
{
	if (!LandscapeInstance())
	{
		return LOCTEXT("AssignNoLandscape",
			"This level has no landscape rendering with a material instance.");
	}

	TArray<UTexture*> Textures;
	SelectedTextures(Textures);
	if (Textures.Num() == 0)
	{
		return LOCTEXT("AssignNoSelection", "Select the layer textures in the Content Browser first.");
	}

	return FText::GetEmpty();
}

void FMobLevelTools::AssignSelectedTextures()
{
	UMaterialInstanceConstant* Instance = LandscapeInstance();
	TArray<UTexture*> Textures;
	SelectedTextures(Textures);
	if (!Instance || Textures.Num() == 0)
	{
		return;
	}

	const TArray<FString> Layers = InstanceLayers(Instance);
	const TArray<FMobTextureSlot> Slots = TextureSlots();

	// Resolved in full before anything is written, so a set with one bad name changes nothing
	// rather than leaving the instance half assigned.
	TMap<FName, UTexture*> Resolved;
	for (UTexture* Texture : Textures)
	{
		const FString Name = Texture->GetName();
		const FString Lower = Name.ToLower();

		const FString* Layer = Layers.FindByPredicate([&Lower](const FString& Candidate)
		{
			return Lower.Contains(Candidate.ToLower());
		});

		if (!Layer)
		{
			NotifyLevelTool(FText::Format(LOCTEXT("AssignNoLayer",
				"{0} does not name any of this material's layers. Nothing was assigned."),
				FText::FromString(Name)), false);
			return;
		}

		const FMobTextureSlot* Slot = Slots.FindByPredicate([&Lower](const FMobTextureSlot& Candidate)
		{
			return Candidate.Endings.ContainsByPredicate([&Lower](const FString& Ending)
			{
				return Lower.EndsWith(Ending);
			});
		});

		if (!Slot)
		{
			NotifyLevelTool(FText::Format(LOCTEXT("AssignNoSlot",
				"{0} does not end in a channel this understands: base colour, normal or HRC. "
				"Nothing was assigned."), FText::FromString(Name)), false);
			return;
		}

		const FName Parameter(*(*Layer + Slot->Suffix));
		if (UTexture** Clash = Resolved.Find(Parameter))
		{
			NotifyLevelTool(FText::Format(LOCTEXT("AssignClash",
				"{0} and {1} both want {2}. Nothing was assigned."),
				FText::FromString((*Clash)->GetName()), FText::FromString(Name),
				FText::FromName(Parameter)), false);
			return;
		}

		Resolved.Add(Parameter, Texture);
	}

	const FScopedTransaction Transaction(LOCTEXT("AssignTransaction", "Assign Layer Textures"));
	Instance->Modify();

	for (const TPair<FName, UTexture*>& Pair : Resolved)
	{
		Instance->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(Pair.Key), Pair.Value);
	}
	UMaterialEditingLibrary::UpdateMaterialInstance(Instance);

	NotifyLevelTool(FText::Format(LOCTEXT("AssignDone",
		"Assigned {0} texture(s) to {1}."), FText::AsNumber(Resolved.Num()),
		FText::FromString(Instance->GetName())), true);
}

bool FMobLevelTools::CanSnapToLandscapeCentre()
{
	TArray<AActor*> Actors;
	GetSelectedNonLandscapeActors(Actors);
	return Actors.Num() > 0 && WorldHasLandscape();
}

FText FMobLevelTools::SnapReason()
{
	if (!WorldHasLandscape())
	{
		return LOCTEXT("SnapNoLandscape", "There is no landscape in this level.");
	}

	TArray<AActor*> Actors;
	GetSelectedNonLandscapeActors(Actors);
	if (Actors.Num() == 0)
	{
		return LOCTEXT("SnapNoSelection", "Select an actor in the level first.");
	}

	return FText::GetEmpty();
}

void FMobLevelTools::SnapToLandscapeCentre()
{
	TArray<AActor*> Actors;
	GetSelectedNonLandscapeActors(Actors);
	if (Actors.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("SnapTransaction", "Snap To Landscape Centre"));

	int32 Moved = 0;
	for (AActor* Actor : Actors)
	{
		FLandscapeTarget Target;
		if (!FindLandscape(Actor->GetWorld(), Actor->GetActorLocation(), Target))
		{
			continue;
		}

		const FVector Centre = Target.Bounds.GetCenter();

		FVector Origin, Extent;
		Actor->GetActorBounds(false, Origin, Extent);

		// The actor's own pivot is wherever the artist put it, so the offset from its pivot to the
		// bottom of its bounds is what decides where it has to sit for its base to touch the ground.
		const FVector Location = Actor->GetActorLocation();
		const double Feet = Location.Z - (Origin.Z - Extent.Z);

		FVector Destination(Centre.X, Centre.Y, Target.Bounds.Min.Z + Feet);
		if (const TOptional<float> Height = HeightAt(Target, FVector(Centre.X, Centre.Y, Centre.Z));
			Height.IsSet())
		{
			Destination.Z = Height.GetValue() + Feet;
		}

		Actor->Modify();
		Actor->SetActorLocation(Destination);
		++Moved;
	}

	if (Moved > 0)
	{
		GEditor->NoteSelectionChange();
		GEditor->RedrawLevelEditingViewports();
	}

	NotifyLevelTool(Moved > 0
		? FText::Format(LOCTEXT("SnapDone", "Snapped {0} actor(s) to the landscape centre."),
			FText::AsNumber(Moved))
		: LOCTEXT("SnapNothing", "Nothing moved: no landscape was found near the selection."),
		Moved > 0);
}

bool FMobLevelTools::CanRecompileLandscapeMaterial()
{
	return Cast<UMaterial>(GetLandscapeMaster()) != nullptr;
}

FText FMobLevelTools::RecompileReason()
{
	if (!WorldHasLandscape())
	{
		return LOCTEXT("RecompileNoLandscape", "There is no landscape in this level.");
	}

	return CanRecompileLandscapeMaterial() ? FText::GetEmpty()
		: LOCTEXT("RecompileNoMaster",
			"The landscape's material has no master behind it to recompile.");
}

void FMobLevelTools::RecompileLandscapeMaterial()
{
	UMaterial* Master = Cast<UMaterial>(GetLandscapeMaster());
	if (!Master)
	{
		return;
	}

	const TArray<FString> Errors = UMaterialEditingLibrary::RecompileMaterial(Master);
	for (const FString& Error : Errors)
	{
		UE_LOG(LogTemp, Warning, TEXT("MobMaterials: %s: %s"), *Master->GetName(), *Error);
	}

	// Without this the freshly built shader lives only in memory, and the stale one on disk is what
	// the next session loads - which is the whole reason this is needed more than once.
	const bool bSaved = UEditorLoadingAndSavingUtils::SavePackages({ Master->GetPackage() }, false);

	NotifyLevelTool(Errors.Num() > 0
		? FText::Format(LOCTEXT("RecompileErrors", "{0} recompiled with {1} error(s). See the Output Log."),
			FText::FromString(Master->GetName()), FText::AsNumber(Errors.Num()))
		: bSaved
			? FText::Format(LOCTEXT("RecompileDone", "{0} recompiled and saved."),
				FText::FromString(Master->GetName()))
			: FText::Format(LOCTEXT("RecompileUnsaved",
				"{0} recompiled, but could not be saved - check it out, or the stale shader comes "
				"back next session."), FText::FromString(Master->GetName())),
		Errors.Num() == 0 && bSaved);
}

bool FMobLevelTools::CanRebuildPhysicalMaterial()
{
	return WorldHasLandscape();
}

FText FMobLevelTools::RebuildReason()
{
	return WorldHasLandscape() ? FText::GetEmpty()
		: LOCTEXT("RebuildNoLandscape", "There is no landscape in this level.");
}

void FMobLevelTools::RebuildPhysicalMaterial()
{
	UWorld* World = EditorWorld();
	if (!World)
	{
		return;
	}

	// The landscape refuses to bake at ES3.1, and leaving the preview does not take effect on the
	// frame it is asked for, so the work waits on a ticker for the feature level to actually move.
	const bool bLeftPreview = UMobEditorLibrary::LeaveMobilePreview();

	NotifyLevelTool(LOCTEXT("RebuildStarted",
		"Rebaking the landscape's physical materials. This leaves the mobile preview while it runs."), true);

	FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda(
		[bLeftPreview, Waited = 0.f](float Delta) mutable
		{
			const UWorld* World = EditorWorld();
			if (!World)
			{
				return false;
			}

			Waited += Delta;
			if (World->GetFeatureLevel() <= ERHIFeatureLevel::ES3_1 && Waited < 30.f)
			{
				return true;
			}

			const int32 Outdated = UMobEditorLibrary::RebuildLandscapePhysicalMaterialSync(16);
			if (bLeftPreview)
			{
				UMobEditorLibrary::RestoreMobilePreview();
			}

			// Saved here rather than left to whoever remembers: the bake lives in the landscape's
			// own packages, and an unsaved one is the same as no bake at all next time it loads.
			int32 Saved = 0;
			if (Outdated == 0)
			{
				TArray<UPackage*> Dirty;
				UEditorLoadingAndSavingUtils::GetDirtyMapPackages(Dirty);
				Saved = Dirty.Num();

				// A read-only package under source control fails silently one at a time, and a bake
				// nobody saved is the same as no bake at all next load, so the count is what is
				// reported rather than the call's own all-or-nothing answer.
				if (!UEditorLoadingAndSavingUtils::SavePackages(Dirty, true))
				{
					int32 Failed = 0;
					for (const UPackage* Package : Dirty)
					{
						if (Package && Package->IsDirty())
						{
							++Failed;
						}
					}
					Saved -= Failed;

					if (Failed > 0)
					{
						NotifyLevelTool(FText::Format(LOCTEXT("RebuildSaveFailed",
							"{0} landscape package(s) could not be saved - check them out and save the "
							"level, or the bake is lost on reload."), FText::AsNumber(Failed)), false);
					}
				}
			}

			NotifyLevelTool(Outdated == 0
				? FText::Format(LOCTEXT("RebuildDone",
					"Rebaked the landscape's physical materials, and saved {0} package(s)."),
					FText::AsNumber(Saved))
				: FText::Format(LOCTEXT("RebuildIncomplete",
					"{0} landscape component(s) still read as outdated. Run it again once the "
					"shaders have finished."), FText::AsNumber(Outdated)),
				Outdated == 0);

			return false;
		}), 0.f);
}

bool FMobLevelTools::CanFitBoxToLandscape()
{
	TArray<AActor*> Actors;
	GetSelectedNonLandscapeActors(Actors);
	return Actors.ContainsByPredicate(&IsBoxVolume) && WorldHasLandscape();
}

FText FMobLevelTools::FitReason()
{
	if (!WorldHasLandscape())
	{
		return LOCTEXT("FitNoLandscape", "There is no landscape in this level.");
	}

	TArray<AActor*> Actors;
	GetSelectedNonLandscapeActors(Actors);
	if (Actors.Num() == 0)
	{
		return LOCTEXT("FitNoSelection", "Select a box volume in the level first.");
	}

	if (!Actors.ContainsByPredicate(&IsBoxVolume))
	{
		return LOCTEXT("FitNotBox",
			"Nothing selected is a box: this wants a volume, or an actor carrying a box component.");
	}

	return FText::GetEmpty();
}

void FMobLevelTools::FitBoxToLandscape()
{
	TArray<AActor*> Selected;
	GetSelectedNonLandscapeActors(Selected);

	TArray<AActor*> Boxes = Selected.FilterByPredicate(&IsBoxVolume);
	if (Boxes.Num() == 0)
	{
		return;
	}

	const FScopedTransaction Transaction(LOCTEXT("FitTransaction", "Fit Box Volume To Landscape"));

	int32 Fitted = 0;
	for (AActor* Actor : Boxes)
	{
		FLandscapeTarget Target;
		if (!FindLandscape(Actor->GetWorld(), Actor->GetActorLocation(), Target))
		{
			continue;
		}

		Actor->Modify();

		// A zero anywhere on the scale cannot be divided back out of the transform below.
		if (Actor->GetActorScale3D().GetAbsMin() <= UE_KINDA_SMALL_NUMBER)
		{
			Actor->SetActorScale3D(FVector::OneVector);
		}

		FBox Local;
		if (!LocalBoxOf(Actor, Local) || Local.GetExtent().GetMin() <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector Size = Local.GetExtent();
		const FVector Wanted = Target.Bounds.GetExtent();
		const FVector Scale(Wanted.X / Size.X, Wanted.Y / Size.Y, Wanted.Z / Size.Z);

		// A rotated box cannot be made to cover an axis aligned one by scaling, so the rotation goes.
		Actor->SetActorRotation(FRotator::ZeroRotator);
		Actor->SetActorScale3D(Scale);

		// The shape is not necessarily centred on the pivot, and at this scale a small offset is a
		// large one, so it is the shape's centre that is placed rather than the actor's.
		Actor->SetActorLocation(Target.Bounds.GetCenter() - Local.GetCenter() * Scale);
		Actor->PostEditMove(true);

		++Fitted;
	}

	if (Fitted > 0)
	{
		GEditor->NoteSelectionChange();
		GEditor->RedrawLevelEditingViewports();
	}

	NotifyLevelTool(Fitted > 0
		? FText::Format(LOCTEXT("FitDone", "Fitted {0} volume(s) to the landscape."),
			FText::AsNumber(Fitted))
		: LOCTEXT("FitNothing", "Nothing fitted: no landscape was found near the selection."),
		Fitted > 0);
}

#undef LOCTEXT_NAMESPACE
