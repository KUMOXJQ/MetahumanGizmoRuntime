// Copyright XujiaqiKumo. All Rights Reserved.

/*
 * MetahumanFaceGizmoComponent.h
 *
 * 中文：声明运行时面部 Gizmo 可视化 Actor 组件。与编辑器 UMetaHumanCharacterEditorSubsystem::GetFaceGizmos
 *       同源数学（FaceState->EvaluateGizmos(Evaluate().Vertices)），但不依赖 GEditor / Slate。
 *       实际评估依赖 WITH_METAHUMAN_GIZMO_RUNTIME_EVAL（链接 MetaHumanCoreTechLib）；纯 Shipping 游戏目标可为桩实现。
 * English: Public API for UMetahumanFaceGizmoComponent — runtime face manipulator spheres driven by Titan/CoreTech
 *          evaluation when EVAL is enabled; optional SourceMetaHumanCharacter for deserialized face state.
 */

#pragma once

#include "Components/ActorComponent.h"
#include "MetahumanFaceGizmoComponent.generated.h"

class UDNAAsset;
class UMetaHumanCharacter;
class USkeletalMeshComponent;

/**
 * Runtime facial gizmo visualization aligned with MetaHuman Creator logic:
 * FaceState->EvaluateGizmos(FaceState->Evaluate().Vertices) via MetaHumanCoreTechLib (Editor target only).
 * Spawns sphere handles in world space; no Slate / MetaHumanCharacterEditorSubsystem.
 * Game/Shipping builds without MetaHumanCoreTechLib: InitializeIdentity returns false (see uplugin / README).
 * Author: XujiaqiKumo
 */
UCLASS(ClassGroup = (MetaHuman), meta = (BlueprintSpawnableComponent))
class UMetahumanFaceGizmoComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMetahumanFaceGizmoComponent();
	virtual ~UMetahumanFaceGizmoComponent();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick) override;

	/** Target face mesh; gizmo positions are transformed with this component's world transform. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	TObjectPtr<USkeletalMeshComponent> FaceMeshComponent = nullptr;

	/** Face DNA used by FMetaHumanCharacterIdentity::Init (same as Creator pipeline). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	TObjectPtr<UDNAAsset> FaceDNAAsset = nullptr;

	/** Absolute or project-relative path to MetaHuman Creator face MHC data (directory containing Titan assets). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	FString FaceMHCDataPath;

	/** Body MHC data path (paired with face; same as editor GetOrCreateCharacterIdentity). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	FString BodyMHCDataPath;

	/** 0 = Y_UP, 1 = Z_UP (same as EMetaHumanCharacterOrientation). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo", meta = (ClampMin = "0", ClampMax = "1"))
	uint8 DNAOrientationIndex = 0;

	/** Optional: if set, Deserialize(GetFaceStateData()) is applied so gizmos match saved face state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	TObjectPtr<UMetaHumanCharacter> SourceMetaHumanCharacter = nullptr;

	/** Sphere radius scale relative to default gizmo mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo", meta = (ClampMin = "0.001"))
	float GizmoSphereScale = 1.f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	bool bAutoInitializeOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	bool bShowGizmoSpheres = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	bool bTickRefreshEveryFrame = false;

	/** Initialize identity + state; call after paths/DNA are valid. Editor: full path. Game: returns false unless you use a source build that links MetaHumanCoreTechLib to game (non-standard). */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Gizmo")
	bool InitializeIdentity();

	/** Recompute Evaluate + EvaluateGizmos and update sphere transforms. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Gizmo")
	bool RefreshGizmoTransforms();

	UFUNCTION(BlueprintPure, Category = "MetaHuman|Gizmo")
	bool IsIdentityInitialized() const { return bIdentityInitialized; }

	UFUNCTION(BlueprintPure, Category = "MetaHuman|Gizmo")
	int32 GetNumGizmos() const;

protected:
	void ReleaseGizmoSpheres();
	void EnsureSphereCount(int32 Count);

private:
	/** Opaque impl (cpp-only); void* avoids UHT + incomplete TUniquePtr destructor issues. */
	void* ImplPtr = nullptr;

	bool bIdentityInitialized = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> GizmoSpheres;

	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CachedSphereMesh = nullptr;

	float BaseGizmoMeshScale = 0.0035f;
};
