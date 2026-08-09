// Copyright (c) Jared Taylor. All Rights Reserved

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FMobMasterMaterialModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
};
