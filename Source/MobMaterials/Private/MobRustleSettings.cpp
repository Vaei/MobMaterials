// Copyright (c) Jared Taylor

#include "MobRustleSettings.h"

#include "Materials/MaterialParameterCollection.h"

UMobRustleSettings::UMobRustleSettings()
{
	ParameterCollection = TSoftObjectPtr<UMaterialParameterCollection>(
		FSoftObjectPath(TEXT("/MobMaterials/MPC_MobRustle.MPC_MobRustle")));
}
