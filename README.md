# MetahumanGizmoRuntime

**作者：** XujiaqiKumo  

在场景中附加 **`UMetahumanFaceGizmoComponent`**，用与 MetaHuman Creator 相同的逻辑（`Evaluate` → `EvaluateGizmos`）在脸部附近显示 Gizmo 小球，**不依赖** `MetaHumanCharacterEditor` / Slate 做核心计算。

## 依赖插件（uplugin 已声明）

- `MetaHumanCoreTech`、`MetaHumanSDK`、`MetaHumanCharacter`、`RigLogic`

## 使用要点

1. 在工程中启用本插件。
2. 在角色 Actor 上添加 **`UMetahumanFaceGizmoComponent`**。
3. 指定 **`FaceMeshComponent`**（脸部 `USkeletalMeshComponent`）；**`FaceDNAAsset`** 可选（未设则从脸 SKM 的 Asset User Data 解析 `UDNAAsset`，与编辑器 `GetDNAReader` 同源）。**`FaceMHCDataPath`** / **`BodyMHCDataPath`** 可与 Creator 一致的手填路径；若 **`bUsePluginDefaultMHCPaths`** 为 true，空字符串会自动填为 `MetaHumanCharacter` 插件 Content 下的 `Face/IdentityTemplate` 与 `Body/IdentityTemplate`。
4. 可选：设置 **`SourceMetaHumanCharacter`**，从 `GetFaceStateData()` 反序列化面部状态以对齐已保存角色。
5. 调用 **`InitializeIdentity`**（或在 **`bAutoInitializeOnBeginPlay`** 为 true 时自动调用），再按需 **`RefreshGizmoTransforms`**；若 **`bTickRefreshEveryFrame`** 为 true，则每帧刷新。更换 **`SourceMetaHumanCharacter`** 或面部 DNA 后须调用 **`ReinitializeIdentity`**（不可仅 Refresh）。
6. **Move 拖拽限制（蓝图 / Details）**：**`Gizmo Bounds Mode`** — **Rig Native Enforce (A)** 使用 **`bEnforceGizmoBounds`** 传给 `FState::SetGizmoPosition`（与旧行为一致）；**Editor Soft Box (B)** 与 MetaHuman Character Editor Face Move 相同：`GetGizmoPositionBounds` + 软边界曲线，可调 **`BoundsBBoxReduction` / `BoundsExpandToCurrent` / `BoundsSoftAmount`**。
7. **Editor/PIE 实时脸网格（`bApplyLiveFaceMeshUpdates`）**：若已对 **`TryAddObjectToEdit`** 使用的角色键注册成功，拖拽时每帧通过 **`UMetaHumanCharacterEditorSubsystem::SetFaceGizmoPosition`** 更新编辑用脸网格（与官方 Face Move 一致，默认仅 **LOD0**）；**松手后**再调用一次 **`ApplyFaceState`** 以刷新全部 LOD 与 mesh description。未注册到子系统时仅更新组件内 `FState`，不驱动子系统网格。
8. **PIE 与 MetaHuman Character 编辑器隔离（`bIsolatePIEFromMetaHumanEditorSubsystem`，默认 true）**：在 **Play-In-Editor** 中，本组件会对 **`SourceMetaHumanCharacter`** 做 **`DuplicateObject`** 得到瞬态副本，并对副本调用 **`TryAddObjectToEdit`** / **`ApplyFaceState`** 等，使 `UMetaHumanCharacterEditorSubsystem::CharacterDataMap` 使用**与资产不同的 UObject 键**。这样在 PIE 里拖拽 Gizmo 不会改写「同一 `.uasset` 已在 MHC 里打开」时的编辑会话脸网格；**非 PIE** 的编辑器关卡仍直接对 **`SourceMetaHumanCharacter`** 注册。若 **`DuplicateObject` 失败**（日志 Error），该会话将回退为不注册子系统（与第 7 条「未注册」行为一致）。若需恢复旧行为（PIE 与 MHC 共用同一指针的编辑态），可将 **`bIsolatePIEFromMetaHumanEditorSubsystem`** 设为 **false**。
9. **PIE 与 `TryAddObjectToEdit`（防崩溃）**：在 **PIE** 中**禁止冷启动** `TryAddObjectToEdit`（子系统尚无该 UObject 键的 `CharacterData` 时）。否则 MetaHuman 会走 DNA→Interchange 建脸网格，而 **Interchange 骨骼网格导入在 runtime 不可用**，可能导致 **`TNotNull` 致命错误**。**预热**：在编辑器中双击打开 **`UMetaHumanCharacter`**，进入 **MetaHuman Character 编辑器** 并保持**资产标签页不要关**，再按 PIE；此时 **`IsObjectAddedForEditing(SourceMetaHumanCharacter)`** 已为真，BeginPlay 会提前返回且不会再次冷注册。**若关闭 MHC 标签页再 PIE**，子系统可能已 `RemoveObjectToEdit`，仍会触发本条的跳过逻辑（仅组件内 `FState` / Gizmo，无子系统实时脸网格）。**需要 PIE 下与 MHC 共用已预热编辑态、并要实时脸网格时**，请将 **`bIsolatePIEFromMetaHumanEditorSubsystem` 设为 false**，否则隔离用的副本键在 PIE 内无法被预热，子系统路径仍会跳过。

