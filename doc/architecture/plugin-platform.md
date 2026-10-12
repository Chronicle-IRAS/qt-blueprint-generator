# 通用插件蓝图平台架构（Draft）

状态：Draft，2026-10-12。本文描述长期 Architecture B，不是已实现功能清单。
蓝图 Schema v2、Plugin API v1、插件 manifest、RPC 和多 Unit 产物元数据均未冻结；当前程序不能加载这些提案。
字段约定见[蓝图 Schema v2](blueprint-schema-v2.md)，插件边界见[Plugin API v1](plugin-api-v1.md)。

## 目标与当前基线

平台长期允许一张蓝图混合多个插件，由插件定义节点、propertiesSchema、稳定强类型端口、连线校验、目标 IR 和代码生成。
宿主提供通用图编辑、文档与插件版本管理、诊断、候选审核及工程操作授权，避免把某个目标的运行语义写进通用画布。
Build Unit 是独立工程与构建的边界，其 ID 与 plugin ID 分开；同一个插件可以服务多个 Unit。

Qt Desktop Editor 是毕业设计要求的 Qt GUI，承担画布、属性、文档生命周期和本地工程交互。
Qt Target Plugin 是目标生成能力，负责 `org.blueprint.qt/widgets` 工程；它不是桌面编辑器本身。
未来 Embedded Target Plugin 通过受支持的芯片、SDK、工具链适配器生成目标工程。
毕业验收要求首个受支持目标真实交叉编译成功，不要求烧录或硬件运行。

当前实现仍是 C++17 / Qt 6 后端，只有固定六类节点和蓝图 Schema 1。
`blueprint.json` 与 `layout.json` 分开保存，当前 IR 是语义数据，不携带画布坐标。
PR #50 已合入 `main`（`843138c`）；Runtime v2 的一个 LogicModule + 一个末端 UiPage 真实验收已完成。
原始 AI C++ 未修改即被接受并首次完整构建通过，原工程和 Export 工程实际传值、显示并正常退出。
范围及回归、中断事实见[最新实现进度](../implementation-progress.md)，不能外推为任意图、插件或嵌入式支持。

| 当前代码依据 | 已有职责 | Draft 演进边界 |
| --- | --- | --- |
| [BlueprintDocument](../../src/blueprint/blueprint_document.h) / [Serializer](../../src/blueprint/blueprint_serializer.cpp) | Schema 1、固定 NodeType | 新模型另建读取器，不改旧 type/target 含义 |
| [ProjectStore](../../src/blueprint/blueprint_project_store.h) / [IrCompiler](../../src/generation/ir_compiler.h) | 分离布局、确定性语义 IR | 新 Draft CoreGraphIR → UnitCoreIR → TargetIR，不改现行编译器 |
| [GenerationController](../../src/app/generation_controller.h) | 快照、取消、旧版/运行时动作 | 扩展 Unit、插件和驱动上下文，而非冒称已有 |
| [GenerationService](../../src/generation/generation_service.h) / [CandidateStore](../../src/generation/candidate_store.cpp) | 校验、写候选、预览与 Accept | 由可信服务保留写入和接受授权 |
| [Scaffolder](../../src/generation/project_scaffolder.h) / [RuntimeAssembler](../../src/generation/runtime_assembler.h) | 旧版产物、线性 Runtime v2 | 只适配可表示的 Qt 语义 |
| [Importer](../../src/workspace/external_code_importer.h) / [BuildService](../../src/workspace/build_service.h) | 黑盒文件导入、用户触发构建 | 驱动与嵌入式工具链需要新增契约 |

## 职责与数据流

```text
Qt Desktop Editor（毕业要求的 GUI）
  画布 / propertiesSchema 表单 / 文档与布局 / 诊断 / Candidate Review
                       |
可信平台服务（初期保留已验证 C++/Qt 后端）
  v1 路径   |   未来 v2 读取器 / 语义校验 / 插件锁 / Unit 计划
  文件策略 / Candidate 写入与 Accept / scaffold / Build / Export 授权
                       |
Plugin Host 抽象（具体语言和进程模型尚未选定）
  Registry / NodeDefinition / 端口与连线校验 / 生命周期 / 目标能力
       |                    |                       |
Qt Target              Embedded Target          其他可信插件
org.blueprint.qt       芯片 / SDK / 工具链         可编译节点定义
/widgets              适配器（未来）             与类型定义
       |                    |
Qt Unit 独立工程         Embedded Unit 独立工程
       +----- 显式 typed Channel / Bridge（仅设计） -----+
```

图中的 Registry、v2 读取器、插件宿主和 Embedded 路径均未实现。
插件输出诊断、IR 或候选计划，可信服务复验后执行允许的文件事务。
平台层校验身份、引用、Unit 边界和类型；插件只补充其节点与目标的语义约束。
目标插件不能放宽宿主的路径、受保护文件、版本或人工确认规则。

