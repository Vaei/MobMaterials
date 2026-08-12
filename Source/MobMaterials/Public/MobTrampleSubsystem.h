// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MobTrampleSubsystem.generated.h"

class AMobTrampleVolume;

/**
 * Routes footsteps to whichever trample volume is under them, and drives the drawing.
 *
 * Anything that disturbs the ground goes through Add Trample: footsteps, a landing, a body being
 * dragged. It knows nothing about who asked, so a bot leaves the same prints a player does and a
 * cart could leave a rut without any of this changing.
 */
UCLASS()
class MOBMATERIALS_API UMobTrampleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	static UMobTrampleSubsystem* Get(const UObject* WorldContext);

	/**
	 * Marks the ground at a world position.
	 *
	 * Does nothing where no volume covers that position, which is the common case and not worth a
	 * warning: an area gets prints because someone placed a volume over it.
	 *
	 * @param Radius            World units across, roughly the size of the print
	 * @param Strength          How deep, 0 to 1, against the material's Trample Depth
	 * @param RotationDegrees   About world Z, so a boot points the way it was walking
	 */
	UFUNCTION(BlueprintCallable, Category="Trample", meta=(WorldContext="WorldContextObject",
		AdvancedDisplay="RotationDegrees"))
	static void AddTrample(const UObject* WorldContextObject, const FVector& WorldLocation,
		float Radius = 24.f, float Strength = 1.f, float RotationDegrees = 0.f);

	/**
	 * Marks a line of ground, at a spacing, as one continuous smear.
	 *
	 * A body being dragged does not land in one place: stamping only where it is leaves a dotted
	 * line at anything above a walking pace, because the gap between two frames is metres.
	 *
	 * @param Spacing   How far apart the marks go. Half the radius reads as continuous
	 */
	UFUNCTION(BlueprintCallable, Category="Trample", meta=(WorldContext="WorldContextObject",
		AdvancedDisplay="Spacing"))
	static void AddTrampleTrail(const UObject* WorldContextObject, const FVector& Start,
		const FVector& End, float Radius = 24.f, float Strength = 1.f, float Spacing = 0.f);

	/** The volume covering a position, or null. */
	UFUNCTION(BlueprintPure, Category="Trample", meta=(WorldContext="WorldContextObject"))
	static AMobTrampleVolume* FindVolume(const UObject* WorldContextObject, const FVector& WorldLocation);

	/**
	 * How trampled a world position is, 0 to 1, and 0 where no volume covers it.
	 *
	 * Answered off the CPU mirror, so this is safe to ask on every footstep. What it is for is
	 * deciding whether a foot is landing on a covering or on what the last one uncovered.
	 */
	UFUNCTION(BlueprintPure, Category="Trample", meta=(WorldContext="WorldContextObject"))
	static float GetTrampleAt(const UObject* WorldContextObject, const FVector& WorldLocation);

	void RegisterVolume(AMobTrampleVolume* Volume);
	void UnregisterVolume(AMobTrampleVolume* Volume);

	//~ Begin UTickableWorldSubsystem Interface
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	//~ End UTickableWorldSubsystem Interface

private:
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<AMobTrampleVolume>> Volumes;

	/**
	 * Which volume the collection currently describes.
	 *
	 * A parameter collection holds one set of values, so only one volume can be the one the
	 * material reads. Prints landing in a different volume move it, which is right when volumes do
	 * not overlap and is why they should not.
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<AMobTrampleVolume> Published;

	void PublishVolume(AMobTrampleVolume* Volume);
};
