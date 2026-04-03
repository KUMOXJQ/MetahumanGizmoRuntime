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
#include "MetaHumanCharacter.h"
#include "MetaHumanCharacterIdentity.h"
#include "MetaHumanRigEvaluatedState.h"
#endif

#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/Actor.h"

#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
#include "HAL/PlatformFileManager.h"
#include "Misc/Paths.h"
#endif

// 输出日志中搜索：[MetahumanGizmo]
// 更细：控制台输入 Log LogMetahumanGizmoRuntime Verbose

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

static void DeleteImpl(void*& ImplPtr)
{
	delete static_cast<FMetahumanFaceGizmoComponentImpl*>(ImplPtr);
	ImplPtr = nullptr;
}

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

	const AActor* Owner = GetOwner();
	UE_LOG(LogMetahumanGizmoRuntime, Log,
		TEXT("[MetahumanGizmo] BeginPlay | Actor=%s | AutoInit=%s | EVAL=%s"),
		Owner ? *Owner->GetName() : TEXT("(null)"),
		bAutoInitializeOnBeginPlay ? TEXT("true") : TEXT("false"),
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
		TEXT("1(Editor/CoreTech)")
#else
		TEXT("0(GameStub)")
#endif
	);

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
	Super::EndPlay(EndPlayReason);
}

void UMetahumanFaceGizmoComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTick)
{
	Super::TickComponent(DeltaTime, TickType, ThisTick);

	if (bTickRefreshEveryFrame && bIdentityInitialized)
	{
		RefreshGizmoTransforms();
	}
}

bool UMetahumanFaceGizmoComponent::InitializeIdentity()
{
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	UE_LOG(LogMetahumanGizmoRuntime, Log, TEXT("[MetahumanGizmo] InitializeIdentity: start"));

	if (!FaceDNAAsset)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning, TEXT("[MetahumanGizmo] InitializeIdentity: FAIL — FaceDNAAsset is null (set Face DNA in details)."));
		return false;
	}
	if (FaceMHCDataPath.IsEmpty() || BodyMHCDataPath.IsEmpty())
	{
		UE_LOG(LogMetahumanGizmoRuntime, Warning, TEXT("[MetahumanGizmo] InitializeIdentity: FAIL — FaceMHCDataPath or BodyMHCDataPath is empty."));
		return false;
	}

	{
		const FString FaceAbs = FPaths::ConvertRelativePathToFull(FaceMHCDataPath);
		const FString BodyAbs = FPaths::ConvertRelativePathToFull(BodyMHCDataPath);
		const bool bFaceDir = FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*FaceAbs);
		const bool bBodyDir = FPlatformFileManager::Get().GetPlatformFile().DirectoryExists(*BodyAbs);
		UE_LOG(LogMetahumanGizmoRuntime, Log,
			TEXT("[MetahumanGizmo] Paths | FaceDNA=%s | FaceMHC exists=%s | %s | BodyMHC exists=%s | %s"),
			*FaceDNAAsset->GetName(),
			bFaceDir ? TEXT("yes") : TEXT("NO"),
			*FaceAbs,
			bBodyDir ? TEXT("yes") : TEXT("NO"),
			*BodyAbs);
		if (!bFaceDir || !bBodyDir)
		{
			UE_LOG(LogMetahumanGizmoRuntime, Error,
				TEXT("[MetahumanGizmo] MHC folder missing on disk — Identity::Init will likely fail. Use full paths to IdentityTemplate-style folders (see README)."));
		}
	}

	ReleaseGizmoSpheres();
	DeleteImpl(ImplPtr);
	bIdentityInitialized = false;

	ImplPtr = new FMetahumanFaceGizmoComponentImpl();
	FMetahumanFaceGizmoComponentImpl* Impl = static_cast<FMetahumanFaceGizmoComponentImpl*>(ImplPtr);

	Impl->Identity = MakeUnique<FMetaHumanCharacterIdentity>();
	const EMetaHumanCharacterOrientation Orient = (DNAOrientationIndex != 0)
		? EMetaHumanCharacterOrientation::Z_UP
		: EMetaHumanCharacterOrientation::Y_UP;

	UE_LOG(LogMetahumanGizmoRuntime, Log,
		TEXT("[MetahumanGizmo] Calling FMetaHumanCharacterIdentity::Init | DNAOrientation=%s"),
		DNAOrientationIndex != 0 ? TEXT("Z_UP") : TEXT("Y_UP"));

	if (!Impl->Identity->Init(FaceMHCDataPath, BodyMHCDataPath, FaceDNAAsset, Orient))
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

	FMetahumanFaceGizmoComponentImpl* Impl = static_cast<FMetahumanFaceGizmoComponentImpl*>(ImplPtr);
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
		UStaticMeshComponent* Sphere = GizmoSpheres[i];
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

int32 UMetahumanFaceGizmoComponent::GetNumGizmos() const
{
#if WITH_METAHUMAN_GIZMO_RUNTIME_EVAL
	if (!ImplPtr)
	{
		return 0;
	}
	const FMetahumanFaceGizmoComponentImpl* Impl = static_cast<const FMetahumanFaceGizmoComponentImpl*>(ImplPtr);
	return (Impl->FaceState.IsValid()) ? Impl->FaceState->NumGizmos() : 0;
#else
	return 0;
#endif
}

void UMetahumanFaceGizmoComponent::ReleaseGizmoSpheres()
{
	for (UStaticMeshComponent* Sphere : GizmoSpheres)
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

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogMetahumanGizmoRuntime, Error, TEXT("[MetahumanGizmo] EnsureSphereCount: FAIL — owner is null (component not on spawned actor?)."));
		return;
	}

	while (GizmoSpheres.Num() < Count)
	{
		UStaticMeshComponent* Sphere = NewObject<UStaticMeshComponent>(Owner, NAME_None, RF_Transactional);
		Sphere->SetStaticMesh(CachedSphereMesh);
		Sphere->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Sphere->SetCastShadow(false);
		if (FaceMeshComponent)
		{
			Sphere->SetupAttachment(FaceMeshComponent);
		}
		else
		{
			Sphere->SetupAttachment(Owner->GetRootComponent());
		}
		Sphere->RegisterComponent();
		GizmoSpheres.Add(Sphere);
	}

	while (GizmoSpheres.Num() > Count)
	{
		UStaticMeshComponent* Sphere = GizmoSpheres.Pop();
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
