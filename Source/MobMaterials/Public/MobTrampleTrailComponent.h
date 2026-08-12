// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "MobTrampleTrailComponent.generated.h"

/**
 * Leaves a continuous mark along whatever its actor is dragged across.
 *
 * For a body being hauled, a crate being pushed, anything that scrapes rather than steps. Add it to
 * the actor and turn it on for as long as the dragging lasts: off, it does not tick.
 */
UCLASS(ClassGroup=(Mob), meta=(BlueprintSpawnableComponent), meta=(DisplayName="Mob Trample Trail"))
class MOBMATERIALS_API UMobTrampleTrailComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMobTrampleTrailComponent();

	/** How wide the smear is. Wider than a boot: a body is not a foot. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="1", ForceUnits="cm"))
	float Radius = 40.f;

	/** How deep, against the material's Trample Depth. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="0", ClampMax="1"))
	float Strength = 0.7f;

	/** How far the actor has to move before another mark goes down. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="1", ForceUnits="cm"))
	float Spacing = 16.f;

	/**
	 * How far below the component to look for the ground.
	 *
	 * The mark belongs on the surface, not at the pivot: a ragdoll's origin is wherever its capsule
	 * used to be, which can be most of a body above the dirt it is being pulled through.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="0", ForceUnits="cm"))
	float GroundTraceDistance = 200.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample|Advanced")
	TEnumAsByte<ECollisionChannel> GroundChannel = ECC_Visibility;

	/** Starts or stops marking. Starting from a stop begins a fresh trail rather than joining the last. */
	UFUNCTION(BlueprintCallable, Category="Trample")
	void SetTrailActive(bool bActive);

	UFUNCTION(BlueprintPure, Category="Trample")
	bool IsTrailActive() const { return IsComponentTickEnabled(); }

	//~ Begin UActorComponent Interface
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	//~ End UActorComponent Interface

protected:
	/** Where the ground is under a point, or the point itself where there is none. */
	FVector GroundUnder(const FVector& Location) const;

private:
	/** Where the last mark went down, so the gap since is what decides the next. */
	FVector LastMark = FVector::ZeroVector;

	bool bHasLastMark = false;
};
