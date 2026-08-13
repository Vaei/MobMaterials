// Copyright (c) Jared Taylor

#include "MobTrampleVolume.h"

#include "MobTrampleSubsystem.h"
#include "Components/BoxComponent.h"
#include "Components/RuntimeVirtualTextureComponent.h"
#include "Engine/Canvas.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Kismet/KismetMaterialLibrary.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace
{
	/** How many distinct strengths a tick can draw before two of them share an instance. */
	constexpr int32 MobTrampleStrengthBuckets = 16;

	const FName MobTrampleStrengthParam(TEXT("Strength"));

	int32 MobTrampleStrengthBucket(float Strength)
	{
		return FMath::Clamp(FMath::RoundToInt(Strength * (MobTrampleStrengthBuckets - 1)),
			0, MobTrampleStrengthBuckets - 1);
	}
}

AMobTrampleVolume::AMobTrampleVolume()
{
	PrimaryActorTick.bCanEverTick = false;

	Bounds = CreateDefaultSubobject<UBoxComponent>(TEXT("Bounds"));
	Bounds->SetBoxExtent(FVector(2000.f, 2000.f, 500.f));
	Bounds->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Bounds->SetGenerateOverlapEvents(false);
	SetRootComponent(Bounds);
}

void AMobTrampleVolume::BeginPlay()
{
	Super::BeginPlay();

	ClearMask();

	if (UMobTrampleSubsystem* Subsystem = UMobTrampleSubsystem::Get(this))
	{
		Subsystem->RegisterVolume(this);
	}
}

void AMobTrampleVolume::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UMobTrampleSubsystem* Subsystem = UMobTrampleSubsystem::Get(this))
	{
		Subsystem->UnregisterVolume(this);
	}

	// The target is an asset the editor viewport keeps sampling once play stops, so prints left in
	// it outlive the session that made them.
	ClearMask();

	Super::EndPlay(EndPlayReason);
}

FVector2D AMobTrampleVolume::WorldExtent() const
{
	const FVector Extent = Bounds ? Bounds->GetScaledBoxExtent() : FVector::ZeroVector;
	return FVector2D(FMath::Max(Extent.X, UE_KINDA_SMALL_NUMBER),
		FMath::Max(Extent.Y, UE_KINDA_SMALL_NUMBER));
}

bool AMobTrampleVolume::Covers(const FVector& WorldLocation) const
{
	const FVector Centre = GetActorLocation();
	const FVector2D Extent = WorldExtent();
	return FMath::Abs(WorldLocation.X - Centre.X) <= Extent.X
		&& FMath::Abs(WorldLocation.Y - Centre.Y) <= Extent.Y;
}

void AMobTrampleVolume::PublishBounds()
{
	if (!Collection)
	{
		return;
	}

	const FVector Centre = GetActorLocation();
	const FVector2D Extent = WorldExtent();

	// Z carries the world size of one texel, which is what turns a difference between two taps
	// into a slope the normal can be tilted by.
	const int32 Width = Mask ? FMath::Max(Mask->SizeX, 1) : 1;
	const double Texel = Extent.X * 2.0 / static_cast<double>(Width);

	UKismetMaterialLibrary::SetVectorParameterValue(this, Collection, CentreParameter,
		FLinearColor(Centre.X, Centre.Y, Centre.Z, 0.f));
	UKismetMaterialLibrary::SetVectorParameterValue(this, Collection, ExtentParameter,
		FLinearColor(Extent.X, Extent.Y, Texel, 0.f));
}

void AMobTrampleVolume::ClearMask()
{
	if (Mask)
	{
		UKismetRenderingLibrary::ClearRenderTarget2D(this, Mask, FLinearColor::Black);
	}

	Pending.Reset();
	Marks.Reset();
	PendingBounds = FBox(ForceInit);
	Mirror.Reset();

	// The target is one asset, so a world that is not this one may still be showing what was in it.
	// This is the only path where that is true, and the only one that pays for a whole-volume pass.
	if (Bounds)
	{
		InvalidateVirtualTextures(Bounds->Bounds.GetBox(), true);
	}
}

void AMobTrampleVolume::AddStamp(const FVector& WorldLocation, float Radius, float Strength,
	float RotationDegrees)
{
	if (!Mask || Radius <= 0.f || Strength <= 0.f)
	{
		return;
	}

	const FVector Centre = GetActorLocation();
	const FVector2D Extent = WorldExtent();
	const FVector2D Local((WorldLocation.X - Centre.X) / Extent.X, (WorldLocation.Y - Centre.Y) / Extent.Y);

	FMobTrampleStamp Stamp;
	Stamp.Centre = FVector2D((Local.X * 0.5 + 0.5) * Mask->SizeX, (Local.Y * 0.5 + 0.5) * Mask->SizeY);
	Stamp.Radius = Radius / (Extent.X * 2.0) * Mask->SizeX;
	Stamp.Rotation = RotationDegrees;
	Stamp.Strength = FMath::Clamp(Strength, 0.f, 1.f);
	Stamp.World = WorldLocation;
	Stamp.WorldRadius = Radius;
	Pending.Add(Stamp);

	// The pages the terrain has already cached over this spot have to be thrown away, or the print
	// is in the target and invisible until something else happens to re-render them.
	const FVector Half(Radius, Radius, Bounds->GetScaledBoxExtent().Z);
	PendingBounds += FBox(WorldLocation - Half, WorldLocation + Half);

}