Draft 编译分层为 `v2 文档 → 可信平台规范化 CoreGraphIR → 按 unitId 投影 UnitCoreIR → TargetAdapter.lower → 版本化 TargetIR`。
CoreGraphIR 是目标无关的全局语义：稳定节点/端口、显式边、Unit、Channel、插件锁与驱动引用；不含布局、猜测的目标执行顺序或 AI 实现类名。
`coreIrVersion: 1.0-draft` 与 `coreIrSha256` 标记本提案的规范化语义版本/摘要，独立于蓝图 Schema、目标 targetIrVersion 和现行 IrCompiler 输出。
平台生成 UnitCoreIR，只含该 Unit 的节点/内部边、相关锁/驱动引用与相邻 Channel 边界；远端只有端点契约，不投影其他 Unit 的节点实现。
`unitIrSha256` 绑定该规范投影，TargetAdapter 只将它 lower 为自己支持的 TargetIR；不支持的语义明确拒绝，插件不能重写全局图或 Unit 边界。
后续契约测试须验证同语义不同布局的 CoreGraphIR/摘要一致、Unit 投影隔离，以及目标 lower 的确定性与拒绝行为；当前没有这些新 IR 实现。

## Node、Port、Build Unit 与 Channel

`typeId` 用 `pluginId/localType` 标识节点，如 `org.blueprint.qt/logic-module`。
`targetId` 同样具备命名空间，如 `org.blueprint.qt/widgets`，不能从 UI 语言或节点外观推断。
节点的 `unitId` 引用 `units[].id`；Unit 指定目标与配置，不能用 plugin ID 充当 Unit 身份。
同一 Unit 可包含兼容该目标的多个插件节点；编译能力是否兼容必须显式验证。

Port 有稳定 `id`、显示名、方向、`kind` 与显式 `typeId`。
`kind` 分为 control/data/event，改名或移动不重建端口 ID，也不靠数组位置连线。
类型相同仍需检查方向、种类、基数和目标约束；不存在隐式类型、转换或猜测连线。
普通 `edges` 只允许 Unit 内连接，两个 Qt Unit 之间也默认拒绝普通边。

跨 Unit/Target 必须声明 `channels`：两端 `unitId/nodeId/portId`、wireSchema、Bridge 插件版本、驱动绑定与配置。
Channel 描述可审查的边界契约，不等同于网络、串口或物理链路已存在。
wireSchema 不把 Qt `QString` 或 C++ 地址当成可传输格式；两端适配与编码需显式声明并验证。
Bridge 是将两端目标表示映射到 wireSchema 的能力，不能成为绕过 Unit 校验的普通边。
串口、共享内存或其他通信方式需在后续独立任务中实现与验收，本次不实现通信。

## 宿主候选与验证顺序

| 方案 | 收益 | 代价及进入条件 |
| --- | --- | --- |
| C++/Qt 进程内 | 复用当前编译、候选和 UI 后端；少一个运行时 | 包/ABI 兼容、生命周期及崩溃影响需验证；不是沙箱 |
| TypeScript/Cordis Sidecar | 可评估依赖、扩展与生命周期生态 | RPC、部署、取消与异常处理增加成本；先做最小离线证明 |
| 按能力混合进程 | 可为特定 SDK 或工具选择语言 | 维护多种运行时和重复契约；只有具体收益成立才采用 |

