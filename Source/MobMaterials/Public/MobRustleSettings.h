// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "MobRustleSettings.generated.h"

class UMaterialParameterCollection;

/**
 * Where the foliage masters read the disturbers from.
 *
 * Settings rather than a constant so a project can point the subsystem at its own collection without
 * subclassing anything. It has to be the collection the master material was authored against, which
 * is what the recipe's Rustle Collection decides.
 */
UCLASS(Config=Engine, DefaultConfig, meta=(DisplayName="Mob Rustle"))
class MOBMATERIALS_API UMobRustleSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UMobRustleSettings();

	UPROPERTY(EditAnywhere, Config, Category="Rustle")
	TSoftObjectPtr<UMaterialParameterCollection> ParameterCollection;

	/**
	 * How long a one-shot impulse holds its slot.
	 *
	 * Long enough for the ring to have died under any sane damping, since a slot released early is a
	 * plant that snaps straight rather than settling.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Rustle", meta=(ClampMin="0.1", ForceUnits="s"))
	float ImpulseLifetime = 3.f;

	/**
	 * How long a slot goes on being written after its disturber has gone.
	 *
	 * Same reason: what leaves a bush has to leave it moving.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Rustle", meta=(ClampMin="0.1", ForceUnits="s"))
	float ReleaseLifetime = 3.f;

	/**
	 * How long a slot spends easing to nothing before its time is up.
	 *
	 * The material decides how fast a plant settles and this decides how long the slot lasts, and
	 * nothing makes the two agree. Without this the slot is simply dropped, taking whatever the
	 * plant had left with it, which reads as a snap at the end of an otherwise smooth settle. This
	 * guarantees the handover arrives at zero however the material is tuned.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Rustle", meta=(ClampMin="0", ForceUnits="s"))
	float ReleaseFadeSeconds = 1.2f;

	/**
	 * How slowly a disturber has to be moving before the plant is allowed to start settling.
	 *
	 * Below this it counts as standing still, and a plant held aside by somebody standing in it
	 * closes over them rather than staying parted forever.
	 */
	UPROPERTY(EditAnywhere, Config, Category="Rustle", meta=(ClampMin="0", ForceUnits="cm/s"))
	float SettleSpeed = 20.f;

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }
};
