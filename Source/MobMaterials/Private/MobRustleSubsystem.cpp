// Copyright (c) Jared Taylor

#include "MobRustleSubsystem.h"

#include "MobRustleDisturbanceComponent.h"
#include "MobRustleSettings.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Materials/MaterialParameterCollection.h"

#if UE_ENABLE_DEBUG_DRAWING
#include "DrawDebugHelpers.h"
#endif

namespace
{
	static int32 GMobRustleEnable = 1;
	static FAutoConsoleVariableRef CVarMobRustleEnable(
		TEXT("mob.Rustle.Enable"), GMobRustleEnable,
		TEXT("Whether bodies moving through foliage push it about. 0 empties every slot."),
		ECVF_Scalability);

#if UE_ENABLE_DEBUG_DRAWING
	static int32 GMobRustleDebug = 0;
	static FAutoConsoleVariableRef CVarMobRustleDebug(
		TEXT("mob.Rustle.Debug"), GMobRustleDebug,
		TEXT("Draws the published rustle slots. 1 the spheres, 2 the pushes as well."),
		ECVF_Cheat);
#endif

	FName SphereParameter(int32 Slot)
	{
		return FName(*FString::Printf(TEXT("Rustle%dSphere"), Slot));
	}

	FName PushParameter(int32 Slot)
	{
		return FName(*FString::Printf(TEXT("Rustle%dPush"), Slot));
	}
}

UMobRustleSubsystem* UMobRustleSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UMobRustleSubsystem>() : nullptr;
}

bool UMobRustleSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMobRustleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMobRustleSubsystem, STATGROUP_Tickables);
}

void UMobRustleSubsystem::Deinitialize()
{
	ClearSlots();
	Disturbers.Reset();

	Super::Deinitialize();
}

void UMobRustleSubsystem::RegisterDisturber(UMobRustleDisturbanceComponent* Disturber)
{
	if (Disturber)
	{
		Disturbers.AddUnique(Disturber);
	}
}

void UMobRustleSubsystem::UnregisterDisturber(UMobRustleDisturbanceComponent* Disturber)
{
	Disturbers.Remove(Disturber);

	// Left ringing rather than emptied. Something walking out of a bush has to leave it moving.
	const int32 Slot = FindSlotFor(Disturber);
	if (Slot != INDEX_NONE)
	{
		Slots[Slot].Owner = nullptr;
		Slots[Slot].Remaining = GetDefault<UMobRustleSettings>()->ReleaseLifetime;
	}
}

void UMobRustleSubsystem::AddRustleImpulse(const UObject* WorldContextObject,
	const FVector& WorldLocation, float Radius, const FVector& Push)
{
	UMobRustleSubsystem* Subsystem = Get(WorldContextObject);
	if (!Subsystem || Radius <= 0.f)
	{
		return;
	}

	int32 Best = INDEX_NONE;
	float Stalest = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		const FMobRustleSlot& Slot = Subsystem->Slots[Index];
		if (Slot.Owner.IsValid())
		{
			continue;
		}
		if (Slot.IsEmpty())
		{
			Best = Index;
			break;
		}
		if (Slot.Remaining < Stalest)
		{
			Stalest = Slot.Remaining;
			Best = Index;
		}
	}

	if (Best == INDEX_NONE)
	{
		return;
	}

	FMobRustleSlot& Slot = Subsystem->Slots[Best];
	Slot.Owner = nullptr;
	Slot.Location = WorldLocation;
	Slot.Radius = Radius;
	Slot.Push = Push;
	Slot.Age = 0.f;
	Slot.Fade = 1.f;
	Slot.Remaining = GetDefault<UMobRustleSettings>()->ImpulseLifetime;
}

int32 UMobRustleSubsystem::FindSlotFor(const UMobRustleDisturbanceComponent* Disturber) const
{
	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		if (Slots[Index].Owner.Get() == Disturber)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

void UMobRustleSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (GMobRustleEnable == 0)
	{
		if (LastEnableApplied != 0)
		{
			ClearSlots();
			LastEnableApplied = 0;
		}
		return;
	}
	LastEnableApplied = 1;

	Disturbers.RemoveAll([](const TWeakObjectPtr<UMobRustleDisturbanceComponent>& D)
	{
		return !D.IsValid();
	});

	for (const TWeakObjectPtr<UMobRustleDisturbanceComponent>& Disturber : Disturbers)
	{
		Disturber->TickVelocity(DeltaTime);
	}

	AssignSlots();
	TickSlots(DeltaTime);
	PublishSlots();

#if UE_ENABLE_DEBUG_DRAWING
	if (GMobRustleDebug > 0)
	{
		for (const FMobRustleSlot& Slot : Slots)
		{
			if (Slot.IsEmpty())
			{
				continue;
			}
			const FColor Color = Slot.Owner.IsValid() ? FColor::Green : FColor::Yellow;
			DrawDebugSphere(GetWorld(), Slot.Location, Slot.Radius, 16, Color, false, -1.f, 0, 1.f);
			if (GMobRustleDebug > 1)
			{
				DrawDebugDirectionalArrow(GetWorld(), Slot.Location, Slot.Location + Slot.Push * 4.f,
					20.f, Color, false, -1.f, 0, 2.f);
				DrawDebugString(GetWorld(), Slot.Location, FString::Printf(TEXT("age %.2f"), Slot.Age),
					nullptr, Color, 0.f);
			}
		}
	}
#endif
}