## Editor 与 Game 编译差异（重要）

Epic **预编译引擎**下，`MetaHumanCoreTechLib` 通常**没有** UnrealGame 的预编译清单。本模块在 **Build.cs** 中仅在 **`Target.Type == Editor`** 时链接 `MetaHumanCoreTechLib` 并定义 **`WITH_METAHUMAN_GIZMO_RUNTIME_EVAL=1`**：

- **Editor / PIE：** 完整实现，`InitializeIdentity` / `RefreshGizmoTransforms` 可用。
- **Game / Shipping（预编译引擎）：** 占位实现，`InitializeIdentity` 会记录警告并返回 `false`；若需在正式游戏中使用完整 Evaluate，需 **从源码编译引擎** 并自行将 `MetaHumanCoreTechLib` 纳入 Game 目标（非标准流程，需自行验证）。

## 路线图与后续 TODO（Runtime / Shipping 全链路捏脸）

以下为本插件在 **「Gizmo + 拖拽驱动脸部网格」** 方向上的**长期计划摘要**；与上文「Editor/PIE + `UMetaHumanCharacterEditorSubsystem`」的**当前实现**区分。

### 环境约束（Epic Launcher 预编译引擎、无自研引擎源码树时）

| 约束 | 说明 |
|------|------|
| **UnrealGame 与 CoreTech** | 预编译引擎下 **Game 目标通常无法链接 `MetaHumanCoreTechLib`**，本模块 Build 仅在 Editor 开启 `WITH_METAHUMAN_GIZMO_RUNTIME_EVAL`。 |
| **脸部网格写入** | 与 MHC 同级的 `SetFaceGizmoPosition` / `ApplyFaceState` 依赖 **`MetaHumanCharacterEditor`**；**Evaluate → 写 `USkeletalMesh`** 的核心工具在 Editor 模块（如 `FMetaHumanCharacterSkelMeshUtils`），**Shipping 默认不链**。 |

**结论：** 在 **仅 Launcher 引擎、不重编引擎** 的前提下，**不宜承诺**「打包游戏内与 MHC 完全等价的 Gizmo 实时捏脸改网格」；**当前可稳定交付**仍为 **Editor / PIE** 下的子系统路径（含预热、`bIsolatePIEFromMetaHumanEditorSubsystem`、PIE 冷 `TryAdd` 防护等）。

### 策略分叉

- **路径 A（推荐作当前目标）**：仅 Launcher — 不推进 Game 全链路代码；产品上将 **捏脸驱动脸网格** 视为 **编辑器能力**；打包游戏若需变形，走 Epic 文档中的 **运行时 MetaHuman 能力**（动画、RigLogic、Live Link 等），**不等价**于 MHC 级 Gizmo sculpt。可选：对工程做一次 **Game 目标编译** 尝试，**记录 UBT 报错** 作为预编译限制的内部依据。
- **路径 B**：取得 **源码引擎** 或可自定义引擎构建后 — 再评估 **Game 链接 `MetaHumanCoreTechLib`**、自建 **`FDNAToSkelMeshMap` + Evaluate → 顶点写入 `USkeletalMesh`**、与 `#if WITH_EDITOR` 子系统路径隔离、性能（节流 / 异步 `RebuildRenderData`）等。
- **路径 C**：若未来 Epic 在 **MetaHumanSDKRuntime** 等提供 **官方 Runtime 捏脸/写网格 API**，再对接；此前仅调研。

### 路径 B 启用后的技术草案（未实施，供实现时对照）

1. Build：Game 条件启用 CoreTech 与 `WITH_METAHUMAN_GIZMO_RUNTIME_EVAL`；可增加 `bUseRuntimeFaceMeshUpdates` 等与 Editor 子系统互斥的开关。  
2. **CharacterData-lite**：组件或辅助对象维护 `FDNAToSkelMeshMap`、运行时脸 SKM 副本策略。  
3. 拖拽/松手：`FaceState->Evaluate()` → 写网格（先 LOD0，再扩展全 LOD）。  
4. 宏：`WITH_EDITOR` 保留现有子系统、PIE 防护、隔离桥接。  
5. Shipping：Cook、包体、帧时间与文档。

