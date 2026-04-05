# MetaHuman Runtime 面部 Gizmo 显示 — 完整开发计划

> 存放位置：`Engine/Plugins/MetaHuman/MetahumanGizmoRuntime/`  
> 目标：在 **编辑器 PIE / 开发环境** 中，用 **与 MetaHuman Creator 同源** 的算法（`FMetaHumanCharacterIdentity::Evaluate` + `EvaluateGizmos`）在脸部 **世界空间** 显示 **Gizmo 点**，**不依赖** `MetaHumanCharacterEditor` 子系统与 Slate 实现核心计算。  
> **作者 / 维护：** XujiaqiKumo

更短的使用说明见同目录 [`README.md`](./README.md)。

---

## 1. 已落地实现（与源码对照）

### 1.1 插件与模块

| 项       | 说明                                                                                                          |
| -------- | ------------------------------------------------------------------------------------------------------------- | ---------------------------------------------------------------------------- |
| 描述文件 | `MetahumanGizmoRuntime.uplugin`（依赖 `MetaHumanCoreTech`、`MetaHumanSDK`、`MetaHumanCharacter`、`RigLogic`） | ![1775381889519](image/MetaHuman_Runtime面部Gizmo开发计划/1775381889519.png) |
| 模块     | `MetahumanGizmoRuntime`（`Runtime` 类型）                                                                     |
| 入口     | `MetahumanGizmoRuntime.cpp`：`DEFINE_LOG_CATEGORY(LogMetahumanGizmoRuntime)`                                  |
| 构建     | `MetahumanGizmoRuntime.Build.cs`：见 **§3.3**                                                                 |

### 1.2 核心类：`UMetahumanFaceGizmoComponent`

| 文件                                      | 职责                                                                                           |
| ----------------------------------------- | ---------------------------------------------------------------------------------------------- |
| `Public/MetahumanFaceGizmoComponent.h`    | 蓝图可 spawn 组件；`UPROPERTY` / `UFUNCTION` 暴露                                              |
| `Private/MetahumanFaceGizmoComponent.cpp` | `InitializeIdentity`、`RefreshGizmoTransforms`、小球池 `EnsureSphereCount`、`ImplPtr` 生命周期 |

**实现要点：**

- **PIMPL**：`void* ImplPtr` 指向 `FMetahumanFaceGizmoComponentImpl`（内含 `TUniquePtr<FMetaHumanCharacterIdentity>`、`TSharedPtr<FState>`），避免 UHT 与 `TUniquePtr` 不完整类型冲突。
- **条件编译**：`WITH_METAHUMAN_GIZMO_RUNTIME_EVAL`（仅 Editor 目标为 `1`）控制是否链接 `MetaHumanCoreTechLib` 与完整 Titan 路径；Game 目标为占位实现。
- **算法链**：`Init` → `CreateState` →（可选）`Deserialize(SourceMetaHumanCharacter->GetFaceStateData())` → `Evaluate()` → `EvaluateGizmos(Vertices)`。
- **可视化**：`/Engine/BasicShapes/Sphere` 动态创建 `UStaticMeshComponent`，数量与 `EvaluateGizmos` 返回数组长度一致；世界位置：`FaceMeshComponent->GetComponentTransform().TransformPosition(FVector(GizmoPos))`（未设置 Face 网格时用 Actor 变换）。
- **生命周期**：`BeginPlay` 中 `bAutoInitializeOnBeginPlay` 为 true 时调用 `InitializeIdentity` + `RefreshGizmoTransforms`；`bTickRefreshEveryFrame` 为 true 时每帧 `RefreshGizmoTransforms`。
- **调试**：统一前缀 `[MetahumanGizmo]`，分类 `LogMetahumanGizmoRuntime`；关键路径含 MHC 目录磁盘存在性检测。

### 1.3 与计划阶段的对应关系

