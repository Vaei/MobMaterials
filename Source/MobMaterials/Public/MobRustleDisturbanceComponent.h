// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "MobRustleDisturbanceComponent.generated.h"

/**
 * Something that pushes foliage around by being in it.
 *
 * A sphere in world space rather than a footprint on the ground, because a canopy is four metres up
 * and a top-down field cannot tell walking under a tree from walking through it. Every plant inside
 * the sphere leans away and rings; a plant a metre outside it does not know this exists.
 *
 * Only a few disturbers can be published at once, so the ones nearest the camera win. That is the
 * right answer for a duel and the wrong one for a crowd, which is what the count is for.
 */
UCLASS(ClassGroup=Rendering, meta=(BlueprintSpawnableComponent))
class MOBMATERIALS_API UMobRustleDisturbanceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UMobRustleDisturbanceComponent();

	/**
	 * How far the disturbance reaches.
	 *
	 * Wider than the body. A canopy is metres of leaves and a sphere sized to a capsule only grazes
	 * the lowest branch of it, which reads as nothing happening.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rustle", meta=(UIMin="10", UIMax="500", ClampMin="1", ForceUnits="cm"))
	float Radius = 170.f;

	/**
	 * How far a plant is pushed by something barely moving through it. World units at the centre.
	 *
	 * A body that stops entirely gives its slot up and the plant closes over it, so this is the
	 * floor for a crawl rather than for standing still.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rustle", meta=(UIMin="0", UIMax="50", ClampMin="0", ForceUnits="cm"))
	float RestStrength = 4.f;

	/** How far it is pushed at ReferenceSpeed and beyond. This is what a sprint through a bush looks like. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rustle", meta=(UIMin="0", UIMax="100", ClampMin="0", ForceUnits="cm"))
	float MoveStrength = 22.f;

	/** World units per second at which the push has reached MoveStrength. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rustle", meta=(UIMin="50", UIMax="1200", ClampMin="1", ForceUnits="cm/s"))
	float ReferenceSpeed = 500.f;

	/** Turns the disturbance off without unregistering, for a body that has gone out of play. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Rustle")
	bool bEnabled = true;

	/** How fast the component is moving, in world units per second, measured from where it was. */
	float GetSpeed() const { return Speed; }

	/** The radius as the field is published with it, scale included. */
	float GetWorldRadius() const;

	/** Which way and how hard, with the speed already folded in. Zero length while disabled. */
	FVector GetPush() const;

	/** Updates the measured velocity. Called by the subsystem once a frame, before the slots are written. */
	void TickVelocity(float DeltaTime);

	//~ Begin UActorComponent Interface
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	//~ End UActorComponent Interface

protected:
	/**
	 * The last direction worth remembering.
	 *
	 * A body standing still has no velocity to take a direction from, and a plant it is holding
	 * aside has to keep being held the way it was pushed rather than snapping to some default.
	 */
	FVector PushDirection = FVector::ForwardVector;

	float Speed = 0.f;
	FVector LastLocation = FVector::ZeroVector;
	bool bHasLastLocation = false;
};