### 后续 TODO（团队跟踪）

| 状态 | 条目 |
|------|------|
| 待办 | 确认团队是否在未来引入 **源码引擎** 或自定义引擎构建；否则将「Game 全链路捏脸」标为 **冻结 / 仅调研**。 |
| 待办 | （可选）在 **Launcher 引擎**上对工程执行 **Game 目标** 编译本插件，**记录完整 UBT 输出**，佐证预编译限制。 |
| 阻塞于路径 B | 查清 **`USkelMeshDNAUtils` / `FDNAToSkelMeshMap`** 等在 **Game** 下的合法模块依赖（仅 Launcher 时记为文档阻塞项）。 |
| 阻塞于路径 B | 实现 **CharacterData-lite** 与 **Runtime 顶点写入**；Launcher-only 阶段**不实施**，仅保留设计。 |
| 阻塞于路径 B | 用 Runtime 路径**替换**子系统 `SetFaceGizmoPosition`/`ApplyFaceState` 的等价行为；当前以 **维护 Editor 子系统路径 + PIE 防护** 为主。 |
| 阻塞于路径 B | **Shipping** 性能验证（节流、异步重建渲染数据等），依赖 Game 链路可行后再做。 |

详细设计讨论可与仓库内 [`docs/IMPLEMENTATION_PLAN_MetaHuman_Alignment.md`](./docs/IMPLEMENTATION_PLAN_MetaHuman_Alignment.md) 对照；引擎内 Epic 源码路径以本机 UE 安装为准。

## 文档

- 与官方子系统对齐的分阶段实现计划：[`docs/IMPLEMENTATION_PLAN_MetaHuman_Alignment.md`](./docs/IMPLEMENTATION_PLAN_MetaHuman_Alignment.md)
- 架构与阶段说明：[`MetaHuman_Runtime面部Gizmo开发计划.md`](./MetaHuman_Runtime面部Gizmo开发计划.md)

## 源码布局

```text
Source/MetahumanGizmoRuntime/
├── MetahumanGizmoRuntime.Build.cs
├── Public/
│   ├── MetahumanGizmoRuntime.h
│   └── MetahumanFaceGizmoComponent.h
└── Private/
    ├── MetahumanGizmoRuntime.cpp
    └── MetahumanFaceGizmoComponent.cpp
```

## 调试（Output Log）

- **日志分类**：`LogMetahumanGizmoRuntime`（定义于 `MetahumanGizmoRuntime.cpp`）。
- **在编辑器中**：菜单 **窗口 → 开发者工具 → 输出日志**，搜索 **`[MetahumanGizmo]`**（每条关键日志均带此前缀）。
- **成功链**（PIE / 播放时）应大致出现：`BeginPlay` → `InitializeIdentity: start` → `Paths`（含磁盘是否存在）→ `CreateState OK | NumGizmos=…` → `InitializeIdentity: SUCCESS` → `RefreshGizmoTransforms: Evaluate verts=…` → `First gizmo world pos=…` → `RefreshGizmoTransforms: DONE`。
- **若 Init 失败**：同窗口搜索 **`LogMetaHumanCoreTechLib`** 或 **`failed to initialize MHC API`**，并核对 **Face/Body MHC 路径**是否为**磁盘上真实存在的目录**（与引擎 `MetaHumanCharacter/Content/.../IdentityTemplate` 结构一致，勿只填自定义 DNA 目录）。
- **更细**：按 **~** 打开控制台，输入 `Log LogMetahumanGizmoRuntime Verbose` 后再 PIE，可看到 `FaceMeshComponent` 位置等 Verbose 行。

## 与 UE 工程同步（开发时必做）

在**引擎插件目录**改完代码后，将副本同步到工程插件（默认 `F:\UEProjects\MetaHumans57\Plugins\MetahumanGizmoRuntime`）再编译工程：

```powershell
powershell -ExecutionPolicy Bypass -File Scripts/Sync-To-MetaHumans57.ps1
```
（若已安装 PowerShell 7，可将 `powershell` 换成 `pwsh`。）

目标路径可通过环境变量 `METAHUMAN_GIZMO_SYNC_DEST` 覆盖。

## 验证构建

在引擎外目录执行（输出路径须在 Engine 之外）：

```bat
Engine\Build\BatchFiles\RunUAT.bat BuildPlugin -Plugin="...\MetahumanGizmoRuntime.uplugin" -Package="C:\Temp\MetahumanGizmoOut" -TargetPlatforms=Win64 -Rocket
```

应同时通过 **UnrealEditor** 与 **UnrealGame**（Development / Shipping）编译。
