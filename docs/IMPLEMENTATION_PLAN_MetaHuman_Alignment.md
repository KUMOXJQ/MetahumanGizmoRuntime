# MetahumanGizmoRuntime — 与官方 MetaHuman Character 管线对齐 — 实现计划

本文档对照 `UMetaHumanCharacterEditorSubsystem` / Creator 同源 Titan 行为，总结 **当前插件差距** 与 **分阶段实现计划**。作者维护用；修改实现后请酌情更新本文。

---

## 一、现状对照

| 维度 | 官方 | 当前 MetahumanGizmoRuntime |
|------|------|----------------------------|
| Identity / MHC API | `GetOrCreateCharacterIdentity`：按 `TemplateType` 缓存；`Init` 用原型脸 DNA + 插件下 `Face/IdentityTemplate`、`Body/IdentityTemplate` | 每实例 `FMetaHumanCharacterIdentity`；`Init` 用 `ResolveFaceDNAForInit()`；MHC 路径多依赖手填 |
| 角色面部状态 | `GetFaceStateData()` → `Deserialize` 进 `FaceState` | `SourceMetaHumanCharacter` → `Deserialize`，方向一致 |
| Gizmo 数学 | `Evaluate` + `EvaluateGizmos(Vertices)` | `RefreshGizmoTransforms` 同源 |
| 换人 / 换 DNA | `ApplyFaceDNA`、兼容性检查、重建会话数据 | 需显式整块 `Re-Init`，禁止仅 `Refresh` 冒充换拓扑 |

---

## 二、分阶段计划

### 阶段 A — 配置与官方一致（优先）

1. **`bUsePluginDefaultMHCPaths`**（可选）：为 true 且路径为空时，用 `IPluginManager::FindPlugin("MetaHumanCharacter")` + `GetContentDir()` 拼接 `Face/IdentityTemplate`、`Body/IdentityTemplate`（与 `GetFaceIdentityTemplateModelPath` / `GetBodyIdentityModelPath` 同源）。非空则保留用户覆盖。
2. **默认 DNA 轴向**：与官方一致推荐 **`DNAOrientationIndex = 0`（Y_UP）**；文档说明。
3. **README**：明确 **`Init` 所用 `UDNAAsset` 须与 Titan/MHC 拓扑兼容**；典型为 **目标脸 SKM 的 UserData DNA**。

### 阶段 B — Init DNA 策略（可选 / 中期）

- 可选 `EInitFaceDNAMode`：`FromTargetCharacter`（默认，当前逻辑）vs 贴近编辑器的原型 DNA 模式（依赖 `GetArchetypeDNAAseet` 或等价路径，可能需 Editor 模块）。
- 不引 Editor 时：以文档约束「原型式 Init + Deserialize」仅限拓扑兼容场景。

### 阶段 C — 换人 / 换 DNA（防崩溃）

1. **`ReinitializeIdentity()`（BlueprintCallable）**：释放 Impl、清球、`InitializeIdentity` 全流程。
2. 契约：**`SourceMetaHumanCharacter` 或脸 DNA 变更后必须 Reinit**，不可只 `RefreshGizmoTransforms`。
3. （可选）Editor 下 `CheckDNACompatibility` 再 `Init`，避免 Titan 内部 AV。

### 阶段 D — 性能与缓存（可选）

- 按 `TemplateType` 或「路径+DNA 指纹」模块级缓存 `FMetaHumanCharacterIdentity`。
- 暴露/同步 `CachedEvaluatedState` 失效策略（若对外改 State）。

### 阶段 E — Game / Shipping

- 维持现有 **仅 Editor 目标链 `MetaHumanCoreTechLib`**；Runtime 完整 Evaluate 需自链 CoreTech 或降级方案，单独立项。

---

## 三、立即落地的最小闭环（本次迭代目标）

1. `bUsePluginDefaultMHCPaths` + 自动填充空 MHC 路径。  
2. `ReinitializeIdentity()` 公开 API。  
3. README / 本文件交叉引用。

---

## 四、参考源码位置（Epic）

- `MetaHumanCharacterEditorSubsystem.cpp`：`GetOrCreateCharacterIdentity`（约 4182+）、`InitializeMetaHumanCharacter`（约 1247+）、`InitializeIdentityStateForFaceAndBody`（约 886+）、`GetFaceGizmos`（约 2421+）。
- `MetaHumanCommonDataUtils.cpp`：`GetFaceDNAFilesystemPath` / Archetype DNA 路径。
- `MetaHumanCreatorAPI.cpp`：`CreateMHCApi` 与 IdentityTemplate 文件列表。

---

*文档版本：与仓库提交同步维护。*
