# Qt Blueprint Generator

Qt Blueprint Generator 当前是一个面向 Qt 6 Widgets 项目的可视化蓝图编辑器。用户通过节点、端口和有向连线描述应用结构，系统负责校验蓝图、编译中间表示和 AI 提示词，并管理生成工程、候选代码、构建与导出。

长期方向是通用、插件优先的蓝图平台：插件定义节点、属性 Schema、稳定的强类型端口、连线校验、目标 IR 和代码生成。Qt Desktop Editor 是毕业设计要求的 Qt 图形界面；Qt Target Plugin 则负责生成 Qt 工程，两者职责独立。未来 Embedded Target Plugin 将通过受支持的芯片、SDK 和工具链适配器生成可真实交叉编译的目标工程，毕业验收不要求烧录或硬件运行。混合插件蓝图、独立 Build Unit 与显式 Bridge/Channel 的设计见 [Draft 架构](doc/architecture/plugin-platform.md)，本次仅增加设计文档，尚未实现插件注册、嵌入式生成或通信。

当前 Qt 能力已实现蓝图持久化、线性 Runtime Assembly Phase 1，以及真实模型生成后构建、执行、传值、页面显示和 Export 独立重建运行的有限样例验收。行级代码评审和 AI 返工仍在后续路线；工作区以生成和评审为核心，不以完整 IDE 为目标。阶段与事实边界见[项目路线](doc/project-roadmap.md)和[最新实现进度](doc/implementation-progress.md)。

蓝图是项目结构和模块契约的主数据源。AI 返回的代码不会直接覆盖工程文件，而是先进入候选区，经过预览和人工确认后再写入。

原 MVP 的 14 项验收标准已通过，Candidate Review 支持 Unicode 草稿保留和对应差异高亮。真实 DeepSeek 验收使用一个简单 Logic Module，完成生成、候选接受、构建、测试及导出；结果及适用边界见 [MVP 验收报告](doc/mvp-acceptance-report.md)。`v0.1.0-mvp` 定位为源码 Pre-release，不包含二进制或 Windows 安装包；[双语发布说明](doc/releases/v0.1.0-mvp.md) 记录核心功能与已知限制，发布状态以仓库的 Tag 和 Release 为准。

## 核心能力

- 六类蓝图节点：Start、End、UI Page、Logic Module、Decision 和 External Code。
- 交互式画布：节点拖动、空白区域拖拽平移画布、以鼠标位置为中心的滚轮缩放、框选、端口拖拽有向连线、分支标签，以及通过结构化表格编辑节点属性并支持撤销/重做。
- GUI 中的新建、打开、保存和另存为；蓝图语义与节点布局分别保存为 `blueprint.json`、`layout.json`，并提供未保存提示及撤销/重做。
- 蓝图 JSON 序列化和静态校验，包括节点 ID、流程结构、可达性、环和 Decision 分支检查。
- 确定性中间表示、合法 C++ 命名空间，以及项目级和模块级提示词。
- OpenAI-compatible 异步客户端、模型响应校验和候选代码生命周期管理。
- 绑定蓝图、节点和工作目录快照的生成控制器，支持取消、脱敏错误和候选持久化，并拒绝过期结果。
- GUI 中可配置非敏感 AI 参数并执行低成本连接测试；默认兼容 DeepSeek Chat Completions。
- 对选中模块发起 AI 生成，查看请求状态，并在候选审核窗口对照、编辑、接受或拒绝文件；手工修改冲突必须明确确认。
- Qt/CMake 工程骨架、公共契约、生成记录及文件 SHA-256 校验。
- 新生成工程的线性运行时装配：按 Flow 连线执行零个或多个 Logic Module，再显示可选的唯一末端 UI Page；确定性入口与控制器通过固定 factory 调用模块实现。
- GUI 中外部 C/C++ 代码的黑盒导入，支持分多次跨目录添加待选文件，再统一导入或确认后重新导入；提供接口契约绑定、SHA-256 完整性复验与只读查看。
- 独立 CMake 构建、日志采集和空目录导出。
- 通过 Workspace Root 旁的 Browse 选择工作区，支持中文路径和首次生成所需的空目录；通过 Workspace Editor 浏览 `generated-project` 文件树，普通文本源码可安全编辑，受保护文件保持只读，切换、关闭及构建/导出前会处理未保存修改。
- 英文与简体中文界面运行时切换。
- 亮色 / 暗色界面主题，覆盖菜单、工具栏、停靠面板、对话框、画布网格、节点、端口和连线；可从 `View > Theme` 运行时切换并记住选择。
- 可恢复的 Properties、Build and Export、Workspace Editor 面板，以及复用现有构建与导出操作的工具栏入口。

## 使用边界

