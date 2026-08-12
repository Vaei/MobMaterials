// Copyright (c) Jared Taylor

#include "MobTrampleSubsystem.h"

#include "MobTrampleVolume.h"
#include "Engine/World.h"

UMobTrampleSubsystem* UMobTrampleSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = GEngine
		? GEngine->GetWorldFromContextObject(WorldContext, EGetWorldErrorMode::ReturnNull)
		: nullptr;
	return World ? World->GetSubsystem<UMobTrampleSubsystem>() : nullptr;
}

bool UMobTrampleSubsystem::DoesSupportWorldType(const EWorldType::Type WorldType) const
{
	return WorldType == EWorldType::Game || WorldType == EWorldType::PIE;
}

TStatId UMobTrampleSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMobTrampleSubsystem, STATGROUP_Tickables);
}

void UMobTrampleSubsystem::RegisterVolume(AMobTrampleVolume* Volume)
{
	if (!Volume)
	{
		return;
	}

	Volumes.AddUnique(Volume);

	// The material needs bounds before anyone has walked anywhere, or the first frame samples a
	// texture it has no coordinate for.
	if (!Published.IsValid())
	{
		PublishVolume(Volume);
	}
}

void UMobTrampleSubsystem::UnregisterVolume(AMobTrampleVolume* Volume)
{
	Volumes.Remove(Volume);

	if (Published.Get() == Volume)
	{
		Published = nullptr;
		for (const TWeakObjectPtr<AMobTrampleVolume>& Other : Volumes)
		{
			if (Other.IsValid())
			{
				PublishVolume(Other.Get());
				break;
			}
		}
	}
}

void UMobTrampleSubsystem::PublishVolume(AMobTrampleVolume* Volume)
{
	if (Volume)
	{
		Published = Volume;
		Volume->PublishBounds();
	}
}

AMobTrampleVolume* UMobTrampleSubsystem::FindVolume(const UObject* WorldContextObject,
	const FVector& WorldLocation)
{
	const UMobTrampleSubsystem* Subsystem = Get(WorldContextObject);
	if (!Subsystem)
	{
		return nullptr;
	}

	for (const TWeakObjectPtr<AMobTrampleVolume>& Volume : Subsystem->Volumes)
	{
		if (Volume.IsValid() && Volume->Covers(WorldLocation))
		{
			return Volume.Get();
		}
	}

	return nullptr;
}

void UMobTrampleSubsystem::AddTrampleTrail(const UObject* WorldContextObject, const FVector& Start,
	const FVector& End, float Radius, float Strength, float Spacing)
{
	const FVector Along = End - Start;
	const double Length = Along.Size();
	const double Step = Spacing > 0.f ? Spacing : FMath::Max(Radius * 0.5f, 1.f);

	// The mark points along the drag rather than along a character's facing: a body being pulled
	// leaves a rut in the direction it is travelling, and there is nothing else to ask.
	const float Yaw = Length > UE_KINDA_SMALL_NUMBER ? Along.Rotation().Yaw : 0.f;

	const int32 Marks = FMath::Min(FMath::FloorToInt(Length / Step), 256);
	for (int32 i = 0; i <= Marks; ++i)
	{
		AddTrample(WorldContextObject, Start + Along * (Marks > 0 ? double(i) / Marks : 0.0),
			Radius, Strength, Yaw);
	}
}

float UMobTrampleSubsystem::GetTrampleAt(const UObject* WorldContextObject, const FVector& WorldLocation)
{
	const AMobTrampleVolume* Volume = FindVolume(WorldContextObject, WorldLocation);
	return Volume ? Volume->GetTrampleAt(WorldLocation) : 0.f;
}

void UMobTrampleSubsystem::AddTrample(const UObject* WorldContextObject, const FVector& WorldLocation,
	float Radius, float Strength, float RotationDegrees)
{
	UMobTrampleSubsystem* Subsystem = Get(WorldContextObject);
	if (!Subsystem)
	{
		return;
	}

	AMobTrampleVolume* Volume = FindVolume(WorldContextObject, WorldLocation);
	if (!Volume)
	{
		return;
	}

	if (Subsystem->Published.Get() != Volume)
	{
		Subsystem->PublishVolume(Volume);
	}

	Volume->AddStamp(WorldLocation, Radius, Strength, RotationDegrees);
}

void UMobTrampleSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (int32 i = Volumes.Num() - 1; i >= 0; --i)
	{
		AMobTrampleVolume* Volume = Volumes[i].Get();
		if (!Volume)
		{
			Volumes.RemoveAtSwap(i);
			continue;
		}

		if (Volume->HasPendingWork())
		{
			Volume->Flush(DeltaTime);
		}
	}
}
