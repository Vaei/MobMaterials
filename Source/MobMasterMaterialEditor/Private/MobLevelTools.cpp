// Copyright (c) Jared Taylor

#include "MobLevelTools.h"

#include "Editor.h"
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

#define LOCTEXT_NAMESPACE "MobMasterMaterialEditor"

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

		// Measured with the actor's scale divided back out, so the answer is the size of the shape
		// itself. Fitting against the already scaled bounds would only ever approach the target.
		FVector Origin, Extent;
		Actor->GetActorBounds(false, Origin, Extent);

		const FVector Scale = Actor->GetActorScale3D();
		const FVector Unscaled(
			Extent.X / FMath::Max(FMath::Abs(Scale.X), UE_KINDA_SMALL_NUMBER),
			Extent.Y / FMath::Max(FMath::Abs(Scale.Y), UE_KINDA_SMALL_NUMBER),
			Extent.Z / FMath::Max(FMath::Abs(Scale.Z), UE_KINDA_SMALL_NUMBER));

		if (Unscaled.GetMin() <= UE_KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector Wanted = Target.Bounds.GetExtent();

		Actor->Modify();

		// A rotated box cannot be made to cover an axis aligned one by scaling, so the rotation goes.
		Actor->SetActorRotation(FRotator::ZeroRotator);
		Actor->SetActorScale3D(FVector(Wanted.X / Unscaled.X, Wanted.Y / Unscaled.Y,
			Wanted.Z / Unscaled.Z));

		// Re-measured after scaling: the pivot is not necessarily the middle of the shape, so where
		// the actor goes depends on where its bounds ended up relative to it.
		Actor->GetActorBounds(false, Origin, Extent);
		Actor->SetActorLocation(Actor->GetActorLocation() + (Target.Bounds.GetCenter() - Origin));

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