float AMobTrampleVolume::StrengthOf(const FMobTrampleStamp& Mark) const
{
	if (HoldSeconds <= 0.f)
	{
		return Mark.Strength;
	}

	float Fading = Mark.Age - HoldSeconds;
	if (Fading <= 0.f)
	{
		return Mark.Strength;
	}

	// Where the last fade starts from, which is the print's own depth unless it settled at a
	// shallower one on the way.
	float From = 1.f;
	if (Fade == EMobTrampleFade::ThroughHalf)
	{
		const float Half = FMath::Clamp(HalfFadeAmount, 0.f, 1.f);
		if (HalfFadeSeconds > 0.f)
		{
			if (Fading < HalfFadeSeconds)
			{
				return Mark.Strength * FMath::Lerp(1.f, Half, Fading / HalfFadeSeconds);
			}
			Fading -= HalfFadeSeconds;
		}

		if (Fading < HoldHalfFadeSeconds)
		{
			return Mark.Strength * Half;
		}
		Fading -= HoldHalfFadeSeconds;
		From = Half;
	}

	if (FadeSeconds <= 0.f)
	{
		return 0.f;
	}

	return Mark.Strength * From * FMath::Max(1.f - Fading / FadeSeconds, 0.f);
}

void AMobTrampleVolume::StampMirror(const FVector& WorldLocation, float Radius, float Strength)
{
	if (Mirror.Num() != MirrorSize * MirrorSize)
	{
		Mirror.SetNumZeroed(MirrorSize * MirrorSize);
	}

	const FVector Centre = GetActorLocation();
	const FVector2D Extent = WorldExtent();
	const double PerCell = Extent.X * 2.0 / MirrorSize;
	const int32 Reach = FMath::Max(FMath::CeilToInt(Radius / PerCell), 0);

	const int32 CellX = FMath::FloorToInt(((WorldLocation.X - Centre.X) / Extent.X * 0.5 + 0.5) * MirrorSize);
	const int32 CellY = FMath::FloorToInt(((WorldLocation.Y - Centre.Y) / Extent.Y * 0.5 + 0.5) * MirrorSize);
	const uint8 Value = static_cast<uint8>(FMath::Clamp(Strength, 0.f, 1.f) * 255.f);

	for (int32 Y = CellY - Reach; Y <= CellY + Reach; ++Y)
	{
		for (int32 X = CellX - Reach; X <= CellX + Reach; ++X)
		{
			if (X < 0 || Y < 0 || X >= MirrorSize || Y >= MirrorSize)
			{
				continue;
			}
			if (FMath::Square(X - CellX) + FMath::Square(Y - CellY) > FMath::Square(Reach))
			{
				continue;
			}
			uint8& Cell = Mirror[Y * MirrorSize + X];
			Cell = FMath::Max(Cell, Value);
		}
	}
}

float AMobTrampleVolume::GetTrampleAt(const FVector& WorldLocation) const
{
	if (Mirror.Num() != MirrorSize * MirrorSize || !Covers(WorldLocation))
	{
		return 0.f;
	}

	const FVector Centre = GetActorLocation();
	const FVector2D Extent = WorldExtent();
	const int32 X = FMath::Clamp(
		FMath::FloorToInt(((WorldLocation.X - Centre.X) / Extent.X * 0.5 + 0.5) * MirrorSize), 0, MirrorSize - 1);
	const int32 Y = FMath::Clamp(
		FMath::FloorToInt(((WorldLocation.Y - Centre.Y) / Extent.Y * 0.5 + 0.5) * MirrorSize), 0, MirrorSize - 1);

	return Mirror[Y * MirrorSize + X] / 255.f;
}

FBox AMobTrampleVolume::MarkBounds(const FMobTrampleStamp& Mark, double HalfHeight)
{
	const FVector Half(Mark.WorldRadius, Mark.WorldRadius, HalfHeight);
	return FBox(Mark.World - Half, Mark.World + Half);
}

