// Copyright XujiaqiKumo. All Rights Reserved.

/*
 * MetahumanFaceGizmoComponent.cpp
 *
 * 中文：实现 UMetahumanFaceGizmoComponent — BeginPlay/Tick 中可选自动 InitializeIdentity + RefreshGizmoTransforms；
 *       通过 FMetaHumanCharacterIdentity 与 FaceState 计算 Gizmo 点列，将引擎基础球体挂到 FaceMeshComponent（或 Actor）
 *       下并变换到世界空间。日志类别 LogMetahumanGizmoRuntime，搜索 "[MetahumanGizmo]" 便于排查。
 * English: Identity init, Evaluate/EvaluateGizmo sphere placement, and sphere pool management (EnsureSphereCount / Release).
 */

#include "MetahumanFaceGizmoComponent.h"

#include "MetahumanGizmoRuntime.h"

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
#include "DNAAsset.h"
#include "DNACommon.h"
#include "DNAReader.h"
#include "DNAUtils.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanRigEvaluatedState.h"
#endif

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"
#include "Engine/EngineTypes.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"

#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
#include "CollisionQueryParams.h"
#include "EngineDefines.h"
#include "Engine/World.h"
#include "PhysicsEngine/BodySetup.h"
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
#endif
#if WITH_EDITOR
#include "Editor.h"
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterEditorSubsystem.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformFile.h"
#include "HAL/UnrealMemory.h"
#endif

// 输出日志中搜索：[MetahumanGizmo]
// 更细：控制台输入 Log LogMetahumanGizmoRuntime Verbose

/** Same as UMetaHumanCharacterEditorFaceMoveTool::GetManipulatorMaterial fallback (MetaHumanCharacterEditorFaceEditingTools.cpp). */
static UMaterialInterface *LoadMetaHumanCharacterEditorGizmoMaterial()
{
	static UMaterialInterface *Cached = nullptr;
	static bool bAttempted = false;
	if (!bAttempted)
	{
		bAttempted = true;
		Cached = LoadObject<UMaterialInterface>(
			nullptr,
			TEXT("/Script/Engine.Material'/MetaHumanCharacter/Tools/M_MoveTool_Gizmo.M_MoveTool_Gizmo'"));
		if (!Cached)
		{
			Cached = LoadObject<UMaterialInterface>(nullptr, TEXT("/MetaHumanCharacter/Tools/M_MoveTool_Gizmo.M_MoveTool_Gizmo"));
		}
		if (Cached)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] Loaded MetaHuman Character Editor gizmo material M_MoveTool_Gizmo."));
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Warning,
				   TEXT("[MetahumanGizmo] Could not load M_MoveTool_Gizmo — ensure MetaHumanCharacter plugin is enabled; spheres use default mesh material."));
		}
	}
	return Cached;
}

/** True if the mesh has simple/cooked collision; otherwise line traces pass through (common cause: rayHits=1 floor only). */
static bool StaticMeshHasQueryableCollision(UStaticMesh *Mesh)
{
	if (!Mesh)
	{
		return false;
	}
	UBodySetup *const BS = Mesh->GetBodySetup();
	if (!BS)
	{
		return false;
	}
	if (BS->bHasCookedCollisionData)
	{
		return true;
	}
	return BS->AggGeom.GetElementCount() > 0;
}

/** Same mesh as UMetaHumanCharacterEditorSettings::MoveManipulatorMesh (MetaHumanCharacterEditorSettings.cpp). */
static UStaticMesh *LoadGizmoManipulatorStaticMesh()
{
	static UStaticMesh *Cached = nullptr;
	static bool bAttempted = false;
	if (!bAttempted)
	{
		bAttempted = true;
		Cached = LoadObject<UStaticMesh>(
			nullptr,
			TEXT("/Script/Engine.StaticMesh'/MetaHumanCharacter/Tools/SM_MoveTool_Gizmo.SM_MoveTool_Gizmo'"));
		if (!Cached)
		{
			Cached = LoadObject<UStaticMesh>(nullptr, TEXT("/MetaHumanCharacter/Tools/SM_MoveTool_Gizmo.SM_MoveTool_Gizmo"));
		}
		if (Cached)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] Loaded gizmo mesh SM_MoveTool_Gizmo (MetaHumanCharacter/Tools)."));
			if (!StaticMeshHasQueryableCollision(Cached))
			{
				UStaticMesh *const Fallback = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
				if (Fallback && StaticMeshHasQueryableCollision(Fallback))
				{
					UE_LOG(LogMetahumanGizmoRuntime, Warning,
						   TEXT("[MetahumanGizmo] SM_MoveTool_Gizmo has no collision geometry — using /Engine/BasicShapes/Sphere for gizmo spheres."));
					Cached = Fallback;
				}
				else if (Fallback)
				{
					UE_LOG(LogMetahumanGizmoRuntime, Warning,
						   TEXT("[MetahumanGizmo] SM_MoveTool_Gizmo has no collision — forcing Engine Sphere mesh (collision check inconclusive)."));
					Cached = Fallback;
				}
				else
				{
					UE_LOG(LogMetahumanGizmoRuntime, Error,
						   TEXT("[MetahumanGizmo] SM_MoveTool_Gizmo has no collision and Engine Sphere failed to load — MoveInteraction may never hit gizmos."));
				}
			}
		}
		else
		{
			Cached = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
			if (Cached)
			{
				UE_LOG(LogMetahumanGizmoRuntime, Warning,
					   TEXT("[MetahumanGizmo] SM_MoveTool_Gizmo not found — falling back to /Engine/BasicShapes/Sphere."));
			}
		}
		if (Cached)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] Gizmo static mesh resolved | path=%s | queryableCollision=%s"),
				   *Cached->GetPathName(),
				   StaticMeshHasQueryableCollision(Cached) ? TEXT("true") : TEXT("false"));
		}
	}
	return Cached;
}

static void ApplyMetaHumanEditorStyleGizmoMaterial(UStaticMeshComponent *Sphere)
{
	if (!Sphere)
	{
		return;
	}
	if (UMaterialInterface *BaseMat = LoadMetaHumanCharacterEditorGizmoMaterial())
	{
		Sphere->CreateAndSetMaterialInstanceDynamicFromMaterial(0, BaseMat);
	}
}

/** Query-only collision for per-gizmo LineTraceComponent picking (Editor HitTest). Block WorldStatic, Visibility, Camera; ignore other channels. */
static void ApplyMetaHumanEditorStyleGizmoCollision(UStaticMeshComponent *Sphere)
{
	if (!Sphere)
	{
		return;
	}
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionObjectType(ECC_WorldDynamic);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	Sphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Sphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Sphere->SetGenerateOverlapEvents(false);
}

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
namespace
{
/**
 * Mirrors FGizmoBoundaryConstraintFunctions::GizmoTranslationFunction (MetaHumanCharacterEditorFaceEditingTools.cpp).
 * DeltaFromBegin is the proposed full offset from BeginDragGizmoPosition; return value is the constrained offset (soft box).
 */
static FVector3f ApplyEditorStyleGizmoTranslationConstraint(
	const FVector3f& BeginDragGizmoPosition,
	const FVector3f& DeltaFromBegin,
	const FVector3f& MinGizmoPosition,
	const FVector3f& MaxGizmoPosition,
	float BBoxSoftBound)
{
	FVector3f NewPosition = BeginDragGizmoPosition + DeltaFromBegin;
	const FVector3f NewBoundedPosition = NewPosition.BoundToBox(MinGizmoPosition, MaxGizmoPosition);
	const FVector3f BoundDelta = NewPosition - NewBoundedPosition;
	for (int32 k = 0; k < 3; ++k)
	{
		NewPosition[k] = NewBoundedPosition[k] + 2.0f / (1.0f + FMath::Exp(-2.0f * BoundDelta[k] * BBoxSoftBound)) - 1.0f;
	}
	return NewPosition - BeginDragGizmoPosition;
}
} // namespace

struct FMetahumanFaceGizmoComponentImpl
{
	TUniquePtr<FMetaHumanCharacterIdentity> Identity;
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState;
	/** Index into Evaluate().Vertices for GizmoAlignmentBoneName (Identity face DNA joint order == MHC joint columns). */
	int32 CachedAlignmentJointIndexInEvaluateBuffer = INDEX_NONE;
};
#else
struct FMetahumanFaceGizmoComponentImpl
{
};
#endif

