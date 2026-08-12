// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MobTrampleVolume.generated.h"

class UBoxComponent;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMaterialParameterCollection;
class UTextureRenderTarget2D;

/** How a print gets from full depth to nothing. */
UENUM(BlueprintType)
enum class EMobTrampleFade : uint8
{
	/** Held at full depth, then away. */
	Straight,

	/** Held at full depth, then down to Half Fade Amount and held there, then away. */
	ThroughHalf		UMETA(DisplayName="Through Half"),
};

/**
 * One print, and how long it has been there.
 *
 * Kept rather than drawn and forgotten, because a print that holds and then fades has to fade on
 * its own clock: a single decay over the whole target fades the print somebody just left at the
 * same rate as the one from a minute ago.
 */
struct FMobTrampleStamp
{
	FVector2D Centre = FVector2D::ZeroVector;
	double Radius = 16.0;
	double Rotation = 0.0;
	float Strength = 1.0f;

	/** Seconds since it was left. */
	float Age = 0.f;

	/** World position, kept so the CPU mirror can be rebuilt when the marks are redrawn. */
	FVector World = FVector::ZeroVector;
	float WorldRadius = 24.f;
};

/**
 * A patch of ground that remembers being walked through.
 *
 * The box is the world footprint of one render target, projected straight down, and the material
 * reads that target to darken, roughen and dent the surface and to take the snow back off it. The
 * target is an asset rather than something created here so the material instance can point at the
 * same one without a dynamic material anywhere in the chain - a landscape material cannot easily be
 * made dynamic, and this is what avoids needing to.
 *
 * One volume is active at a time. The bounds go into a parameter collection, which has a single set
 * of values, so two overlapping volumes would each be describing the other's texture.
 */
UCLASS(BlueprintType, Blueprintable, meta=(DisplayName="Mob Trample Volume"))
class MOBMATERIALS_API AMobTrampleVolume : public AActor
{
	GENERATED_BODY()

public:
	AMobTrampleVolume();

	/** The world footprint. Fit Selected Box Volume To Landscape from the Mat menu sizes this. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="Trample")
	TObjectPtr<UBoxComponent> Bounds;

	/**
	 * What is written to and what the material samples. The same asset has to be set on the
	 * material instance's Trample Mask parameter.
	 *
	 * Its resolution against the box size is the footprint resolution: 1024 across a 40 m box is
	 * 4 cm a texel, which is about as coarse as a boot survives.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample")
	TObjectPtr<UTextureRenderTarget2D> Mask;

	/** Where the material reads this volume's position and size from. Both must name real parameters. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample")
	TObjectPtr<UMaterialParameterCollection> Collection;

	/** Vector parameter taking the box centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample|Advanced")
	FName CentreParameter = TEXT("TrampleCentre");

	/** Vector parameter taking the box extent in XY, and the world size of one texel in Z. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample|Advanced")
	FName ExtentParameter = TEXT("TrampleExtent");

	/** Drawn additively per footstep. Authored by the landscape builder as MI_MobTrampleStamp. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample|Advanced")
	TObjectPtr<UMaterialInterface> StampMaterial;

	/**
	 * How long a print stays at full depth before it begins to fade. Zero holds forever, which is
	 * what snow that settles once wants.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="0.0", ForceUnits="s"))
	float HoldSeconds = 0.f;

	/**
	 * Whether it goes straight to nothing from there, or settles at a shallower depth first and
	 * stays that way for a while.
	 *
	 * The half step is what ground that keeps a mark rather than losing it wants: soft earth that
	 * takes a deep print underfoot and then holds a shallow one, or snow that a print sinks into
	 * and then only slumps.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(EditCondition="HoldSeconds > 0.0"))
	EMobTrampleFade Fade = EMobTrampleFade::Straight;

	/** How long the drop to the shallower depth takes. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="0.0", ForceUnits="s",
		EditCondition="HoldSeconds > 0.0 && Fade == EMobTrampleFade::ThroughHalf", EditConditionHides))
	float HalfFadeSeconds = 4.f;

	/** What depth it settles at, against the print's own. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="0.0", ClampMax="1.0",
		EditCondition="HoldSeconds > 0.0 && Fade == EMobTrampleFade::ThroughHalf", EditConditionHides))
	float HalfFadeAmount = 0.5f;

	/** How long it stays at that depth before it starts leaving. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="0.0", ForceUnits="s",
		EditCondition="HoldSeconds > 0.0 && Fade == EMobTrampleFade::ThroughHalf", EditConditionHides))
	float HoldHalfFadeSeconds = 0.f;

	/**
	 * How long it then takes to disappear. Each print runs its own clock, so one left now fades on
	 * its own schedule rather than at whatever rate the oldest one is on.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample", meta=(ClampMin="0.0", ForceUnits="s",
		EditCondition="HoldSeconds > 0.0"))
	float FadeSeconds = 8.f;

	/** How many prints are kept before the oldest is dropped. Only reached where nothing fades. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample|Advanced", meta=(ClampMin="16"))
	int32 MaxMarks = 512;

	/** How often the target is written. Stamps wait for the next one, which nobody can see. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Trample|Advanced", meta=(ClampMin="0.01"))
	float FlushInterval = 0.1f;

	/** Whether a world position is over this volume. Z is ignored: the mask is projected down. */
	UFUNCTION(BlueprintPure, Category="Trample")
	bool Covers(const FVector& WorldLocation) const;

