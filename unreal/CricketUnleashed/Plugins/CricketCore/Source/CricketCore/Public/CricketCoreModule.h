#pragma once

#include "Modules/ModuleManager.h"

class FCricketCoreModule : public IModuleInterface
{
public:
	virtual void StartupModule() override {}
	virtual void ShutdownModule() override {}
};