| 原阶段       | 状态         | 说明                                                                                                |
| ------------ | ------------ | --------------------------------------------------------------------------------------------------- |
| A 可行性     | **已完成**   | Editor 目标下 `Init` + `Evaluate` + `EvaluateGizmos` 可走通（依赖正确 DNA + IdentityTemplate 路径） |
| B 组件可视化 | **已完成**   | `UMetahumanFaceGizmoComponent` + 小球池 + `RefreshGizmoTransforms`                                  |
| C 坐标对齐   | **部分完成** | 当前为「Identity 空间点 × 脸部组件世界矩阵」；与 Creator 像素级一致需项目内标定（原 C2/C3）         |
| D 打包与性能 | **未做**     | Game 目标为 stub；未做 Cook 路径抽象、异步 Evaluate、节流策略                                       |

---

## 2. 与 MetaHuman Creator / Character Editor 的差异

**相同（数学层）：**

```text
FaceState->EvaluateGizmos(FaceState->Evaluate().Vertices)
```

与 `UMetaHumanCharacterEditorSubsystem::GetFaceGizmos`、`FMetaHumanCharacterIdentity::FState::EvaluateGizmos`（Titan `MetaHumanCreatorAPI`）一致。

**不同（工程层）：**

| 维度            | Creator / `MetaHumanCharacterEditor`                                        | 本插件                                                                                                                   |
| --------------- | --------------------------------------------------------------------------- | ------------------------------------------------------------------------------------------------------------------------ |
| `FState` 来源   | 子系统 `CharacterDataMap` 与 `UMetaHumanCharacter` 绑定                     | 组件内自建 `FMetaHumanCharacterIdentity` + `CreateState`，可选从 `UMetaHumanCharacter::GetFaceStateData()` `Deserialize` |
| Gizmo 显示      | Face Move 等 **Interactive Tool**：`ManipulatorsActor` + `SetWorldLocation` | 自建 `UStaticMeshComponent` 小球 + `SetWorldLocation`                                                                    |
| 脸部网格更新    | `UpdateFaceMeshInternal` 等完整编辑管线                                     | **不修改**网格，仅读 Evaluate 结果                                                                                       |
| 交互 / Undo     | 完整工具链                                                                  | **无**（仅可视化）                                                                                                       |
| `Init` 入参来源 | `GetFaceIdentityTemplateModelPath` / 原型脸 DNA 等 **代码写死**             | 蓝图 / 细节面板 **手动指定** DNA 与 MHC 路径                                                                             |

结论：**「点怎么算」对齐 Creator；「编辑器管线、工具、资产绑定」未复刻。**

---

## 3. 参数在何处被消费（实现级）

### 3.1 插件 `UMetahumanFaceGizmoComponent` 内

| 参数                           | 用途                                                                                       |
| ------------------------------ | ------------------------------------------------------------------------------------------ |
| **FaceMeshComponent**          | `RefreshGizmoTransforms`：世界变换矩阵；`EnsureSphereCount`：小球 `SetupAttachment` 父组件 |
| **FaceDNAAsset**               | 传入 `FMetaHumanCharacterIdentity::Init`                                                   |
| **FaceMHCDataPath**            | 传入 `Init` → Epic 传入 `CreateMHCApi` 的 **脸 MHC 根目录**（磁盘路径字符串）              |
| **BodyMHCDataPath**            | 插件内 **非空校验**；见下节 Epic 侧实际使用情况                                            |
| **DNAOrientationIndex**        | `0` = Y_UP，`1` = Z_UP，映射 `EMetaHumanCharacterOrientation`                              |
| **SourceMetaHumanCharacter**   | 若有效且 `GetFaceStateData()` 非空，则 `FaceState->Deserialize`                            |
| **GizmoSphereScale**           | 与内部 `BaseGizmoMeshScale` 相乘作为小球世界缩放                                           |
| **bAutoInitializeOnBeginPlay** | `BeginPlay` 是否自动 `InitializeIdentity`                                                  |
| **bShowGizmoSpheres**          | 小球 `SetVisibility` / `SetHiddenInGame`                                                   |
| **bTickRefreshEveryFrame**     | `TickComponent` 是否每帧 `RefreshGizmoTransforms`                                          |