static void DeleteImpl(void *&ImplPtr)
{
	delete static_cast<FMetahumanFaceGizmoComponentImpl *>(ImplPtr);
	ImplPtr = nullptr;
}

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
/** When bUsePluginDefault is true, fill empty InOutFace/InOutBody from MetaHumanCharacter plugin Content (IdentityTemplate dirs). */
static void ApplyPluginDefaultMHCPathsIfNeeded(const bool bUsePluginDefault, FString &InOutFace, FString &InOutBody)
{
	if (!bUsePluginDefault)
	{
		return;
	}

	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MetaHumanCharacter"));
	if (!Plugin.IsValid())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] bUsePluginDefaultMHCPaths: MetaHumanCharacter plugin not found — Face/Body paths unchanged."));
		return;
	}

	const FString ContentDir = FPaths::ConvertRelativePathToFull(Plugin->GetContentDir());
	bool bFilled = false;
	if (InOutFace.IsEmpty())
	{
		InOutFace = ContentDir / TEXT("Face/IdentityTemplate");
		bFilled = true;
	}
	if (InOutBody.IsEmpty())
	{
		InOutBody = ContentDir / TEXT("Body/IdentityTemplate");
		bFilled = true;
	}
	if (bFilled)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] Applied plugin default MHC paths (empty slots only) | Face=%s | Body=%s"),
			   *InOutFace,
			   *InOutBody);
	}
}

/** MetaHumanCoreTech plugin Content dir (ArchetypeDNA, etc.). Empty if plugin missing. */
static FString GetMetaHumanCoreTechContentDir()
{
	TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("MetaHumanCoreTech"));
	if (!Plugin.IsValid())
	{
		return FString();
	}
	return FPaths::ConvertRelativePathToFull(Plugin->GetContentDir());
}

static bool FileExistsNonEmpty(const FString &Path, int64 &OutSizeBytes)
{
	IPlatformFile &PF = FPlatformFileManager::Get().GetPlatformFile();
	if (!PF.FileExists(*Path))
	{
		OutSizeBytes = -1;
		return false;
	}
	OutSizeBytes = static_cast<int64>(PF.FileSize(*Path));
	return OutSizeBytes > 0;
}

/**
 * Pre-flight logs for Face IdentityTemplate vs MetaHumanCreatorAPI::CreateMHCApi (see MetaHumanCreatorAPI.cpp).
 * Original MetahumanFaceGizmoComponent only tested directory existence; it did not verify individual files.
 */
static void LogIdentityTemplateFacePreChecks(const FString &FaceAbs)
{
	IPlatformFile &PF = FPlatformFileManager::Get().GetPlatformFile();
	UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] IdentityTemplate(Face) file read check | path=%s"), *FaceAbs);

	if (!PF.DirectoryExists(*FaceAbs))
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] IdentityTemplate(Face): directory does not exist — cannot read template files."));
		return;
	}

	struct FEntry
	{
		const TCHAR *RelName;
		bool bCritical;
	};
	static const FEntry Files[] = {
		{TEXT("symmetry.json"), true},
		{TEXT("landmarks_config.json"), true},
		{TEXT("presets.json"), false},
		{TEXT("skinningWeightsConfig.json"), false},
		{TEXT("masks_face.json"), false},
	};

	for (const FEntry &E : Files)
	{
		const FString Full = FaceAbs / E.RelName;
		int64 Sz = 0;
		const bool bOk = FileExistsNonEmpty(Full, Sz);
		if (E.bCritical)
		{
			if (!bOk)
			{
				UE_LOG(LogMetahumanGizmoRuntime, Error,
					   TEXT("[MetahumanGizmo] IdentityTemplate(Face): CRITICAL missing or empty: %s (CreateMHCApi reads this; init will fail)."),
					   E.RelName);
			}
			else
			{
				UE_LOG(LogMetahumanGizmoRuntime, Log,
					   TEXT("[MetahumanGizmo] IdentityTemplate(Face): OK %s (bytes=%lld)"),
					   E.RelName,
					   Sz);
			}
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] IdentityTemplate(Face): %s %s"),
				   bOk ? TEXT("OK") : TEXT("absent"),
				   E.RelName);
		}
	}

	const FString RigCalib = FaceAbs / TEXT("uemhc_rig_calibration_data.json");
	const FString GeoPca = FaceAbs / TEXT("geo_and_bindpose.pca");
	int64 RigSz = 0;
	int64 PcaSz = 0;
	const bool bRig = FileExistsNonEmpty(RigCalib, RigSz);
	const bool bPca = FileExistsNonEmpty(GeoPca, PcaSz);
	if (bRig)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] IdentityTemplate(Face): PCA rig calibration present (bytes=%lld)"),
			   RigSz);
	}
	else if (bPca)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] IdentityTemplate(Face): geo_and_bindpose.pca present (bytes=%lld)"),
			   PcaSz);
	}
	else
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] IdentityTemplate(Face): CRITICAL — need uemhc_rig_calibration_data.json OR geo_and_bindpose.pca (neither readable/non-empty)."));
	}
}

/** FMetaHumanCharacterIdentity::Init loads combined DNA from disk (see MetaHumanCharacterIdentity.cpp). */
static void LogCombinedBodyDnaPreCheck()
{
	const FString Dir = GetMetaHumanCoreTechContentDir();
	if (Dir.IsEmpty())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] Combined DNA check: MetaHumanCoreTech plugin not found (cannot verify body_head_combined.dna)."));
		return;
	}
	const FString Combined = Dir / TEXT("ArchetypeDNA/body_head_combined.dna");
	int64 Sz = 0;
	if (FileExistsNonEmpty(Combined, Sz))
	{
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] Combined DNA readable OK | body_head_combined.dna bytes=%lld | %s"),
			   Sz,
			   *Combined);
	}
	else
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] Combined DNA missing or empty — FMetaHumanCharacterIdentity::Init may fail | %s"),
			   *Combined);
	}
}

/** Compare face DNA geometry topology to plugin Face archetype (SKM_Face.dna); mismatch often breaks Titan / PatchBlendModel. */
static void LogFaceDnaVsArchetypeTopology(UDNAAsset *FaceDNA)
{
	if (!IsValid(FaceDNA))
	{
		return;
	}

	const FString ContentDir = GetMetaHumanCoreTechContentDir();
	if (ContentDir.IsEmpty())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] DNA vs archetype: skipped (MetaHumanCoreTech not found)."));
		return;
	}

	TSharedPtr<IDNAReader> FaceGeo = FaceDNA->GetGeometryReader();
	if (!FaceGeo.IsValid())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] DNA vs archetype: Face DNA '%s' GeometryReader is null (cannot compare topology)."),
			   *FaceDNA->GetName());
		return;
	}

	const FString ArchetypePath = FPaths::ConvertRelativePathToFull(ContentDir / TEXT("ArchetypeDNA/SKM_Face.dna"));
	int64 ArchFileSize = 0;
	if (!FileExistsNonEmpty(ArchetypePath, ArchFileSize))
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] DNA vs archetype: plugin Face archetype DNA missing or empty | %s"),
			   *ArchetypePath);
		return;
	}

	TSharedPtr<IDNAReader> ArchGeo = ReadDNAFromFile(ArchetypePath, EDNADataLayer::Geometry);
	if (!ArchGeo.IsValid())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] DNA vs archetype: ReadDNAFromFile failed for | %s"),
			   *ArchetypePath);
		return;
	}

	const uint16 NumMeshesFace = FaceGeo->GetMeshCount();
	const uint16 NumMeshesArch = ArchGeo->GetMeshCount();
	const uint32 V0Face = NumMeshesFace > 0 ? FaceGeo->GetVertexPositionCount(0) : 0u;
	const uint32 V0Arch = NumMeshesArch > 0 ? ArchGeo->GetVertexPositionCount(0) : 0u;

	UE_LOG(LogMetahumanGizmoRuntime, Log,
		   TEXT("[MetahumanGizmo] DNA vs archetype topology | FaceDNA='%s' | DnaFileName='%s' | meshes: faceDNA=%u archetype=%u | mesh0 verts: faceDNA=%u archetype=%u"),
		   *FaceDNA->GetName(),
		   *FaceDNA->DnaFileName,
		   NumMeshesFace,
		   NumMeshesArch,
		   V0Face,
		   V0Arch);

	const bool bMatch = (NumMeshesFace == NumMeshesArch) && (V0Face == V0Arch);
	if (bMatch)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] DNA vs archetype: mesh count and mesh0 vertex count MATCH plugin SKM_Face.dna (good sign for IdentityTemplate)."));
	}
	else
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] DNA vs archetype: TOPOLOGY MISMATCH — custom/wrong-version face DNA often causes CreateMHCApi failure or PatchBlendModel crashes; prefer MetaHuman-version-matched DNA or archetype face DNA."));
	}
}

