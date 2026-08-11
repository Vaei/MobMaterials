// Copyright (c) Jared Taylor

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FMobMaterialsModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
