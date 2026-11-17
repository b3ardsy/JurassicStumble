#include "JurassicPhysicsRefit.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Chaos/ChaosEngineInterface.h"
#include "CompGeom/FitOrientedBox3.h"
#include "Engine/SkeletalMesh.h"
#include "Factories/FbxSkeletalMeshImportData.h"
#include "HAL/IConsoleManager.h"
#include "MeshUtilitiesCommon.h"
#include "MeshUtilitiesEngine.h"
#include "Misc/PackageName.h"
#include "PhysicsAssetUtils.h"
#include "PhysicsEngine/AggregateGeom.h"
#include "PhysicsEngine/BodySetup.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkinWeightVertexBuffer.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Editor.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/World.h"
#include "Subsystems/EditorAssetSubsystem.h"

namespace JurassicPhysicsRefit
{
	constexpr TCHAR MeshPath[] = TEXT("/Game/ThirdParty/KayKit/Characters/Monster/SK_Monster.SK_Monster");
	constexpr TCHAR BaselinePath[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_Player.PA_Monster_Player");
	constexpr TCHAR DestinationPackage[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_RefitTest");
	constexpr TCHAR DestinationObjectPath[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_RefitTest.PA_Monster_RefitTest");
	constexpr TCHAR DestinationName[] = TEXT("PA_Monster_RefitTest");
	constexpr TCHAR NineBodyPackage[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_9BodyTest");
	constexpr TCHAR NineBodyObjectPath[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_9BodyTest.PA_Monster_9BodyTest");
	constexpr TCHAR NineBodyName[] = TEXT("PA_Monster_9BodyTest");
	constexpr TCHAR LowerLegFitPackage[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_12BodyFitTest");
	constexpr TCHAR LowerLegFitObjectPath[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_12BodyFitTest.PA_Monster_12BodyFitTest");
	constexpr TCHAR LowerLegFitName[] = TEXT("PA_Monster_12BodyFitTest");
	constexpr TCHAR MassBalancePackage[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_MassBalanceTest");
	constexpr TCHAR MassBalanceObjectPath[] = TEXT("/Game/JurassicStumble/Physics/PA_Monster_MassBalanceTest.PA_Monster_MassBalanceTest");

	const TArray<FName>& NineBodyBones()
	{
		static const TArray<FName> Names = { TEXT("hips"), TEXT("chest"), TEXT("head"), TEXT("upperarm_l"), TEXT("upperarm_r"), TEXT("upperleg_l"), TEXT("lowerleg_l"), TEXT("upperleg_r"), TEXT("lowerleg_r") };
		return Names;
	}

	TArray<FTransform> ComponentRefTransforms(const USkeletalMesh* Mesh);
	FString BoundsText(const FBox& Box);

	bool SameGeometry(const FKAggregateGeom& A, const FKAggregateGeom& B)
	{
		if (A.SphereElems.Num()!=B.SphereElems.Num() || A.SphylElems.Num()!=B.SphylElems.Num() || A.BoxElems.Num()!=B.BoxElems.Num() || A.ConvexElems.Num()!=B.ConvexElems.Num()) return false;
		for(int32 I=0;I<A.SphereElems.Num();++I) if(A.SphereElems[I].Center!=B.SphereElems[I].Center || A.SphereElems[I].Radius!=B.SphereElems[I].Radius) return false;
		for(int32 I=0;I<A.SphylElems.Num();++I) if(!A.SphylElems[I].GetTransform().Equals(B.SphylElems[I].GetTransform()) || A.SphylElems[I].Radius!=B.SphylElems[I].Radius || A.SphylElems[I].Length!=B.SphylElems[I].Length) return false;
		for(int32 I=0;I<A.BoxElems.Num();++I) if(!A.BoxElems[I].GetTransform().Equals(B.BoxElems[I].GetTransform()) || A.BoxElems[I].X!=B.BoxElems[I].X || A.BoxElems[I].Y!=B.BoxElems[I].Y || A.BoxElems[I].Z!=B.BoxElems[I].Z) return false;
		return true;
	}

	const TCHAR* AngularMotionText(EAngularConstraintMotion Motion)
	{
		switch(Motion) { case ACM_Free:return TEXT("Free"); case ACM_Limited:return TEXT("Limited"); case ACM_Locked:return TEXT("Locked"); default:return TEXT("Unknown"); }
	}

	const TCHAR* LinearMotionText(ELinearConstraintMotion Motion)
	{
		switch(Motion) { case LCM_Free:return TEXT("Free"); case LCM_Limited:return TEXT("Limited"); case LCM_Locked:return TEXT("Locked"); default:return TEXT("Unknown"); }
	}

	FString TransformText(const FTransform& T)
	{
		return FString::Printf(TEXT("P=%s R=%s S=%s"),*T.GetTranslation().ToString(),*T.Rotator().ToString(),*T.GetScale3D().ToString());
	}

	FString AxesText(const FTransform& T)
	{
		return FString::Printf(TEXT("TwistX=%s Swing2Y=%s Swing1Z=%s"),*T.GetUnitAxis(EAxis::X).ToString(),*T.GetUnitAxis(EAxis::Y).ToString(),*T.GetUnitAxis(EAxis::Z).ToString());
	}

	FName FirstDirectChild(const FReferenceSkeleton& Ref, int32 ParentIndex)
	{
		for(int32 I=0;I<Ref.GetNum();++I) if(Ref.GetParentIndex(I)==ParentIndex) return Ref.GetBoneName(I);
		return NAME_None;
	}

	void LogDrive(const TCHAR* Label,const FConstraintDrive& D)
	{
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT DRIVE %s position=%s velocity=%s stiffness=%.6f damping=%.6f maxForce=%.6f"),Label,D.bEnablePositionDrive?TEXT("true"):TEXT("false"),D.bEnableVelocityDrive?TEXT("true"):TEXT("false"),D.Stiffness,D.Damping,D.MaxForce);
	}

	bool AuditAllConstraints(UPhysicsAsset* Asset, USkeletalMesh* Mesh)
	{
		if(!Asset||!Mesh||Asset->ConstraintSetup.Num()!=11||Asset->SkeletalBodySetups.Num()!=12) return false;
		const bool WasDirty=Asset->GetOutermost()->IsDirty();
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
		const TArray<FTransform> Component=ComponentRefTransforms(Mesh);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT AUDIT BEGIN asset=%s bodies=%d constraints=%d packageDirtyBefore=%s axisConvention=Twist:X,Swing1:Z,Swing2:Y"),*Asset->GetPathName(),Asset->SkeletalBodySetups.Num(),Asset->ConstraintSetup.Num(),WasDirty?TEXT("true"):TEXT("false"));
		for(int32 I=0;I<Asset->ConstraintSetup.Num();++I)
		{
			const UPhysicsConstraintTemplate* Template=Asset->ConstraintSetup[I]; if(!Template) return false;
			const FConstraintInstance& C=Template->DefaultInstance;
			const int32 ChildIndex=Ref.FindBoneIndex(C.ConstraintBone1),ParentIndex=Ref.FindBoneIndex(C.ConstraintBone2);
			if(!Component.IsValidIndex(ChildIndex)||!Component.IsValidIndex(ParentIndex)) return false;
			const FTransform ChildLocal=C.GetRefFrame(EConstraintFrame::Frame1),ParentLocal=C.GetRefFrame(EConstraintFrame::Frame2);
			const FTransform ChildFrame=ChildLocal*Component[ChildIndex],ParentFrame=ParentLocal*Component[ParentIndex];
			const FVector AnchorDelta=ChildFrame.GetTranslation()-ParentFrame.GetTranslation();
			const FVector Proximal=(Component[ChildIndex].GetTranslation()-Component[ParentIndex].GetTranslation()).GetSafeNormal();
			const FName DistalName=FirstDirectChild(Ref,ChildIndex); const int32 DistalIndex=Ref.FindBoneIndex(DistalName);
			const FVector Distal=Component.IsValidIndex(DistalIndex)?(Component[DistalIndex].GetTranslation()-Component[ChildIndex].GetTranslation()).GetSafeNormal():FVector::ZeroVector;
			const FVector X=ChildFrame.GetUnitAxis(EAxis::X),Y=ChildFrame.GetUnitAxis(EAxis::Y),Z=ChildFrame.GetUnitAxis(EAxis::Z);
			const FConstraintProfileProperties& P=C.ProfileInstance;
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT[%d] name=%s child=%s parent=%s distalChild=%s"),I,*C.JointName.ToString(),*C.ConstraintBone1.ToString(),*C.ConstraintBone2.ToString(),*DistalName.ToString());
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT[%d] childRefLocal {%s} parentRefLocal {%s}"),I,*TransformText(ChildLocal),*TransformText(ParentLocal));
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT[%d] childFrameCS {%s %s} parentFrameCS {%s %s} anchorDeltaCm=%s"),I,*TransformText(ChildFrame),*AxesText(ChildFrame),*TransformText(ParentFrame),*AxesText(ParentFrame),*AnchorDelta.ToString());
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT[%d] anatomy parentPos=%s childPos=%s distalPos=%s proximalDir=%s distalDir=%s absDotProx[X,Y,Z]=[%.6f,%.6f,%.6f] absDotDistal[X,Y,Z]=[%.6f,%.6f,%.6f]"),I,*Component[ParentIndex].GetTranslation().ToString(),*Component[ChildIndex].GetTranslation().ToString(),Component.IsValidIndex(DistalIndex)?*Component[DistalIndex].GetTranslation().ToString():TEXT("none"),*Proximal.ToString(),*Distal.ToString(),FMath::Abs(X|Proximal),FMath::Abs(Y|Proximal),FMath::Abs(Z|Proximal),FMath::Abs(X|Distal),FMath::Abs(Y|Distal),FMath::Abs(Z|Distal));
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT[%d] angular Twist=%s %.3fdeg Swing1=%s %.3fdeg Swing2=%s %.3fdeg softTwist=%s softSwing=%s twistSoft[k=%.3f d=%.3f r=%.3f] swingSoft[k=%.3f d=%.3f r=%.3f]"),I,AngularMotionText(P.TwistLimit.TwistMotion),P.TwistLimit.TwistLimitDegrees,AngularMotionText(P.ConeLimit.Swing1Motion),P.ConeLimit.Swing1LimitDegrees,AngularMotionText(P.ConeLimit.Swing2Motion),P.ConeLimit.Swing2LimitDegrees,P.TwistLimit.bSoftConstraint?TEXT("true"):TEXT("false"),P.ConeLimit.bSoftConstraint?TEXT("true"):TEXT("false"),P.TwistLimit.Stiffness,P.TwistLimit.Damping,P.TwistLimit.Restitution,P.ConeLimit.Stiffness,P.ConeLimit.Damping,P.ConeLimit.Restitution);
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT[%d] linear X=%s Y=%s Z=%s limit=%.3f soft=%s k=%.3f d=%.3f r=%.3f collisionDisabled=%s projection=%s [linearTol=%.3f angularTol=%.3f linearAlpha=%.3f angularAlpha=%.3f] shock=%s alpha=%.3f massConditioning=%s parentDominates=%s breakable[L=%s A=%s]"),I,LinearMotionText(P.LinearLimit.XMotion),LinearMotionText(P.LinearLimit.YMotion),LinearMotionText(P.LinearLimit.ZMotion),P.LinearLimit.Limit,P.LinearLimit.bSoftConstraint?TEXT("true"):TEXT("false"),P.LinearLimit.Stiffness,P.LinearLimit.Damping,P.LinearLimit.Restitution,P.bDisableCollision?TEXT("true"):TEXT("false"),P.bEnableProjection?TEXT("true"):TEXT("false"),P.ProjectionLinearTolerance,P.ProjectionAngularTolerance,P.ProjectionLinearAlpha,P.ProjectionAngularAlpha,P.bEnableShockPropagation?TEXT("true"):TEXT("false"),P.ShockPropagationAlpha,P.bEnableMassConditioning?TEXT("true"):TEXT("false"),P.bParentDominates?TEXT("true"):TEXT("false"),P.bLinearBreakable?TEXT("true"):TEXT("false"),P.bAngularBreakable?TEXT("true"):TEXT("false"));
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT[%d] angularDrive mode=%d acceleration=%s targetR=%s targetVel=%s limitResponse=%d"),I,(int32)P.AngularDrive.AngularDriveMode,P.AngularDrive.bAccelerationMode?TEXT("true"):TEXT("false"),*P.AngularDrive.OrientationTarget.ToString(),*P.AngularDrive.AngularVelocityTarget.ToString(),(int32)P.AngularDrive.LimitViolationResponse);
			LogDrive(*FString::Printf(TEXT("joint[%d].twist"),I),P.AngularDrive.TwistDrive); LogDrive(*FString::Printf(TEXT("joint[%d].swing"),I),P.AngularDrive.SwingDrive); LogDrive(*FString::Printf(TEXT("joint[%d].slerp"),I),P.AngularDrive.SlerpDrive);
		}
		const bool IsDirtyAfter=Asset->GetOutermost()->IsDirty();
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit JOINT AUDIT PASS constraints=11 packageDirtyBefore=%s packageDirtyAfter=%s"),WasDirty?TEXT("true"):TEXT("false"),IsDirtyAfter?TEXT("true"):TEXT("false"));
		return WasDirty==IsDirtyAfter;
	}


	int32 FindConstraint(const UPhysicsAsset* Asset, FName Child, FName Parent)
	{
		for(int32 I=0;I<Asset->ConstraintSetup.Num();++I)
		{
			const FConstraintInstance& C=Asset->ConstraintSetup[I]->DefaultInstance;
			if(C.ConstraintBone1==Child && C.ConstraintBone2==Parent) return I;
		}
		return INDEX_NONE;
	}

	bool BuildAndValidate9Body(UPhysicsAsset* Working, const UPhysicsAsset* Baseline, const USkeletalMesh* Mesh)
	{
		const EAngularConstraintMotion LimitedMotion=static_cast<EAngularConstraintMotion>(1);
		const int32 ChestSpineIndex=FindConstraint(Working,TEXT("chest"),TEXT("spine"));
		if(ChestSpineIndex==INDEX_NONE) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit 9BODY: baseline chest-spine constraint missing.")); return false; }
		FConstraintInstance ChestConstraint=Working->ConstraintSetup[ChestSpineIndex]->DefaultInstance;
		const TArray<FTransform> Component=ComponentRefTransforms(Mesh);
		const int32 ChestBone=Mesh->GetRefSkeleton().FindBoneIndex(TEXT("chest"));
		const int32 HipsBone=Mesh->GetRefSkeleton().FindBoneIndex(TEXT("hips"));
		if(!Component.IsValidIndex(ChestBone)||!Component.IsValidIndex(HipsBone)) return false;
		const FTransform JointComponent=ChestConstraint.GetRefFrame(EConstraintFrame::Frame1)*Component[ChestBone];
		const FTransform NewHipsFrame=JointComponent.GetRelativeTransform(Component[HipsBone]);

		for(const FName Bone : { FName(TEXT("spine")), FName(TEXT("lowerarm_l")), FName(TEXT("lowerarm_r")) })
		{
			const int32 Index=Working->FindBodyIndex(Bone);
			if(Index==INDEX_NONE) return false;
			FPhysicsAssetUtils::DestroyBody(Working,Index);
		}

		const int32 NewConstraintIndex=FPhysicsAssetUtils::CreateNewConstraint(Working,TEXT("chest"));
		if(!Working->ConstraintSetup.IsValidIndex(NewConstraintIndex)) return false;
		FConstraintInstance& NewConstraint=Working->ConstraintSetup[NewConstraintIndex]->DefaultInstance;
		NewConstraint=ChestConstraint;
		NewConstraint.ConstraintBone1=TEXT("chest");
		NewConstraint.ConstraintBone2=TEXT("hips");
		NewConstraint.SetRefFrame(EConstraintFrame::Frame1,ChestConstraint.GetRefFrame(EConstraintFrame::Frame1));
		NewConstraint.SetRefFrame(EConstraintFrame::Frame2,NewHipsFrame);
		NewConstraint.SetAngularSwing1Limit(LimitedMotion,45.0f);
		NewConstraint.SetAngularSwing2Limit(LimitedMotion,45.0f);
		NewConstraint.SetAngularTwistLimit(LimitedMotion,45.0f);
		NewConstraint.SetDisableCollision(true);
		Working->ConstraintSetup[NewConstraintIndex]->SetDefaultProfile(NewConstraint);
		Working->DisableCollision(Working->FindBodyIndex(TEXT("chest")),Working->FindBodyIndex(TEXT("hips")));

		if(Working->SkeletalBodySetups.Num()!=9 || Working->ConstraintSetup.Num()!=8) return false;
		TSet<FName> Expected(NineBodyBones());
		TMap<FName,TSet<FName>> Graph;
		for(const TObjectPtr<USkeletalBodySetup>& Setup : Working->SkeletalBodySetups)
		{
			if(!Setup || !Expected.Contains(Setup->BoneName) || Setup->DefaultInstance.MassScale!=1.0f) return false;
			const int32 BaseIndex=Baseline->FindBodyIndex(Setup->BoneName);
			if(BaseIndex==INDEX_NONE || !SameGeometry(Setup->AggGeom,Baseline->SkeletalBodySetups[BaseIndex]->AggGeom)) return false;
			Graph.Add(Setup->BoneName);
		}
		for(const TObjectPtr<UPhysicsConstraintTemplate>& Template : Working->ConstraintSetup)
		{
			if(!Template) return false; const FConstraintInstance& C=Template->DefaultInstance;
			if(!Expected.Contains(C.ConstraintBone1)||!Expected.Contains(C.ConstraintBone2)||!C.GetRefFrame(EConstraintFrame::Frame1).IsValid()||!C.GetRefFrame(EConstraintFrame::Frame2).IsValid()) return false;
			if(C.GetAngularSwing1Motion()!=LimitedMotion||C.GetAngularSwing2Motion()!=LimitedMotion||C.GetAngularTwistMotion()!=LimitedMotion||!FMath::IsNearlyEqual(C.GetAngularSwing1Limit(),45.0f)||!FMath::IsNearlyEqual(C.GetAngularSwing2Limit(),45.0f)||!FMath::IsNearlyEqual(C.GetAngularTwistLimit(),45.0f)) return false;
			Graph.FindOrAdd(C.ConstraintBone1).Add(C.ConstraintBone2); Graph.FindOrAdd(C.ConstraintBone2).Add(C.ConstraintBone1);
		}
		TSet<FName> Seen; TArray<FName> Queue={TEXT("hips")};
		while(Queue.Num()) { const FName N=Queue.Pop(EAllowShrinking::No); if(Seen.Contains(N)) continue; Seen.Add(N); for(const FName Adj:Graph.FindRef(N)) Queue.Add(Adj); }
		if(Seen.Num()!=9) return false;
		const int32 Direct=FindConstraint(Working,TEXT("chest"),TEXT("hips")); if(Direct==INDEX_NONE) return false;
		const FConstraintInstance& C=Working->ConstraintSetup[Direct]->DefaultInstance;
		const FTransform A=C.GetRefFrame(EConstraintFrame::Frame1)*Component[ChestBone]; const FTransform B=C.GetRefFrame(EConstraintFrame::Frame2)*Component[HipsBone];
		if(A.GetLocation().ContainsNaN()||B.GetLocation().ContainsNaN()||FVector::Distance(A.GetLocation(),B.GetLocation())>0.01) return false;
		for(const TObjectPtr<UPhysicsConstraintTemplate>& Template : Working->ConstraintSetup) { const FConstraintInstance& K=Template->DefaultInstance; UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit 9BODY CONSTRAINT: %s -> %s"),*K.ConstraintBone1.ToString(),*K.ConstraintBone2.ToString()); }
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit 9BODY VALID: bodies=9 constraints=8 connected=9 chestHipsAnchorDeltaCm=%.9g retainedGeometryExact=true massScaleAllOne=true"),FVector::Distance(A.GetLocation(),B.GetLocation()));
		return true;
	}

	struct FLowerLegFitResult
	{
		FName Bone;
		FName FootBone;
		FKSphylElem Original;
		FKSphylElem Fitted;
		FVector BoneAxisComponent=FVector::ZeroVector;
		FVector GeometryAxisComponent=FVector::ZeroVector;
		double AxisAgreement=0;
		double HierarchyScale=0;
		double BoneLengthCm=0;
		double FitSpanCm=0;
		double FitRadiusCm=0;
		double RawRadialPercentileCm=0;
		double OriginalCalculatedMass=0;
		double FittedCalculatedMass=0;
	};

	double Quantile(TArray<double> Values, double Q)
	{
		Values.Sort();
		if(!Values.Num()) return 0;
		const double P=FMath::Clamp(Q,0.0,1.0)*(Values.Num()-1); const int32 L=FMath::FloorToInt(P), H=FMath::CeilToInt(P);
		return FMath::Lerp(Values[L],Values[H],P-L);
	}

	bool FitOneLowerLeg(UPhysicsAsset* Working, const USkeletalMesh* Mesh, const TArray<FBoneVertInfo>& Infos, const FName Bone, FLowerLegFitResult& Out)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); const TArray<FTransform> Component=ComponentRefTransforms(Mesh);
		const int32 BoneIndex=Ref.FindBoneIndex(Bone); if(BoneIndex==INDEX_NONE||!Infos.IsValidIndex(BoneIndex)||Infos[BoneIndex].Positions.Num()<8) return false;
		int32 FootIndex=INDEX_NONE;
		for(int32 I=0;I<Ref.GetNum();++I) if(Ref.GetParentIndex(I)==BoneIndex && Ref.GetBoneName(I).ToString().Contains(TEXT("foot"),ESearchCase::IgnoreCase)) { FootIndex=I; break; }
		if(FootIndex==INDEX_NONE) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit LOWERLEG: %s has no direct foot child."),*Bone.ToString()); return false; }
		const FVector Origin=Component[BoneIndex].GetLocation(), Foot=Component[FootIndex].GetLocation(); const FVector BoneAxis=(Foot-Origin).GetSafeNormal(); const double BoneLength=FVector::Distance(Origin,Foot);
		if(!BoneAxis.IsNormalized()||BoneLength<1.0) return false;
		TArray<FVector3d> Points; Points.Reserve(Infos[BoneIndex].Positions.Num()); for(const FVector3f& P:Infos[BoneIndex].Positions) Points.Add(FVector3d(Component[BoneIndex].TransformPosition(FVector(P))));
		const UE::Geometry::FOrientedBox3d OBB=UE::Geometry::FitOrientedBox3Points<double>(Points,UE::Geometry::EBox3FitCriteria::Volume);
		int32 LongAxis=0; if(OBB.Extents.Y>OBB.Extents.X) LongAxis=1; if(OBB.Extents.Z>OBB.Extents[LongAxis]) LongAxis=2;
		FVector GeometryAxis=FVector(OBB.GetAxis(LongAxis)).GetSafeNormal(); if(FVector::DotProduct(GeometryAxis,BoneAxis)<0) GeometryAxis*=-1;
		const double Agreement=FVector::DotProduct(GeometryAxis,BoneAxis);
		const int32 BodyIndex=Working->FindBodyIndex(Bone); if(BodyIndex==INDEX_NONE) return false; FKAggregateGeom& A=Working->SkeletalBodySetups[BodyIndex]->AggGeom;
		if(A.SphylElems.Num()!=1||A.SphereElems.Num()||A.BoxElems.Num()||A.ConvexElems.Num()) return false;
		FBox VertexBounds(ForceInit); for(const FVector3d& P:Points) VertexBounds+=FVector(P);
		const FVector BaselineAxis=Component[BoneIndex].TransformVectorNoScale(A.SphylElems[0].GetTransform().GetUnitAxis(EAxis::Z)).GetSafeNormal();
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit LOWERLEG DIAGNOSTIC %s child=%s boneOriginCS=%s footOriginCS=%s boneAxisCS=%s geometryAxisCS=%s dot=%.6f baselineAxisCS=%s vertexBoundsCS={%s}"),*Bone.ToString(),*Ref.GetBoneName(FootIndex).ToString(),*Origin.ToString(),*Foot.ToString(),*BoneAxis.ToString(),*GeometryAxis.ToString(),Agreement,*BaselineAxis.ToString(),*BoundsText(VertexBounds));
		TArray<double> Axial; Axial.Reserve(Points.Num()); TArray<int32> SegmentIndices;
		for(int32 I=0;I<Points.Num();++I) { const double T=FVector::DotProduct(FVector(Points[I])-Origin,BoneAxis); Axial.Add(T); if(T>=BoneLength*0.05&&T<=BoneLength*0.95) SegmentIndices.Add(I); }
		if(SegmentIndices.Num()<8) return false; TArray<double> SegmentAxial; for(const int32 I:SegmentIndices) SegmentAxial.Add(Axial[I]);
		const double MinT=BoneLength*0.05, MaxT=BoneLength*0.95, Span=MaxT-MinT;
		FVector RadialMean=FVector::ZeroVector; int32 Used=0;
		for(const int32 I:SegmentIndices) if(Axial[I]>=MinT&&Axial[I]<=MaxT) { const FVector D=FVector(Points[I])-Origin; RadialMean+=D-BoneAxis*FVector::DotProduct(D,BoneAxis); ++Used; }
		if(!Used) return false; RadialMean/=Used;
		TArray<double> Radial; Radial.Reserve(Used); for(const int32 I:SegmentIndices) if(Axial[I]>=MinT&&Axial[I]<=MaxT) { const FVector D=FVector(Points[I])-Origin-BoneAxis*Axial[I]-RadialMean; Radial.Add(D.Length()); }
		const double Radius=Quantile(Radial,0.50); const double RadialP75=Quantile(Radial,0.75); const double CylinderLengthCm=BoneLength*0.10; const double TotalLengthCm=Radius*2.0+CylinderLengthCm;
		if(!FMath::IsFinite(Radius)||!FMath::IsFinite(Span)||Radius<5.0||Radius>15.0||TotalLengthCm<15.0||TotalLengthCm>30.0)
		{
			UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit LOWERLEG: %s implausible fit boneLength=%.6f span=%.6f radius=%.6f; stopped."),*Bone.ToString(),BoneLength,Span,Radius); return false;
		}
		const FVector CenterComponent=Origin+BoneAxis*((MinT+MaxT)*0.5)+RadialMean;
		const FQuat RotationComponent=FQuat::FindBetweenNormals(FVector::UpVector,BoneAxis);
		const FTransform DesiredComponent(RotationComponent,CenterComponent,FVector::OneVector);
		const FTransform Local=DesiredComponent.GetRelativeTransform(Component[BoneIndex]);
		const FVector Scale=Component[BoneIndex].GetScale3D().GetAbs(); const double UniformScale=(Scale.X+Scale.Y+Scale.Z)/3.0;
		if(!FMath::IsNearlyEqual(Scale.X,Scale.Y,0.001)||!FMath::IsNearlyEqual(Scale.X,Scale.Z,0.001)||UniformScale<=0) return false;
		Out.Bone=Bone; Out.FootBone=Ref.GetBoneName(FootIndex); Out.Original=A.SphylElems[0]; Out.BoneAxisComponent=BoneAxis; Out.GeometryAxisComponent=GeometryAxis; Out.AxisAgreement=Agreement; Out.HierarchyScale=UniformScale; Out.BoneLengthCm=BoneLength; Out.FitSpanCm=Span; Out.FitRadiusCm=Radius; Out.RawRadialPercentileCm=RadialP75; Out.OriginalCalculatedMass=Working->SkeletalBodySetups[BodyIndex]->CalculateMass();
		FKSphylElem& E=A.SphylElems[0]; E.Center=Local.GetLocation(); E.Rotation=Local.GetRotation().Rotator(); E.Radius=Radius/UniformScale; E.Length=CylinderLengthCm/UniformScale; Out.Fitted=E;
		const FVector EffectiveAxis=Component[BoneIndex].TransformVectorNoScale(E.GetTransform().GetUnitAxis(EAxis::Z)).GetSafeNormal();
		const FVector EffectiveCenter=Component[BoneIndex].TransformPosition(E.Center);
		const double AxisDot=FMath::Abs(FVector::DotProduct(EffectiveAxis,BoneAxis)); Out.FittedCalculatedMass=Working->SkeletalBodySetups[BodyIndex]->CalculateMass();
		if(E.GetTransform().ContainsNaN()||!FMath::IsFinite(E.Radius)||!FMath::IsFinite(E.Length)||E.Radius<=0||AxisDot<0.95||FVector::Distance(EffectiveCenter,CenterComponent)>0.01) return false;
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit LOWERLEG FIT %s child=%s verts=%d segmentVerts=%d hierarchyScale=%.6f boneLengthCm=%.6f boneAxisCS=%s geometryAxisCS=%s geometryDot=%.6f originalCenter=%s originalRotation=%s originalRadius=%g originalLength=%g newCenterLocal=%s newRotationLocal=%s newRadiusLocal=%g newLengthLocal=%g centerCS=%s axisCS=%s axisDot=%.9f radialP50Cm=%.6f radialP75Cm=%.6f effectiveRadiusCm=%.6f effectiveCylinderCm=%.6f effectiveTotalCm=%.6f oldMass=%g newMass=%g"),*Bone.ToString(),*Out.FootBone.ToString(),Points.Num(),SegmentIndices.Num(),UniformScale,BoneLength,*BoneAxis.ToString(),*GeometryAxis.ToString(),Agreement,*Out.Original.Center.ToString(),*Out.Original.Rotation.ToString(),Out.Original.Radius,Out.Original.Length,*E.Center.ToString(),*E.Rotation.ToString(),E.Radius,E.Length,*EffectiveCenter.ToString(),*EffectiveAxis.ToString(),AxisDot,Radius,RadialP75,E.Radius*UniformScale,E.Length*UniformScale,(E.Length+2*E.Radius)*UniformScale,Out.OriginalCalculatedMass,Out.FittedCalculatedMass);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit LOWERLEG EXACT %s localCenter=(%.12g,%.12g,%.12g) localRotation=(%.12g,%.12g,%.12g) centerCS=(%.12g,%.12g,%.12g) axisCS=(%.12g,%.12g,%.12g) radiusLocal=%.12g lengthLocal=%.12g"),*Bone.ToString(),E.Center.X,E.Center.Y,E.Center.Z,E.Rotation.Pitch,E.Rotation.Yaw,E.Rotation.Roll,EffectiveCenter.X,EffectiveCenter.Y,EffectiveCenter.Z,EffectiveAxis.X,EffectiveAxis.Y,EffectiveAxis.Z,E.Radius,E.Length);
		return true;
	}

	bool Validate12BodyLowerLegFit(const UPhysicsAsset* Working, const UPhysicsAsset* Baseline)
	{
		if(Working->SkeletalBodySetups.Num()!=12||Working->ConstraintSetup.Num()!=11) return false;
		for(const TObjectPtr<USkeletalBodySetup>& Setup:Working->SkeletalBodySetups)
		{
			const int32 BaseIndex=Baseline->FindBodyIndex(Setup->BoneName); if(BaseIndex==INDEX_NONE) return false; const USkeletalBodySetup* Base=Baseline->SkeletalBodySetups[BaseIndex];
			if(!FBodyInstance::StaticStruct()->CompareScriptStruct(&Setup->DefaultInstance,&Base->DefaultInstance,0)) return false;
			if(Setup->BoneName!=TEXT("lowerleg_l")&&Setup->BoneName!=TEXT("lowerleg_r")&&!SameGeometry(Setup->AggGeom,Base->AggGeom)) return false;
		}
		for(int32 I=0;I<11;++I) if(!FConstraintInstance::StaticStruct()->CompareScriptStruct(&Working->ConstraintSetup[I]->DefaultInstance,&Baseline->ConstraintSetup[I]->DefaultInstance,0)) return false;
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit LOWERLEG STRUCTURE VALID: bodies=12 constraints=11 otherGeometryExact=true bodyPropertiesExact=true constraintsExact=true")); return true;
	}

	void AnalyzeHipsGeometry(const UPhysicsAsset* Target, const USkeletalMesh* Mesh, const TArray<FBoneVertInfo>& Infos)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); const TArray<FTransform> Component=ComponentRefTransforms(Mesh);
		const int32 Hips=Ref.FindBoneIndex(TEXT("hips")), Spine=Ref.FindBoneIndex(TEXT("spine")), LeftLeg=Ref.FindBoneIndex(TEXT("upperleg_l")), RightLeg=Ref.FindBoneIndex(TEXT("upperleg_r"));
		if(Hips==INDEX_NONE||Spine==INDEX_NONE||LeftLeg==INDEX_NONE||RightLeg==INDEX_NONE||!Infos.IsValidIndex(Hips)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit HIPS: required anatomy missing.")); return; }
		const FVector HipsPos=Component[Hips].GetLocation(), SpinePos=Component[Spine].GetLocation(), LeftPos=Component[LeftLeg].GetLocation(), RightPos=Component[RightLeg].GetLocation();
		const FVector Up=(SpinePos-HipsPos).GetSafeNormal(); const FVector Side=(LeftPos-RightPos).GetSafeNormal(); FVector Forward=FVector::CrossProduct(Side,Up).GetSafeNormal();
		if(!Up.IsNormalized()||!Side.IsNormalized()||!Forward.IsNormalized()) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit HIPS: invalid anatomical basis.")); return; }
		TArray<FVector> Points; TArray<double> Axial, Lateral, ForeAft, Radial;
		for(const FVector3f& P:Infos[Hips].Positions)
		{
			const FVector C=Component[Hips].TransformPosition(FVector(P)); const FVector D=C-HipsPos; Points.Add(C);
			Axial.Add(FVector::DotProduct(D,Up)); Lateral.Add(FVector::DotProduct(D,Side)); ForeAft.Add(FVector::DotProduct(D,Forward));
			Radial.Add((D-Up*FVector::DotProduct(D,Up)).Length());
		}
		FBox Bounds(ForceInit); for(const FVector& P:Points) Bounds+=P;
		const int32 Body=Target->FindBodyIndex(TEXT("hips")); if(Body==INDEX_NONE) return; const USkeletalBodySetup* Setup=Target->SkeletalBodySetups[Body]; const FKAggregateGeom& A=Setup->AggGeom;
		if(A.SphylElems.Num()!=1||A.SphereElems.Num()||A.BoxElems.Num()||A.ConvexElems.Num()) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit HIPS: expected exactly one capsule.")); return; }
		const FKSphylElem& E=A.SphylElems[0]; const FVector CenterCS=Component[Hips].TransformPosition(E.Center); const FVector AxisCS=Component[Hips].TransformVectorNoScale(E.GetTransform().GetUnitAxis(EAxis::Z)).GetSafeNormal();
		const FVector Scale=Component[Hips].GetScale3D().GetAbs(); const double UniformScale=(Scale.X+Scale.Y+Scale.Z)/3.0;
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS ANATOMY hipsCS=%s spineCS=%s upperlegLCS=%s upperlegRCS=%s upAxisCS=%s sideAxisCS=%s forwardAxisCS=%s hipsSpineCm=%.6f legSeparationCm=%.6f hierarchyScale=%.9g"),*HipsPos.ToString(),*SpinePos.ToString(),*LeftPos.ToString(),*RightPos.ToString(),*Up.ToString(),*Side.ToString(),*Forward.ToString(),FVector::Distance(HipsPos,SpinePos),FVector::Distance(LeftPos,RightPos),UniformScale);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS VERTS count=%d boundsCS={%s} axial[p00=%.6f p05=%.6f p10=%.6f p25=%.6f p50=%.6f p75=%.6f p90=%.6f p95=%.6f p100=%.6f] lateral[p05=%.6f p25=%.6f p50=%.6f p75=%.6f p95=%.6f] foreaft[p05=%.6f p25=%.6f p50=%.6f p75=%.6f p95=%.6f] radial[p50=%.6f p75=%.6f p90=%.6f]"),Points.Num(),*BoundsText(Bounds),Quantile(Axial,0),Quantile(Axial,.05),Quantile(Axial,.10),Quantile(Axial,.25),Quantile(Axial,.50),Quantile(Axial,.75),Quantile(Axial,.90),Quantile(Axial,.95),Quantile(Axial,1),Quantile(Lateral,.05),Quantile(Lateral,.25),Quantile(Lateral,.50),Quantile(Lateral,.75),Quantile(Lateral,.95),Quantile(ForeAft,.05),Quantile(ForeAft,.25),Quantile(ForeAft,.50),Quantile(ForeAft,.75),Quantile(ForeAft,.95),Quantile(Radial,.50),Quantile(Radial,.75),Quantile(Radial,.90));
		for(const double ForeAftLimit:{20.0,25.0,30.0})
		{
			TArray<double> CA,CL,CF,CR; FBox CB(ForceInit);
			for(int32 I=0;I<Points.Num();++I) if(FMath::Abs(ForeAft[I])<=ForeAftLimit && FMath::Abs(Lateral[I])<=25.0 && Axial[I]>=-12.0 && Axial[I]<=16.0)
			{
				CA.Add(Axial[I]); CL.Add(Lateral[I]); CF.Add(ForeAft[I]); CR.Add(FMath::Sqrt(FMath::Square(Lateral[I])+FMath::Square(ForeAft[I]))); CB+=Points[I];
			}
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS CENTRAL foreaftLimit=%.1f count=%d boundsCS={%s} axial[p05=%.6f p50=%.6f p95=%.6f] lateral[p05=%.6f p50=%.6f p95=%.6f] foreaft[p05=%.6f p50=%.6f p95=%.6f] radial[p50=%.6f p75=%.6f p90=%.6f p95=%.6f]"),ForeAftLimit,CA.Num(),*BoundsText(CB),Quantile(CA,.05),Quantile(CA,.50),Quantile(CA,.95),Quantile(CL,.05),Quantile(CL,.50),Quantile(CL,.95),Quantile(CF,.05),Quantile(CF,.50),Quantile(CF,.95),Quantile(CR,.50),Quantile(CR,.75),Quantile(CR,.90),Quantile(CR,.95));
		}
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS CURRENT localCenter=%s localRotation=%s radiusLocal=%.12g lengthLocal=%.12g centerCS=%s axisCS=%s upDot=%.9f effectiveRadiusCm=%.6f effectiveCylinderCm=%.6f effectiveTotalCm=%.6f calculatedMass=%.12g massScale=%.6f"),*E.Center.ToString(),*E.Rotation.ToString(),E.Radius,E.Length,*CenterCS.ToString(),*AxisCS.ToString(),FMath::Abs(FVector::DotProduct(AxisCS,Up)),E.Radius*UniformScale,E.Length*UniformScale,(E.Length+2*E.Radius)*UniformScale,Setup->CalculateMass(),Setup->DefaultInstance.MassScale);
	}

	bool BuildAndValidateHipsProposal(UPhysicsAsset* Working, const UPhysicsAsset* Source, const USkeletalMesh* Mesh, const TArray<FBoneVertInfo>& Infos)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); const TArray<FTransform> Component=ComponentRefTransforms(Mesh);
		const int32 Hips=Ref.FindBoneIndex(TEXT("hips")), Spine=Ref.FindBoneIndex(TEXT("spine")), LeftLeg=Ref.FindBoneIndex(TEXT("upperleg_l")), RightLeg=Ref.FindBoneIndex(TEXT("upperleg_r"));
		if(Hips==INDEX_NONE||Spine==INDEX_NONE||LeftLeg==INDEX_NONE||RightLeg==INDEX_NONE||!Infos.IsValidIndex(Hips)) return false;
		const FVector HipsPos=Component[Hips].GetLocation(), SpinePos=Component[Spine].GetLocation(), LeftPos=Component[LeftLeg].GetLocation(), RightPos=Component[RightLeg].GetLocation();
		const FVector Up=(SpinePos-HipsPos).GetSafeNormal(), Side=(LeftPos-RightPos).GetSafeNormal(); const FVector Forward=FVector::CrossProduct(Side,Up).GetSafeNormal();
		const double HipsSpine=FVector::Distance(HipsPos,SpinePos), LegSeparation=FVector::Distance(LeftPos,RightPos); const double ForeLimit=LegSeparation*0.60, LateralLimit=LegSeparation*0.75;
		TArray<FVector> Selected; TArray<double> A,L,F;
		for(const FVector3f& P:Infos[Hips].Positions)
		{
			const FVector C=Component[Hips].TransformPosition(FVector(P)), D=C-HipsPos; const double PA=FVector::DotProduct(D,Up), PL=FVector::DotProduct(D,Side), PF=FVector::DotProduct(D,Forward);
			if(FMath::Abs(PF)<=ForeLimit && FMath::Abs(PL)<=LateralLimit && PA>=-HipsSpine*0.60 && PA<=HipsSpine*0.85) { Selected.Add(C); A.Add(PA); L.Add(PL); F.Add(PF); }
		}
		if(Selected.Num()<50) return false;
		const double A05=Quantile(A,.05),A95=Quantile(A,.95),L05=Quantile(L,.05),L95=Quantile(L,.95),F05=Quantile(F,.05),F95=Quantile(F,.95);
		const FVector CenterCS=HipsPos+Up*((A05+A95)*0.5)+Forward*((F05+F95)*0.5); // Keep left/right center on the anatomical mid-plane.
		double Radius=0,Cylinder=0,BestTotal=TNumericLimits<double>::Max();
		for(double CandidateCylinder=0;CandidateCylinder<=25.0;CandidateCylinder+=0.25)
		{
			const FVector SA=CenterCS-Side*(CandidateCylinder*.5), SB=CenterCS+Side*(CandidateCylinder*.5); TArray<double> Distances; Distances.Reserve(Selected.Num());
			for(const FVector& P:Selected) Distances.Add(FMath::Sqrt(FMath::PointDistToSegmentSquared(P,SA,SB)));
			const double CandidateRadius=Quantile(Distances,.80), CandidateTotal=CandidateCylinder+2.0*CandidateRadius;
			if(CandidateRadius>=12.0&&CandidateRadius<=24.0&&CandidateTotal<BestTotal) { Radius=CandidateRadius; Cylinder=CandidateCylinder; BestTotal=CandidateTotal; }
		}
		if(!FMath::IsFinite(Radius)||!FMath::IsFinite(Cylinder)||Radius<12.0||Radius>24.0||Cylinder>25.0||Cylinder+2.0*Radius>60.0) return false;
		const FTransform DesiredCS(FQuat::FindBetweenNormals(FVector::UpVector,Side),CenterCS,FVector::OneVector); const FTransform Local=DesiredCS.GetRelativeTransform(Component[Hips]);
		const FVector Scale=Component[Hips].GetScale3D().GetAbs(); const double UniformScale=(Scale.X+Scale.Y+Scale.Z)/3.0; if(UniformScale<=0||!FMath::IsNearlyEqual(Scale.X,Scale.Y,.001)||!FMath::IsNearlyEqual(Scale.X,Scale.Z,.001)) return false;
		const int32 Body=Working->FindBodyIndex(TEXT("hips")); if(Body==INDEX_NONE) return false; USkeletalBodySetup* Setup=Working->SkeletalBodySetups[Body]; FKAggregateGeom& G=Setup->AggGeom;
		if(G.SphylElems.Num()!=1||G.SphereElems.Num()||G.BoxElems.Num()||G.ConvexElems.Num()) return false; const FKSphylElem Original=G.SphylElems[0]; const double OldMass=Setup->CalculateMass();
		FKSphylElem& E=G.SphylElems[0]; E.Center=Local.GetLocation(); E.Rotation=Local.GetRotation().Rotator(); E.Radius=Radius/UniformScale; E.Length=Cylinder/UniformScale; const double NewMass=Setup->CalculateMass();
		const FVector EffectiveCenter=Component[Hips].TransformPosition(E.Center), EffectiveAxis=Component[Hips].TransformVectorNoScale(E.GetTransform().GetUnitAxis(EAxis::Z)).GetSafeNormal(); const double AxisDot=FMath::Abs(FVector::DotProduct(EffectiveAxis,Side));
		int32 Covered=0; const FVector SegmentHalf=EffectiveAxis*(Cylinder*0.5); const FVector SegmentA=EffectiveCenter-SegmentHalf, SegmentB=EffectiveCenter+SegmentHalf;
		for(const FVector& P:Selected) if(FMath::Sqrt(FMath::PointDistToSegmentSquared(P,SegmentA,SegmentB))<=Radius) ++Covered;
		const double Coverage=Selected.Num()?double(Covered)/Selected.Num():0;
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS PROPOSAL RAW selected=%d coverage=%.6f radius=%.6f cylinder=%.6f centerDelta=%.9f axisDot=%.9f localValid=%s"),Selected.Num(),Coverage,Radius,Cylinder,FVector::Distance(EffectiveCenter,CenterCS),AxisDot,E.GetTransform().ContainsNaN()?TEXT("false"):TEXT("true"));
		if(E.GetTransform().ContainsNaN()||AxisDot<.99||FVector::Distance(EffectiveCenter,CenterCS)>.01||Coverage<.75) return false;
		if(Working->SkeletalBodySetups.Num()!=12||Working->ConstraintSetup.Num()!=11) return false;
		for(const TObjectPtr<USkeletalBodySetup>& W:Working->SkeletalBodySetups)
		{
			const int32 S=Source->FindBodyIndex(W->BoneName); if(S==INDEX_NONE) return false; const USkeletalBodySetup* OriginalSetup=Source->SkeletalBodySetups[S];
			if(!FBodyInstance::StaticStruct()->CompareScriptStruct(&W->DefaultInstance,&OriginalSetup->DefaultInstance,0)) return false;
			if(W->BoneName!=TEXT("hips")&&!SameGeometry(W->AggGeom,OriginalSetup->AggGeom)) return false;
		}
		for(int32 I=0;I<11;++I) if(!FConstraintInstance::StaticStruct()->CompareScriptStruct(&Working->ConstraintSetup[I]->DefaultInstance,&Source->ConstraintSetup[I]->DefaultInstance,0)) return false;
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS PROPOSAL selected=%d/%d coverage=%.6f anatomyForeLimit=%.6f anatomyLateralLimit=%.6f robustAxial=[%.6f,%.6f] robustLateral=[%.6f,%.6f] robustForeAft=[%.6f,%.6f]"),Selected.Num(),Infos[Hips].Positions.Num(),Coverage,ForeLimit,LateralLimit,A05,A95,L05,L95,F05,F95);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS BEFORE localCenter=%s localRotation=%s radiusLocal=%.12g lengthLocal=%.12g effectiveRadiusCm=%.6f effectiveCylinderCm=%.6f effectiveTotalCm=%.6f mass=%.12g"),*Original.Center.ToString(),*Original.Rotation.ToString(),Original.Radius,Original.Length,Original.Radius*UniformScale,Original.Length*UniformScale,(Original.Length+2*Original.Radius)*UniformScale,OldMass);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS AFTER localCenter=(%.12g,%.12g,%.12g) localRotation=(%.12g,%.12g,%.12g) radiusLocal=%.12g lengthLocal=%.12g centerCS=(%.12g,%.12g,%.12g) axisCS=(%.12g,%.12g,%.12g) sideDot=%.9f effectiveRadiusCm=%.6f effectiveCylinderCm=%.6f effectiveTotalCm=%.6f mass=%.12g"),E.Center.X,E.Center.Y,E.Center.Z,E.Rotation.Pitch,E.Rotation.Yaw,E.Rotation.Roll,E.Radius,E.Length,EffectiveCenter.X,EffectiveCenter.Y,EffectiveCenter.Z,EffectiveAxis.X,EffectiveAxis.Y,EffectiveAxis.Z,AxisDot,Radius,Cylinder,Cylinder+2*Radius,NewMass);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS VALID: bodies=12 constraints=11 nonHipsGeometryExact=true lowerlegsExact=true bodyPropertiesExact=true constraintFramesExact=true")); return true;
	}

	struct FEffectiveCapsule
	{
		FVector Center=FVector::ZeroVector, Axis=FVector::UpVector, A=FVector::ZeroVector, B=FVector::ZeroVector;
		double Radius=0, Cylinder=0;
	};

	bool GetEffectiveCapsule(const UPhysicsAsset* Asset, const USkeletalMesh* Mesh, FName Bone, FEffectiveCapsule& Out)
	{
		const int32 BoneIndex=Mesh->GetRefSkeleton().FindBoneIndex(Bone), BodyIndex=Asset->FindBodyIndex(Bone); if(BoneIndex==INDEX_NONE||BodyIndex==INDEX_NONE) return false;
		const TArray<FTransform> Component=ComponentRefTransforms(Mesh); const USkeletalBodySetup* Setup=Asset->SkeletalBodySetups[BodyIndex]; const FKAggregateGeom& G=Setup->AggGeom;
		if(G.SphylElems.Num()!=1||G.SphereElems.Num()||G.BoxElems.Num()||G.ConvexElems.Num()) return false; const FKSphylElem& E=G.SphylElems[0];
		const FVector Scale=Component[BoneIndex].GetScale3D().GetAbs(); const double Uniform=(Scale.X+Scale.Y+Scale.Z)/3.0; if(Uniform<=0) return false;
		Out.Center=Component[BoneIndex].TransformPosition(E.Center); Out.Axis=Component[BoneIndex].TransformVectorNoScale(E.GetTransform().GetUnitAxis(EAxis::Z)).GetSafeNormal(); Out.Radius=E.Radius*Uniform; Out.Cylinder=E.Length*Uniform; Out.A=Out.Center-Out.Axis*(Out.Cylinder*.5); Out.B=Out.Center+Out.Axis*(Out.Cylinder*.5); return true;
	}

	double CapsuleOverlap(const FEffectiveCapsule& X, const FEffectiveCapsule& Y)
	{
		FVector CX,CY; FMath::SegmentDistToSegmentSafe(X.A,X.B,Y.A,Y.B,CX,CY); return X.Radius+Y.Radius-FVector::Distance(CX,CY);
	}

	void AnalyzeTorsoGeometry(const UPhysicsAsset* Target, const USkeletalMesh* Mesh, const TArray<FBoneVertInfo>& Infos)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); const TArray<FTransform> Component=ComponentRefTransforms(Mesh);
		for(const FName Bone:{FName(TEXT("hips")),FName(TEXT("spine")),FName(TEXT("chest")),FName(TEXT("head")),FName(TEXT("upperarm_l")),FName(TEXT("upperarm_r")),FName(TEXT("upperleg_l")),FName(TEXT("upperleg_r"))}) { const int32 I=Ref.FindBoneIndex(Bone); if(I!=INDEX_NONE) UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO BONE %s componentCS=%s parent=%s"),*Bone.ToString(),*Component[I].GetLocation().ToString(),Ref.GetParentIndex(I)==INDEX_NONE?TEXT("none"):*Ref.GetBoneName(Ref.GetParentIndex(I)).ToString()); }
		for(const FName Bone:{FName(TEXT("hips")),FName(TEXT("spine")),FName(TEXT("chest"))})
		{
			const int32 BI=Ref.FindBoneIndex(Bone), Body=Target->FindBodyIndex(Bone); if(BI==INDEX_NONE||Body==INDEX_NONE||!Infos.IsValidIndex(BI)) continue;
			const USkeletalBodySetup* Setup=Target->SkeletalBodySetups[Body]; const FKSphylElem& E=Setup->AggGeom.SphylElems[0]; FEffectiveCapsule C; GetEffectiveCapsule(Target,Mesh,Bone,C);
			FBox Bounds(ForceInit); TArray<double> X,Y,Z; for(const FVector3f& P:Infos[BI].Positions) { const FVector CS=Component[BI].TransformPosition(FVector(P)); Bounds+=CS; X.Add(CS.X); Y.Add(CS.Y); Z.Add(CS.Z); }
			const FBox ShapeBox=Setup->AggGeom.CalcAABB(Component[BI]);
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO BODY %s boneCS=%s localRef={%s} verts=%d vertexBoundsCS={%s} robustX=[%.6f,%.6f] robustY=[%.6f,%.6f] robustZ=[%.6f,%.6f] localCenter=%s localRotation=%s radiusLocal=%.12g lengthLocal=%.12g centerCS=%s axisCS=%s effectiveRadiusCm=%.6f effectiveCylinderCm=%.6f effectiveTotalCm=%.6f shapeAABB={%s} mass=%.12g massScale=%.6f"),*Bone.ToString(),*Component[BI].GetLocation().ToString(),*Ref.GetRefBonePose()[BI].ToHumanReadableString(),Infos[BI].Positions.Num(),*BoundsText(Bounds),Quantile(X,.05),Quantile(X,.95),Quantile(Y,.05),Quantile(Y,.95),Quantile(Z,.05),Quantile(Z,.95),*E.Center.ToString(),*E.Rotation.ToString(),E.Radius,E.Length,*C.Center.ToString(),*C.Axis.ToString(),C.Radius,C.Cylinder,C.Cylinder+2*C.Radius,*BoundsText(ShapeBox),Setup->CalculateMass(),Setup->DefaultInstance.MassScale);
		}
		const int32 HipsI=Ref.FindBoneIndex(TEXT("hips")),SpineI=Ref.FindBoneIndex(TEXT("spine")),ChestI=Ref.FindBoneIndex(TEXT("chest")),HeadI=Ref.FindBoneIndex(TEXT("head")),ArmLI=Ref.FindBoneIndex(TEXT("upperarm_l")),ArmRI=Ref.FindBoneIndex(TEXT("upperarm_r"));
		for(const FName Bone:{FName(TEXT("spine")),FName(TEXT("chest"))})
		{
			const int32 BI=Ref.FindBoneIndex(Bone); if(BI==INDEX_NONE) continue; const bool IsSpine=Bone==TEXT("spine"); const double ZMin=IsSpine?Component[HipsI].GetLocation().Z+5.0:Component[SpineI].GetLocation().Z+20.0; const double ZMax=IsSpine?Component[ChestI].GetLocation().Z-3.0:Component[HeadI].GetLocation().Z-3.0;
			const double SideLimit=IsSpine?FMath::Abs(Component[ArmLI].GetLocation().X)*.80:FMath::Abs(Component[ArmLI].GetLocation().X)*.95; const double ForeLimit=30.0;
			TArray<double>X,Y,Z;FBox B(ForceInit); int32 Count=0; for(const FVector3f& P:Infos[BI].Positions) { const FVector C=Component[BI].TransformPosition(FVector(P)); if(FMath::Abs(C.X)<=SideLimit&&FMath::Abs(C.Y)<=ForeLimit&&C.Z>=ZMin&&C.Z<=ZMax) { X.Add(C.X);Y.Add(C.Y);Z.Add(C.Z);B+=C;++Count; } }
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO CENTRAL %s count=%d/%d envelope side=%.6f fore=%.6f z=[%.6f,%.6f] bounds={%s} robustX=[%.6f,%.6f] robustY=[%.6f,%.6f] robustZ=[%.6f,%.6f]"),*Bone.ToString(),Count,Infos[BI].Positions.Num(),SideLimit,ForeLimit,ZMin,ZMax,*BoundsText(B),Quantile(X,.05),Quantile(X,.95),Quantile(Y,.05),Quantile(Y,.95),Quantile(Z,.05),Quantile(Z,.95));
		}
		FEffectiveCapsule H,S,C; GetEffectiveCapsule(Target,Mesh,TEXT("hips"),H); GetEffectiveCapsule(Target,Mesh,TEXT("spine"),S); GetEffectiveCapsule(Target,Mesh,TEXT("chest"),C);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO OVERLAP hipsSpineCm=%.6f spineChestCm=%.6f hipsChestCm=%.6f"),CapsuleOverlap(H,S),CapsuleOverlap(S,C),CapsuleOverlap(H,C));
		for(const TPair<FName,FName>& Pair:{TPair<FName,FName>(TEXT("spine"),TEXT("hips")),TPair<FName,FName>(TEXT("chest"),TEXT("spine"))})
		{
			const int32 CI=FindConstraint(Target,Pair.Key,Pair.Value), Child=Ref.FindBoneIndex(Pair.Key), Parent=Ref.FindBoneIndex(Pair.Value); if(CI==INDEX_NONE||Child==INDEX_NONE||Parent==INDEX_NONE) continue;
			const FConstraintInstance& K=Target->ConstraintSetup[CI]->DefaultInstance; const FTransform A=K.GetRefFrame(EConstraintFrame::Frame1)*Component[Child], B=K.GetRefFrame(EConstraintFrame::Frame2)*Component[Parent];
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO CONSTRAINT %s->%s frame1CS=%s frame2CS=%s anchorDeltaCm=%.9f frame1Rot=%s frame2Rot=%s"),*Pair.Key.ToString(),*Pair.Value.ToString(),*A.GetLocation().ToString(),*B.GetLocation().ToString(),FVector::Distance(A.GetLocation(),B.GetLocation()),*A.GetRotation().Rotator().ToString(),*B.GetRotation().Rotator().ToString());
		}
		for(const FName Other:{FName(TEXT("head")),FName(TEXT("upperleg_l")),FName(TEXT("upperleg_r")),FName(TEXT("upperarm_l")),FName(TEXT("upperarm_r"))})
		{
			const int32 OI=Ref.FindBoneIndex(Other), OB=Target->FindBodyIndex(Other); if(OI==INDEX_NONE||OB==INDEX_NONE) continue; const FBox OBBox=Target->SkeletalBodySetups[OB]->AggGeom.CalcAABB(Component[OI]);
			for(const FName Torso:{FName(TEXT("hips")),FName(TEXT("spine")),FName(TEXT("chest"))}) { const int32 TI=Ref.FindBoneIndex(Torso),TB=Target->FindBodyIndex(Torso); const FBox TBBox=Target->SkeletalBodySetups[TB]->AggGeom.CalcAABB(Component[TI]); const FBox I=TBBox.Overlap(OBBox); UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO AABB %s vs %s intersects=%s overlapSize=%s"),*Torso.ToString(),*Other.ToString(),I.IsValid?TEXT("true"):TEXT("false"),*I.GetSize().ToString()); }
		}
	}

	struct FTorsoFitResult
	{
		FName Bone; FKSphylElem Before,After; FVector CenterCS=FVector::ZeroVector,AxisCS=FVector::UpVector; double Radius=0,Cylinder=0,Coverage=0,OldMass=0,NewMass=0; int32 Selected=0,Total=0;
	};

	bool FitTorsoBody(UPhysicsAsset* Working,const USkeletalMesh* Mesh,const TArray<FBoneVertInfo>& Infos,FName Bone,FVector Axis,double SideLimit,double ForeLimit,double ZMin,double ZMax,double MaxCylinder,double TargetCoverage,FTorsoFitResult& Out)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); const TArray<FTransform> Component=ComponentRefTransforms(Mesh); const int32 BI=Ref.FindBoneIndex(Bone),Body=Working->FindBodyIndex(Bone); if(BI==INDEX_NONE||Body==INDEX_NONE||!Infos.IsValidIndex(BI)) return false;
		Axis.Normalize(); TArray<FVector> P; TArray<double>X,Y,Z; for(const FVector3f& V:Infos[BI].Positions) { const FVector C=Component[BI].TransformPosition(FVector(V)); if(FMath::Abs(C.X)<=SideLimit&&FMath::Abs(C.Y)<=ForeLimit&&C.Z>=ZMin&&C.Z<=ZMax) { P.Add(C);X.Add(C.X);Y.Add(C.Y);Z.Add(C.Z); } }
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO FIT RAW %s selected=%d total=%d envelopeSide=%.6f envelopeFore=%.6f z=[%.6f,%.6f]"),*Bone.ToString(),P.Num(),Infos[BI].Positions.Num(),SideLimit,ForeLimit,ZMin,ZMax); if(P.Num()<50) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit TORSO FIT %s insufficient central points=%d."),*Bone.ToString(),P.Num()); return false; }
		FVector Center(0.0,(Quantile(Y,.05)+Quantile(Y,.95))*.5,(Quantile(Z,.05)+Quantile(Z,.95))*.5);
		TArray<double> Along,Cross; for(const FVector& V:P){const FVector D=V-Center;const double T=FVector::DotProduct(D,Axis);Along.Add(T);Cross.Add((D-Axis*T).Length());} UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO FIT DISTRIBUTION %s center=%s along[p05=%.6f p95=%.6f] cross[p50=%.6f p60=%.6f p70=%.6f p75=%.6f p80=%.6f] robustXYZ x=[%.6f,%.6f] y=[%.6f,%.6f] z=[%.6f,%.6f]"),*Bone.ToString(),*Center.ToString(),Quantile(Along,.05),Quantile(Along,.95),Quantile(Cross,.50),Quantile(Cross,.60),Quantile(Cross,.70),Quantile(Cross,.75),Quantile(Cross,.80),Quantile(X,.05),Quantile(X,.95),Quantile(Y,.05),Quantile(Y,.95),Quantile(Z,.05),Quantile(Z,.95));
		double Radius=0,Cylinder=0,BestTotal=TNumericLimits<double>::Max(),RawRadius=0,RawCylinder=0,RawTotal=TNumericLimits<double>::Max(); for(double C=0;C<=MaxCylinder;C+=.25) { const FVector A=Center-Axis*(C*.5),B=Center+Axis*(C*.5);TArray<double>D;D.Reserve(P.Num());for(const FVector& V:P)D.Add(FMath::Sqrt(FMath::PointDistToSegmentSquared(V,A,B)));const double R=Quantile(D,TargetCoverage),T=C+2*R;if(T<RawTotal){RawRadius=R;RawCylinder=C;RawTotal=T;}if(R>=12&&R<=34&&T<BestTotal){Radius=R;Cylinder=C;BestTotal=T;} }
		if(Bone==TEXT("spine")){FEffectiveCapsule Existing;GetEffectiveCapsule(Working,Mesh,Bone,Existing);Radius=FMath::Min(Quantile(Cross,.50),Existing.Radius);Cylinder=FMath::Max(0.0,(Quantile(Along,.95)-Quantile(Along,.05))-2.0*Radius);BestTotal=Cylinder+2.0*Radius;const FBox OldBox=Working->SkeletalBodySetups[Body]->AggGeom.CalcAABB(Component[BI]);Center.Z=FMath::Max(Center.Z,OldBox.Min.Z+Radius);}
		if(Bone==TEXT("chest")){FEffectiveCapsule Existing;GetEffectiveCapsule(Working,Mesh,Bone,Existing);Radius=FMath::Min(Quantile(Cross,.75),Existing.Radius);Cylinder=FMath::Max(0.0,(Quantile(Along,.95)-Quantile(Along,.05))-2.0*Radius);BestTotal=Cylinder+2.0*Radius;const FBox OldBox=Working->SkeletalBodySetups[Body]->AggGeom.CalcAABB(Component[BI]);Center.Z=FMath::Max(Center.Z,OldBox.Min.Z+Radius);}
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO FIT SEARCH %s target=%.6f rawBestRadius=%.6f rawBestCylinder=%.6f rawBestTotal=%.6f"),*Bone.ToString(),TargetCoverage,RawRadius,RawCylinder,RawTotal);
		if(Radius<=0||BestTotal>75) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit TORSO FIT %s no plausible capsule radius=%.6f total=%.6f."),*Bone.ToString(),Radius,BestTotal); return false; } const int32 Covered=[&](){int32 N=0;const FVector A=Center-Axis*(Cylinder*.5),B=Center+Axis*(Cylinder*.5);for(const FVector& V:P)if(FMath::Sqrt(FMath::PointDistToSegmentSquared(V,A,B))<=Radius)++N;return N;}(); const double Coverage=double(Covered)/P.Num(); UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO FIT RAW %s center=%s axis=%s radius=%.6f cylinder=%.6f total=%.6f jointCoverage=%.6f target=%.6f"),*Bone.ToString(),*Center.ToString(),*Axis.ToString(),Radius,Cylinder,BestTotal,Coverage,TargetCoverage); if(Bone==TEXT("chest")&&Coverage<.50) return false;
		USkeletalBodySetup* Setup=Working->SkeletalBodySetups[Body];FKAggregateGeom& G=Setup->AggGeom;if(G.SphylElems.Num()!=1||G.SphereElems.Num()||G.BoxElems.Num()||G.ConvexElems.Num())return false;Out.Before=G.SphylElems[0];Out.OldMass=Setup->CalculateMass();
		const FVector Scale=Component[BI].GetScale3D().GetAbs();const double Uniform=(Scale.X+Scale.Y+Scale.Z)/3.;if(Uniform<=0||!FMath::IsNearlyEqual(Scale.X,Scale.Y,.001)||!FMath::IsNearlyEqual(Scale.X,Scale.Z,.001))return false;
		const FTransform Desired(FQuat::FindBetweenNormals(FVector::UpVector,Axis),Center,FVector::OneVector),Local=Desired.GetRelativeTransform(Component[BI]);FKSphylElem& E=G.SphylElems[0];E.Center=Local.GetLocation();E.Rotation=Local.GetRotation().Rotator();E.Radius=Radius/Uniform;E.Length=Cylinder/Uniform;Out.After=E;Out.NewMass=Setup->CalculateMass();
		FEffectiveCapsule Effective;if(!GetEffectiveCapsule(Working,Mesh,Bone,Effective)||FMath::Abs(FVector::DotProduct(Effective.Axis,Axis))<.99||FVector::Distance(Effective.Center,Center)>.01)return false;
		Out.Bone=Bone;Out.CenterCS=Effective.Center;Out.AxisCS=Effective.Axis;Out.Radius=Radius;Out.Cylinder=Cylinder;Out.Coverage=Coverage;Out.Selected=P.Num();Out.Total=Infos[BI].Positions.Num();return true;
	}

	double BoxOverlapVolume(const FBox& A,const FBox& B){const FBox I=A.Overlap(B);if(!I.IsValid)return 0;const FVector S=I.GetSize();return S.X*S.Y*S.Z;}

	bool BuildAndValidateTorsoProposal(UPhysicsAsset* Working,const UPhysicsAsset* Source,const USkeletalMesh* Mesh,const TArray<FBoneVertInfo>& Infos)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();const TArray<FTransform> C=ComponentRefTransforms(Mesh);auto P=[&](FName N){return C[Ref.FindBoneIndex(N)].GetLocation();};
		const FVector Hips=P(TEXT("hips")),Spine=P(TEXT("spine")),Chest=P(TEXT("chest")),Head=P(TEXT("head")),ArmL=P(TEXT("upperarm_l")),ArmR=P(TEXT("upperarm_r"));const FVector UpSpine=(Chest-Spine).GetSafeNormal(),Side=(ArmL-ArmR).GetSafeNormal(),Forward=FVector::CrossProduct(Side,UpSpine).GetSafeNormal();
		FTorsoFitResult SpineFit,ChestFit;const double ShoulderHalf=FVector::Distance(ArmL,ArmR)*.5;
		const bool SpineOk=FitTorsoBody(Working,Mesh,Infos,TEXT("spine"),Forward,ShoulderHalf*1.70,FVector::Distance(Hips,Spine)*1.75,Hips.Z+FVector::Distance(Hips,Spine)*.25,Chest.Z-FVector::Distance(Spine,Chest)*.10,FVector::Distance(Spine,Chest),.60,SpineFit);
		const bool ChestOk=FitTorsoBody(Working,Mesh,Infos,TEXT("chest"),Side,ShoulderHalf*1.25,FVector::Distance(Chest,Head)*1.25,Chest.Z-FVector::Distance(Spine,Chest)*.10,Head.Z-FVector::Distance(Chest,Head)*.10,FVector::Distance(ArmL,ArmR),.80,ChestFit); if(!SpineOk||!ChestOk)return false;
		if(Working->SkeletalBodySetups.Num()!=12||Working->ConstraintSetup.Num()!=11)return false;
		for(const TObjectPtr<USkeletalBodySetup>& W:Working->SkeletalBodySetups){const int32 S=Source->FindBodyIndex(W->BoneName);if(S==INDEX_NONE)return false;const USkeletalBodySetup* O=Source->SkeletalBodySetups[S];if(!FBodyInstance::StaticStruct()->CompareScriptStruct(&W->DefaultInstance,&O->DefaultInstance,0))return false;if(W->BoneName!=TEXT("spine")&&W->BoneName!=TEXT("chest")&&!SameGeometry(W->AggGeom,O->AggGeom))return false;}
		for(int32 I=0;I<11;++I)if(!FConstraintInstance::StaticStruct()->CompareScriptStruct(&Working->ConstraintSetup[I]->DefaultInstance,&Source->ConstraintSetup[I]->DefaultInstance,0))return false;
		for(const TPair<FName,FName>& Pair:{TPair<FName,FName>(TEXT("spine"),TEXT("hips")),TPair<FName,FName>(TEXT("chest"),TEXT("spine"))}){const int32 CI=FindConstraint(Working,Pair.Key,Pair.Value),Child=Ref.FindBoneIndex(Pair.Key),Parent=Ref.FindBoneIndex(Pair.Value);const FConstraintInstance& K=Working->ConstraintSetup[CI]->DefaultInstance;const FVector A=(K.GetRefFrame(EConstraintFrame::Frame1)*C[Child]).GetLocation(),B=(K.GetRefFrame(EConstraintFrame::Frame2)*C[Parent]).GetLocation();if(FVector::Distance(A,B)>.001)return false;}
		FEffectiveCapsule EH,ES,EC;GetEffectiveCapsule(Working,Mesh,TEXT("hips"),EH);GetEffectiveCapsule(Working,Mesh,TEXT("spine"),ES);GetEffectiveCapsule(Working,Mesh,TEXT("chest"),EC);const double HS=CapsuleOverlap(EH,ES),SC=CapsuleOverlap(ES,EC);UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO GROUP RAW hipsSpineOverlap=%.6f spineChestOverlap=%.6f"),HS,SC);if(HS<2||HS>30||SC<2||SC>35)return false;
		for(const FName Changed:{FName(TEXT("spine")),FName(TEXT("chest"))})for(const FName Other:{FName(TEXT("head")),FName(TEXT("upperleg_l")),FName(TEXT("upperleg_r")),FName(TEXT("upperarm_l")),FName(TEXT("upperarm_r"))}){const int32 CI=Ref.FindBoneIndex(Changed),CB=Working->FindBodyIndex(Changed),OI=Ref.FindBoneIndex(Other),OB=Working->FindBodyIndex(Other);const FBox NewBox=Working->SkeletalBodySetups[CB]->AggGeom.CalcAABB(C[CI]),OldBox=Source->SkeletalBodySetups[Source->FindBodyIndex(Changed)]->AggGeom.CalcAABB(C[CI]),OtherBox=Working->SkeletalBodySetups[OB]->AggGeom.CalcAABB(C[OI]);const double NV=BoxOverlapVolume(NewBox,OtherBox),OV=BoxOverlapVolume(OldBox,OtherBox);UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO GROUP RAW %s/%s oldAabbOverlapVolume=%.6f newAabbOverlapVolume=%.6f"),*Changed.ToString(),*Other.ToString(),OV,NV);if(NV>OV+1.0)return false;}
		for(const FTorsoFitResult& R:{SpineFit,ChestFit})UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO FIT %s selected=%d/%d coverage=%.6f beforeCenter=%s beforeRotation=%s beforeRadiusLocal=%.12g beforeLengthLocal=%.12g afterCenter=(%.12g,%.12g,%.12g) afterRotation=(%.12g,%.12g,%.12g) afterRadiusLocal=%.12g afterLengthLocal=%.12g centerCS=(%.12g,%.12g,%.12g) axisCS=(%.12g,%.12g,%.12g) effectiveRadiusCm=%.6f effectiveCylinderCm=%.6f effectiveTotalCm=%.6f oldMass=%.12g newMass=%.12g"),*R.Bone.ToString(),R.Selected,R.Total,R.Coverage,*R.Before.Center.ToString(),*R.Before.Rotation.ToString(),R.Before.Radius,R.Before.Length,R.After.Center.X,R.After.Center.Y,R.After.Center.Z,R.After.Rotation.Pitch,R.After.Rotation.Yaw,R.After.Rotation.Roll,R.After.Radius,R.After.Length,R.CenterCS.X,R.CenterCS.Y,R.CenterCS.Z,R.AxisCS.X,R.AxisCS.Y,R.AxisCS.Z,R.Radius,R.Cylinder,R.Cylinder+2*R.Radius,R.OldMass,R.NewMass);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO GROUP VALID hipsPreserved=true lowerlegsExact=true allOtherGeometryExact=true bodyPropertiesExact=true constraintsExact=true anchorsCoincident=true hipsSpineOverlapCm=%.6f spineChestOverlapCm=%.6f bodies=12 constraints=11"),HS,SC);return true;
	}

	FName MassRegion(FName Bone)
	{
		if(Bone==TEXT("head"))return TEXT("Head");if(Bone==TEXT("hips")||Bone==TEXT("spine")||Bone==TEXT("chest"))return TEXT("Torso");if(Bone.ToString().Contains(TEXT("arm")))return TEXT("Arms");return TEXT("Legs");
	}

	void AnalyzeMassDistribution(UPhysicsAsset* Source,USkeletalMesh* Mesh)
	{
		TMap<FName,double> RegionMass;double Total=0;for(const TObjectPtr<USkeletalBodySetup>& S:Source->SkeletalBodySetups){const double M=S->CalculateMass();Total+=M;RegionMass.FindOrAdd(MassRegion(S->BoneName))+=M;UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS SOURCE body=%s calculatedMass=%.12g massScale=%.9g override=%s overrideKg=%.9g"),*S->BoneName.ToString(),M,S->DefaultInstance.MassScale,S->DefaultInstance.bOverrideMass?TEXT("true"):TEXT("false"),S->DefaultInstance.GetMassOverride());}
		for(const FName R:{FName(TEXT("Head")),FName(TEXT("Torso")),FName(TEXT("Arms")),FName(TEXT("Legs"))})UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS REGION %s mass=%.12g percent=%.9f"),*R.ToString(),RegionMass.FindRef(R),100.0*RegionMass.FindRef(R)/Total);UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS TOTAL calculated=%.12g"),Total);
		UWorld* World=GEditor?GEditor->GetEditorWorldContext().World():nullptr;USkeletalMeshComponent* Preview=World?NewObject<USkeletalMeshComponent>(GetTransientPackage(),TEXT("JurassicMassDiagnostic")):nullptr;TMap<FName,double> RuntimeRegion;double RuntimeTotal=0;
		if(Preview){Preview->SetSkeletalMeshAsset(Mesh);Preview->SetPhysicsAsset(Source,false);Preview->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Preview->RegisterComponentWithWorld(World);Preview->SetSimulatePhysics(true);for(const TObjectPtr<USkeletalBodySetup>& S:Source->SkeletalBodySetups){FBodyInstance* BI=Preview->GetBodyInstance(S->BoneName);const double RM=BI?BI->GetBodyMass():-1;RuntimeTotal+=FMath::Max(0.0,RM);RuntimeRegion.FindOrAdd(MassRegion(S->BoneName))+=FMath::Max(0.0,RM);UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS RUNTIME body=%s runtimeMass=%.12g calculatedMass=%.12g ratio=%.9f valid=%s"),*S->BoneName.ToString(),RM,S->CalculateMass(),S->CalculateMass()>0?RM/S->CalculateMass():0,BI&&BI->IsValidBodyInstance()?TEXT("true"):TEXT("false"));}UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS RUNTIME TOTAL=%.12g"),RuntimeTotal);Preview->SetSimulatePhysics(false);Preview->UnregisterComponent();Preview->ConditionalBeginDestroy();}else UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit MASS: transient runtime component unavailable."));
		const TMap<FName,double> Targets={{TEXT("Head"),.25},{TEXT("Torso"),.40},{TEXT("Arms"),.15},{TEXT("Legs"),.20}};TMap<FName,double> Factors;for(const TPair<FName,double>& T:Targets)Factors.Add(T.Key,T.Value*Total/RegionMass.FindRef(T.Key));UPhysicsAsset* Working=DuplicateObject<UPhysicsAsset>(Source,GetTransientPackage(),TEXT("PA_Monster_MassPlan"));double NewTotal=0;bool Valid=Working!=nullptr;
		if(Working)for(TObjectPtr<USkeletalBodySetup>& S:Working->SkeletalBodySetups){const double Old=S->CalculateMass(),Scale=Factors.FindRef(MassRegion(S->BoneName));S->DefaultInstance.MassScale*=Scale;const double New=S->CalculateMass();NewTotal+=New;Valid&=Scale>=.001&&Scale<=20&&New>0&&FMath::IsNearlyEqual(New,Old*Scale,Old*Scale*.001);UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS PLAN body=%s region=%s oldMass=%.12g oldScale=%.9g newMass=%.12g newScale=%.9g factor=%.9g"),*S->BoneName.ToString(),*MassRegion(S->BoneName).ToString(),Old,S->DefaultInstance.MassScale/Scale,New,S->DefaultInstance.MassScale,Scale);}
		if(Working)for(const TObjectPtr<UPhysicsConstraintTemplate>& K:Working->ConstraintSetup){const int32 A=Working->FindBodyIndex(K->DefaultInstance.ConstraintBone1),B=Working->FindBodyIndex(K->DefaultInstance.ConstraintBone2);const double MA=Working->SkeletalBodySetups[A]->CalculateMass(),MB=Working->SkeletalBodySetups[B]->CalculateMass(),Ratio=FMath::Max(MA,MB)/FMath::Min(MA,MB);UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS JOINT %s->%s ratio=%.9f masses=(%.12g,%.12g)"),*K->DefaultInstance.ConstraintBone1.ToString(),*K->DefaultInstance.ConstraintBone2.ToString(),Ratio,MA,MB);Valid&=Ratio<=12;}
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS PLAN SUMMARY valid=%s oldTotal=%.12g newTotal=%.12g deltaPercent=%.9f factors head=%.9g torso=%.9g arms=%.9g legs=%.9g"),Valid?TEXT("true"):TEXT("false"),Total,NewTotal,100*(NewTotal-Total)/Total,Factors.FindRef(TEXT("Head")),Factors.FindRef(TEXT("Torso")),Factors.FindRef(TEXT("Arms")),Factors.FindRef(TEXT("Legs")));
		if(RuntimeRegion.Num()==4)
		{
			double Denominator=0;for(const TPair<FName,double>& T:Targets)Denominator+=RegionMass.FindRef(T.Key)*T.Value/RuntimeRegion.FindRef(T.Key);const double DesiredRuntimeTotal=Total/Denominator;TMap<FName,double> ChaosFactors;for(const TPair<FName,double>& T:Targets)ChaosFactors.Add(T.Key,DesiredRuntimeTotal*T.Value/RuntimeRegion.FindRef(T.Key));
			UPhysicsAsset* ChaosWorking=DuplicateObject<UPhysicsAsset>(Source,GetTransientPackage(),TEXT("PA_Monster_ChaosMassPlan"));double ChaosCalcTotal=0;bool ChaosValid=ChaosWorking!=nullptr;for(TObjectPtr<USkeletalBodySetup>& S:ChaosWorking->SkeletalBodySetups){const double Old=S->CalculateMass(),F=ChaosFactors.FindRef(MassRegion(S->BoneName));S->DefaultInstance.MassScale*=F;const double New=S->CalculateMass();ChaosCalcTotal+=New;ChaosValid&=F>=.001&&F<=20&&New>0;UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS CHAOS PLAN body=%s oldCalculated=%.12g newCalculated=%.12g oldScale=1 newScale=%.12g"),*S->BoneName.ToString(),Old,New,S->DefaultInstance.MassScale);}
			USkeletalMeshComponent* Check=World?NewObject<USkeletalMeshComponent>(GetTransientPackage(),TEXT("JurassicMassPlanCheck")):nullptr;TMap<FName,double> NewRuntimeRegion;double NewRuntimeTotal=0;if(Check){Check->SetSkeletalMeshAsset(Mesh);Check->SetPhysicsAsset(ChaosWorking,false);Check->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);Check->RegisterComponentWithWorld(World);Check->SetSimulatePhysics(true);for(const TObjectPtr<USkeletalBodySetup>& S:ChaosWorking->SkeletalBodySetups){FBodyInstance* BI=Check->GetBodyInstance(S->BoneName);const double M=BI?BI->GetBodyMass():-1;NewRuntimeTotal+=FMath::Max(0.0,M);NewRuntimeRegion.FindOrAdd(MassRegion(S->BoneName))+=FMath::Max(0.0,M);UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS CHAOS RUNTIME body=%s mass=%.12g scale=%.12g valid=%s"),*S->BoneName.ToString(),M,S->DefaultInstance.MassScale,BI&&BI->IsValidBodyInstance()?TEXT("true"):TEXT("false"));}Check->SetSimulatePhysics(false);Check->UnregisterComponent();Check->ConditionalBeginDestroy();}else ChaosValid=false;
			for(const TPair<FName,double>& T:Targets){const double Percent=NewRuntimeTotal>0?NewRuntimeRegion.FindRef(T.Key)/NewRuntimeTotal:0;ChaosValid&=FMath::Abs(Percent-T.Value)<.002;UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS CHAOS REGION %s mass=%.12g percent=%.9f target=%.9f"),*T.Key.ToString(),NewRuntimeRegion.FindRef(T.Key),Percent*100,T.Value*100);}
			for(const TObjectPtr<UPhysicsConstraintTemplate>& K:ChaosWorking->ConstraintSetup){const int32 A=ChaosWorking->FindBodyIndex(K->DefaultInstance.ConstraintBone1),B=ChaosWorking->FindBodyIndex(K->DefaultInstance.ConstraintBone2);const double MA=ChaosWorking->SkeletalBodySetups[A]->CalculateMass(),MB=ChaosWorking->SkeletalBodySetups[B]->CalculateMass();ChaosValid&=FMath::Max(MA,MB)/FMath::Min(MA,MB)<=12;}
			ChaosValid&=FMath::Abs(ChaosCalcTotal-Total)/Total<.001;UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS CHAOS SUMMARY valid=%s calculatedOld=%.12g calculatedNew=%.12g calculatedDeltaPercent=%.9f runtimeOld=%.12g runtimeNew=%.12g runtimeDeltaPercent=%.9f factors head=%.12g torso=%.12g arms=%.12g legs=%.12g"),ChaosValid?TEXT("true"):TEXT("false"),Total,ChaosCalcTotal,100*(ChaosCalcTotal-Total)/Total,RuntimeTotal,NewRuntimeTotal,100*(NewRuntimeTotal-RuntimeTotal)/RuntimeTotal,ChaosFactors.FindRef(TEXT("Head")),ChaosFactors.FindRef(TEXT("Torso")),ChaosFactors.FindRef(TEXT("Arms")),ChaosFactors.FindRef(TEXT("Legs")));
		}
	}

	const TArray<FName>& TargetBones()
	{
		static const TArray<FName> Names = { TEXT("hips"), TEXT("spine"), TEXT("chest"), TEXT("head"), TEXT("upperarm_l"), TEXT("lowerarm_l"), TEXT("upperarm_r"), TEXT("lowerarm_r"), TEXT("upperleg_l"), TEXT("lowerleg_l"), TEXT("upperleg_r"), TEXT("lowerleg_r") };
		return Names;
	}

	EPhysAssetFitGeomType GeometryForBone(const FName Bone)
	{
		if (Bone == TEXT("head")) return EFG_Sphere;
		if (Bone == TEXT("hips") || Bone == TEXT("spine") || Bone == TEXT("chest")) return EFG_Box;
		return EFG_Sphyl;
	}

	TArray<FTransform> ComponentRefTransforms(const USkeletalMesh* Mesh)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton(); TArray<FTransform> Out; Out.SetNum(Ref.GetNum());
		for(int32 I=0;I<Ref.GetNum();++I) { const int32 P=Ref.GetParentIndex(I); Out[I]=P==INDEX_NONE ? Ref.GetRefBonePose()[I] : Ref.GetRefBonePose()[I]*Out[P]; }
		return Out;
	}

	bool LoadAndValidate(USkeletalMesh*& Mesh, UPhysicsAsset*& Baseline, TArray<FBoneVertInfo>& Infos, TArray<int32>& BoneIndices)
	{
		Mesh = LoadObject<USkeletalMesh>(nullptr, MeshPath);
		Baseline = LoadObject<UPhysicsAsset>(nullptr, BaselinePath);
		if (!Mesh || !Baseline || Baseline->SkeletalBodySetups.Num() != 12 || Baseline->ConstraintSetup.Num() != 11)
		{
			UE_LOG(LogTemp, Error, TEXT("JurassicPhysicsRefit: required assets missing or baseline is not 12 bodies/11 constraints."));
			return false;
		}
		FMeshUtilitiesEngine::CalcBoneVertInfos(Mesh, Infos, true, 0);
		const FReferenceSkeleton& RefSkeleton = Mesh->GetRefSkeleton();
		BoneIndices.Reset();
		for (const FName Bone : TargetBones())
		{
			const int32 BoneIndex = RefSkeleton.FindBoneIndex(Bone);
			const int32 BodyIndex = Baseline->FindBodyIndex(Bone);
			if (BoneIndex == INDEX_NONE || BodyIndex == INDEX_NONE || !Infos.IsValidIndex(BoneIndex) || Infos[BoneIndex].Positions.Num() < 4)
			{
				UE_LOG(LogTemp, Error, TEXT("JurassicPhysicsRefit: %s lacks a bone, body, or sufficient dominant vertices."), *Bone.ToString());
				return false;
			}
			BoneIndices.Add(BoneIndex);
			const FTransform& Local = RefSkeleton.GetRefBonePose()[BoneIndex];
			UE_LOG(LogTemp, Display, TEXT("JurassicPhysicsRefit BONE: %s index=%d body=%d verts=%d T=%s R=%s S=%s"), *Bone.ToString(), BoneIndex, BodyIndex, Infos[BoneIndex].Positions.Num(), *Local.GetTranslation().ToString(), *Local.GetRotation().Rotator().ToString(), *Local.GetScale3D().ToString());
		}
		return true;
	}

	FString BoundsText(const FBox& Box)
	{
		return Box.IsValid ? FString::Printf(TEXT("min=%s max=%s size=%s center=%s"),*Box.Min.ToString(),*Box.Max.ToString(),*Box.GetSize().ToString(),*Box.GetCenter().ToString()) : TEXT("INVALID");
	}

	void DiagnoseSpaces(USkeletalMesh* Mesh,UPhysicsAsset* Baseline,const TArray<FBoneVertInfo>& Infos)
	{
		const FReferenceSkeleton& Ref=Mesh->GetRefSkeleton();
		const TArray<FTransform> Component=ComponentRefTransforms(Mesh);

		TMap<int32,FBox> ImportedBounds;
		if(const FSkeletalMeshModel* Model=Mesh->GetImportedModel(); Model && Model->LODModels.Num())
		{
			for(const FSkelMeshSection& S:Model->LODModels[0].Sections)
			{
				for(const FSoftSkinVertex& V:S.SoftVertices)
				{
					uint16 BestWeight=0; int32 BestBone=INDEX_NONE;
					for(int32 J=0;J<MAX_TOTAL_INFLUENCES;++J) if(V.InfluenceWeights[J]>BestWeight && S.BoneMap.IsValidIndex(V.InfluenceBones[J])) { BestWeight=V.InfluenceWeights[J]; BestBone=S.BoneMap[V.InfluenceBones[J]]; }
					if(BestBone!=INDEX_NONE) ImportedBounds.FindOrAdd(BestBone,FBox(ForceInit))+=FVector(V.Position);
				}
			}
		}

		TMap<int32,FBox> RenderBounds; FBox RenderAll(ForceInit);
		if(const FSkeletalMeshRenderData* Render=Mesh->GetResourceForRendering(); Render && Render->LODRenderData.Num())
		{
			const FSkeletalMeshLODRenderData& LOD=Render->LODRenderData[0]; const FSkinWeightVertexBuffer& W=LOD.SkinWeightVertexBuffer;
			for(uint32 V=0;V<LOD.GetNumVertices();++V)
			{
				const FVector P=FVector(LOD.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(V)); RenderAll+=P;
				int32 SectionIndex=INDEX_NONE,LocalVertex=INDEX_NONE; LOD.GetSectionFromVertexIndex(V,SectionIndex,LocalVertex);
				if(!LOD.RenderSections.IsValidIndex(SectionIndex)) continue; const FSkelMeshRenderSection& S=LOD.RenderSections[SectionIndex];
				uint16 BestWeight=0; int32 BestBone=INDEX_NONE;
				for(uint32 J=0;J<W.GetMaxBoneInfluences();++J) { const uint16 Weight=W.GetBoneWeight(V,J); const uint32 LocalBone=W.GetBoneIndex(V,J); if(Weight>BestWeight && S.BoneMap.IsValidIndex(LocalBone)) { BestWeight=Weight; BestBone=S.BoneMap[LocalBone]; } }
				if(BestBone!=INDEX_NONE) RenderBounds.FindOrAdd(BestBone,FBox(ForceInit))+=P;
			}
		}

		if(const UFbxSkeletalMeshImportData* Import=Cast<UFbxSkeletalMeshImportData>(Mesh->GetAssetImportData()))
		{
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit IMPORT: uniformScale=%.9g convertScene=%s convertSceneUnit=%s forceFrontX=%s sourceFiles=%s"),Import->ImportUniformScale,Import->bConvertScene?TEXT("true"):TEXT("false"),Import->bConvertSceneUnit?TEXT("true"):TEXT("false"),Import->bForceFrontXAxis?TEXT("true"):TEXT("false"),*Import->GetFirstFilename());
		}
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit GLOBAL: assetBounds=%s importedBounds=%s renderLOD0=%s"),*Mesh->GetBounds().GetBox().ToString(),*Mesh->GetImportedBounds().GetBox().ToString(),*BoundsText(RenderAll));

		for(const FName Bone:TargetBones())
		{
			const int32 I=Ref.FindBoneIndex(Bone); FBox CalcLocal(ForceInit),CalcMesh(ForceInit);
			for(const FVector3f& P:Infos[I].Positions) { CalcLocal+=FVector(P); CalcMesh+=Component[I].TransformPosition(FVector(P)); }
			const FBox Imported=ImportedBounds.FindRef(I), Render=RenderBounds.FindRef(I);
			const FVector CalcSize=CalcMesh.GetSize(), RenderSize=Render.GetSize();
			const FVector Ratio(CalcSize.X!=0?RenderSize.X/CalcSize.X:0,CalcSize.Y!=0?RenderSize.Y/CalcSize.Y:0,CalcSize.Z!=0?RenderSize.Z/CalcSize.Z:0);
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit SPACE %s: localRef={%s} componentRef={%s} calcBoneLocal={%s} calcToMesh={%s} importedDominant={%s} renderDominant={%s} renderOverCalc=%s"),*Bone.ToString(),*Ref.GetRefBonePose()[I].ToHumanReadableString(),*Component[I].ToHumanReadableString(),*BoundsText(CalcLocal),*BoundsText(CalcMesh),*BoundsText(Imported),*BoundsText(Render),*Ratio.ToString());
			const USkeletalBodySetup* Setup=Baseline->SkeletalBodySetups[Baseline->FindBodyIndex(Bone)].Get(); const FKAggregateGeom& A=Setup->AggGeom;
			for(const FKSphereElem& E:A.SphereElems) UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit BASELINE %s Sphere diameter=%g center=%s"),*Bone.ToString(),E.Radius*2,*E.Center.ToString());
			for(const FKSphylElem& E:A.SphylElems) UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit BASELINE %s Capsule diameter=%g totalLength=%g transform=%s"),*Bone.ToString(),E.Radius*2,E.Length+E.Radius*2,*E.GetTransform().ToHumanReadableString());
			for(const FKBoxElem& E:A.BoxElems) UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit BASELINE %s Box size=%s transform=%s"),*Bone.ToString(),*FVector(E.X,E.Y,E.Z).ToString(),*E.GetTransform().ToHumanReadableString());
		}
	}

	bool Refit(UPhysicsAsset* Asset, USkeletalMesh* Mesh, const TArray<FBoneVertInfo>& Infos, const TArray<int32>& BoneIndices)
	{
		for (int32 I = 0; I < TargetBones().Num(); ++I)
		{
			const FName Bone = TargetBones()[I];
			const int32 BodyIndex = Asset->FindBodyIndex(Bone);
			TArray<FVector3d> Points; Points.Reserve(Infos[BoneIndices[I]].Positions.Num()); for(const FVector3f& P:Infos[BoneIndices[I]].Positions) Points.Add(FVector3d(P));
			if(Points.Num()<4) return false;
			const UE::Geometry::FOrientedBox3d OBB=UE::Geometry::FitOrientedBox3Points<double>(Points,UE::Geometry::EBox3FitCriteria::Volume);
			FKAggregateGeom& A=Asset->SkeletalBodySetups[BodyIndex]->AggGeom;
			A.SphereElems.Reset(); A.SphylElems.Reset(); A.BoxElems.Reset(); A.ConvexElems.Reset();
			if(Bone==TEXT("head"))
			{
				FKSphereElem E; E.Center=FVector(OBB.Center()); E.Radius=0; for(const FVector3d& P:Points) E.Radius=FMath::Max(E.Radius,static_cast<float>((P-OBB.Center()).Length())); A.SphereElems.Add(E);
			}
			else if(Bone==TEXT("hips") || Bone==TEXT("spine") || Bone==TEXT("chest"))
			{
				FKBoxElem E; E.Center=FVector(OBB.Center()); E.Rotation=FQuat(OBB.Frame.Rotation).Rotator(); E.X=OBB.Extents.X*2; E.Y=OBB.Extents.Y*2; E.Z=OBB.Extents.Z*2; A.BoxElems.Add(E);
			}
			else
			{
				int32 AxisIndex=0; if(OBB.Extents.Y>OBB.Extents.X) AxisIndex=1; if(OBB.Extents.Z>OBB.Extents[AxisIndex]) AxisIndex=2; const FVector3d Axis=OBB.GetAxis(AxisIndex);
				double MinT=TNumericLimits<double>::Max(),MaxT=TNumericLimits<double>::Lowest(),Radius=0; for(const FVector3d& P:Points) { const FVector3d Delta=P-OBB.Center(); const double T=Delta.Dot(Axis); MinT=FMath::Min(MinT,T); MaxT=FMath::Max(MaxT,T); Radius=FMath::Max(Radius,(Delta-Axis*T).Length()); }
				FKSphylElem E; E.Center=FVector(OBB.Center()+Axis*((MinT+MaxT)*0.5)); E.Rotation=FQuat::FindBetweenNormals(FVector::UpVector,FVector(Axis)).Rotator(); E.Radius=Radius; E.Length=MaxT-MinT; A.SphylElems.Add(E);
			}
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit CORRECTION %s: direct deterministic fit from %d verified bone-local vertices; no scalar assumption."),*Bone.ToString(),Points.Num());
		}
		return true;
	}

	bool ValidateGeometry(UPhysicsAsset* Asset, USkeletalMesh* Mesh, const TArray<FBoneVertInfo>& Infos)
	{
		const TArray<FTransform> Component=ComponentRefTransforms(Mesh);
		for (const FName Bone : TargetBones())
		{
			const int32 BoneIndex=Mesh->GetRefSkeleton().FindBoneIndex(Bone);
			const USkeletalBodySetup* Setup = Asset->SkeletalBodySetups[Asset->FindBodyIndex(Bone)].Get();
			const FKAggregateGeom& A = Setup->AggGeom;
			if (A.SphereElems.Num() + A.SphylElems.Num() + A.BoxElems.Num() != 1) return false;
			FVector D; FTransform X = FTransform::Identity; const TCHAR* Type;
			if (A.SphereElems.Num()) { const FKSphereElem& E=A.SphereElems[0]; D=FVector(E.Radius*2); X.SetTranslation(E.Center); Type=TEXT("Sphere"); }
			else if (A.SphylElems.Num()) { const FKSphylElem& E=A.SphylElems[0]; D=FVector(E.Radius*2,E.Radius*2,E.Length+E.Radius*2); X=E.GetTransform(); Type=TEXT("Capsule"); }
			else { const FKBoxElem& E=A.BoxElems[0]; D=FVector(E.X,E.Y,E.Z); X=E.GetTransform(); Type=TEXT("Box"); }
			FBox VertexBox(ForceInit); for(const FVector3f& P:Infos[BoneIndex].Positions) VertexBox+=Component[BoneIndex].TransformPosition(FVector(P));
			const FBox ShapeBox=A.CalcAABB(Component[BoneIndex]); const FVector Effective=ShapeBox.GetSize(); const double PointDiagonal=VertexBox.GetSize().Length(); const double ShapeDiagonal=Effective.Length();
			const bool IsContaining=ShapeBox.Min.X<=VertexBox.Min.X+1 && ShapeBox.Min.Y<=VertexBox.Min.Y+1 && ShapeBox.Min.Z<=VertexBox.Min.Z+1 && ShapeBox.Max.X>=VertexBox.Max.X-1 && ShapeBox.Max.Y>=VertexBox.Max.Y-1 && ShapeBox.Max.Z>=VertexBox.Max.Z-1;
			if (D.ContainsNaN() || Effective.ContainsNaN() || D.GetMin()<=UE_SMALL_NUMBER || PointDiagonal<=UE_SMALL_NUMBER || ShapeDiagonal<PointDiagonal*0.75 || ShapeDiagonal>PointDiagonal*2.5 || !IsContaining)
			{
				UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit: rejected %s local=%s effective=%s vertices=%s ratio=%g contains=%s."),*Bone.ToString(),*D.ToString(),*BoundsText(ShapeBox),*BoundsText(VertexBox),ShapeDiagonal/PointDiagonal,IsContaining?TEXT("true"):TEXT("false"));
				return false;
			}
			UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit RESULT: %s type=%s storedBoneLocal=%s effectiveComponentAABBcm=%s vertexRegionCm=%s ratio=%g massScale=%.6f"),*Bone.ToString(),Type,*D.ToString(),*BoundsText(ShapeBox),*BoundsText(VertexBox),ShapeDiagonal/PointDiagonal,Setup->DefaultInstance.MassScale);
		}
		return Asset->SkeletalBodySetups.Num()==12 && Asset->ConstraintSetup.Num()==11;
	}
}

void FJurassicPhysicsRefitModule::StartupModule()
{
	AnalyzeCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.Analyze"),TEXT("Read-only refit preflight."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::Analyze));
	ExecuteCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.Execute"),TEXT("Create validated PA_Monster_RefitTest."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::Execute));
	Create9BodyCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.Create9Body"),TEXT("Create validated PA_Monster_9BodyTest."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::Create9Body));
	Create12BodyLowerLegFitCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.Create12BodyLowerLegFit"),TEXT("Create validated PA_Monster_12BodyFitTest with lower-leg-only fitting."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::Create12BodyLowerLegFit));
	AnalyzeHipsCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.AnalyzeHips"),TEXT("Read-only hips anatomy and geometry diagnostic."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::AnalyzeHips));
	PlanHipsRefitCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.PlanHipsRefit"),TEXT("Build and validate a transient hips-only proposal."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::PlanHipsRefit));
	AnalyzeTorsoCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.AnalyzeTorso"),TEXT("Read-only hips, spine, and chest group diagnostic."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::AnalyzeTorso));
	PlanTorsoRefitCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.PlanTorsoRefit"),TEXT("Build and validate a transient spine/chest proposal."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::PlanTorsoRefit));
	AnalyzeMassBalanceCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.AnalyzeMassBalance"),TEXT("Read-only calculated/runtime mass and target-distribution diagnostic."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::AnalyzeMassBalance));
	ValidateMassBalanceCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.ValidateMassBalance"),TEXT("Validate serialized mass-only candidate and runtime distribution."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::ValidateMassBalance));
	AuditConstraintsCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.AuditConstraints"),TEXT("Read-only component-space constraint-frame and settings audit."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::AuditConstraints));
	CleanupCommand=MakeUnique<FAutoConsoleCommand>(TEXT("JurassicPhysicsRefit.Cleanup"),TEXT("Delete the experimental destination through the editor asset subsystem."),FConsoleCommandDelegate::CreateRaw(this,&FJurassicPhysicsRefitModule::Cleanup));
}

void FJurassicPhysicsRefitModule::ShutdownModule() { CleanupCommand.Reset(); AuditConstraintsCommand.Reset(); ValidateMassBalanceCommand.Reset(); AnalyzeMassBalanceCommand.Reset(); PlanTorsoRefitCommand.Reset(); AnalyzeTorsoCommand.Reset(); PlanHipsRefitCommand.Reset(); AnalyzeHipsCommand.Reset(); Create12BodyLowerLegFitCommand.Reset(); Create9BodyCommand.Reset(); ExecuteCommand.Reset(); AnalyzeCommand.Reset(); }

void FJurassicPhysicsRefitModule::Analyze()
{
	USkeletalMesh* Mesh=nullptr; UPhysicsAsset* Baseline=nullptr; TArray<FBoneVertInfo> Infos; TArray<int32> Indices;
	if (JurassicPhysicsRefit::LoadAndValidate(Mesh,Baseline,Infos,Indices))
	{
		JurassicPhysicsRefit::DiagnoseSpaces(Mesh,Baseline,Infos);
		UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit ANALYZE PASS: mesh bounds cm=%s; baseline 12/11; all targets valid."),*((Mesh->GetBounds().BoxExtent*2).ToString()));
	}
}

void FJurassicPhysicsRefitModule::Execute()
{
	using namespace JurassicPhysicsRefit;
	if (FindObject<UObject>(nullptr,DestinationObjectPath) || FPackageName::DoesPackageExist(DestinationPackage)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit: destination exists; stopped.")); return; }
	USkeletalMesh* Mesh=nullptr; UPhysicsAsset* Baseline=nullptr; TArray<FBoneVertInfo> Infos; TArray<int32> Indices;
	if (!LoadAndValidate(Mesh,Baseline,Infos,Indices)) return;
	UPhysicsAsset* Working=DuplicateObject<UPhysicsAsset>(Baseline,GetTransientPackage(),TEXT("PA_Monster_RefitWorking"));
	if (!Working || !Refit(Working,Mesh,Infos,Indices) || !ValidateGeometry(Working,Mesh,Infos)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit: transient validation failed; no asset created.")); return; }
	UPackage* Package=CreatePackage(DestinationPackage);
	UPhysicsAsset* Result=DuplicateObject<UPhysicsAsset>(Working,Package,DestinationName);
	Result->SetFlags(RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Result); Result->MarkPackageDirty();
	FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
	const FString Filename=FPackageName::LongPackageNameToFilename(DestinationPackage,FPackageName::GetAssetPackageExtension());
	if (!UPackage::SavePackage(Package,Result,*Filename,Args)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit: save failed.")); return; }
	UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit EXECUTE PASS: saved %s with 12 bodies and 11 constraints."),DestinationPackage);
}

void FJurassicPhysicsRefitModule::Create9Body()
{
	using namespace JurassicPhysicsRefit;
	if(FindObject<UObject>(nullptr,NineBodyObjectPath)||FPackageName::DoesPackageExist(NineBodyPackage)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit 9BODY: destination exists; stopped.")); return; }
	USkeletalMesh* Mesh=nullptr; UPhysicsAsset* Baseline=nullptr; TArray<FBoneVertInfo> Infos; TArray<int32> Indices;
	if(!LoadAndValidate(Mesh,Baseline,Infos,Indices)) return;
	UPhysicsAsset* Working=DuplicateObject<UPhysicsAsset>(Baseline,GetTransientPackage(),TEXT("PA_Monster_9BodyWorking"));
	if(!Working||!BuildAndValidate9Body(Working,Baseline,Mesh)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit 9BODY: transient structural validation failed; no asset created.")); return; }
	UPackage* Package=CreatePackage(NineBodyPackage); UPhysicsAsset* Result=DuplicateObject<UPhysicsAsset>(Working,Package,NineBodyName);
	Result->SetFlags(RF_Public|RF_Standalone); FAssetRegistryModule::AssetCreated(Result); Result->MarkPackageDirty();
	FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError;
	const FString Filename=FPackageName::LongPackageNameToFilename(NineBodyPackage,FPackageName::GetAssetPackageExtension());
	if(!UPackage::SavePackage(Package,Result,*Filename,Args)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit 9BODY: save failed.")); return; }
	UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit 9BODY PASS: saved %s; meshPhysicsAsset=%s; candidateAssigned=%s"),NineBodyPackage,*GetNameSafe(Mesh->GetPhysicsAsset()),Mesh->GetPhysicsAsset()==Result?TEXT("true"):TEXT("false"));
}

void FJurassicPhysicsRefitModule::Create12BodyLowerLegFit()
{
	using namespace JurassicPhysicsRefit;
	UPhysicsAsset* Target=LoadObject<UPhysicsAsset>(nullptr,LowerLegFitObjectPath); if(!Target) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit LOWERLEG: target asset missing; stopped.")); return; }
	USkeletalMesh* Mesh=nullptr; UPhysicsAsset* Baseline=nullptr; TArray<FBoneVertInfo> Infos; TArray<int32> Indices; if(!LoadAndValidate(Mesh,Baseline,Infos,Indices)) return;
	UPhysicsAsset* Working=DuplicateObject<UPhysicsAsset>(Target,GetTransientPackage(),TEXT("PA_Monster_12BodyFitWorking")); FLowerLegFitResult Left,Right;
	if(!Working) return; const bool LeftOk=FitOneLowerLeg(Working,Mesh,Infos,TEXT("lowerleg_l"),Left); const bool RightOk=FitOneLowerLeg(Working,Mesh,Infos,TEXT("lowerleg_r"),Right);
	if(!LeftOk||!RightOk||!Validate12BodyLowerLegFit(Working,Baseline)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit LOWERLEG: transient validation failed; no asset created.")); return; }
	Target->Modify(); for(const FName Bone:{FName(TEXT("lowerleg_l")),FName(TEXT("lowerleg_r"))}) { const int32 T=Target->FindBodyIndex(Bone),W=Working->FindBodyIndex(Bone); if(T==INDEX_NONE||W==INDEX_NONE) return; Target->SkeletalBodySetups[T]->Modify(); Target->SkeletalBodySetups[T]->AggGeom=Working->SkeletalBodySetups[W]->AggGeom; Target->SkeletalBodySetups[T]->InvalidatePhysicsData(); Target->SkeletalBodySetups[T]->CreatePhysicsMeshes(); }
	Target->MarkPackageDirty(); FSavePackageArgs Args; Args.TopLevelFlags=RF_Public|RF_Standalone; Args.SaveFlags=SAVE_NoError; const FString Filename=FPackageName::LongPackageNameToFilename(LowerLegFitPackage,FPackageName::GetAssetPackageExtension());
	if(!UPackage::SavePackage(Target->GetOutermost(),Target,*Filename,Args)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit LOWERLEG: save failed.")); return; }
	UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit LOWERLEG PASS: updated %s meshPhysicsAsset=%s candidateAssigned=%s"),LowerLegFitPackage,*GetNameSafe(Mesh->GetPhysicsAsset()),Mesh->GetPhysicsAsset()==Target?TEXT("true"):TEXT("false"));
}

void FJurassicPhysicsRefitModule::AnalyzeHips()
{
	using namespace JurassicPhysicsRefit;
	UPhysicsAsset* Target=LoadObject<UPhysicsAsset>(nullptr,LowerLegFitObjectPath); USkeletalMesh* Mesh=nullptr; UPhysicsAsset* Baseline=nullptr; TArray<FBoneVertInfo> Infos; TArray<int32> Indices;
	if(!Target||!LoadAndValidate(Mesh,Baseline,Infos,Indices)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit HIPS: target or baseline unavailable.")); return; }
	AnalyzeHipsGeometry(Target,Mesh,Infos);
}

void FJurassicPhysicsRefitModule::PlanHipsRefit()
{
	using namespace JurassicPhysicsRefit;
	UPhysicsAsset* Target=LoadObject<UPhysicsAsset>(nullptr,LowerLegFitObjectPath); USkeletalMesh* Mesh=nullptr; UPhysicsAsset* Baseline=nullptr; TArray<FBoneVertInfo> Infos; TArray<int32> Indices;
	if(!Target||!LoadAndValidate(Mesh,Baseline,Infos,Indices)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit HIPS: proposal preflight failed.")); return; }
	UPhysicsAsset* Working=DuplicateObject<UPhysicsAsset>(Target,GetTransientPackage(),TEXT("PA_Monster_HipsProposal"));
	if(!Working||!BuildAndValidateHipsProposal(Working,Target,Mesh,Infos)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit HIPS: transient proposal rejected; target unchanged.")); return; }
	UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit HIPS PROPOSAL PASS: target unchanged; exact values ready for supported live-editor application."));
}

void FJurassicPhysicsRefitModule::AnalyzeTorso()
{
	using namespace JurassicPhysicsRefit; UPhysicsAsset* Target=LoadObject<UPhysicsAsset>(nullptr,LowerLegFitObjectPath); USkeletalMesh* Mesh=nullptr; UPhysicsAsset* Baseline=nullptr; TArray<FBoneVertInfo> Infos; TArray<int32> Indices;
	if(!Target||!LoadAndValidate(Mesh,Baseline,Infos,Indices)) { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit TORSO: diagnostic preflight failed.")); return; }
	AnalyzeTorsoGeometry(Target,Mesh,Infos); UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO ANALYZE PASS: target unchanged."));
}

void FJurassicPhysicsRefitModule::PlanTorsoRefit()
{
	using namespace JurassicPhysicsRefit;UPhysicsAsset* Target=LoadObject<UPhysicsAsset>(nullptr,LowerLegFitObjectPath);USkeletalMesh* Mesh=nullptr;UPhysicsAsset* Baseline=nullptr;TArray<FBoneVertInfo> Infos;TArray<int32> Indices;if(!Target||!LoadAndValidate(Mesh,Baseline,Infos,Indices))return;UPhysicsAsset* Working=DuplicateObject<UPhysicsAsset>(Target,GetTransientPackage(),TEXT("PA_Monster_TorsoProposal"));if(!Working||!BuildAndValidateTorsoProposal(Working,Target,Mesh,Infos)){UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit TORSO: transient proposal rejected; target unchanged."));return;}UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit TORSO PROPOSAL PASS: target unchanged; exact values ready for supported application."));
}

void FJurassicPhysicsRefitModule::AnalyzeMassBalance()
{
	using namespace JurassicPhysicsRefit;if(FPackageName::DoesPackageExist(MassBalancePackage)){UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit MASS: destination exists; stopped."));return;}UPhysicsAsset* Source=LoadObject<UPhysicsAsset>(nullptr,LowerLegFitObjectPath);USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,MeshPath);if(!Source||!Mesh||Source->SkeletalBodySetups.Num()!=12||Source->ConstraintSetup.Num()!=11){UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit MASS: source validation failed."));return;}AnalyzeMassDistribution(Source,Mesh);
}

void FJurassicPhysicsRefitModule::ValidateMassBalance()
{
	using namespace JurassicPhysicsRefit;UPhysicsAsset* Source=LoadObject<UPhysicsAsset>(nullptr,LowerLegFitObjectPath);UPhysicsAsset* Candidate=LoadObject<UPhysicsAsset>(nullptr,MassBalanceObjectPath);USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,MeshPath);bool Valid=Source&&Candidate&&Mesh&&Source->SkeletalBodySetups.Num()==12&&Candidate->SkeletalBodySetups.Num()==12&&Source->ConstraintSetup.Num()==11&&Candidate->ConstraintSetup.Num()==11;
	if(Valid)for(const TObjectPtr<USkeletalBodySetup>& C:Candidate->SkeletalBodySetups){const int32 I=Source->FindBodyIndex(C->BoneName);if(I==INDEX_NONE){Valid=false;break;}const USkeletalBodySetup* S=Source->SkeletalBodySetups[I];Valid&=SameGeometry(C->AggGeom,S->AggGeom);FBodyInstance A=C->DefaultInstance,B=S->DefaultInstance;B.MassScale=A.MassScale;Valid&=FBodyInstance::StaticStruct()->CompareScriptStruct(&A,&B,0);}
	if(Valid)for(int32 I=0;I<11;++I)Valid&=FConstraintInstance::StaticStruct()->CompareScriptStruct(&Candidate->ConstraintSetup[I]->DefaultInstance,&Source->ConstraintSetup[I]->DefaultInstance,0);
	if(!Valid){UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit MASS VALIDATION FAIL: candidate structure or non-mass properties differ."));return;}UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit MASS STRUCTURE VALID: bodies=12 constraints=11 geometryExact=true framesExact=true nonMassBodyPropertiesExact=true"));AnalyzeMassDistribution(Candidate,Mesh);
}

void FJurassicPhysicsRefitModule::AuditConstraints()
{
	using namespace JurassicPhysicsRefit;
	UPhysicsAsset* Asset=LoadObject<UPhysicsAsset>(nullptr,MassBalanceObjectPath); USkeletalMesh* Mesh=LoadObject<USkeletalMesh>(nullptr,MeshPath);
	if(!AuditAllConstraints(Asset,Mesh)) UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit JOINT AUDIT FAIL: invalid structure, transform, or dirty-state change."));
}

void FJurassicPhysicsRefitModule::Cleanup()
{
	UEditorAssetSubsystem* Assets=GEditor ? GEditor->GetEditorSubsystem<UEditorAssetSubsystem>() : nullptr;
	const bool bDeleted=Assets && Assets->DeleteAsset(JurassicPhysicsRefit::DestinationPackage);
	if (bDeleted) { UE_LOG(LogTemp,Display,TEXT("JurassicPhysicsRefit CLEANUP PASS: %s"),JurassicPhysicsRefit::DestinationPackage); }
	else { UE_LOG(LogTemp,Error,TEXT("JurassicPhysicsRefit CLEANUP FAIL: %s"),JurassicPhysicsRefit::DestinationPackage); }
}

IMPLEMENT_MODULE(FJurassicPhysicsRefitModule,JurassicPhysicsRefit)
