# 项目方向与后续路线

长期目标是通用、插件优先的蓝图平台。插件提供节点定义、属性 Schema、稳定的强类型端口、连线校验、目标 IR 与代码生成；同一蓝图可以容纳多个插件，各节点明确归属独立 Build Unit。跨 Unit 或跨 Target 只能经过显式 Bridge/Channel，即便两端都是 Qt，也不能用普通边隐式连接。该方向是 Architecture B 的 Draft，尚未实现插件注册、多 Unit 或通信。

Qt Desktop Editor 承担毕业设计要求的 Qt 图形界面、画布和本地工程交互；Qt Target Plugin 承担 Qt 目标工程生成。未来 Embedded Target Plugin 采用受支持的芯片、SDK 和工具链适配器，允许用户驱动作为黑盒接口绑定。毕业范围要求真实交叉编译有效的目标工程，不要求烧录或硬件运行。工作区继续服务于生成、人工评审和返工，不扩展为完整 IDE。

当前能力已有蓝图编辑与工程持久化（语义与布局分开）、ExternalCode GUI 导入与验证、Qt 模块生成、候选审核、Workspace Editor 安全编辑、构建和导出。Runtime Assembly Phase 1 的 PR #50 已于 2026-10-11 合入 `main`，合并提交为 `843138c0a5f295eefaf1799a4a8e92656275f301`；真实 Runtime v2 有限样例验收于 2026-10-12 完成。行级评论与 AI 返工闭环仍未实现。

## Runtime Assembly Phase 1 当前范围

默认运行时生成使用 `startRuntime()` / `createRuntime()`，创建 generation manifest v2 工程。确定性生成器负责受保护的 `main.cpp`、WorkflowController、CMake 和公共契约，AI 负责当前节点 `implementation/` 与 `tests/`，必须定义契约声明的固定 `createModule_HASH` factory，返回保护契约类的 `std::unique_ptr`；装配器不猜测 AI 的具体类名。manifest 严格保留 `version`、`blueprintSha256`、`modules`、`protectedFiles`、`files`、`batches` 六字段，只接受版本 1 或 2。

AI 菜单和节点右键菜单保留明确的 legacy v1 模块生成动作，通过原 `create()` / `start()` 保留 module-only v1 原字节产物和占位入口；既有 v1 工程可继续生成候选及 Build / Export。两种模式不互迁，运行时失败不自动回退到旧版，新运行时使用新的空工作区。蓝图读取器本身仍为 Schema 1、固定六类 NodeType；generation manifest v2 不意味着蓝图 Schema v2 已实现。Candidate JSON、固定 factory 与 ExternalCode import manifest 本次均不变。

支持 `Start → 零个或多个 LogicModule → 可选的唯一末端 UiPage → End`，按 Flow 连线执行一次启动链。第一个模块的 Inputs 为空；后续仅从直接前驱 Outputs 绑定同名且声明类型相同的输入，没有重命名、默认值、转换或全局上下文。端口类型限定为 `bool`、`int`、`double`、`QString`、`QByteArray`、`QStringList`、`QVariantList`、`QVariantMap` 和 `QVariant`；输出键必须与声明一致，值必须有效，除接受任意有效值的 `QVariant` 外必须匹配精确 Qt 类型。末端 UiPage factory 接收 Inputs 并复制页面后续使用的数据；控制器显示并持续持有页面。End 只表示启动链终点，有页面时进入 Qt 事件循环，仅逻辑链执行后直接退出。

Phase 1 不支持 Decision、ExternalCode 运行时、连线标签、分支、合流、环、并行、多页面、非末端 UiPage、UiPage 输出、Start/End 端口、外部初始输入、自定义类型和隐式转换；GUI 六类节点编辑仍保留。使用流程是支持的线性蓝图、空工作区、逐节点 Generate 与人工 Candidate Review / Accept、完整 Build、从匹配 Qt 运行库环境运行 `generated_app`、Export 后独立构建运行。Accept、CMake configure、模块库或 smoke 测试均不能代替完整应用构建；缺少 factory 会链接失败。本阶段不增加 GUI Run、行级 Review、AI 返工、前端框架或 Agent 框架。

离线 `runtime-linear` fixture 已验证实际启动、顺序、传值、窗口及导出重建；fixture 自行退出，普通生成应用没有自动退出逻辑。新增真实验收使用 `Start → LogicModule → UiPage → End`：生产 DeepSeek `deepseek-flash` 两次调用均 HTTP 200，原始 C++ 未修改即经人工审核接受，首次完整构建通过；原工程和独立 Export 工程均实际执行、传值、显示 QLabel 并正常退出 0。生成工程测试仅各有 `scaffold_smoke` 1/1，不能称为 AI 模块专用测试。此样例不代表任意蓝图可靠性，详细 Token、完整 main 回归与中断记录见[最新实现进度](implementation-progress.md)。

