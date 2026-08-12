// Copyright (c) Jared Taylor

#include "MobTrampleTrailComponent.h"

#include "MobTrampleSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UMobTrampleTrailComponent::UMobTrampleTrailComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	bAutoActivate = true;
}

void UMobTrampleTrailComponent::SetTrailActive(bool bActive)
{
	SetComponentTickEnabled(bActive);

	// A fresh trail rather than a line joining wherever this was last dragged, which for a body
	// picked up and carried across a courtyard would be a rut through everything in between.
	bHasLastMark = false;
}

FVector UMobTrampleTrailComponent::GroundUnder(const FVector& Location) const
{
	const UWorld* World = GetWorld();
	if (!World || GroundTraceDistance <= 0.f)
	{
		return Location;
	}

	FCollisionQueryParams Params(SCENE_QUERY_STAT(MobTrampleTrail), false, GetOwner());

	FHitResult Hit;
	if (World->LineTraceSingleByChannel(Hit, Location + FVector(0.f, 0.f, GroundTraceDistance * 0.5f),
		Location - FVector(0.f, 0.f, GroundTraceDistance), GroundChannel, Params))
	{
		return Hit.ImpactPoint;
	}

	return Location;
}

void UMobTrampleTrailComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	const AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	const FVector Here = GroundUnder(Owner->GetActorLocation());

	if (!bHasLastMark)
	{
		LastMark = Here;
		bHasLastMark = true;
		UMobTrampleSubsystem::AddTrample(this, Here, Radius, Strength, 0.f);
		return;
	}

	if (FVector::DistSquared2D(Here, LastMark) < FMath::Square(Spacing))
	{
		return;
	}

	// Drawn as a line rather than a mark at each end: a frame at a run is metres of ground, and
	// stamping only where the body is now leaves a dotted line behind it.
	UMobTrampleSubsystem::AddTrampleTrail(this, LastMark, Here, Radius, Strength, Spacing);
	LastMark = Here;
}
