// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "Modules/ModuleManager.h"

class FJurassicPhysicsRefitModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	void Analyze();
	void Execute();
	void Create9Body();
	void Create12BodyLowerLegFit();
	void AnalyzeHips();
	void PlanHipsRefit();
	void AnalyzeTorso();
	void PlanTorsoRefit();
	void AnalyzeMassBalance();
	void ValidateMassBalance();
	void Cleanup();
	TUniquePtr<class FAutoConsoleCommand> AnalyzeCommand;
	TUniquePtr<class FAutoConsoleCommand> ExecuteCommand;
	TUniquePtr<class FAutoConsoleCommand> Create9BodyCommand;
	TUniquePtr<class FAutoConsoleCommand> Create12BodyLowerLegFitCommand;
	TUniquePtr<class FAutoConsoleCommand> AnalyzeHipsCommand;
	TUniquePtr<class FAutoConsoleCommand> PlanHipsRefitCommand;
	TUniquePtr<class FAutoConsoleCommand> AnalyzeTorsoCommand;
	TUniquePtr<class FAutoConsoleCommand> PlanTorsoRefitCommand;
	TUniquePtr<class FAutoConsoleCommand> AnalyzeMassBalanceCommand;
	TUniquePtr<class FAutoConsoleCommand> ValidateMassBalanceCommand;
	TUniquePtr<class FAutoConsoleCommand> CleanupCommand;
};