[使用指南](project-usage-guide.md)仍保留 Phase 1 首次交付时“等待 PR / 未做 v2 真实模型验收”的快照文字；PR 合并和真实验收状态以[最新实现进度](implementation-progress.md)为准。原 MVP 单个 LogicModule 的真实验收和 `manual_ai_integration` CLI 属于 legacy v1，不能与这次 v2 验收混用。

## 后续阶段与进入条件

以下阶段是顺序收敛的 Draft 计划，各阶段需独立实现、验证和审核。参考[平台架构](architecture/plugin-platform.md)、[蓝图 Schema v2](architecture/blueprint-schema-v2.md)和[Plugin API v1](architecture/plugin-api-v1.md)。新核心服务、protocol、NodeRegistry、IRCompiler、AIProvider 优先评估 TS/Node；现有 C++/Qt 保留不迁移，Cordis 仍是候选，Python 仅按具体 SDK/tool 需求评估，不预设 Worker。新协议仍未冻结。

1. **Draft 协议与兼容验证**：P0.1 已实现唯一机器 Blueprint v2 JSON Schema（Draft 2020-12）、离线 fixtures 和独立 TS/Ajv/Vitest 工具，运行见 [schemas/README](../schemas/README.md)。仅覆盖结构；对象 ID/引用/Unit 语义、Bridge 必需端点驱动、动态 ID 历史、未知原字节保存、Core IR/hash 和显式复制转换仍待实现。GUI/C++ 读取器继续只读 Schema 1；蓝图、Plugin API、包版本、NodeDefinition 与 workspace manifest 分别管理。
2. **最小插件注册与 Qt 编辑接入**：先支持可信插件定义的节点、属性与端口，提供缺失插件占位和禁止生成的诊断；不预建通用调度框架。优先评估 TS/Node；Cordis/本地 Sidecar 须先证明结果一致、取消、异常及权限边界，P0.1 不实现宿主或 Registry。
3. **Qt Target 适配器**：优先复用已验证 Qt IR、候选和 Runtime v2 路径。v2 → LegacyQt 转接只接收现有线性流程、直接前驱同名同类型等可表示语义；拒绝其他语义，不猜边、不转换类型、不破坏原图。Legacy v1 字节产物、v2 固定 factory 和旧清单保持兼容。
4. **首个 Embedded Target 的真实交叉编译**：选择一套明确支持的芯片/SDK/工具链，验证完整目标工程和可审查产物。用户驱动 Snapshot 方案拟复用当前导入器的复制及哈希机制，Reference 模式需另行设计；AI 只接收接口摘要。烧录、硬件运行及广泛芯片支持不作为毕业条件。
5. **多 Build Unit 与 Bridge/Channel**：再验证一图混合插件、独立工程和独立构建；Unit ID 不是 plugin ID。跨 Unit 普通边默认禁止，先定义端点、wire Schema、Bridge 版本、驱动与配置契约，再单独规划通信实现和目标验证。本次没有物理通信实现。
6. **AI Review 与返工**：源码行或行范围评论、意见汇总与差异驱动新 Candidate，人工 Accept/Reject 后再 Build/Export。复用现有 hash、冲突和文件保护机制；生成结果不得直接覆盖正式工程。

毕业范围优先收敛为 Qt GUI、可复用插件边界、Qt 目标适配和首个 Embedded 目标真实交叉编译。多 Unit 通信与完整 AI Review/返工属于后续增量，不提前承诺全部交付。已完成的 Candidate Accept/工作区集成不再按旧 Issue #32 作为待补工作；后续评审功能应建立新的明确范围。

## 迁移与信任约束

现有 `blueprint.json` / `layout.json` 不做自动升级或降级；未来转换必须显式复制到新工程，给出诊断并保留原文件、布局、未知字段与 opaque 插件 payload 原始字节，不要求新 JSON 文件整体字节一致。旧读取器拒绝未知版本。未来 v2 读取器才计划具备缺失插件占位、读取/保存保留原始数据的能力，生成则失败关闭；不能把这个计划描述成当前 v1 能力。

多 Unit 产物索引采用独立 Draft 元数据，不能向严格六字段的旧 generation manifest 添加字段。插件 capability manifest 只是能力声明，不是 OS 沙箱；初期只加载可信插件，未经验证隔离的不可信插件被拒绝。可信服务拥有 Candidate 写入、Accept、scaffold 和 Build 授权；Sidecar 进程本身不能证明文件权限隔离，构建执行也不是代码沙箱。生成/接受上下文最终应绑定项目、文档、Unit、插件版本及驱动/受保护文件哈希，变化或取消后拒绝旧结果。

每阶段只做支持当前目标的最小实现，不引入 React、Electron、Monaco、新网络服务、完整 IDE 或通用运行时 Decision 调度。
