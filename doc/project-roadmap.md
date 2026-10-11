# 项目方向与后续路线

项目目标是让用户通过蓝图描述软件结构，由 AI 生成可以构建、运行的 Qt 应用原型，再通过行级代码评审逐步修改和验证。工作区服务于生成、评审和返工，不以完整 IDE 为目标。

目前已有蓝图编辑与工程持久化（含节点布局）、ExternalCode GUI 导入与验证、模块源码生成、候选审核、Workspace Editor 安全编辑、构建和导出。Runtime Assembly Phase 1 已接入 GUI 的新生成路径并完成离线验证；它装配线性启动链和可选末端页面，等待 PR 人工审核。legacy v1 工程仍保留占位入口。行级评论和 AI 返工闭环尚未实现。

## Runtime Assembly Phase 1 当前范围

默认运行时生成使用 `startRuntime()` / `createRuntime()`，创建 manifest v2 工程。确定性生成器负责受保护的 `main.cpp`、WorkflowController、CMake 和公共契约，AI 负责当前节点 `implementation/` 与 `tests/`，必须定义契约声明的固定 `createModule_HASH` factory，返回保护契约类的 `std::unique_ptr`；装配器不猜测 AI 的具体类名。manifest 仍严格保留 `version`、`blueprintSha256`、`modules`、`protectedFiles`、`files`、`batches` 六个字段，只接受版本 1 或 2。AI 菜单及节点右键菜单另有明确的 legacy v1 模块生成动作，通过原 `create()` / `start()` 保留 module-only v1 原字节产物和占位入口；既有 v1 工程可继续生成候选及 Build / Export。两种模式不互迁，运行时失败不自动回退到旧版，新运行时使用新的空工作区。

支持 `Start → 零个或多个 LogicModule → 可选的唯一末端 UiPage → End`，按 Flow 连线执行一次启动链。第一个模块的 Inputs 为空；后续仅从直接前驱 Outputs 绑定同名且声明类型相同的输入，没有重命名、默认值、转换或全局上下文。端口类型限定为 `bool`、`int`、`double`、`QString`、`QByteArray`、`QStringList`、`QVariantList`、`QVariantMap` 和 `QVariant`；输出键必须与声明一致，值必须有效，除接受任意有效值的 `QVariant` 外必须匹配精确 Qt 类型。末端 UiPage factory 接收 Inputs 并复制页面后续使用的数据；控制器显示并持续持有页面。End 只表示启动链终点，有页面时进入 Qt 事件循环，仅逻辑链执行后直接退出。

Phase 1 不支持 Decision、ExternalCode 运行时、连线标签、分支、合流、环、并行、多页面、非末端 UiPage、UiPage 输出、Start/End 端口、外部初始输入、自定义类型和隐式转换；GUI 六类节点编辑仍保留。使用流程是支持的线性蓝图、空工作区、逐节点 Generate 与人工 Candidate Review / Accept、完整 Build、从匹配 Qt 运行库环境运行 `generated_app`、Export 后独立构建运行。Accept、CMake configure、模块库或 smoke 测试均不能代替完整应用构建；缺少 factory 会链接失败。本阶段不增加 GUI Run、行级 Review、AI 返工、前端框架或 Agent 框架。

新增 `runtime-linear` 离线 fixture 通过启动真实生成应用检查流程顺序、数据传递和窗口显示，并覆盖导出后的独立构建运行；fixture 自行退出，普通生成应用没有自动退出逻辑。完整 CTest 32/32 通过，独立规格与代码质量复审无阻塞项；没有对 v2 factory 发起真实付费 DeepSeek 验证，原 MVP 的单个简单 LogicModule 实测属于 legacy v1。

## 后续阶段

1. **蓝图持久化（已完成）**：复用 BlueprintSerializer，GUI 支持新建、打开、保存、另存为和未保存提示；语义与节点布局分别保存。
2. **可运行原型生成（Phase 1 已实现，等待 PR 审核）**：当前线性启动链的范围见上文。Decision、页面交互触发后续业务、分支及更复杂运行语义属于后续阶段，尚未实现；需要单独确定契约和验收范围。
3. **AI Code Review Workspace**：以源码浏览、行或行范围评论、评审意见汇总及增删改、代码差异为主要交互，保留手动编辑作为辅助。参考 GitHub PR Review，不引入完整 Git UI、LSP 或 IDE 功能。
4. **AI 返工闭环**：代码评论汇总为修改要求，经 AI 生成新 Candidate，人工 Diff 审核与 Accept/Reject 后再 Build 验证，继续评论或 Export。复用 AI Client、Candidate Review、manifest、hash 和文件保护机制；AI 不得未经确认覆盖正式工程。

Issue #32 剩余的 Candidate Accept 与工作区集成，在评审和返工阶段收口。每阶段应单独确认范围、实现并审核，不因本路线提前重构；当前不引入 React、Electron、Monaco 或 Agent 框架。