/** Same Y/Z swap as FMetaHumanCharacterIdentity::FState::EvaluateGizmos on raw MHC vertex data (MetaHumanCharacterIdentity.cpp). */
static FVector3f ConvertRawMHCVertexToUE(const FVector3f &Raw, const EMetaHumanCharacterOrientation Orient)
{
	if (Orient == EMetaHumanCharacterOrientation::Y_UP)
	{
		return FVector3f{Raw.X, Raw.Z, Raw.Y};
	}
	return FVector3f{Raw.X, -Raw.Y, Raw.Z};
}

static int32 FindDNAJointIndexByName(const TSharedPtr<IDNAReader> &Geo, const FName &Name)
{
	if (!Geo.IsValid() || Name.IsNone())
	{
		return INDEX_NONE;
	}
	const FString Target = Name.ToString();
	const uint16 N = Geo->GetJointCount();
	for (uint16 I = 0; I < N; ++I)
	{
		if (Geo->GetJointName(I) == Target)
		{
			return static_cast<int32>(I);
		}
	}
	return INDEX_NONE;
}
#endif

UMetahumanFaceGizmoComponent::UMetahumanFaceGizmoComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

UMetahumanFaceGizmoComponent::~UMetahumanFaceGizmoComponent()
{
	DeleteImpl(ImplPtr);
}

void UMetahumanFaceGizmoComponent::BeginPlay()
{
	Super::BeginPlay();

	const AActor *Owner = GetOwner();
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	const TCHAR *const EvalModeStr = TEXT("1(Editor/CoreTech)");
#else
	const TCHAR *const EvalModeStr = TEXT("0(GameStub)");
#endif
	UE_LOG(LogMetahumanGizmoRuntime, Log,
		   TEXT("[MetahumanGizmo] BeginPlay | Actor=%s | AutoInit=%s | EVAL=%s"),
		   Owner ? *Owner->GetName() : TEXT("(null)"),
		   bAutoInitializeOnBeginPlay ? TEXT("true") : TEXT("false"),
		   EvalModeStr);

	if (bAutoInitializeOnBeginPlay)
	{
		if (InitializeIdentity())
		{
			if (!RefreshGizmoTransforms())
			{
				UE_LOG(LogMetahumanGizmoRuntime, Error, TEXT("[MetahumanGizmo] BeginPlay: InitializeIdentity OK but RefreshGizmoTransforms failed (see earlier logs)."));
			}
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Error, TEXT("[MetahumanGizmo] BeginPlay: InitializeIdentity failed — no gizmos. Check DNA, MHC paths, and LogMetaHumanCoreTechLib if Init failed."));
		}
	}

	TryRegisterMetaHumanWithEditorSubsystem(false);
}

void UMetahumanFaceGizmoComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ReleaseGizmoSpheres();
	DeleteImpl(ImplPtr);
	bIdentityInitialized = false;
	CachedArchetypeFaceDNAForIdentity = nullptr;
	Super::EndPlay(EndPlayReason);
}

void UMetahumanFaceGizmoComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction *ThisTick)
{
	Super::TickComponent(DeltaTime, TickType, ThisTick);

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	if (bEnableMoveInteraction && bIdentityInitialized)
	{
		ProcessMoveInteraction(DeltaTime);
	}
#endif

	if (bTickRefreshEveryFrame && bIdentityInitialized)
	{
		RefreshGizmoTransforms();
	}
}

UDNAAsset *UMetahumanFaceGizmoComponent::ResolveFaceDNAForInit()
{
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	auto TryResolveExplicitOrMeshDNA = [this]() -> UDNAAsset *
	{
		if (IsValid(FaceDNAAsset))
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] Face DNA: using explicit FaceDNAAsset '%s'."),
				   *FaceDNAAsset->GetName());
			return FaceDNAAsset;
		}

		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] Face DNA: FaceDNAAsset unset — trying UDNAAsset from FaceMeshComponent SkeletalMesh (Asset User Data)."));

		if (!IsValid(FaceMeshComponent))
		{
			UE_LOG(LogMetahumanGizmoRuntime, Warning,
				   TEXT("[MetahumanGizmo] Face DNA: FAIL — FaceMeshComponent is null. Assign Face Mesh Component or set Face DNA Asset explicitly."));
			return nullptr;
		}

		USkeletalMesh *SkelMesh = FaceMeshComponent->GetSkeletalMeshAsset();
		if (!IsValid(SkelMesh))
		{
			UE_LOG(LogMetahumanGizmoRuntime, Warning,
				   TEXT("[MetahumanGizmo] Face DNA: FAIL — FaceMeshComponent '%s' has no skeletal mesh (GetSkeletalMeshAsset() null)."),
				   *FaceMeshComponent->GetName());
			return nullptr;
		}

		const FString MeshPackageName = SkelMesh->GetOutermost() ? SkelMesh->GetOutermost()->GetName() : FString();
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] Face DNA: inspecting SkeletalMesh asset '%s' | package: %s"),
			   *SkelMesh->GetName(),
			   *MeshPackageName);

		UAssetUserData *UserData = SkelMesh->GetAssetUserDataOfClass(UDNAAsset::StaticClass());
		if (!UserData)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Warning,
				   TEXT("[MetahumanGizmo] Face DNA: FAIL — SkeletalMesh '%s' has no UDNAAsset Asset User Data. ")
					   TEXT("MetaHuman face SKM normally stores DNA here; re-import or assign DNA on the mesh, or set Face DNA Asset on this component."),
				   *SkelMesh->GetName());
			return nullptr;
		}

		UDNAAsset *const FromMesh = Cast<UDNAAsset>(UserData);
		if (!IsValid(FromMesh))
		{
			UE_LOG(LogMetahumanGizmoRuntime, Error,
				   TEXT("[MetahumanGizmo] Face DNA: FAIL — Asset User Data class UDNAAsset mismatch on '%s'."),
				   *SkelMesh->GetName());
			return nullptr;
		}

		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] Face DNA: OK — UDNAAsset '%s' from SkeletalMesh '%s'."),
			   *FromMesh->GetName(),
			   *SkelMesh->GetName());
		return FromMesh;
	};

	if (bUsePluginArchetypeFaceDNAForIdentity)
	{
		if (IsValid(CachedArchetypeFaceDNAForIdentity))
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] Face DNA: using cached plugin archetype SKM_Face (UDNAAsset '%s')."),
				   *CachedArchetypeFaceDNAForIdentity->GetName());
			return CachedArchetypeFaceDNAForIdentity;
		}

		const FString ContentDir = GetMetaHumanCoreTechContentDir();
		const FString ArchetypePath = ContentDir.IsEmpty()
										  ? FString()
										  : FPaths::ConvertRelativePathToFull(ContentDir / TEXT("ArchetypeDNA/SKM_Face.dna"));
		int64 FileSz = 0;
		if (!ArchetypePath.IsEmpty() && FileExistsNonEmpty(ArchetypePath, FileSz))
		{
			CachedArchetypeFaceDNAForIdentity = GetDNAAssetFromFile(ArchetypePath, GetTransientPackage(), EDNADataLayer::All);
			if (IsValid(CachedArchetypeFaceDNAForIdentity))
			{
				UE_LOG(LogMetahumanGizmoRuntime, Log,
					   TEXT("[MetahumanGizmo] Face DNA: using plugin archetype file SKM_Face.dna (bytes=%lld) — matches editor GetArchetypeDNAAseet(Face). Path=%s"),
					   FileSz,
					   *ArchetypePath);
				return CachedArchetypeFaceDNAForIdentity;
			}
			UE_LOG(LogMetahumanGizmoRuntime, Warning,
				   TEXT("[MetahumanGizmo] Face DNA: GetDNAAssetFromFile failed for archetype | %s — falling back to FaceDNAAsset / mesh DNA."),
				   *ArchetypePath);
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Warning,
				   TEXT("[MetahumanGizmo] Face DNA: MetaHumanCoreTech missing or SKM_Face.dna not found — falling back to FaceDNAAsset / mesh DNA."));
		}

		if (UDNAAsset *const Fallback = TryResolveExplicitOrMeshDNA())
		{
			return Fallback;
		}
		return nullptr;
	}

	if (UDNAAsset *const Resolved = TryResolveExplicitOrMeshDNA())
	{
		return Resolved;
	}
	return nullptr;