编辑器窗口已经支持画布编辑、从输出端口拖拽创建连线、双击节点直接修改属性、右侧 Inspector 辅助编辑、语言切换、主题切换、AI 服务设置和连接测试，以及对已准备工作目录的构建和导出。两处属性入口使用相同的结构化控件，输入和输出端口可以逐行填写名称、类型和说明，约束与验收标准可以逐项增删。View 菜单可以重新显示 Properties、Build and Export 面板，Reset Layout 可恢复默认停靠区域和可见性。

代码生成会初始化或复验指定工作目录的工程骨架，结果先保存到候选区，不会自动接受、构建或执行。候选审核提供当前文件与候选的双栏对照、基础差异高亮和逐文件操作。GUI 默认的 `Generate runtime module / 生成运行时模块` 使用 v2 运行时工程，支持 `Start → 零个或多个 Logic Module → 可选的唯一末端 UI Page → End`。运行顺序由 Flow 连线决定；第一个模块接收空 Inputs，后续模块仅绑定直接前驱的同名、同声明类型输出。输出键、有效性和 Qt 类型不符时，应用启动失败。末端页面由控制器显示并持有到 Qt 事件循环结束；End 表示启动链装配完成，仅逻辑链执行后直接退出。

运行时 Phase 1 不支持 Decision、External Code、带标签连线、分支、合流、环、并行、多页面或非末端页面、页面输出、Start/End 端口、外部初始输入、自定义端口类型或隐式转换。六类节点及其编辑功能仍保留。受保护的主入口、控制器、CMake 和契约由系统生成；AI 只实现当前节点的实现与测试，并定义契约中指定的 `createModule_HASH` factory。候选接受或 `scaffold_smoke` 成功不等于完整应用构建成功；缺少 factory 会使 `generated_app` 链接失败。当前没有 GUI Run 入口，需构建后从配置了匹配 Qt `bin` 路径的终端运行；完整步骤和支持类型见[使用指南](doc/project-usage-guide.md#82-从窗口生成与审核代码)。

原有模块工程使用 v1，核心 `create()` / `start()` 接口与原字节产物保留，旧工作区不会自动迁移。AI 菜单和节点右键菜单提供明确的 `Generate module only (legacy v1) / 仅生成模块（旧版 v1）`，用于 Logic Module、UI Page 或 Decision 的旧版模块生成及已有 v1 工作区的继续生成；它不装配运行流程，入口仍为占位窗口。默认工具栏动作仍是运行时生成，失败不会自动回退到 v1。两种动作均拒绝复用另一版本的工作区；旧 v1 工程可继续 Build / Export，新运行时请使用新的空工作区。原 MVP 的简单 Logic Module 真实 DeepSeek 验收与离线登录示例属于 legacy v1。另一次 Runtime v2 验收已于 2026-10-12 完成：一个 Logic Module 和一个末端 UI Page 的两次真实请求均成功，原始 C++ 未修改即被接受并首次完整构建通过，原工程与独立导出工程均实际执行、传值、显示 QLabel 并正常退出。这是有限线性样例，不保证任意蓝图或每次生成正确；详情见[最新实现进度](doc/implementation-progress.md)。

当前蓝图读取器仍只接受 `schemaVersion: 1` 和固定六类 NodeType，布局另存于 `layout.json`，IR 只包含语义。生成工作区的 `generation-manifest.json` 版本 1/2 与蓝图 Schema 版本是不同概念；该 manifest 严格保持 `version`、`blueprintSha256`、`modules`、`protectedFiles`、`files`、`batches` 六字段。[蓝图 Schema v2](doc/architecture/blueprint-schema-v2.md)、[Plugin API v1](doc/architecture/plugin-api-v1.md) 和多 Unit 元数据均为 Draft，当前程序不能加载这些示例，不会自动转换或以旧版格式保存它们。

[使用指南](doc/project-usage-guide.md)中“等待 PR / 未做 v2 真实模型验收”的状态文字是 Phase 1 首次交付快照；PR #50 已于 2026-10-11 合入 `main`（`843138c`），当前验收状态以[最新实现进度](doc/implementation-progress.md)为准。指南中的操作与 Phase 1 使用边界仍供当前 Qt 路径参考。

从 `File / 文件` 菜单管理蓝图工程，保存后重新打开可恢复节点、连线、属性和位置。蓝图工程目录与生成工作区分开使用；切换蓝图会清理旧的生成请求和候选审核上下文。选中 External Code 节点，在属性面板应用接口契约后管理外部代码；导入文件保存在生成工作区中，AI 只接收契约、不接收源码。重新导入需要明确确认，只能替换原清单中的同一组文件，不会删除文件或接管未知内容。已经持久化的候选和工程文件会保留在工作目录中，但当前窗口不支持跨重启重新打开历史候选批次。

项目目前生成 C++17、Qt 6 Widgets 和 CMake 工程。插件宿主语言尚未选定；继续维护已验证的 C++/Qt 后端，Python 或 TypeScript/Cordis 本地 Sidecar 只作为需先验证契约的可选方案，见[多语言设计的当前方向](doc/multilanguage-development-design.md)。

## 技术栈

- C++17
- Qt 6 Widgets、Network、Test 和 LinguistTools
- CMake 3.22+
- Ninja
- MinGW 64-bit

## 项目结构

~~~text
project/
├─ CMakeLists.txt
├─ README.md
├─ doc/                 # 使用说明、设计和实现进度
├─ translations/        # Qt Linguist 翻译目录
├─ src/
│  ├─ ai/               # AI 接口、Fake 客户端和 HTTPS 客户端
│  ├─ app/              # 主窗口
│  ├─ blueprint/        # 蓝图模型、序列化和校验
│  ├─ editor/           # 画布、节点和连线图元
│  ├─ generation/       # IR、提示词、工程骨架和候选管理
│  ├─ ui/               # 集中式界面主题与语义颜色
│  └─ workspace/        # 外部代码导入、构建和导出
├─ tools/               # 需显式启用的人工 AI 集成工具
└─ tests/               # Qt Test 自动化测试和离线示例
~~~

## AI 设置与人工集成

从窗口的 `AI > AI Settings...` 可设置 OpenAI-compatible HTTPS 端点与模型，并用 `Test Connection` 做一次受限连接测试。默认端点是 `https://api.deepseek.com/chat/completions`，默认模型是 `deepseek-flash`。API 密钥只从启动进程的 `BLUEPRINT_AI_API_KEY` 环境变量读取；不要把密钥放入 `.env`、脚本、蓝图、项目文件或 CMake 参数。

在构建面板填写或浏览选择已存在的绝对工作目录，首次生成建议使用空目录。完成符合 Phase 1 范围的线性蓝图并选中一个 Logic Module 或末端 UI Page 后，可通过默认工具栏或 AI 菜单发起运行时生成；Decision 仅通过明确的旧版模块生成动作生成代码，不执行分支。修改蓝图结构、节点属性或工作目录会使旧请求和审核上下文失效；初始化失败会显示具体原因，已有工程与新蓝图或选定模式不匹配时，请使用新的工作目录，不会自动改选模式或覆盖工程。详细操作见[使用指南](doc/project-usage-guide.md#8-ai-设置与人工真实集成)。

也可使用显式启用的 legacy v1 命令行验收工具 `manual_ai_integration`，默认构建不会产生该程序；它不使用 Runtime v2 factory，不是上述 v2 真实验收的入口：

```powershell
$qtRoot = 'C:\path\to\Qt\6.x\mingw_64' # 按本机安装位置修改
$manualBuild = Join-Path $env:LOCALAPPDATA 'QtBlueprintGenerator\manual-ai'
cmake -S . -B $manualBuild -G Ninja `
  -DBUILD_TESTING=OFF `
  -DBUILD_MANUAL_AI_INTEGRATION=ON `
  -DCMAKE_PREFIX_PATH="$qtRoot"
cmake --build $manualBuild --target manual_ai_integration
$env:BLUEPRINT_AI_API_KEY = '<paste the key only in this terminal>'
try {
    & (Join-Path $manualBuild 'manual_ai_integration.exe')
} finally {
    Remove-Item Env:BLUEPRINT_AI_API_KEY -ErrorAction SilentlyContinue
}
```

可用 `--endpoint`、`--model` 和 `--timeout-seconds` 覆盖服务参数。默认会创建隔离的临时工作目录；如传 `--workspace`，该目录必须预先存在、为空、使用绝对路径，并且路径各级不能包含 symlink、junction 或 reparse point，工具不会代为创建或清空它。工具严格校验模型 JSON，并只保存待人工审查的候选；它不会调用接受操作，也不会打印密钥、Authorization 头、完整提示词、原始响应或模型派生的校验细节。真实请求可能产生费用，并可能因认证、余额、限流、模型、端点、网络或超时而失败。密钥疑似泄露时应立即在服务商处撤销并轮换；固定退出码和操作边界见[项目说明与使用指南](doc/project-usage-guide.md)。

## 文档

- [项目说明与使用指南](doc/project-usage-guide.md)
- [项目方向与后续路线](doc/project-roadmap.md)
- [插件平台架构（Draft）](doc/architecture/plugin-platform.md)
- [蓝图 Schema v2（Draft）](doc/architecture/blueprint-schema-v2.md)
- [Plugin API v1（Draft）](doc/architecture/plugin-api-v1.md)
- [界面样式与缩放验收指南](doc/visual-style-verification.md)
- [MVP 实施计划](doc/mvp-implementation-plan.md)
- [MVP 验收报告](doc/mvp-acceptance-report.md)
- [实现进度](doc/implementation-progress.md)
- [中英文界面切换计划](doc/plans/2026-09-09-bilingual-ui.md)
- [多语言设计：当前宿主选项与早期 Qt/Python 提案](doc/multilanguage-development-design.md)

## 许可

本仓库目前尚未添加开源许可证。未经仓库所有者明确授权，不代表允许复制、分发或商用。
