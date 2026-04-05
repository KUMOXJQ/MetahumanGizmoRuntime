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

/** Corrects rig-space gizmo positions to the rendered face skeletal mesh (see RefreshGizmoTransforms). */
UENUM(BlueprintType)
enum class EMetahumanGizmoWorldAlignment : uint8
{
	/** No extra translation after FaceMeshComponent transform. */
	None UMETA(DisplayName = "None"),
	/**
	 * Match Evaluate() joint position (from Identity face DNA order) to the same bone on FaceMeshComponent.
	 * Recommended for MetaHuman (default bone: FACIAL_C_FacialRoot).
	 */
	FacialRootBone UMETA(DisplayName = "Facial root bone"),
	/** Legacy: translate so gizmo centroid matches mesh bounds origin (can be skewed by hair/collisions). */
	MeshBoundsCentroid UMETA(DisplayName = "Mesh bounds center (legacy)"),
};
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

	/**
	 * Face DNA for FMetaHumanCharacterIdentity::Init when bUsePluginArchetypeFaceDNAForIdentity is false.
	 * Optional when Face Mesh Component is set and its USkeletalMesh has UDNAAsset as Asset User Data.
	 * When bUsePluginArchetypeFaceDNAForIdentity is true (default), Init uses plugin SKM_Face.dna instead (same as editor GetArchetypeDNAAseet(Face)) to match IdentityTemplate and avoid PatchBlendModel::Reduce crashes.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	TObjectPtr<UDNAAsset> FaceDNAAsset = nullptr;

	/**
	 * If true (default), load MetaHumanCoreTech Content/ArchetypeDNA/SKM_Face.dna for Identity::Init — aligned with UMetaHumanCharacterEditorSubsystem::GetOrCreateCharacterIdentity.
	 * Set false to use FaceDNAAsset or DNA on the face skeletal mesh (must be topology-compatible with IdentityTemplate).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	bool bUsePluginArchetypeFaceDNAForIdentity = true;

	/**
	 * If true and Face/Body MHC path strings are empty, fill them from MetaHumanCharacter plugin Content:
	 * Face/IdentityTemplate and Body/IdentityTemplate (same disk layout as GetFaceIdentityTemplateModelPath).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	bool bUsePluginDefaultMHCPaths = true;

	/** Absolute or project-relative path to MetaHuman Creator face MHC data (directory containing Titan assets). If empty and bUsePluginDefaultMHCPaths, filled from plugin. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	FString FaceMHCDataPath;

	/** Body MHC data path. If empty and bUsePluginDefaultMHCPaths, filled from plugin. */
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

	/** How to correct rig-space gizmo positions to the skinned face. Default: match FACIAL_C_FacialRoot rig joint to bone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	EMetahumanGizmoWorldAlignment GizmoWorldAlignment = EMetahumanGizmoWorldAlignment::FacialRootBone;

	/** DNA joint / skeletal bone used when GizmoWorldAlignment == FacialRootBone (must exist in Identity face DNA and on the skeleton). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	FName GizmoAlignmentBoneName = FName(TEXT("FACIAL_C_FacialRoot"));

	/**
	 * If true (default), when FacialRootBone alignment fails (missing joint or bone), fall back to mesh-bounds centroid alignment
	 * when bUsePluginArchetypeFaceDNAForIdentity is true (same conditions as the old MeshBoundsCentroid-only path).
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MetaHuman|Gizmo")
	bool bFallbackToMeshBoundsIfBoneAlignmentFails = true;

	/** Lazily loaded from disk when bUsePluginArchetypeFaceDNAForIdentity is true; do not assign manually. */
	UPROPERTY(Transient)
	TObjectPtr<UDNAAsset> CachedArchetypeFaceDNAForIdentity = nullptr;

	/** Initialize identity + state; call after paths/DNA are valid. Editor: full path. Game: returns false unless you use a source build that links MetaHumanCoreTechLib to game (non-standard). */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Gizmo")
	bool InitializeIdentity();

	/** Recompute Evaluate + EvaluateGizmos and update sphere transforms. */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Gizmo")
	bool RefreshGizmoTransforms();

	/**
	 * Tear down MHC glue and run InitializeIdentity again. Call after SourceMetaHumanCharacter or face DNA change;
	 * RefreshGizmoTransforms alone is not enough.
	 */
	UFUNCTION(BlueprintCallable, Category = "MetaHuman|Gizmo")
	bool ReinitializeIdentity();

	UFUNCTION(BlueprintPure, Category = "MetaHuman|Gizmo")
	bool IsIdentityInitialized() const { return bIdentityInitialized; }

	UFUNCTION(BlueprintPure, Category = "MetaHuman|Gizmo")
	int32 GetNumGizmos() const;

protected:
	void ReleaseGizmoSpheres();
	void EnsureSphereCount(int32 Count);

private:
	/** Resolves DNA for FMetaHumanCharacterIdentity::Init (archetype SKM_Face vs explicit/mesh). */
	UDNAAsset* ResolveFaceDNAForInit();

	/** Opaque impl (cpp-only); void* avoids UHT + incomplete TUniquePtr destructor issues. */
	void* ImplPtr = nullptr;

	bool bIdentityInitialized = false;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> GizmoSpheres;

	/** SM_MoveTool_Gizmo when available, else cached fallback mesh from EnsureSphereCount. */
	UPROPERTY(Transient)
	TObjectPtr<UStaticMesh> CachedGizmoStaticMesh = nullptr;

	float BaseGizmoMeshScale = 0.0035f;
};