#else
	return nullptr;
#endif
}

bool UMetahumanFaceGizmoComponent::InitializeIdentity()
{
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] InitializeIdentity: start"));

	UDNAAsset *const ResolvedFaceDNA = ResolveFaceDNAForInit();
	if (!IsValid(ResolvedFaceDNA))
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] InitializeIdentity: FAIL — no face UDNAAsset (explicit property empty and none on face SKM). See earlier [MetahumanGizmo] Face DNA logs."));
		return false;
	}

	FString FacePath = FaceMHCDataPath;
	FString BodyPath = BodyMHCDataPath;
	ApplyPluginDefaultMHCPathsIfNeeded(bUsePluginDefaultMHCPaths, FacePath, BodyPath);
	if (FacePath.IsEmpty() || BodyPath.IsEmpty())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] InitializeIdentity: FAIL — Face or Body MHC path still empty after optional plugin defaults (set paths or enable bUsePluginDefaultMHCPaths with MetaHumanCharacter plugin)."));
		return false;
	}

	{
		const FString FaceAbs = FPaths::ConvertRelativePathToFull(FacePath);
		const FString BodyAbs = FPaths::ConvertRelativePathToFull(BodyPath);
		const bool bFaceDir = FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*FaceAbs);
		const bool bBodyDir = FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*BodyAbs);
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] Paths | FaceDNA=%s | FaceMHC exists=%s | %s | BodyMHC exists=%s | %s"),
			   *ResolvedFaceDNA->GetName(),
			   bFaceDir ? TEXT("yes") : TEXT("NO"),
			   *FaceAbs,
			   bBodyDir ? TEXT("yes") : TEXT("NO"),
			   *BodyAbs);
		if (!bFaceDir || !bBodyDir)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Error,
				   TEXT("[MetahumanGizmo] MHC folder missing on disk — Identity::Init will likely fail. Use full paths to IdentityTemplate-style folders (see README)."));
		}

		LogIdentityTemplateFacePreChecks(FaceAbs);
		LogCombinedBodyDnaPreCheck();
		LogFaceDnaVsArchetypeTopology(ResolvedFaceDNA);
	}

	ReleaseGizmoSpheres();
	DeleteImpl(ImplPtr);
	bIdentityInitialized = false;

	ImplPtr = new FMetahumanFaceGizmoComponentImpl();
	FMetahumanFaceGizmoComponentImpl *Impl = static_cast<FMetahumanFaceGizmoComponentImpl *>(ImplPtr);

	Impl->Identity = MakeUnique<FMetaHumanCharacterIdentity>();
	const EMetaHumanCharacterOrientation Orient = (DNAOrientationIndex != 0)
													  ? EMetaHumanCharacterOrientation::Z_UP
													  : EMetaHumanCharacterOrientation::Y_UP;

	UE_LOG(LogMetahumanGizmoRuntime, Log,
		   TEXT("[MetahumanGizmo] Calling FMetaHumanCharacterIdentity::Init | DNAOrientation=%s"),
		   DNAOrientationIndex != 0 ? TEXT("Z_UP") : TEXT("Y_UP"));

	if (!Impl->Identity->Init(FacePath, BodyPath, ResolvedFaceDNA, Orient))
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] InitializeIdentity: FAIL — FMetaHumanCharacterIdentity::Init returned false. Also search Output Log for LogMetaHumanCoreTechLib / \"failed to initialize MHC API\"."));
		DeleteImpl(ImplPtr);
		return false;
	}

	Impl->FaceState = Impl->Identity->CreateState();
	if (!Impl->FaceState.IsValid())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error, TEXT("[MetahumanGizmo] InitializeIdentity: FAIL — CreateState returned null."));
		DeleteImpl(ImplPtr);
		return false;
	}

	const int32 NumG = Impl->FaceState->NumGizmos();
	UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] CreateState OK | NumGizmos=%d"), NumG);

	if (SourceMetaHumanCharacter)
	{
		const FSharedBuffer Buffer = SourceMetaHumanCharacter->GetFaceStateData();
		if (Buffer.GetSize() > 0)
		{
			if (!Impl->FaceState->Deserialize(Buffer))
			{
				UE_LOG(LogMetahumanGizmoRuntime, Warning, TEXT("[MetahumanGizmo] Deserialize face state failed; using default state."));
			}
			else
			{
				UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] Deserialized face state from SourceMetaHumanCharacter OK (bytes=%llu)."), static_cast<uint64>(Buffer.GetSize()));
			}
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Verbose, TEXT("[MetahumanGizmo] SourceMetaHumanCharacter set but GetFaceStateData() is empty; skipping Deserialize."));
		}
	}

	Impl->CachedAlignmentJointIndexInEvaluateBuffer = INDEX_NONE;
	if (TSharedPtr<IDNAReader> Geo = ResolvedFaceDNA->GetGeometryReader())
	{
		Impl->CachedAlignmentJointIndexInEvaluateBuffer = FindDNAJointIndexByName(Geo, GizmoAlignmentBoneName);
		if (Impl->CachedAlignmentJointIndexInEvaluateBuffer == INDEX_NONE)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Warning,
				   TEXT("[MetahumanGizmo] Gizmo alignment: joint '%s' not found in Identity face DNA '%s' — FacialRootBone mode will fall back or skip."),
				   *GizmoAlignmentBoneName.ToString(),
				   *ResolvedFaceDNA->GetName());
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] Gizmo alignment: joint '%s' -> Evaluate().Vertices[%d] (DNA joint order)."),
				   *GizmoAlignmentBoneName.ToString(),
				   Impl->CachedAlignmentJointIndexInEvaluateBuffer);
		}
	}

	bIdentityInitialized = true;
	UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] InitializeIdentity: SUCCESS"));
	return true;
#else
	UE_LOG(LogMetahumanGizmoRuntime, Warning,
		   TEXT("[MetahumanGizmo] InitializeIdentity: FAIL — EVAL=0 (Game target / no MetaHumanCoreTechLib). Use MutableSampleEditor + PIE, or rebuild game with CoreTech linked."));
	return false;
#endif
}

