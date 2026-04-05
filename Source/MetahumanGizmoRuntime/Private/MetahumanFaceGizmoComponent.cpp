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

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
#include "HAL/PlatformFileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Paths.h"
#include "UObject/UObjectGlobals.h"
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

/** Query-only collision so LineTraceComponent / visibility traces can hit (see UMetaHumanCharacterEditorMeshEditingTool::HitTest). */
static void ApplyMetaHumanEditorStyleGizmoCollision(UStaticMeshComponent *Sphere)
{
	if (!Sphere)
	{
		return;
	}
	Sphere->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Sphere->SetCollisionObjectType(ECC_WorldDynamic);
	Sphere->SetCollisionResponseToAllChannels(ECR_Ignore);
	Sphere->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Sphere->SetCollisionResponseToChannel(ECC_Camera, ECR_Block);
	Sphere->SetGenerateOverlapEvents(false);
}

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
struct FMetahumanFaceGizmoComponentImpl
{
	TUniquePtr<FMetaHumanCharacterIdentity> Identity;
	TSharedPtr<FMetaHumanCharacterIdentity::FState> FaceState;
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

	UE_LOG(LogMetahumanGizmoRuntime, Log,
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

		const FVector WorldPos = MeshXform.TransformPosition(FVector(ManipulatorPositions[i]));
		Sphere->SetWorldLocation(WorldPos);
		Sphere->SetWorldScale3D(FVector(BaseGizmoMeshScale * GizmoSphereScale));
		Sphere->SetVisibility(bShowGizmoSpheres);
		Sphere->SetHiddenInGame(!bShowGizmoSpheres);

		if (i == 0)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Log,
				   TEXT("[MetahumanGizmo] First gizmo world pos=%s | ShowSpheres=%s | Scale=%.4f"),
				   *WorldPos.ToString(),
				   bShowGizmoSpheres ? TEXT("true") : TEXT("false"),
				   BaseGizmoMeshScale * GizmoSphereScale);
		}
	}

	UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: DONE"));
	return true;
#else
	UE_LOG(LogMetahumanGizmoRuntime, Verbose, TEXT("[MetahumanGizmo] RefreshGizmoTransforms: no-op (EVAL=0)."));
	return false;
#endif
}

bool UMetahumanFaceGizmoComponent::ReinitializeIdentity()
{
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

	if (!CachedSphereMesh)
	{
		CachedSphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	}

	if (!CachedSphereMesh)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error, TEXT("[MetahumanGizmo] EnsureSphereCount: FAIL — cannot load /Engine/BasicShapes/Sphere."));
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
		UStaticMeshComponent *Sphere = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transactional);
		Sphere->SetStaticMesh(CachedSphereMesh);
		ApplyMetaHumanEditorStyleGizmoCollision(Sphere);
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

	UE_LOG(LogMetahumanGizmoRuntime, Log,
		   TEXT("[MetahumanGizmo] EnsureSphereCount: OK | need=%d | have=%d"),
		   Count,
		   GizmoSpheres.Num());
}
