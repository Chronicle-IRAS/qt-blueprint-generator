# P1.2A Blueprint v2 Read-only Document Reader Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** 为已有 Plugin Registry 增加独立、只读的 Blueprint v2 JSON 文本读取 API，保留原文和未知节点数据，不开放生成能力。

**Architecture:** JSON.parse 负责文本解析，现有 P0.1 Schema 负责结构门禁，现有 Registry.validate 复用 P0.2/P0.3 完整语义与声明校验。读取器只组织不可变结果与节点展示投影，不增加第二套 Blueprint、锁、端口或依赖验证规则。

**Tech Stack:** 现有 TypeScript / Node.js / Ajv / Vitest；不增加依赖。

**Spec:** 本轮用户的 P1.2A 要求；既有 [Blueprint v2 Draft](../architecture/blueprint-schema-v2.md)、[平台边界](../architecture/plugin-platform.md)、[Plugin API Draft](../architecture/plugin-api-v1.md)、[Descriptor Contracts](../architecture/plugin-descriptor-contracts.md) 与 [Registry API](../../tools/schema-contracts/README.md#minimal-plugin-registry)。下列范围刻意小于未来 opaque payload 保存设计。

## Global Constraints

- 只实现文本读取、只读展示与诊断；不提供保存 API，不承诺 opaque payload 原始字节往返。
- 原始输入文本逐字符保留；输入是调用者已按 UTF-8 解码的 string，不新增文件 I/O 或字节解码 API。
- 未支持的 schemaVersion、畸形 JSON、结构非法数据拒绝，不修复、不迁移、不降级。
- 结构合法时保留完整解析数据，包括缺失插件/定义的节点、properties、ports 与未知扩展；语义错误不通过删数据消除。
- 始终 `canGenerate: false`；语义无诊断、KnownNode 或解析成功均不是生成授权。
- 不修改现有 Qt/C++ Schema 1、Legacy v1 / Runtime v2、Candidate、机器 Schema、Registry/P0 公共 API 或依赖版本。
- 不实现 GUI、QProcess、RPC、Cordis、保存、动态端口、插件执行、Core IR 或目标生成；不调用付费 AI。
- 按 AGENTS.md 采用一名实现者和随后的一名独立审查者；由主 Agent 集成、亲自验证并统一 Git 操作，不启动复杂编排。

## Review Focus

1. 中文、转义、空白/换行及嵌套未知值不得被重新序列化；测试 `preserves original text and nested extension values`。
2. 未知键 `__proto__`、`constructor`、`canGenerate` 不得污染对象或覆盖结果的禁止生成标志；测试 `keeps extension keys as inert data`。
3. Registry 后续注销不能使已返回的节点展示悄悄改变；测试 `returns a detached read-time result`。
4. 重复 ID、悬空引用等语义错误仍需保留结构合法文档，不选取“正确节点”或删边；测试 `retains semantically invalid records with diagnostics`。
5. null/数组根、缺字段、错误版本类型、空文本、BOM/注释/尾逗号要明确拒绝，不能因宽松解析绕过门禁；测试 `rejects unsupported or invalid input without repair`。

## Existing Evidence

- 基线：远端 main `4600a05cb8565825d506ceaf50a4624a9aed455d`，包含 P1.1 与 PR #57。
- 父功能分支：`feature/p1.2a-blueprint-document-reader`，直接从最新远端 main 创建；全部步骤在此分支集成。独立 worktree：`C:/Users/Lenovo/.config/superpowers/worktrees/project/blueprint-v2-document-reader`。
- 基线亲跑：Vitest 4 个文件、265/265；TypeScript typecheck 通过。仅执行既有 Node 工具链，未构建生成工程或运行 CTest。
- `createContracts().validateBlueprint` 已提供严格、非修改式结构门禁；`PluginRegistry.validate()` 已提供完整 descriptor-aware 校验；`snapshot()`、`getNode(typeId, version)` 已提供冻结描述与精确查询。缺口是文本读取及文档展示结果，而不是验证规则。

## Task 1: Read-only document result, regression coverage and delivery

**Files:**
- Create: `tools/schema-contracts/blueprint-document-reader.ts`
- Create: `tools/schema-contracts/blueprint-document-reader.test.ts`
- Modify: `tools/schema-contracts/README.md`（API、诊断、最小离线示例及边界）
- Modify: `doc/project-roadmap.md`、`doc/implementation-progress.md`（新增已验证范围，不改写历史证据）
- Modify: `doc/architecture/blueprint-schema-v2.md`（仅补充独立只读实现与未来保存设计的区别，不修改协议）
- Update: 本计划的核对项与最终测试证据。

**Interfaces:**
- Consumes: `createContracts()`、`PluginRegistry` 的现有 `validate` / `snapshot` / 精确节点描述，以及现有 `Diagnostic` 类型。
- Produces: `readBlueprintDocument(sourceText: string, registry: PluginRegistry): BlueprintDocumentReadResult`，同步、无文件写入。
- 共同字段：`sourceText`、稳定有序的 `diagnostics`、字面量 `canGenerate: false`。
- `status: "rejected"`：没有可用 document 或节点视图。畸形 JSON 使用 `JSON_PARSE_ERROR`；数值版本不为 2 使用 `SCHEMA_VERSION_UNSUPPORTED`；缺失/类型错误的版本及其他结构错误沿用 `STRUCTURE_INVALID`。
- `status: "read"`：`document` 为保留全部键值的递归只读 JSON 对象，`semanticValid` 反映 Registry 诊断是否为空，`nodes` 按文档顺序提供只读展示视图。
- 节点视图保留 `sourceIndex`（原 nodes 数组下标）、`nodeId/unitId/typeId/nodeDefinitionVersion` 与完整只读 `data`，不能用展示数据替换原文档。即使 nodeId 重复，也逐项展示，不按 ID 去重或覆盖；`sourceIndex` 精确关联 `/nodes/{index}` 诊断。
- `KnownNode`：`kind: "known"`，携带精确定义的展示 title；`UnknownNode`：`kind: "unknown"`，携带 `reasonCodes`，不猜测版本、重建端口或使用近似定义。
- KnownNode 必须同时满足：`getNode(typeId, nodeDefinitionVersion)` 精确匹配；对应 provider 已登记；文档对该 provider 的锁唯一、存在并与已登记可信锁的五字段完全一致。精确定义只是必要条件，缺插件、缺定义、锁缺失/歧义/不匹配均只能作为 UnknownNode。
- 上述锁信任判定复用现有语义诊断（`PLUGIN_LOCK_MISSING`、`PLUGIN_UNTRUSTED`、`PLUGIN_LOCK_MISMATCH`、`DUPLICATE_PLUGIN_LOCK`）和精确描述查询，不实现第二份五字段比较或依赖/端口校验。
- 节点定义识别与整图语义有效性独立：普通属性、静态端口、连接或 Unit 等语义错误不会单独把已可信识别的 KnownNode 改为 UnknownNode；保留定义及原诊断，`semanticValid: false`。
- 文档、节点视图、嵌套数据和诊断在发布前递归冻结；旧结果不持有 Registry 的可变状态。保留解析后值不等于原始数字字面量、重复键或 opaque 片段的字节往返；原文单独保留。
- 严格 JSON.parse，不去 BOM、不接受注释/尾逗号、不改写 Unicode；语法诊断不回显整个输入或依赖引擎的错误文案。

- [ ] **Step 1 — RED: format gate.** 先写 `reads a valid v2 document`、`reads an empty blueprint without plugins`、`rejects unsupported or invalid input without repair`。使用现有 Descriptor/Blueprint fixtures 与真实 Registry，空图使用完整六个空数组；不改旧 fixture。
- [ ] **Step 2 — Observe RED.** 运行 `npm test --prefix tools/schema-contracts -- blueprint-document-reader.test.ts`，记录新增行为缺失的失败。若需要接口占位以收集测试，先写测试，确认最终观察到行为断言失败，不把导入拼写错误当 RED 证据。
- [ ] **Step 3 — GREEN: format result.** 实现最小文本解析与结构门禁、拒绝结果及原文保留；复用机器 Schema，不复制 required/type 枚举。运行专项测试。
- [ ] **Step 4 — RED: plugin-aware views.** 增加 `retains nodes when their plugin is missing`、`does not substitute a different definition version`、锁的 version/package/sha256/pluginApiVersion 参数化不匹配（同名精确定义仍存在时也必须 UnknownNode）、缺失和重复锁、`forwards descriptor property and static-port diagnostics while retaining KnownNode` 及合法多 Unit 读取测试。确认预期错误码和保留记录，先观察失败。
- [ ] **Step 5 — GREEN: semantic result.** 使用现有 Registry 验证与精确描述生成 KnownNode/UnknownNode；完整保留文档与原诊断顺序；无条件关闭生成。运行专项测试。
- [ ] **Step 6 — RED/GREEN: preservation boundary.** 实现并验证 Review Focus 的五组测试，补充重复读取结果一致、结果嵌套修改失败、Registry 状态不变、注销后旧结果不变、重新读取反映新 Registry 状态。重复 nodeId 的两项须各有独立 `sourceIndex`、data，保持顺序且能精确关联路径；不允许 Map 按 nodeId 覆盖视图。断言不得由被测实现计算预期值，不使用 Fake Registry 替代真实路径。
- [ ] **Step 7 — Document actual behavior.** 更新 API 示例、结构/语义状态区别、UnknownNode 含义、原文与解析值的保留边界、无保存和无生成；路线图区分独立 TS 读取与尚未实现的 Qt GUI/持久化。进度仅记录亲跑结果。
- [ ] **Step 8 — Verify.** 运行全部 `npm test --prefix tools/schema-contracts`、`npm run typecheck --prefix tools/schema-contracts`、`git diff --check`。检查无依赖/锁文件、机器 Schema、既有 fixtures 或 Qt 路径变更；报告实际总数，不预先承诺数量。
- [ ] **Step 9 — Independent review.** 单独只读审查者检查数据保留、版本拒绝、Registry 复用、展示投影与诊断、信任边界和兼容性。按需先回归再修复；主 Agent 复核并重跑最终检查。
- [ ] **Step 10 — Deliver.** fetch 核对最新 main；若已更新则正常同步并复验。提交本功能及文档，常规 push 本分支，创建 base 为 main 的独立 PR 并附实际测试与限制。禁止自动 merge、force push、覆盖 Tag 或进入下一阶段。

## Approval and current state

用户已批准直接执行步骤 1–10，并补充 KnownNode 的精确可信锁门禁和逐项 sourceIndex 要求；两项已纳入 Task 1。当前开始执行，最终测试、独立审查和 PR 结果完成后更新核对项，不提前记录通过。
