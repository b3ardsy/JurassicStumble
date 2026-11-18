// Copyright Epic Games, Inc. All Rights Reserved.

#include "JurassicPhysicsRefit.h"

#include "Engine/SkeletalMesh.h"
#include "HAL/IConsoleManager.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "PhysicsEngine/SkeletalBodySetup.h"

namespace JurassicPhysicsDiagnostics
{
	FString NormalizeObjectPath(const FString& Input)
	{
		FString Path = Input.TrimStartAndEnd();
		if (Path.IsEmpty() || Path.Contains(TEXT(".")))
		{
			return Path;
		}

		return FString::Printf(TEXT("%s.%s"), *Path, *FPackageName::GetShortName(Path));
	}

	const TCHAR* MotionName(const EAngularConstraintMotion Motion)
	{
		switch (Motion)
		{
		case ACM_Free: return TEXT("Free");
		case ACM_Limited: return TEXT("Limited");
		case ACM_Locked: return TEXT("Locked");
		default: return TEXT("Unknown");
		}
	}
}

void FJurassicPhysicsRefitModule::StartupModule()
{
	AuditAssetCommand = MakeUnique<FAutoConsoleCommand>(
		TEXT("JurassicPhysicsRefit.AuditAsset"),
		TEXT("Read-only Physics Asset audit. Usage: JurassicPhysicsRefit.AuditAsset /Game/Path/PA_Asset"),
		FConsoleCommandWithArgsDelegate::CreateRaw(this, &FJurassicPhysicsRefitModule::AuditAsset));
}

void FJurassicPhysicsRefitModule::ShutdownModule()
{
	AuditAssetCommand.Reset();
}

