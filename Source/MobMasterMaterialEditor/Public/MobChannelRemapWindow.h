// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "UObject/Object.h"
#include "MobChannelRemapWindow.generated.h"

class UTexture2D;

/** Which channel of a source texture a slot reads. */
UENUM()
enum class EMobTextureChannel : uint8
{
	R,
	G,
	B,
	A,
};

/** What the three output channels are being packed for. Names the file and sets the suffix. */
UENUM()
enum class EMobRemapTarget : uint8
{
	/** Height, Roughness, Cavity. What a landscape layer reads. */
	HRC		UMETA(DisplayName = "HRC (landscape)"),

	/** Cavity, Roughness, Metallic. What a surface or foliage material reads. */
	CRM		UMETA(DisplayName = "CRM (surface, foliage)"),

	/** Whatever the three slots are set to. Named by hand. */
	Custom	UMETA(DisplayName = "Custom"),
};

/**
 * How the incoming art is packed. Choosing one fills the three output slots; they stay editable
 * afterwards, so a preset is a starting point rather than a decision made behind your back.
 */
UENUM()
enum class EMobRemapSource : uint8
{
	/** Leave the slots alone. */
	Custom			UMETA(DisplayName = "Custom"),

	/** One texture, R occlusion, G roughness, B metallic. Substance and glTF default. */
	ORM				UMETA(DisplayName = "Packed ORM"),

	/** One texture, R metallic, G roughness, B occlusion. */
	MRAO			UMETA(DisplayName = "Packed MRAO"),

	/** One texture, R roughness, G metallic, B occlusion. */
	RMA				UMETA(DisplayName = "Packed RMA"),

	/** A texture each, read from their red channels. */
	Separate		UMETA(DisplayName = "Separate maps"),
};

/** One output channel: a texture and the channel of it to take, or a constant when no texture is set. */
USTRUCT()
struct FMobChannelSource
{
	GENERATED_BODY()

	/** Leave empty to write Constant across the whole channel. */
	UPROPERTY(EditAnywhere, Category="Channel")
	TObjectPtr<UTexture2D> Texture = nullptr;

	UPROPERTY(EditAnywhere, Category="Channel")
	EMobTextureChannel Channel = EMobTextureChannel::R;

	/** Write one minus the value. A gloss map becomes a roughness map. */
	UPROPERTY(EditAnywhere, Category="Channel")
	bool bInvert = false;

	/** What the channel holds where no texture is given. */
	UPROPERTY(EditAnywhere, Category="Channel", meta=(UIMin="0", UIMax="1", ClampMin="0", ClampMax="1"))
	float Constant = 1.f;
};

/** What Remap Texture Channels is set to. Config, so it opens where it was left. */
UCLASS(Config=EditorPerProjectUserSettings)
class MOBMASTERMATERIALEDITOR_API UMobChannelRemapOptions : public UObject
{
	GENERATED_BODY()

public:
	/** What the result is for. Sets the output's name suffix and what each slot means. */
	UPROPERTY(EditAnywhere, Config, Category="Remap")
	EMobRemapTarget Target = EMobRemapTarget::CRM;

	/** How the incoming art is packed. Changing this fills the three slots below. */
	UPROPERTY(EditAnywhere, Category="Remap")
	EMobRemapSource Source = EMobRemapSource::ORM;

	/** The packed texture the preset reads from. */
	UPROPERTY(EditAnywhere, Category="Remap",
		meta=(EditCondition="Source != EMobRemapSource::Custom && Source != EMobRemapSource::Separate",
			EditConditionHides))
	TObjectPtr<UTexture2D> PackedTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="Remap",
		meta=(EditCondition="Source == EMobRemapSource::Separate", EditConditionHides))
	TObjectPtr<UTexture2D> RoughnessTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="Remap",
		meta=(EditCondition="Source == EMobRemapSource::Separate", EditConditionHides))
	TObjectPtr<UTexture2D> MetallicTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="Remap",
		meta=(EditCondition="Source == EMobRemapSource::Separate", EditConditionHides))
	TObjectPtr<UTexture2D> HeightTexture = nullptr;

	UPROPERTY(EditAnywhere, Category="Remap",
		meta=(EditCondition="Source == EMobRemapSource::Separate", EditConditionHides))
	TObjectPtr<UTexture2D> CavityTexture = nullptr;

	/**
	 * Ambient occlusion, if the art has it.
	 *
	 * It is deliberately not wired to anything by a preset. These materials have no AO input: the
	 * renderer supplies its own occlusion and a baked one on top double darkens. It is here so a
	 * pack that only ships AO can be pointed at the cavity slot on purpose.
	 */
	UPROPERTY(EditAnywhere, Category="Remap",
		meta=(EditCondition="Source == EMobRemapSource::Separate", EditConditionHides))
	TObjectPtr<UTexture2D> OcclusionTexture = nullptr;

	/** Height on an HRC, cavity on a CRM. */
	UPROPERTY(EditAnywhere, Category="Output Channels", meta=(DisplayName="Red"))
	FMobChannelSource Red;

	/** Roughness on both. */
	UPROPERTY(EditAnywhere, Category="Output Channels", meta=(DisplayName="Green"))
	FMobChannelSource Green;

	/** Cavity on an HRC, metallic on a CRM. */
	UPROPERTY(EditAnywhere, Category="Output Channels", meta=(DisplayName="Blue"))
	FMobChannelSource Blue;

	/** Where the packed texture is written. */
	UPROPERTY(EditAnywhere, Config, Category="Output", meta=(ContentDir))
	FDirectoryPath OutputPath;

	/**
	 * Name of the result. Empty takes the first source texture's name with its suffix replaced.
	 *
	 * An existing asset of that name is overwritten in place, so a repack keeps every material that
	 * already points at it.
	 */
	UPROPERTY(EditAnywhere, Category="Output")
	FString OutputName;

	/**
	 * Set while a preset has put ambient occlusion in the cavity slot, so the window can say so.
	 *
	 * Cleared as soon as that slot is edited by hand: past that point the choice is yours and the
	 * warning would be describing something that is no longer there.
	 */
	UPROPERTY(Transient)
	bool bCavityFromOcclusion = false;

	/** Fills Red, Green and Blue from Source and the textures above. */
	void ApplyPreset();

#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
};

/** Remap Texture Channels: a details view over the options, and Remap. */
class FMobChannelRemapWindow
{
public:
	static void Open();

private:
	/** Packs the three slots into one texture. Returns it, or null with a reason. */
	static UTexture2D* Remap(const UMobChannelRemapOptions& Options, FText& OutError);

	/** The suffix a target's output carries. */
	static FString TargetSuffix(EMobRemapTarget Target);

	/** One line saying what Remap would write, shown under the details view. */
	static FText Summary(const UMobChannelRemapOptions& Options);

	/**
	 * Whether the three slots would copy one texture through unchanged.
	 *
	 * ORM and CRM are the same three channels in the same order - occlusion sits where cavity does -
	 * so asking for one from the other writes a second identical asset and nothing else.
	 */
	static bool IsIdentityCopy(const UMobChannelRemapOptions& Options, FText& OutReason);

	/** What the window says above the buttons, and whether it says it as a warning. */
	static FText Warning(const UMobChannelRemapOptions& Options);
};