void AMobTrampleVolume::InvalidateVirtualTextures(const FBox& WorldBounds, bool bAllWorlds)
{
	if (!WorldBounds.IsValid)
	{
		return;
	}

	const FBoxSphereBounds Dirty(WorldBounds);
	for (TObjectIterator<URuntimeVirtualTextureComponent> It; It; ++It)
	{
		URuntimeVirtualTextureComponent* Component = *It;
		if (!Component || !Component->IsRegistered())
		{
			continue;
		}
		if (!bAllWorlds && Component->GetWorld() != GetWorld())
		{
			continue;
		}
		Component->Invalidate(Dirty);
	}
}

UMaterialInstanceDynamic* AMobTrampleVolume::StampInstanceFor(float Strength)
{
	const int32 Bucket = MobTrampleStrengthBucket(Strength);

	if (const TObjectPtr<UMaterialInstanceDynamic>* Existing = StampVariants.Find(Bucket))
	{
		return *Existing;
	}

	UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(StampMaterial, this);
	if (!Instance)
	{
		return nullptr;
	}

	Instance->SetScalarParameterValue(MobTrampleStrengthParam,
		static_cast<float>(Bucket) / static_cast<float>(MobTrampleStrengthBuckets - 1));
	StampVariants.Add(Bucket, Instance);
	return Instance;
}

void AMobTrampleVolume::Flush(float DeltaSeconds)
{
	SinceFlush += DeltaSeconds;
	if (SinceFlush < FlushInterval)
	{
		return;
	}

	const float Elapsed = SinceFlush;
	SinceFlush = 0.f;
	FlushNow(Elapsed);
}

void AMobTrampleVolume::FlushNow(float Elapsed)
{
	if (!Mask || !StampMaterial)
	{
		return;
	}

	// Ages advance whether or not anything is drawn, so a print left while the camera was elsewhere
	// is as old as it should be when it is next looked at.
	const double HalfHeight = Bounds ? Bounds->GetScaledBoxExtent().Z : 0.0;
	FBox Dirty = PendingBounds;
	bool bAnyFading = false;

	for (int32 i = Marks.Num() - 1; i >= 0; --i)
	{
		const float Was = StrengthOf(Marks[i]);
		Marks[i].Age += Elapsed;
		const float Now = StrengthOf(Marks[i]);

		const bool bGone = Now <= 1.f / 255.f;
		const bool bFaded = bSkipUnchangedRedraws
			? MobTrampleStrengthBucket(Was) != MobTrampleStrengthBucket(Now)
			: !FMath::IsNearlyEqual(Was, Now);

		if (bGone || bFaded)
		{
			Dirty += MarkBounds(Marks[i], HalfHeight);
			bAnyFading = true;
		}

		if (bGone)
		{
			Marks.RemoveAt(i);
		}
	}

	if (Pending.Num() == 0 && !bAnyFading)
	{
		return;
	}

	Marks.Append(Pending);
	const int32 Cap = FMath::Max(MaxMarks, 16);
	if (Marks.Num() > Cap)
	{
		const int32 Dropped = Marks.Num() - Cap;
		for (int32 i = 0; i < Dropped; ++i)
		{
			Dirty += MarkBounds(Marks[i], HalfHeight);
		}
		Marks.RemoveAt(0, Dropped);
	}

	// Redrawn from the list rather than decayed in place. A print's own age decides what it is
	// worth, and there is no way to take one print's depth back out of a target they all share.
	UCanvas* Canvas = nullptr;
	FVector2D Size = FVector2D::ZeroVector;
	FDrawToRenderTargetContext Context;

	UKismetRenderingLibrary::ClearRenderTarget2D(this, Mask, FLinearColor::Black);
	UKismetRenderingLibrary::BeginDrawCanvasToRenderTarget(this, Mask, Canvas, Size, Context);

	if (Canvas)
	{
		for (const FMobTrampleStamp& Mark : Marks)
		{
			if (UMaterialInstanceDynamic* Instance = StampInstanceFor(StrengthOf(Mark)))
			{
				const FVector2D Extent(Mark.Radius * 2.0, Mark.Radius * 2.0);
				Canvas->K2_DrawMaterial(Instance, Mark.Centre - Extent * 0.5, Extent,
					FVector2D::ZeroVector, FVector2D::UnitVector, Mark.Rotation, FVector2D(0.5, 0.5));
			}
		}
	}

	UKismetRenderingLibrary::EndDrawCanvasToRenderTarget(this, Context);

	// The mirror is what gameplay asks, so it is rebuilt from the same list the target was.
	Mirror.Reset();
	for (const FMobTrampleStamp& Mark : Marks)
	{
		StampMirror(Mark.World, Mark.WorldRadius, StrengthOf(Mark));
	}

	InvalidateVirtualTextures(!bInvalidateChangedAreaOnly && bAnyFading && Bounds
		? Bounds->Bounds.GetBox() : Dirty);
	PendingBounds = FBox(ForceInit);
	Pending.Reset();
}
