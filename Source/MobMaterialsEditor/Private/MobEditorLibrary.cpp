// Copyright (c) Jared Taylor

#include "MobEditorLibrary.h"

#include "Editor.h"
#include "EngineUtils.h"
#include "LandscapeProxy.h"
#include "RenderingThread.h"

bool UMobEditorLibrary::LeaveMobilePreview()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || World->GetFeatureLevel() > ERHIFeatureLevel::ES3_1)
	{
		return false;
	}

	// Toggled off rather than replaced. Handing SetPreviewPlatform a bare feature level throws away
	// the shader platform and platform names with it, and the toolbar has nothing left to offer to
	// turn back on.
	if (GEditor->IsFeatureLevelPreviewActive())
	{
		GEditor->ToggleFeatureLevelPreview();
	}

	return true;
}

void UMobEditorLibrary::RestoreMobilePreview()
{
	if (GEditor && GEditor->IsFeatureLevelPreviewEnabled() && !GEditor->IsFeatureLevelPreviewActive())
	{
		GEditor->ToggleFeatureLevelPreview();
	}
}

int32 UMobEditorLibrary::RebuildLandscapePhysicalMaterial()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return 0;
	}

	// ULandscapeComponent::CanUpdatePhysicalMaterial refuses outright below SM5, so in the mobile
	// preview this project switches to on every map load the bake silently never runs - and an
	// invalidated component then stays invalid, reporting the engine default underfoot.
	if (LeaveMobilePreview())
	{
		UE_LOG(LogTemp, Display,
			TEXT("MobMaterials: left the mobile preview to bake; the landscape cannot bake at ES3.1."));
	}

	int32 Rebuilt = 0;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		ALandscapeProxy* Proxy = *It;

		// Invalidated first: the build is a no-op on components the proxy still believes are up to
		// date, and after a regenerate they all believe that while holding the old master's answer.
		Proxy->InvalidatePhysicalMaterial();
		if (Proxy->BuildPhysicalMaterial())
		{
			++Rebuilt;
		}
	}

	return Rebuilt;
}

int32 UMobEditorLibrary::RebuildLandscapePhysicalMaterialSync(int32 MaxPasses)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return 0;
	}

	// The preview is the caller's business: leaving it is asynchronous, and baking before the
	// feature level has actually moved silently does nothing at all.
	TArray<ALandscapeProxy*> Proxies;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		It->InvalidatePhysicalMaterial();
		Proxies.Add(*It);
	}

	// Each pass starts what it can and finishes what the last one started, and the readback in
	// between only lands once the renderer has caught up.
	for (int32 Pass = 0; Pass < FMath::Max(MaxPasses, 1); ++Pass)
	{
		for (ALandscapeProxy* Proxy : Proxies)
		{
			Proxy->BuildPhysicalMaterial();
		}
		FlushRenderingCommands();
	}

	return GetOutdatedPhysicalMaterialComponentCount();
}

int32 UMobEditorLibrary::GetOutdatedPhysicalMaterialComponentCount()
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World)
	{
		return 0;
	}

	int32 Outdated = 0;
	for (TActorIterator<ALandscapeProxy> It(World); It; ++It)
	{
		Outdated += It->GetOudatedPhysicalMaterialComponentsCount();
	}

	return Outdated;
}
