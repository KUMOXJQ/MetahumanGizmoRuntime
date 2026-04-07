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
7. **Editor/PIE 实时脸网格（`bApplyLiveFaceMeshUpdates`）**：若已对 **`SourceMetaHumanCharacter`** 调用 **`TryAddObjectToEdit`**，拖拽时每帧通过 **`UMetaHumanCharacterEditorSubsystem::SetFaceGizmoPosition`** 更新编辑用脸网格（与官方 Face Move 一致，默认仅 **LOD0**）；**松手后**再调用一次 **`ApplyFaceState`** 以刷新全部 LOD 与 mesh description。未注册到子系统时仅更新组件内 `FState`，不驱动子系统网格。

## Editor 与 Game 编译差异（重要）

Epic **预编译引擎**下，`MetaHumanCoreTechLib` 通常**没有** UnrealGame 的预编译清单。本模块在 **Build.cs** 中仅在 **`Target.Type == Editor`** 时链接 `MetaHumanCoreTechLib` 并定义 **`WITH_METAHUMAN_GIZMO_RUNTIME_EVAL=1`**：

- **Editor / PIE：** 完整实现，`InitializeIdentity` / `RefreshGizmoTransforms` 可用。
- **Game / Shipping（预编译引擎）：** 占位实现，`InitializeIdentity` 会记录警告并返回 `false`；若需在正式游戏中使用完整 Evaluate，需 **从源码编译引擎** 并自行将 `MetaHumanCoreTechLib` 纳入 Game 目标（非标准流程，需自行验证）。

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