void FJurassicPhysicsRefitModule::AuditAsset(const TArray<FString>& Arguments)
{
	using namespace JurassicPhysicsDiagnostics;

	if (Arguments.Num() != 1)
	{
		UE_LOG(LogTemp, Error, TEXT("JurassicPhysicsRefit AUDIT: expected exactly one Physics Asset path. Usage: JurassicPhysicsRefit.AuditAsset /Game/Path/PA_Asset"));
		return;
	}

	const FString ObjectPath = NormalizeObjectPath(Arguments[0]);
	UPhysicsAsset* Asset = LoadObject<UPhysicsAsset>(nullptr, *ObjectPath);
	if (!Asset)
	{
		UE_LOG(LogTemp, Error, TEXT("JurassicPhysicsRefit AUDIT: unable to load %s"), *ObjectPath);
		return;
	}

	USkeletalMesh* PreviewMesh = Asset->GetPreviewMesh();
	const FReferenceSkeleton* ReferenceSkeleton = PreviewMesh ? &PreviewMesh->GetRefSkeleton() : nullptr;
	bool IsValid = Asset->SkeletalBodySetups.Num() > 0;

	UE_LOG(LogTemp, Display,
		TEXT("JurassicPhysicsRefit AUDIT BEGIN asset=%s bodies=%d constraints=%d collisionDisablePairs=%d previewMesh=%s"),
		*Asset->GetPathName(),
		Asset->SkeletalBodySetups.Num(),
		Asset->ConstraintSetup.Num(),
		Asset->CollisionDisableTable.Num(),
		*GetNameSafe(PreviewMesh));

	for (int32 BodyIndex = 0; BodyIndex < Asset->SkeletalBodySetups.Num(); ++BodyIndex)
	{
		const USkeletalBodySetup* Body = Asset->SkeletalBodySetups[BodyIndex];
		if (!Body)
		{
			IsValid = false;
			UE_LOG(LogTemp, Error, TEXT("JurassicPhysicsRefit AUDIT BODY index=%d is null"), BodyIndex);
			continue;
		}

		const FKAggregateGeom& Geometry = Body->AggGeom;
		const bool IsCompatibleBone = !ReferenceSkeleton || ReferenceSkeleton->FindBoneIndex(Body->BoneName) != INDEX_NONE;
		IsValid &= IsCompatibleBone;

		UE_LOG(LogTemp, Display,
			TEXT("JurassicPhysicsRefit AUDIT BODY index=%d bone=%s physicsType=%s calculatedMass=%.12g massScale=%.9g overrideMass=%s collisionResponse=%d geometry[sphere=%d box=%d capsule=%d taperedCapsule=%d convex=%d] compatibleBone=%s"),
			BodyIndex,
			*Body->BoneName.ToString(),
			LexToString(static_cast<EPhysicsType>(Body->PhysicsType.GetValue())),
			Body->CalculateMass(),
			Body->DefaultInstance.MassScale,
			Body->DefaultInstance.bOverrideMass ? TEXT("true") : TEXT("false"),
			static_cast<int32>(Body->CollisionReponse.GetValue()),
			Geometry.SphereElems.Num(),
			Geometry.BoxElems.Num(),
			Geometry.SphylElems.Num(),
			Geometry.TaperedCapsuleElems.Num(),
			Geometry.ConvexElems.Num(),
			IsCompatibleBone ? TEXT("true") : TEXT("false"));
	}

	for (int32 ConstraintIndex = 0; ConstraintIndex < Asset->ConstraintSetup.Num(); ++ConstraintIndex)
	{
		const UPhysicsConstraintTemplate* Template = Asset->ConstraintSetup[ConstraintIndex];
		if (!Template)
		{
			IsValid = false;
			UE_LOG(LogTemp, Error, TEXT("JurassicPhysicsRefit AUDIT CONSTRAINT index=%d is null"), ConstraintIndex);
			continue;
		}

		const FConstraintInstance& Constraint = Template->DefaultInstance;
		const FConstraintProfileProperties& Profile = Constraint.ProfileInstance;
		const FAngularDriveConstraint& Drive = Profile.AngularDrive;
		const bool HasChildBody = Asset->FindBodyIndex(Constraint.ConstraintBone1) != INDEX_NONE;
		const bool HasParentBody = Asset->FindBodyIndex(Constraint.ConstraintBone2) != INDEX_NONE;
		const bool IsProfileSynchronized = FConstraintProfileProperties::StaticStruct()->CompareScriptStruct(
			&Template->GetConstraintProfilePropertiesOrDefault(NAME_None), &Profile, 0);
		IsValid &= HasChildBody && HasParentBody && IsProfileSynchronized;

		UE_LOG(LogTemp, Display,
			TEXT("JurassicPhysicsRefit AUDIT CONSTRAINT index=%d child=%s parent=%s connected=%s motions[T=%s S1=%s S2=%s] limits[T=%.3f S1=%.3f S2=%.3f] driveMode=%d orientationDrive=%s velocityDrive=%s accelMode=%s target=%s targetVelocity=%s profileSynced=%s"),
			ConstraintIndex,
			*Constraint.ConstraintBone1.ToString(),
			*Constraint.ConstraintBone2.ToString(),
			HasChildBody && HasParentBody ? TEXT("true") : TEXT("false"),
			MotionName(Profile.TwistLimit.TwistMotion),
			MotionName(Profile.ConeLimit.Swing1Motion),
			MotionName(Profile.ConeLimit.Swing2Motion),
			Profile.TwistLimit.TwistLimitDegrees,
			Profile.ConeLimit.Swing1LimitDegrees,
			Profile.ConeLimit.Swing2LimitDegrees,
			static_cast<int32>(Drive.AngularDriveMode),
			Drive.IsOrientationDriveEnabled() ? TEXT("true") : TEXT("false"),
			Drive.IsVelocityDriveEnabled() ? TEXT("true") : TEXT("false"),
			Drive.bAccelerationMode ? TEXT("true") : TEXT("false"),
			*Drive.OrientationTarget.ToString(),
			*Drive.AngularVelocityTarget.ToString(),
			IsProfileSynchronized ? TEXT("true") : TEXT("false"));
	}

	UE_LOG(LogTemp, Display,
		TEXT("JurassicPhysicsRefit AUDIT %s asset=%s bodies=%d constraints=%d"),
		IsValid ? TEXT("PASS") : TEXT("FAIL"),
		*Asset->GetPathName(),
		Asset->SkeletalBodySetups.Num(),
		Asset->ConstraintSetup.Num());
}

IMPLEMENT_MODULE(FJurassicPhysicsRefitModule, JurassicPhysicsRefit)