### 3.2 Epic `FMetaHumanCharacterIdentity::Init`（`MetaHumanCharacterIdentity.cpp`）

| 形参                  | 实际使用                                                                                                                   |
| --------------------- | -------------------------------------------------------------------------------------------------------------------------- |
| **InMHCDataPath**     | 传入 `MetaHumanCreatorAPI::CreateMHCApi(..., TCHAR_TO_UTF8(*InMHCDataPath), ...)`，即 **Face IdentityTemplate** 磁盘根目录 |
| **InBodyMHCDataPath** | **当前实现未在函数体内引用**（仅保留签名，与 Editor 调用形式一致）                                                         |
| **InDNAAsset**        | `GetGeometryReader()->Unwrap()` → 脸 `dna::Reader`                                                                         |
| **InDNAAssetOrient**  | 存入 `Impl`，供状态求值时坐标系转换                                                                                        |

**合并身体 DNA（与蓝图里的 BodyMHCDataPath 无关）：**

- `FMetaHumanCommonDataUtils::GetCombinedDNAFilesystemPath()` → 通常为 **`MetaHumanCoreTech` 插件 Content 下 `ArchetypeDNA/body_head_combined.dna`**，由 `ReadDNAFromFile` 读入，作为 `CreateMHCApi` 的 **body `dna::Reader`** 参数（可为 null）。

因此：**脸模板路径 = 你填的 Face MHC；身体合并 DNA = 引擎插件内固定文件。**

### 3.3 `CreateMHCApi` 对 Face MHC 目录的期望（节选）

在 `InMHCDataPath` 下会拼接并读取如：`symmetry.json`、`landmarks_config.json`、`uemhc_rig_calibration_data.json`、`skinningWeightsConfig.json` 等。**不得**仅指向用户自定义 DNA 导出目录（缺少上述文件会 `failed to initialize MHC API` 或异常）。

**推荐与 Editor 子系统一致的路径示例（按本机引擎根修改）：**

- Face：`.../Engine/Plugins/MetaHuman/MetaHumanCharacter/Content/Face/IdentityTemplate`
- Body（字符串占位 / 文档一致）：`.../Engine/Plugins/MetaHuman/MetaHumanCharacter/Content/Body/IdentityTemplate`

---

## 4. 运行环境与可见性

| 场景                              | 行为                                                                                                    |
| --------------------------------- | ------------------------------------------------------------------------------------------------------- |
| **Editor + PIE / 播放**           | `WITH_METAHUMAN_GIZMO_RUNTIME_EVAL=1` 时完整逻辑；`BeginPlay` 执行后创建小球                            |
| **蓝图编辑器静态视口**            | **不执行** `BeginPlay`，**默认看不到** Gizmo；需 PIE                                                    |
| **Game / Shipping（预编译引擎）** | 通常 `EVAL=0`，`InitializeIdentity` 失败并打日志；完整 Evaluate 需源码引擎自行链 `MetaHumanCoreTechLib` |

`FMetaHumanCharacterIdentity::Init` 依赖 **`WITH_EDITORONLY_DATA`** 与 DNA **GeometryReader**，与 Epic 对 Creator 管线限制一致。

---

## 5. 已知问题与排查

