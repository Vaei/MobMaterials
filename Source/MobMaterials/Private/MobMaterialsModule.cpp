// Copyright (c) Jared Taylor

#include "MobMaterialsModule.h"

#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "ShaderCore.h"

#define LOCTEXT_NAMESPACE "MobMaterials"

void FMobMaterialsModule::StartupModule()
{
	// The master materials reach their maths through Custom nodes that include
	// /MobMaterials/Public/*.ush, so the virtual directory has to exist before anything
	// compiles a material. Hence PostConfigInit rather than Default: a material can be pulled in
	// by a startup asset, and an include that resolves to nothing is stripped from the cached
	// data, which surfaces as an undeclared identifier rather than a missing file.
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MobMaterials"));
	if (Plugin.IsValid())
	{
		const FString PluginRoot = FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir());
		AddShaderSourceDirectoryMapping(TEXT("/MobMaterials"), FPaths::Combine(PluginRoot, TEXT("Shaders")));
	}
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FMobMaterialsModule, MobMaterials)