void UMobRustleSubsystem::AssignSlots()
{
	FVector ViewLocation;
	if (!GetViewLocation(ViewLocation))
	{
		ViewLocation = FVector::ZeroVector;
	}

	// Only bodies actually doing something. A slot held by somebody standing still is a slot showing
	// nothing, because the ring under it has long since decayed - and with four of them stood about
	// a tree, there would be no room left for anything to knock it.
	const float SettleSpeed = GetDefault<UMobRustleSettings>()->SettleSpeed;

	// Nearest the view first, so in a crowd the bodies on screen are the ones moving the leaves.
	TArray<TWeakObjectPtr<UMobRustleDisturbanceComponent>> Wanted;
	Wanted.Reserve(Disturbers.Num());
	for (const TWeakObjectPtr<UMobRustleDisturbanceComponent>& Disturber : Disturbers)
	{
		if (Disturber->bEnabled && Disturber->GetSpeed() > SettleSpeed)
		{
			Wanted.Add(Disturber);
		}
	}
	Wanted.Sort([&ViewLocation](const TWeakObjectPtr<UMobRustleDisturbanceComponent>& A,
		const TWeakObjectPtr<UMobRustleDisturbanceComponent>& B)
	{
		return FVector::DistSquared(A->GetComponentLocation(), ViewLocation)
			< FVector::DistSquared(B->GetComponentLocation(), ViewLocation);
	});
	if (Wanted.Num() > NumSlots)
	{
		Wanted.SetNum(NumSlots);
	}

	// Anyone who has lost their place is released rather than cut off, for the same reason
	// unregistering releases: what leaves a bush has to leave it moving.
	const float ReleaseLifetime = GetDefault<UMobRustleSettings>()->ReleaseLifetime;
	for (FMobRustleSlot& Slot : Slots)
	{
		if (Slot.Owner.IsValid() && !Wanted.Contains(Slot.Owner))
		{
			Slot.Owner = nullptr;
			Slot.Remaining = ReleaseLifetime;
		}
	}

	for (const TWeakObjectPtr<UMobRustleDisturbanceComponent>& Disturber : Wanted)
	{
		if (FindSlotFor(Disturber.Get()) != INDEX_NONE)
		{
			continue;
		}

		// An empty slot first, then the ringing slot with the least left in it. An owned slot is
		// never taken, or two bodies close together would trade one slot back and forth.
		int32 Best = INDEX_NONE;
		float Stalest = TNumericLimits<float>::Max();
		for (int32 Index = 0; Index < NumSlots; ++Index)
		{
			if (Slots[Index].Owner.IsValid())
			{
				continue;
			}
			if (Slots[Index].IsEmpty())
			{
				Best = Index;
				break;
			}
			if (Slots[Index].Remaining < Stalest)
			{
				Stalest = Slots[Index].Remaining;
				Best = Index;
			}
		}

		if (Best == INDEX_NONE)
		{
			break;
		}

		Slots[Best].Owner = Disturber;
		Slots[Best].Age = 0.f;
	}
}

void UMobRustleSubsystem::TickSlots(float DeltaTime)
{
	const UMobRustleSettings* Settings = GetDefault<UMobRustleSettings>();
	const float SettleSpeed = Settings->SettleSpeed;
	const float FadeSeconds = FMath::Max(Settings->ReleaseFadeSeconds, UE_KINDA_SMALL_NUMBER);

	for (FMobRustleSlot& Slot : Slots)
	{
		if (const UMobRustleDisturbanceComponent* Owner = Slot.Owner.Get())
		{
			Slot.Location = Owner->GetComponentLocation();
			Slot.Radius = Owner->GetWorldRadius();
			Slot.Push = Owner->GetPush();
			Slot.Fade = 1.f;

			// Held at nothing while they are still moving, which is what keeps a plant parted around
			// somebody walking through it instead of settling under their feet.
			Slot.Age = Owner->GetSpeed() > SettleSpeed ? 0.f : Slot.Age + DeltaTime;
			continue;
		}

		if (Slot.IsEmpty())
		{
			continue;
		}

		Slot.Age += DeltaTime;
		Slot.Remaining -= DeltaTime;
		Slot.Fade = FMath::Clamp(Slot.Remaining / FadeSeconds, 0.f, 1.f);

		if (Slot.Remaining <= 0.f)
		{
			Slot = FMobRustleSlot();
		}
	}
}

void UMobRustleSubsystem::PublishSlots() const
{
	for (int32 Index = 0; Index < NumSlots; ++Index)
	{
		const FMobRustleSlot& Slot = Slots[Index];
		SetVector(SphereParameter(Index), FLinearColor(
			Slot.Location.X, Slot.Location.Y, Slot.Location.Z, Slot.Radius));
		const FVector Push = Slot.Push * Slot.Fade;
		SetVector(PushParameter(Index), FLinearColor(Push.X, Push.Y, Push.Z, Slot.Age));
	}
}

void UMobRustleSubsystem::ClearSlots()
{
	for (FMobRustleSlot& Slot : Slots)
	{
		Slot = FMobRustleSlot();
	}
	PublishSlots();
}

bool UMobRustleSubsystem::GetViewLocation(FVector& OutLocation) const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	if (World->ViewLocationsRenderedLastFrame.Num() > 0)
	{
		OutLocation = World->ViewLocationsRenderedLastFrame[0];
		return true;
	}

	if (const APlayerController* Controller = World->GetFirstPlayerController())
	{
		FRotator Unused;
		Controller->GetPlayerViewPoint(OutLocation, Unused);
		return true;
	}

	return false;
}

UMaterialParameterCollection* UMobRustleSubsystem::GetCollection() const
{
	return GetDefault<UMobRustleSettings>()->ParameterCollection.LoadSynchronous();
}

void UMobRustleSubsystem::SetVector(FName Name, const FLinearColor& Value) const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (UMaterialParameterCollection* Collection = GetCollection())
	{
		UKismetMaterialLibrary::SetVectorParameterValue(World, Collection, Name, Value);
	}
}