| 现象                                           | 可能原因                                                                          | 建议                                                                                                              |
| ---------------------------------------------- | --------------------------------------------------------------------------------- | ----------------------------------------------------------------------------------------------------------------- |
| `failed to initialize MHC API`                 | Face MHC 非 IdentityTemplate、缺 `symmetry.json` 等；或合并 DNA 读失败            | 核对磁盘路径；搜 `LogMetaHumanCoreTechLib`                                                                        |
| `Init` 成功但位置不对                          | `FaceMeshComponent` 未指脸骨骼；或仅用了 Actor 变换                               | 指定正确 `USkeletalMeshComponent`                                                                                 |
| **`PatchBlendModel::Reduce` 访问冲突（崩溃）** | **脸 DNA** 与 **IdentityTemplate** 版本/拓扑不一致（如自定义 DNA + 官方模板硬配） | 先用 **原型脸 DNA**（与 `GetArchetypeDNAAseet(Face)` 同类）验证；再换角色 DNA 并保证与当前引擎 MetaHuman 版本匹配 |
| Game 目标无 Gizmo                              | `EVAL=0` 占位                                                                     | 预期行为；见 README                                                                                               |

详细日志说明见 [`README.md`](./README.md) **调试**一节。

---

## 6. 范围与非目标（仍适用）

| 包含                                                | 不包含（默认不做）                                |
| --------------------------------------------------- | ------------------------------------------------- |
| Runtime 插件、`MetaHumanCoreTechLib`（Editor 目标） | DNA **Blend** 实时混合、Sculpt                    |
| `Evaluate` / `EvaluateGizmos` + 小球可视化          | 复刻 `UInteractiveGizmoManager` / 完整 Creator UI |
| 可选 `Deserialize` 对齐 `UMetaHumanCharacter`       | 完整复刻侧栏与分区 Debug 材质（可后续迭代）       |

---

## 7. 工程目录（当前）

```text
MetahumanGizmoRuntime/
├── MetahumanGizmoRuntime.uplugin
├── MetaHuman_Runtime面部Gizmo开发计划.md   （本文件）
├── README.md
├── Config/
│   └── FilterPlugin.ini
└── Source/
    └── MetahumanGizmoRuntime/
        ├── MetahumanGizmoRuntime.Build.cs
        ├── Public/
        │   ├── MetahumanGizmoRuntime.h
        │   └── MetahumanFaceGizmoComponent.h
        └── Private/
            ├── MetahumanGizmoRuntime.cpp
            └── MetahumanFaceGizmoComponent.cpp
```

---

## 8. 依赖与 Build.cs（与实现一致）

### 8.1 必选模块

| 模块                          | 用途                                                                                           |
| ----------------------------- | ---------------------------------------------------------------------------------------------- |
| **MetaHumanCoreTechLib**      | `FMetaHumanCharacterIdentity`、`FState::Evaluate` / `EvaluateGizmos`（**仅 Editor 目标**链入） |
| **Core, CoreUObject, Engine** | 组件、静态网格、世界                                                                           |
| **MetaHumanCharacter**        | `UMetaHumanCharacter`（可选 Deserialize）                                                      |
| **MetaHumanSDKRuntime**       | 与 MetaHuman 资产管线一致                                                                      |
| **RigLogicModule**            | `UDNAAsset`                                                                                    |

### 8.2 Build.cs 要点

- **Editor 目标**：`PublicDependencyModuleNames` 含 `MetaHumanCoreTechLib`；`WITH_METAHUMAN_GIZMO_RUNTIME_EVAL=1`。
- **Game 目标（Epic 预编译）**：不链 `MetaHumanCoreTechLib`；`WITH_METAHUMAN_GIZMO_RUNTIME_EVAL=0`，占位实现；**UAT BuildPlugin** 的 UnrealGame 可通过。
- **不**在本模块引用 `MetaHumanCharacterEditor`、`UnrealEd`（除非未来单独 Editor 子模块）。

---

## 9. 分阶段计划（修订版：剩余工作）

### 阶段 C（坐标与产品预期）— 进行中