	/**
	 * How trampled a world position is, 0 to 1, without reading the render target back.
	 *
	 * Mirrored on the CPU as the stamps go down, because gameplay asks this on a footstep and a
	 * readback would cost a stall for one byte. Coarser than the target and good enough to decide
	 * whether a foot is landing on snow or on what the last one uncovered.
	 */
	UFUNCTION(BlueprintPure, Category="Trample")
	float GetTrampleAt(const FVector& WorldLocation) const;

	/** Queues one print. Radius and depth are world units; rotation is degrees about world Z. */
	UFUNCTION(BlueprintCallable, Category="Trample")
	void AddStamp(const FVector& WorldLocation, float Radius, float Strength, float RotationDegrees);

	/** Writes this volume's position and size into the collection, so the material reads this target. */
	UFUNCTION(BlueprintCallable, Category="Trample")
	void PublishBounds();

	/** Empties the target. Nothing survives a level restart otherwise, which is the intent. */
	UFUNCTION(BlueprintCallable, Category="Trample")
	void ClearMask();

	/**
	 * Draws whatever has been queued and fades what was already there, without waiting for the
	 * flush interval. Elapsed is how much time the fade should account for.
	 */
	UFUNCTION(BlueprintCallable, Category="Trample")
	void FlushNow(float ElapsedSeconds = 0.f);

	/** Ticked by the subsystem. Draws once the flush interval is up. */
	void Flush(float DeltaSeconds);

	/** Whether anything is waiting, so the subsystem can leave an idle volume alone. */
	bool HasPendingWork() const { return Pending.Num() > 0 || Marks.Num() > 0; }

	//~ Begin AActor Interface
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	//~ End AActor Interface

protected:
	/** Half extent of the box in world units, XY only. */
	FVector2D WorldExtent() const;

private:
	/** Stamps waiting for the next flush. */
	TArray<FMobTrampleStamp> Pending;

	/** Every print still on the ground, with its own age. */
	TArray<FMobTrampleStamp> Marks;

	/** World area the pending stamps cover, so only the pages they touched are invalidated. */
	FBox PendingBounds = FBox(ForceInit);

	/** A coarse copy of the mask, for GetTrampleAt. One byte a cell, MirrorSize square. */
	TArray<uint8> Mirror;

	static constexpr int32 MirrorSize = 128;

	/**
	 * One stamp instance per quantised strength.
	 *
	 * A canvas resolves a material's parameters when the pass is submitted, not when the draw is
	 * queued, so every stamp sharing one instance would come out at whatever the last one asked
	 * for. Bucketing is what lets a tick's worth of prints go down in a single pass.
	 */
	UPROPERTY(Transient)
	TMap<int32, TObjectPtr<UMaterialInstanceDynamic>> StampVariants;

	float SinceFlush = 0.f;

	/** What a mark is worth now: full until the hold is up, then down to nothing over the fade. */
	float StrengthOf(const FMobTrampleStamp& Mark) const;

	UMaterialInstanceDynamic* StampInstanceFor(float Strength);

	/** Writes a print into the CPU mirror GetTrampleAt reads. */
	void StampMirror(const FVector& WorldLocation, float Radius, float Strength);

	/** Throws away the terrain's cached pages over an area, so a print drawn now is a print seen now. */
	void InvalidateVirtualTextures(const FBox& WorldBounds);
};