bool UMetahumanFaceGizmoComponent::RefreshGizmoTransforms()
{
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	if (!bIdentityInitialized)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: SKIP — not initialized (call InitializeIdentity first or fix BeginPlay)."));
		return false;
	}
	if (!ImplPtr)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: SKIP — ImplPtr null."));
		return false;
	}

	FMetahumanFaceGizmoComponentImpl *Impl = static_cast<FMetahumanFaceGizmoComponentImpl *>(ImplPtr);
	if (!Impl->FaceState.IsValid())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: SKIP — FaceState invalid."));
		return false;
	}

	const FMetaHumanRigEvaluatedState Evaluated = Impl->FaceState->Evaluate();
	const TArray<FVector3f> ManipulatorPositions = Impl->FaceState->EvaluateGizmos(Evaluated.Vertices);

	UE_LOG(LogMetahumanGizmoRuntime, Verbose,
		   TEXT("[MetahumanGizmo] RefreshGizmoTransforms: Evaluate verts=%d | gizmo points=%d"),
		   Evaluated.Vertices.Num(),
		   ManipulatorPositions.Num());

	if (ManipulatorPositions.Num() == 0)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: gizmo count is 0 — nothing to draw."));
	}

	EnsureSphereCount(ManipulatorPositions.Num());

	if (GizmoSpheres.Num() != ManipulatorPositions.Num())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] RefreshGizmoTransforms: sphere count mismatch (spheres=%d points=%d) — EnsureSphereCount may have failed (owner/mesh?)."),
			   GizmoSpheres.Num(),
			   ManipulatorPositions.Num());
	}

	const FTransform MeshXform = FaceMeshComponent
									 ? FaceMeshComponent->GetComponentTransform()
									 : (GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity);

	const EMetaHumanCharacterOrientation Orient = (DNAOrientationIndex != 0)
													  ? EMetaHumanCharacterOrientation::Z_UP
													  : EMetaHumanCharacterOrientation::Y_UP;

	FVector AlignmentDeltaWorld = FVector::ZeroVector;

	const auto ComputeMeshBoundsCentroidDelta = [&]() -> FVector
	{
		if (!FaceMeshComponent || !bUsePluginArchetypeFaceDNAForIdentity || ManipulatorPositions.Num() == 0)
		{
			return FVector::ZeroVector;
		}
		FVector RigCentroid = FVector::ZeroVector;
		for (const FVector3f &P : ManipulatorPositions)
		{
			RigCentroid += FVector(P);
		}
		RigCentroid /= static_cast<float>(ManipulatorPositions.Num());
		const FVector RigCentroidWorld = MeshXform.TransformPosition(RigCentroid);
		const FVector MeshBoundsCenterWorld = FaceMeshComponent->Bounds.Origin;
		return MeshBoundsCenterWorld - RigCentroidWorld;
	};

	if (FaceMeshComponent && ManipulatorPositions.Num() > 0)
	{
		if (GizmoWorldAlignment == EMetahumanGizmoWorldAlignment::FacialRootBone)
		{
			const int32 JointIdx = Impl->CachedAlignmentJointIndexInEvaluateBuffer;
			const int32 BoneIdx = FaceMeshComponent->GetBoneIndex(GizmoAlignmentBoneName);
			if (JointIdx != INDEX_NONE && Evaluated.Vertices.IsValidIndex(JointIdx) && BoneIdx != INDEX_NONE)
			{
				const FVector3f RigJointUE = ConvertRawMHCVertexToUE(Evaluated.Vertices[JointIdx], Orient);
				const FVector RigJointWorld = MeshXform.TransformPosition(FVector(RigJointUE));
				const FTransform BoneCS = FaceMeshComponent->GetBoneTransform(BoneIdx);
				const FVector BoneWorldPos = MeshXform.TransformPosition(BoneCS.GetLocation());
				AlignmentDeltaWorld = BoneWorldPos - RigJointWorld;
				UE_LOG(LogMetahumanGizmoRuntime, Verbose,
					   TEXT("[MetahumanGizmo] Gizmo alignment: FacialRootBone | RigJointWorld=%s | BoneWorld=%s | Delta=%s"),
					   *RigJointWorld.ToString(),
					   *BoneWorldPos.ToString(),
					   *AlignmentDeltaWorld.ToString());
			}
			else
			{
				UE_LOG(LogMetahumanGizmoRuntime, Verbose,
					   TEXT("[MetahumanGizmo] FacialRootBone alignment skipped (jointIdx=%d verts=%d boneIdx=%d) — fallback=%s"),
					   JointIdx,
					   Evaluated.Vertices.Num(),
					   BoneIdx,
					   bFallbackToMeshBoundsIfBoneAlignmentFails ? TEXT("bounds") : TEXT("none"));
				if (bFallbackToMeshBoundsIfBoneAlignmentFails)
				{
					AlignmentDeltaWorld = ComputeMeshBoundsCentroidDelta();
				}
			}
		}
		else if (GizmoWorldAlignment == EMetahumanGizmoWorldAlignment::MeshBoundsCentroid)
		{
			AlignmentDeltaWorld = ComputeMeshBoundsCentroidDelta();
		}
	}

	// During LMB drag, optional freeze: reuse AlignmentDeltaWorld from the first refresh of this drag so bone/rig mismatch after
	// ApplyFaceState does not oscillate the gizmo spheres every frame (see ProcessMoveInteraction push order).
	if (MoveDragGizmoIndex != INDEX_NONE && bFreezeGizmoAlignmentWhileDragging)
	{
		if (bDragAlignmentCacheValid)
		{
			AlignmentDeltaWorld = CachedDragAlignmentDeltaWorld;
		}
		else
		{
			CachedDragAlignmentDeltaWorld = AlignmentDeltaWorld;
			bDragAlignmentCacheValid = true;
		}
	}

	if (FaceMeshComponent)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Verbose,
			   TEXT("[MetahumanGizmo] Using FaceMeshComponent=%s | Loc=%s"),
			   *FaceMeshComponent->GetName(),
			   *FaceMeshComponent->GetComponentLocation().ToString());
	}
	else
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning,
			   TEXT("[MetahumanGizmo] FaceMeshComponent unset — using Actor transform (gizmos may be offset)."));
	}

	for (int32 i = 0; i < ManipulatorPositions.Num(); ++i)
	{
		UStaticMeshComponent *Sphere = GizmoSpheres[i];
		if (!Sphere)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Error, TEXT("[MetahumanGizmo] Sphere[%d] null after EnsureSphereCount."), i);
			continue;
		}

		const FVector WorldPos = MeshXform.TransformPosition(FVector(ManipulatorPositions[i])) + AlignmentDeltaWorld;
		Sphere->SetWorldLocation(WorldPos);
		Sphere->SetWorldScale3D(FVector(BaseGizmoMeshScale * GizmoSphereScale));
		Sphere->SetVisibility(bShowGizmoSpheres);
		Sphere->SetHiddenInGame(!bShowGizmoSpheres);

		if (i == 0)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Verbose,
				   TEXT("[MetahumanGizmo] First gizmo world pos=%s | ShowSpheres=%s | Scale=%.4f"),
				   *WorldPos.ToString(),
				   bShowGizmoSpheres ? TEXT("true") : TEXT("false"),
				   BaseGizmoMeshScale * GizmoSphereScale);
		}
	}

	UE_LOG(LogMetahumanGizmoRuntime, Verbose, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: DONE"));
	return true;
#else
	UE_LOG(LogMetahumanGizmoRuntime, Verbose, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: no-op (EVAL=0)."));
	return false;
#endif
}

bool UMetahumanFaceGizmoComponent::ReinitializeIdentity()
{
	// Match a fresh ResolveFaceDNAForInit (e.g. after toggling bUsePluginArchetypeFaceDNAForIdentity or switching mesh DNA).
	CachedArchetypeFaceDNAForIdentity = nullptr;

	ReleaseGizmoSpheres();
	DeleteImpl(ImplPtr);
	bIdentityInitialized = false;

	const bool bOk = InitializeIdentity();
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	if (bOk)
	{
		(void)RefreshGizmoTransforms();
	}
#endif
	return bOk;
}

bool UMetahumanFaceGizmoComponent::SetActiveMetaHumanCharacter(UMetaHumanCharacter *NewCharacter, USkeletalMeshComponent *NewFaceMesh,
															  bool bUnregisterPreviousCharacterFromEditor)
{
#if WITH_EDITOR
	UMetaHumanCharacter *const PreviousCharacter = SourceMetaHumanCharacter;
#endif
	if (IsValid(NewFaceMesh))
	{
		FaceMeshComponent = NewFaceMesh;
	}
	SourceMetaHumanCharacter = NewCharacter;

#if WITH_EDITOR
	if (bUnregisterPreviousCharacterFromEditor && GEditor && IsValid(PreviousCharacter) && PreviousCharacter != NewCharacter)
	{
		if (UMetaHumanCharacterEditorSubsystem *const SubSys = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>())
		{
			if (SubSys->IsObjectAddedForEditing(PreviousCharacter))
			{
				SubSys->RemoveObjectToEdit(PreviousCharacter);
				UE_LOG(LogMetahumanGizmoRuntime, Log,
					   TEXT("[MetahumanGizmo] SetActiveMetaHumanCharacter: RemoveObjectToEdit('%s') before switching active character."),
					   *PreviousCharacter->GetName());
			}
		}
	}
#endif

	const bool bOk = ReinitializeIdentity();
#if WITH_EDITOR
	if (bOk)
	{
		TryRegisterMetaHumanWithEditorSubsystem(true);
	}
#endif
	return bOk;
}

void UMetahumanFaceGizmoComponent::RegisterSourceCharacterForEditorFaceUpdates()
{
	TryRegisterMetaHumanWithEditorSubsystem(true);
}

int32 UMetahumanFaceGizmoComponent::GetNumGizmos() const
{
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	if (!ImplPtr)
	{
		return 0;
	}
	const FMetahumanFaceGizmoComponentImpl *Impl = static_cast<const FMetahumanFaceGizmoComponentImpl *>(ImplPtr);
	return (Impl->FaceState.IsValid()) ? Impl->FaceState->NumGizmos() : 0;
#else
	return 0;
#endif
}

void UMetahumanFaceGizmoComponent::ReleaseGizmoSpheres()
{
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	if (MoveDragGizmoIndex != INDEX_NONE)
	{
		const int32 EndedIndex = MoveDragGizmoIndex;
		SetGizmoSphereDragMaterialState(MoveDragGizmoIndex, false);
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			   TEXT("[MetahumanGizmo] MoveInteraction | DRAG_END gizmo index=%d (ReleaseGizmoSpheres / reinit) | hadApply=%s"),
			   EndedIndex,
			   bMoveAppliedThisDrag ? TEXT("true") : TEXT("false"));
		bMoveAppliedThisDrag = false;
		OnGizmoDragEnd.Broadcast(EndedIndex);
		MoveDragGizmoIndex = INDEX_NONE;
		bDragAlignmentCacheValid = false;
	}
#endif
	for (UStaticMeshComponent *Sphere : GizmoSpheres)
	{
		if (Sphere)
		{
			Sphere->UnregisterComponent();
			Sphere->DestroyComponent();
		}
	}
	GizmoSpheres.Empty();
}