| 序号 | 任务                                              | 说明                                                                            |
| ---- | ------------------------------------------------- | ------------------------------------------------------------------------------- |
| C1   | 文档化「仅 DNA 状态 Gizmo」与「屏幕网格」可能偏差 | 动画 / Morph 未写入 `FState` 时，点不跟表情                                     |
| C2   | 可选 **标定偏移** 或 Socket                       | 若项目有自定义缩放/绑定，增加 `UPROPERTY` 偏移或矩阵                            |
| C3   | 与 **Control Rig / 动画** 同步策略                | 若需每帧一致，开 `bTickRefreshEveryFrame` 或动画通知里 `RefreshGizmoTransforms` |

### 阶段 D — 未开始

| 序号 | 任务                              | 说明                                                     |
| ---- | --------------------------------- | -------------------------------------------------------- |
| D1   | Cook 后路径：避免写死本机绝对路径 | `FPaths`、项目设置或 Data Asset 配置 IdentityTemplate 根 |
| D2   | Evaluate 节流 / 异步              | DNA 未变跳过；大数据量时防卡顿                           |
| D3   | 引擎升级回归                      | `EvaluateGizmos` / `NumGizmos` API 变更                  |

---

## 10. 核心 API 调用链（实现清单）

```text
FMetaHumanCharacterIdentity::Init(FaceMHCDataPath, BodyMHCDataPath, FaceDNAAsset, Orientation)
  → CreateMHCApi(脸 DNA Reader, Face MHC 目录, threads, 合并身体 DNA Reader 或 null)
  → TSharedPtr<FState> = Identity.CreateState()
  → [可选] State->Deserialize(GetFaceStateData())

按需 / 每帧：
  FMetaHumanRigEvaluatedState Ev = State->Evaluate()
  TArray<FVector3f> Gizmo = State->EvaluateGizmos(Ev.Vertices)

可视化：
  for i: WorldPos = FaceMeshComponent->GetComponentTransform().TransformPosition(FVector(Gizmo[i]))
       Sphere[i]->SetWorldLocation(WorldPos)
```

**禁止**在 Runtime 插件中依赖 `UMetaHumanCharacterEditorSubsystem::GetFaceGizmos`（Editor 子系统）。

---

## 11. 可选扩展

| 主题                  | 说明                                                                                     |
| --------------------- | ---------------------------------------------------------------------------------------- |
| 交互                  | 小球 `LineTrace` → 业务事件                                                              |
| Debug 分区色          | 需 Titan Patch 索引与材质，工作量大                                                      |
| 纯 Game 完整 Evaluate | 需源码编译并链 `MetaHumanCoreTechLib` 至 Game，且验证 `WITH_EDITORONLY_DATA` 与 DNA 读写 |

---

## 12. 风险登记

| 风险                                   | 缓解                                     |
| -------------------------------------- | ---------------------------------------- |
| Identity 顶点与屏幕网格不一致          | 阶段 C 标定；文档说明「静态 FState」含义 |
| DNA 与 IdentityTemplate 不匹配导致崩溃 | 使用原型 DNA 或版本匹配的导出 DNA        |
| 包体与许可                             | 遵守 MetaHuman / 引擎条款                |

---

## 13. 里程碑（Mermaid）

```mermaid
flowchart LR
    A[阶段A 可行性] --> B[阶段B 组件可视化]
    B --> C[阶段C 坐标对齐]
    C --> D[阶段D 打包与性能]
```

当前：**A、B 已完成**；**C 部分完成**；**D 未开始**。

---

## 14. 相关文档（MetaHuman 插件根目录）

- `MetaHuman_Creator_Head_Transform与视口Gizmo实现.md` — Creator 里 Editor Gizmo 与 Slate
- `MetaHuman_DNA_Blending.md` — Titan Blend（本插件不实现 Blend，Deserialize 时可参考）
- `MetaHuman_Creator_Blend案例中Slate与后端CPP关系.md` — 模块边界

---

_文档版本随 `MetahumanGizmoRuntime` 实现迭代；基准：UE 5.7、`Engine/Plugins/MetaHuman/`。_