没有选定 TypeScript、Cordis、Python 或固定“两进程”架构。
Cordis 只是可选候选，其[官方仓库](https://github.com/cordiverse/cordis)提示 API 仍在开发，不据此承诺稳定宿主。
先用同一组离线 fixture 比较进程内与 Sidecar 的语义结果、诊断、取消及失败行为。
Sidecar 握手失败或崩溃不能丢失未保存文档，也不能触发自动迁移、Accept 或执行。
只有目标收益和兼容验证成立，才讨论替换具体接口；现有 C++/Qt 后端继续维护。
历史 Qt/Python 提案及当前优先级见[多语言设计](../multilanguage-development-design.md)。

## 信任、权限与生成上下文

插件 capability manifest 描述请求的能力，不是 OS 沙箱或可执行权限证明。
初期只加载可信插件；不可信插件在隔离验证完成前一律拒绝加载与执行。
进程内可信插件与宿主同权；仅放到 Sidecar 进程也不能阻止它直接访问文件或网络。
后续若接受不可信插件，必须验证实际 OS 隔离、资源限制、进程终止与越权测试，而非依赖声明。

可信服务拥有 Candidate 落盘、Accept、scaffold、Build 和 Export 的最终授权。
插件只能提交限于节点/Unit 所有权的候选计划，不能提交任意写盘指令或直接改受保护文件。
受保护入口、控制器、CMake 和公共契约由确定性生成路径管理，AI 实现进入人工审核。
模型文本、插件返回和驱动配置都属于需要校验的数据，不能改变写入策略。
Build 执行会运行工具链与项目脚本，不是生成代码的沙箱；需要用户明确触发和工具链约束。

未来生成上下文至少绑定 `projectId`、文档语义摘要/修订、`unitId`、请求 ID、插件锁摘要、驱动哈希和受保护文件哈希。
项目切换、语义变化、Unit 配置变化、插件替换、驱动变化或取消，使旧上下文失效。
服务在开始生成、接收结果、落盘和 Accept 前逐次复验；迟到结果不进入可接受状态。
Accept 还需核对当前文件/基线哈希与冲突确认，不能用请求完成时的校验替代。
布局或选中项变化本身不改变语义；当前控制器已有文档/工作区失效机制，新增维度仍是 Draft。
RPC 不传 API key、完整环境或任意绝对输出路径；凭据留在已授权的可信服务内。

## 用户驱动与 Embedded 适配

目标适配器明确支持的芯片、SDK 版本、工具链、启动文件、链接布局和工程产物。
不承诺任意 MCU 或用户任意 Makefile 自动适配；不匹配时给出诊断，停止生成/构建。
用户驱动始终是黑盒：AI 只接收接口契约、允许调用方式和必要类型摘要，不接收驱动源码。
驱动源码在本地由构建适配器纳入工程，不能由 AI Candidate 覆盖。

| 模式 | 计划与现有机制关系 | 变化处理 |
| --- | --- | --- |
| Snapshot | 未来驱动绑定拟复用现有 Importer 的显式选择、复制与哈希复验 | 只在明确重新导入时替换已追踪路径集合 |
| Reference | 未来新增受控引用、可移植导出与哈希契约 | 原路径或字节变化使上下文失效，不能静默采用 |

当前 Importer 服务于 Schema 1 的 ExternalCode，已有 import manifest 保持不变。
它尚不是插件驱动系统，Runtime Phase 1 也不执行 ExternalCode；Reference 模式未实现。
首个 Embedded 验收应记录具体芯片/SDK/工具链、完整交叉编译日志和目标产物；不是只有空骨架或宿主 smoke。
烧录与硬件运行可后续规划，不是本毕业范围的完成条件。

## 版本、工作区与兼容迁移

插件锁单独 pin `pluginId/package/version/sha256/pluginApiVersion`，包哈希不能替代兼容判断。
NodeDefinition 版本、Plugin API 版本、蓝图 Schema 版本和 workspace manifest 版本分别管理，不能用“v2”互相推导。
当前 generation manifest v1/v2 严格六字段：`version`、`blueprintSha256`、`modules`、`protectedFiles`、`files`、`batches`。
依据是 [WorkspaceIo](../../src/generation/workspace_io.cpp)；Candidate JSON、ExternalCode import manifest 均不变。
多 Unit 产物索引计划放入独立 `unit-artifacts.draft.json`，采用单独 `workspaceManifestVersion`，不能塞进旧清单。

Legacy v1 的 `create()` / `start()` 与原字节产物保留；Runtime v2 的 `createRuntime()` / `startRuntime()` 与固定 factory 保留。
不会自动转换蓝图或工作区，不会因新路径失败回退到旧路径。
未来 v1 → v2 只做显式复制转换，保留原图、布局、未知字段和 opaque 插件 payload，并展示损失或不兼容诊断。
v1 没有端口级数据边，转换器不能凭画布、节点顺序猜造 data edge；保持原 Flow/label 与既有前驱绑定语义，不能表达时留在 legacy 或要求明确建模选择。
v2 → LegacyQt 只接受现有线性流程和直接前驱同名同声明类型等可表示关系，其他一律拒绝；不猜边、不做类型转换。
未来 v2 读取器计划保留缺失插件的未知字段与 opaque payload 原始字节并显示占位，读取/保存可继续，生成失败关闭。
无损保留要求针对未知字段与未改动 opaque payload，不要求重新保存后的整个 JSON 文件字节一致；复制转换保留的原工程不改写。
当前 v1 读取器没有上述保留能力，它拒绝 Schema 2/未知版本及未知 NodeType，不能静默降级保存。

## 分阶段收敛与完成依据

1. Draft 契约：离线确认身份、字段、端口、锁、未知数据和迁移诊断；不冻结未经验证的协议。
2. 最小 Registry：可信节点可编辑、依赖可解析、缺失插件可占位且不能生成，不预建大型框架。
3. Qt Target：通过严格适配复用当前 Qt 后端，保留 legacy 与 Runtime v2 回归证据。
4. Embedded Target：首个受支持适配器真实交叉编译完整工程；Qt GUI 仍是产品主体。
5. 多 Unit：独立生成/构建/导出；Channel 先验证契约，通信另立实现与验收范围。
6. AI Review/返工：评论形成新 Candidate，人工 Diff/Accept 后 Build，复用文件保护和冲突检测。

毕业优先范围是 Qt GUI、最小插件边界、Qt 适配与首个 Embedded 真实交叉编译。
多 Unit 通信与完整 Review/返工是后续增量，不能因架构图出现它们就声称已交付。
本设计不引入 React/Electron/Monaco、完整 IDE、新网络服务器或通用 Decision 运行时。
实施和验收必须分别记录，后续契约测试条件见另外两份 Draft；当前进度见[路线](../project-roadmap.md)。