void UMetahumanFaceGizmoComponent::EnsureSphereCount(int32 Count)
{
	if (Count <= 0)
	{
		ReleaseGizmoSpheres();
		UE_LOG(LogMetahumanGizmoRuntime, Verbose, TEXT("[MetahumanGizmo] EnsureSphereCount: Count<=0, cleared spheres."));
		return;
	}

	if (!CachedGizmoStaticMesh)
	{
		CachedGizmoStaticMesh = LoadGizmoManipulatorStaticMesh();
	}

	if (!CachedGizmoStaticMesh)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error,
			   TEXT("[MetahumanGizmo] EnsureSphereCount: FAIL — cannot load SM_MoveTool_Gizmo or fallback Sphere."));
		return;
	}

	AActor *Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error, TEXT("[MetahumanGizmo] EnsureSphereCount: FAIL — owner is null (component not on spawned actor?)."));
		return;
	}

	while (GizmoSpheres.Num() < Count)
	{
		const bool bFirstSphere = (GizmoSpheres.Num() == 0);
		UStaticMeshComponent *Sphere = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transactional);
		Sphere->SetStaticMesh(CachedGizmoStaticMesh);
		ApplyMetaHumanEditorStyleGizmoCollision(Sphere);
		if (bFirstSphere)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Verbose,
				   TEXT("[MetahumanGizmo] Gizmo sphere collision | CollisionEnabled=QueryOnly | ObjectType=WorldDynamic | "
						"Response: Block WorldStatic, Visibility, Camera; Ignore all other channels | OverlapEvents=false"));
		}
		Sphere->SetCastShadow(false);
		if (FaceMeshComponent)
		{
			Sphere->SetupAttachment(FaceMeshComponent);
		}
		else
		{
			Sphere->SetupAttachment(Owner->GetRootComponent());
		}
		ApplyMetaHumanEditorStyleGizmoMaterial(Sphere);
		Sphere->RegisterComponent();
		Sphere->RecreatePhysicsState();
		GizmoSpheres.Add(Sphere);
	}

	while (GizmoSpheres.Num() > Count)
	{
		UStaticMeshComponent *Sphere = GizmoSpheres.Pop();
		if (Sphere)
		{
			Sphere->UnregisterComponent();
			Sphere->DestroyComponent();
		}
	}

	UE_LOG(LogMetahumanGizmoRuntime, Verbose,
		   TEXT("[MetahumanGizmo] EnsureSphereCount: OK | need=%d | have=%d"),
		   Count,
		   GizmoSpheres.Num());
}

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL

/** For MoveInteraction LMB logs: actor + component names and full object paths (not just class types). */
static FString FormatHitObjectDetailsForLog(const FHitResult &Hit)
{
	if (!Hit.GetComponent())
	{
		return FString::Printf(TEXT("HitComponent=null | HitActor=%s"),
							   Hit.GetActor() ? *Hit.GetActor()->GetPathName() : TEXT("(null)"));
	}
	UActorComponent *const C = Hit.GetComponent();
	AActor *const A = Hit.GetActor();
	return FString::Printf(
		TEXT("HitActorName=%s | HitActorPath=%s | HitComponentName=%s | HitComponentClass=%s | HitComponentPath=%s | BoneName=%s"),
		A ? *A->GetName() : TEXT("(null)"),
		A ? *A->GetPathName() : TEXT("(null)"),
		*C->GetName(),
		*C->GetClass()->GetName(),
		*C->GetPathName(),
		*Hit.BoneName.ToString());
}

static bool IntersectRayPlane(const FVector &RayOrigin, const FVector &RayDir, const FVector &PlanePoint, const FVector &PlaneNormal, FVector &OutPoint)
{
	const float Denom = FVector::DotProduct(RayDir, PlaneNormal);
	if (FMath::Abs(Denom) < KINDA_SMALL_NUMBER)
	{
		return false;
	}
	const float T = FVector::DotProduct(PlanePoint - RayOrigin, PlaneNormal) / Denom;
	OutPoint = RayOrigin + RayDir * T;
	return true;
}

void UMetahumanFaceGizmoComponent::SetGizmoSphereDragMaterialState(int32 GizmoIndex, bool bDragging)
{
	if (!GizmoSpheres.IsValidIndex(GizmoIndex) || !GizmoSpheres[GizmoIndex])
	{
		return;
	}
	UMaterialInstanceDynamic *const MID = Cast<UMaterialInstanceDynamic>(GizmoSpheres[GizmoIndex]->GetMaterial(0));
	if (MID)
	{
		MID->SetScalarParameterValue(TEXT("Drag"), bDragging ? 1.f : 0.f);
	}
}

void UMetahumanFaceGizmoComponent::TryRegisterMetaHumanWithEditorSubsystem(bool bFromSetActiveOrExplicit)
{
#if WITH_EDITOR
	if (!bApplyLiveFaceMeshUpdates || !IsValid(SourceMetaHumanCharacter))
	{
		return;
	}
	if (!bFromSetActiveOrExplicit && !bAutoRegisterCharacterForEditorFaceUpdates)
	{
		return;
	}
	if (!GEditor)
	{
		return;
	}
	UMetaHumanCharacterEditorSubsystem *const SubSys = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
	if (!SubSys)
	{
		return;
	}
	if (SubSys->IsObjectAddedForEditing(SourceMetaHumanCharacter))
	{
		return;
	}
	const bool bOk = SubSys->TryAddObjectToEdit(SourceMetaHumanCharacter);
	UE_LOG(LogMetahumanGizmoRuntime, Log,
		   TEXT("[MetahumanGizmo] TryAddObjectToEdit(SourceMetaHumanCharacter) -> %s (live face mesh updates require this in Editor/PIE)."),
		   bOk ? TEXT("OK") : TEXT("FAILED"));
#endif
}

