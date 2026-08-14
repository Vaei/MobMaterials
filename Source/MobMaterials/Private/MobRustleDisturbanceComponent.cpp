// Copyright (c) Jared Taylor

#include "MobRustleDisturbanceComponent.h"

#include "MobRustleSubsystem.h"
#include "Engine/World.h"

UMobRustleDisturbanceComponent::UMobRustleDisturbanceComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	bWantsOnUpdateTransform = false;
}

void UMobRustleDisturbanceComponent::OnRegister()
{
	Super::OnRegister();

	LastLocation = GetComponentLocation();
	bHasLastLocation = true;

	if (const UWorld* World = GetWorld())
	{
		if (UMobRustleSubsystem* Subsystem = World->GetSubsystem<UMobRustleSubsystem>())
		{
			Subsystem->RegisterDisturber(this);
		}
	}
}

void UMobRustleDisturbanceComponent::OnUnregister()
{
	if (const UWorld* World = GetWorld())
	{
		if (UMobRustleSubsystem* Subsystem = World->GetSubsystem<UMobRustleSubsystem>())
		{
			Subsystem->UnregisterDisturber(this);
		}
	}

	Super::OnUnregister();
}

float UMobRustleDisturbanceComponent::GetWorldRadius() const
{
	const FVector Scale = GetComponentScale();
	return Radius * FMath::Max3(FMath::Abs(Scale.X), FMath::Abs(Scale.Y), FMath::Abs(Scale.Z));
}

void UMobRustleDisturbanceComponent::TickVelocity(float DeltaTime)
{
	const FVector Location = GetComponentLocation();

	if (!bHasLastLocation || DeltaTime <= KINDA_SMALL_NUMBER)
	{
		LastLocation = Location;
		bHasLastLocation = true;
		Speed = 0.f;
		return;
	}

	const FVector Delta = Location - LastLocation;
	LastLocation = Location;

	// Smoothed rather than taken raw. One frame's delta is noisy enough that the push flickers
	// direction on a character the movement component is only nudging about.
	const float Instant = Delta.Size() / DeltaTime;
	const float Blend = FMath::Clamp(DeltaTime * 8.f, 0.f, 1.f);
	Speed = FMath::Lerp(Speed, Instant, Blend);

	// Horizontal only. A body walking through a plant shoves it aside; the vertical part of the
	// delta is the capsule settling and stepping, and taking it would push the leaves into the
	// ground.
	if (const FVector Direction = FVector(Delta.X, Delta.Y, 0.0).GetSafeNormal(); !Direction.IsNearlyZero())
	{
		PushDirection = FMath::Lerp(PushDirection, Direction, Blend).GetSafeNormal();
	}
}

FVector UMobRustleDisturbanceComponent::GetPush() const
{
	if (!bEnabled)
	{
		return FVector::ZeroVector;
	}

	const float Alpha = FMath::Clamp(Speed / FMath::Max(ReferenceSpeed, 1.f), 0.f, 1.f);
	return PushDirection * FMath::Lerp(RestStrength, MoveStrength, Alpha);
}
