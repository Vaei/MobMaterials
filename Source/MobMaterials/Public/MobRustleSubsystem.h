// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Containers/StaticArray.h"
#include "Subsystems/WorldSubsystem.h"
#include "MobRustleSubsystem.generated.h"

class UMaterialParameterCollection;
class UMobRustleDisturbanceComponent;

/**
 * One disturbance the foliage masters are currently reading.
 *
 * Age is what the plant settles on. It is held at nothing for as long as whoever owns the slot is
 * still moving, and only starts running once they stop or leave - so a bush stays parted around
 * somebody walking through it and closes over them when they stand still.
 */
struct FMobRustleSlot
{
	/** Whose slot this is, or null for a slot left ringing after its disturber has gone. */
	TWeakObjectPtr<UMobRustleDisturbanceComponent> Owner;

	FVector Location = FVector::ZeroVector;
	FVector Push = FVector::ZeroVector;

	/** Zero means the slot is empty, which the material's falloff already resolves to no offset. */
	float Radius = 0.f;

	float Age = 0.f;

	/** Seconds an unowned slot has left before it is free again. Unused while owned. */
	float Remaining = 0.f;

	bool IsEmpty() const { return Radius <= 0.f; }
};

/**
 * Publishes which bodies are pushing through foliage, for the masters to read.
 *
 * A fixed handful of slots in a parameter collection, because a collection has no arrays and because
 * a plant only ever needs to know about whoever is close enough to touch it. Nearest the view wins,
 * so in a crowd the ones on screen are the ones that move the leaves.
 *
 * It knows nothing about who asked. A player, a bot and a thrown body all leave the same disturbance,
 * and a leaf pile reading these slots reacts to all three without any of them knowing it exists.
 */
UCLASS()
class MOBMATERIALS_API UMobRustleSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	/** Has to match the slot count the foliage master was authored with. */
	static constexpr int32 NumSlots = 4;

	static UMobRustleSubsystem* Get(const UObject* WorldContext);

	/**
	 * Kicks the foliage at a world position once, with no component behind it.
	 *
	 * For a blade going through a hedge, an arrow landing in a bush, anything that disturbs a plant
	 * and then is not there any more. Takes an empty slot if there is one and the stalest ringing
	 * slot if there is not; it never takes a slot off a body that is standing in something.
	 *
	 * @param Radius    World units the kick reaches
	 * @param Push      Which way and how far, in world units at the centre
	 */
	UFUNCTION(BlueprintCallable, Category="Rustle", meta=(WorldContext="WorldContextObject"))
	static void AddRustleImpulse(const UObject* WorldContextObject, const FVector& WorldLocation,
		float Radius, const FVector& Push);

	void RegisterDisturber(UMobRustleDisturbanceComponent* Disturber);
	void UnregisterDisturber(UMobRustleDisturbanceComponent* Disturber);

	/** The slots as they were last published. What a leaf pile reads to know who is standing in it. */
	const TStaticArray<FMobRustleSlot, NumSlots>& GetSlots() const { return Slots; }

	//~ Begin UTickableWorldSubsystem Interface
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;
	//~ End UTickableWorldSubsystem Interface

protected:
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<UMobRustleDisturbanceComponent>> Disturbers;

	TStaticArray<FMobRustleSlot, NumSlots> Slots;

	/** What mob.Rustle.Enable last said. Starts unset so the first tick of a world applies it. */
	int32 LastEnableApplied = -1;

	/** Where the foliage is being looked at from, which is what decides whose disturbance is published. */
	bool GetViewLocation(FVector& OutLocation) const;

	/** Hands out slots to the nearest disturbers, releasing anyone who has lost their place. */
	void AssignSlots();

	void TickSlots(float DeltaTime);

	/** Writes every slot, whether or not it changed. Eight vectors is cheaper than tracking eight. */
	void PublishSlots() const;

	/** Empties every slot and publishes that, so nothing is left leaning when the field is turned off. */
	void ClearSlots();

	int32 FindSlotFor(const UMobRustleDisturbanceComponent* Disturber) const;

	UMaterialParameterCollection* GetCollection() const;
	void SetVector(FName Name, const FLinearColor& Value) const;
};