void UMetahumanFaceGizmoComponent::TryPushFaceStateToEditorSubsystem()
{
#if WITH_EDITOR
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	if (!bApplyLiveFaceMeshUpdates || !IsValid(SourceMetaHumanCharacter) || !bIdentityInitialized || !ImplPtr)
	{
		return;
	}
	FMetahumanFaceGizmoComponentImpl *const Impl = static_cast<FMetahumanFaceGizmoComponentImpl *>(ImplPtr);
	if (!Impl->FaceState.IsValid())
	{
		return;
	}
	if (!GEditor)
	{
		return;
	}
	UMetaHumanCharacterEditorSubsystem *const SubSys = GEditor->GetEditorSubsystem<UMetaHumanCharacterEditorSubsystem>();
	if (!SubSys || !SubSys->IsObjectAddedForEditing(SourceMetaHumanCharacter))
	{
		return;
	}
	const TSharedRef<const FMetaHumanCharacterIdentity::FState> StateCopy = MakeShared<FMetaHumanCharacterIdentity::FState>(*Impl->FaceState);
	SubSys->ApplyFaceState(SourceMetaHumanCharacter, StateCopy);
	UE_LOG(LogMetahumanGizmoRuntime, Verbose, TEXT("[MetahumanGizmo] ApplyFaceState pushed after gizmo edit (live face SKM update)."));

	// #region agent log
	{
		const FSharedBuffer AssetBuf = SourceMetaHumanCharacter->GetFaceStateData();
		FSharedBuffer EditBuf;
		Impl->FaceState->Serialize(EditBuf);
		const uint64 AssetSize = static_cast<uint64>(AssetBuf.GetSize());
		const uint64 EditSize = static_cast<uint64>(EditBuf.GetSize());
		const bool bContentMatch =
			(AssetSize == EditSize) && (AssetSize == 0uLL || FMemory::Memcmp(AssetBuf.GetData(), EditBuf.GetData(), static_cast<SIZE_T>(AssetSize)) == 0);
		const FString AgentDebugNdjsonPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("MetaHuman/debug-2cd2dd.log"));
		const int64 Ts = FDateTime::UtcNow().ToUnixTimestamp() * 1000;
		const FString Line = FString::Printf(
			TEXT("{\"sessionId\":\"2cd2dd\",\"timestamp\":%lld,\"location\":\"MetahumanFaceGizmoComponent.cpp:TryPushFaceStateToEditorSubsystem\",\"message\":\"face_state_asset_vs_edit\",\"hypothesisId\":\"H1\",\"data\":{\"assetBytes\":%llu,\"editBytes\":%llu,\"contentMatch\":%s}}\n"),
			Ts, AssetSize, EditSize, bContentMatch ? TEXT("true") : TEXT("false"));
		FFileHelper::SaveStringToFile(Line, *AgentDebugNdjsonPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
	}
	// #endregion

	// ApplyFaceState updates CharacterData->FaceMesh in place; the editor preview path calls OnFaceMeshUpdated on actors in
	// CharacterActorList (CreateMetaHumanCharacterEditorActor). PIE/placed characters are usually not in that list, and their
	// FaceMeshComponent may still reference a different USkeletalMesh asset — sync to the subsystem edit mesh and refresh.
	if (FaceMeshComponent)
	{
		// TNotNull<const USkeletalMesh*> converts implicitly to pointer (no .Get() in UE5.7).
		const USkeletalMesh *const EditMeshConst = SubSys->Debug_GetFaceEditMesh(SourceMetaHumanCharacter);
		USkeletalMesh *const EditMesh = const_cast<USkeletalMesh *>(EditMeshConst);
		if (EditMesh)
		{
			// UE 5.1+: use GetSkeletalMeshAsset / SetSkeletalMeshAsset (GetSkeletalMesh removed from public API).
			if (FaceMeshComponent->GetSkeletalMeshAsset() != EditMesh)
			{
				FaceMeshComponent->SetSkeletalMeshAsset(EditMesh);
				UE_LOG(LogMetahumanGizmoRuntime, Log,
					   TEXT("[MetahumanGizmo] FaceMeshComponent now uses subsystem edit mesh '%s' (required for live deformation in PIE)."),
					   *EditMesh->GetName());
			}
			FaceMeshComponent->MarkRenderStateDirty();
			FaceMeshComponent->UpdateBounds();
		}
	}
#endif
#endif
}

void UMetahumanFaceGizmoComponent::ProcessMoveInteraction(float DeltaTime)
{
	(void)DeltaTime;
	if (!ImplPtr)
	{
		return;
	}
	FMetahumanFaceGizmoComponentImpl *Impl = static_cast<FMetahumanFaceGizmoComponentImpl *>(ImplPtr);
	if (!Impl->FaceState.IsValid())
	{
		return;
	}

	UWorld *const World = GetWorld();
	if (!World)
	{
		return;
	}

	APlayerController *PC = UGameplayStatics::GetPlayerController(World, MoveInteractionPlayerIndex);
	if (!PC)
	{
		return;
	}

	const FTransform MeshXform = FaceMeshComponent ? FaceMeshComponent->GetComponentTransform()
												   : (GetOwner() ? GetOwner()->GetActorTransform() : FTransform::Identity);

	const bool bDown = PC->IsInputKeyDown(EKeys::LeftMouseButton);

	if (MoveDragGizmoIndex != INDEX_NONE && !bDown)
	{
		const int32 EndedIndex = MoveDragGizmoIndex;
		SetGizmoSphereDragMaterialState(MoveDragGizmoIndex, false);
		if (bMoveAppliedThisDrag)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] MoveInteraction | DRAG_END gizmo index=%d (LMB release) — session had successful SetGizmoPosition applies"),
				   EndedIndex);
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] MoveInteraction | DRAG_END gizmo index=%d (LMB release) — no position apply (click only or zero delta)"),
				   EndedIndex);
		}
		bMoveAppliedThisDrag = false;
		OnGizmoDragEnd.Broadcast(EndedIndex);
#if WITH_EDITOR
		// #region agent log
		{
			const FString AgentDebugNdjsonPath = FPaths::Combine(FPaths::EnginePluginsDir(), TEXT("MetaHuman/debug-2cd2dd.log"));
			const int64 Ts = FDateTime::UtcNow().ToUnixTimestamp() * 1000;
			const FString Line = FString::Printf(
				TEXT("{\"sessionId\":\"2cd2dd\",\"timestamp\":%lld,\"location\":\"MetahumanFaceGizmoComponent.cpp:ProcessMoveInteraction\",\"message\":\"drag_end_no_commit\",\"hypothesisId\":\"H4\",\"data\":{\"gizmoIndex\":%d}}\n"),
				Ts, EndedIndex);
			FFileHelper::SaveStringToFile(Line, *AgentDebugNdjsonPath, FFileHelper::EEncodingOptions::AutoDetect, &IFileManager::Get(), FILEWRITE_Append);
		}
		// #endregion
