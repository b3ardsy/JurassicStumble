// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "HAL/IConsoleManager.h"
#include "Modules/ModuleManager.h"

class FJurassicPhysicsRefitModule final : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void AuditAsset(const TArray<FString>& Arguments);

	TUniquePtr<FAutoConsoleCommand> AuditAssetCommand;
};