#endif
		bMoveDragEditorBoundsValid = false;
		MoveDragAccumulatedRigDelta = FVector3f::ZeroVector;
		MoveDragGizmoIndex = INDEX_NONE;
		bDragAlignmentCacheValid = false;
		return;
	}

	if (bDown && MoveDragGizmoIndex == INDEX_NONE && PC->WasInputKeyJustPressed(EKeys::LeftMouseButton))
	{
		// Align with UMetaHumanCharacterEditorMeshEditingTool::HitTest: per-gizmo UStaticMeshComponent::LineTraceComponent only
		// (no UWorld line trace — avoids floor/scene competition). bTraceComplex=false. Prefer closest hit by FHitResult::Distance.
		FVector RayOrigin;
		FVector RayDir;
		if (!PC->DeprojectMousePositionToWorld(RayOrigin, RayDir))
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] MoveInteraction | LMB: DeprojectMousePositionToWorld failed (no pick ray)"));
			return;
		}
		RayDir.Normalize();
		const FVector RayEnd = RayOrigin + RayDir * HALF_WORLD_MAX;
		const bool bTraceComplex = false;
		FCollisionQueryParams PickParams(NAME_None, bTraceComplex);

		int32 Idx = INDEX_NONE;
		float BestDistance = -1.f;
		FHitResult GizmoHit;
		FHitResult PerSphereHit;
		for (int32 I = 0; I < GizmoSpheres.Num(); ++I)
		{
			UStaticMeshComponent *const Sphere = GizmoSpheres[I];
			if (!Sphere)
			{
				continue;
			}
			if (Sphere->LineTraceComponent(PerSphereHit, RayOrigin, RayEnd, PickParams))
			{
				if (Idx == INDEX_NONE || PerSphereHit.Distance < BestDistance)
				{
					Idx = I;
					BestDistance = PerSphereHit.Distance;
					GizmoHit = PerSphereHit;
				}
			}
		}

		if (Idx != INDEX_NONE)
		{
			MoveDragGizmoIndex = Idx;
			bMoveAppliedThisDrag = false;
			bDragAlignmentCacheValid = false;
			if (GizmoBoundsMode == EMetahumanGizmoBoundsMode::EditorStyleSoftBox)
			{
				Impl->FaceState->GetGizmoPosition(Idx, MoveDragBeginRig);
				Impl->FaceState->GetGizmoPositionBounds(Idx, MoveDragMinBounds, MoveDragMaxBounds, BoundsBBoxReduction, BoundsExpandToCurrent);
				MoveDragAccumulatedRigDelta = FVector3f::ZeroVector;
				bMoveDragEditorBoundsValid = true;
			}
			else
			{
				bMoveDragEditorBoundsValid = false;
			}
			PC->GetMousePosition(LastMoveScreenX, LastMoveScreenY);
			SetGizmoSphereDragMaterialState(Idx, true);
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] MoveInteraction | PICK gizmo index=%d (LMB LineTraceComponent per sphere, bTraceComplex=false, closestDist=%.4f, begin drag) | %s"),
				   Idx,
				   BestDistance,
				   *FormatHitObjectDetailsForLog(GizmoHit));
			OnGizmoDragBegin.Broadcast(Idx);
		}
		else
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] MoveInteraction | LMB: no gizmo hit (LineTraceComponent on %d spheres, bTraceComplex=false, HALF_WORLD_MAX ray — same strategy as Editor HitTest)"),
				   GizmoSpheres.Num());
		}
		return;
	}

	if (bDown && MoveDragGizmoIndex != INDEX_NONE)
	{
		float X = 0.f;
		float Y = 0.f;
		PC->GetMousePosition(X, Y);
		// Editor (MetaHumanCharacterEditorMeshEditingTools) gets pixel deltas from Slate drag events; PIE/game often
		// uses mouse capture (FPS look) so GetMousePosition stays fixed while the user moves the mouse — use
		// GetInputMouseDelta in that case or WorldDelta stays zero (DRAG_END "no position apply").
		float MouseDeltaX = 0.f;
		float MouseDeltaY = 0.f;
		PC->GetInputMouseDelta(MouseDeltaX, MouseDeltaY);
		const bool bAbsUnchanged =
			FMath::IsNearlyEqual(X, LastMoveScreenX, 0.5f) && FMath::IsNearlyEqual(Y, LastMoveScreenY, 0.5f);
		const bool bHasFrameMouseDelta = !FMath::IsNearlyZero(MouseDeltaX) || !FMath::IsNearlyZero(MouseDeltaY);
		if (bAbsUnchanged && bHasFrameMouseDelta)
		{
			X = LastMoveScreenX + MouseDeltaX;
			Y = LastMoveScreenY + MouseDeltaY;
			UE_LOG(LogMetahumanGizmoRuntime, Verbose,
				   TEXT("[MetahumanGizmo] MoveInteraction | using GetInputMouseDelta (%.3f, %.3f) — viewport position was fixed (mouse capture / look)"),
				   MouseDeltaX,
				   MouseDeltaY);
		}

		UStaticMeshComponent *const DragSphere = GizmoSpheres.IsValidIndex(MoveDragGizmoIndex) ? GizmoSpheres[MoveDragGizmoIndex] : nullptr;
		const FVector GizmoWorld = DragSphere ? DragSphere->GetComponentLocation() : FVector::ZeroVector;

		FRotator CamRot = PC->PlayerCameraManager ? PC->PlayerCameraManager->GetCameraRotation() : FRotator::ZeroRotator;
		const FVector PlaneNormal = CamRot.Vector().GetSafeNormal();

		FVector WorldDelta = FVector::ZeroVector;
		FVector WorldOrigin = FVector::ZeroVector;
		FVector WorldDir = FVector::ZeroVector;
		if (UGameplayStatics::DeprojectScreenToWorld(PC, FVector2D(LastMoveScreenX, LastMoveScreenY), WorldOrigin, WorldDir))
		{
			WorldDir.Normalize();
			FVector P0;
			if (IntersectRayPlane(WorldOrigin, WorldDir, GizmoWorld, PlaneNormal, P0))
			{
				if (UGameplayStatics::DeprojectScreenToWorld(PC, FVector2D(X, Y), WorldOrigin, WorldDir))
				{
					WorldDir.Normalize();
					FVector P1;
					if (IntersectRayPlane(WorldOrigin, WorldDir, GizmoWorld, PlaneNormal, P1))
					{
						WorldDelta = (P1 - P0) * MoveInteractionSpeed;
					}
				}
			}
		}

		LastMoveScreenX = X;
		LastMoveScreenY = Y;

		if (!WorldDelta.IsNearlyZero(UE_KINDA_SMALL_NUMBER))
		{
			FVector3f CurrentRig;
			Impl->FaceState->GetGizmoPosition(MoveDragGizmoIndex, CurrentRig);
			const FVector RigDelta = MeshXform.InverseTransformVector(WorldDelta);
			const FVector3f RigDeltaF(static_cast<float>(RigDelta.X), static_cast<float>(RigDelta.Y), static_cast<float>(RigDelta.Z));

			FVector3f NewRig;
			if (GizmoBoundsMode == EMetahumanGizmoBoundsMode::EditorStyleSoftBox && bMoveDragEditorBoundsValid)
			{
				MoveDragAccumulatedRigDelta += RigDeltaF;
				const FVector3f ConstrainedOffset = ApplyEditorStyleGizmoTranslationConstraint(
					MoveDragBeginRig,
					MoveDragAccumulatedRigDelta,
					MoveDragMinBounds,
					MoveDragMaxBounds,
					BoundsSoftAmount);
				NewRig = MoveDragBeginRig + ConstrainedOffset;
				Impl->FaceState->SetGizmoPosition(MoveDragGizmoIndex, NewRig, bSymmetricMove, false);
			}
			else
			{
				NewRig = CurrentRig + RigDeltaF;
				Impl->FaceState->SetGizmoPosition(MoveDragGizmoIndex, NewRig, bSymmetricMove, bEnforceGizmoBounds);
			}
			// Push face mesh first so bones/Evaluate align with deformed SKM before recomputing gizmo world positions.
			TryPushFaceStateToEditorSubsystem();
			(void)RefreshGizmoTransforms();
			const bool bFirstApplyThisDrag = !bMoveAppliedThisDrag;
			bMoveAppliedThisDrag = true;
			if (bFirstApplyThisDrag)
			{
				UE_LOG(LogMetahumanGizmoRuntime, Log,
					   TEXT("[MetahumanGizmo] MoveInteraction | FIRST_APPLY gizmo index=%d (drag session will update rig; further frames: Verbose)"),
					   MoveDragGizmoIndex);
			}
			UE_LOG(LogMetahumanGizmoRuntime, Verbose,
				   TEXT("[MetahumanGizmo] MoveInteraction | APPLY gizmo index=%d | worldDelta=%s | rigDelta=%s | newRig=%s | symmetric=%s boundsMode=%d enforceBounds=%s speed=%.3f"),
				   MoveDragGizmoIndex,
				   *WorldDelta.ToString(),
				   *RigDelta.ToString(),
				   *FVector(NewRig).ToString(),
				   bSymmetricMove ? TEXT("true") : TEXT("false"),
				   static_cast<int32>(GizmoBoundsMode),
				   (GizmoBoundsMode == EMetahumanGizmoBoundsMode::EditorStyleSoftBox) ? TEXT("n/a(B)") : (bEnforceGizmoBounds ? TEXT("true") : TEXT("false")),
				   MoveInteractionSpeed);
			OnGizmoMoved.Broadcast(MoveDragGizmoIndex, FVector(NewRig));
		}
	}
}

#else

void UMetahumanFaceGizmoComponent::SetGizmoSphereDragMaterialState(int32 GizmoIndex, bool bDragging)
{
}

void UMetahumanFaceGizmoComponent::ProcessMoveInteraction(float DeltaTime)
{
}

#endif
