# Qt Blueprint Generator

一个面向 Qt 6 Widgets 项目的可视化蓝图编辑器。用户通过节点、端口和有向连线描述应用结构，系统对蓝图执行静态校验，将其编译为稳定的中间表示和 AI 生成提示词，并提供工程骨架与候选文件审核的核心服务。离线验收已串联生成、候选接受、构建和导出 C++17/CMake 工程；生成与候选审核目前通过 API 使用。

## 当前状态

项目当前已完成 MVP Task 1 至 Task 10 范围内的核心服务和离线自动化验收：

- Qt 6 Widgets / C++17 / CMake 工程骨架和 Qt Test 测试环境。
- 蓝图领域模型及 `blueprint.json` 序列化往返。
- 节点 ID、流程结构、可达性、环、Decision 分支和外部代码路径校验。
- 六类节点的交互式画布、端口、有向连线、Decision 标签、属性编辑、缩放、框选及撤销/重做。
- 确定性 IR 编译、合法 C++ 命名空间、最小邻接上下文，以及项目级和模块级提示词模板。
- 异步 AI 客户端、离线 Fake、OpenAI-compatible HTTPS 请求，以及模型响应的 JSON、路径、扩展名和大小校验。
- 确定性 Qt 工程骨架、公共契约、生成清单与 SHA-256，以及候选保存、预览、逐文件接受/拒绝/取消和人工修改保护。
- 外部 C/C++ 文件黑盒导入、接口契约绑定、文件哈希复验，以及与 AI 提示词和候选覆盖流程的隔离。
- 显式异步 CMake 配置与构建、构建日志面板，以及携带外部代码且保留当前文件字节的空目录导出。
- 三模块离线登录示例、候选接受与导出全链路测试，以及导出项目自身的 Qt Test。
- 编辑器界面可在运行时切换英文与简体中文，并记住上次选择。

MVP Task 1 至 Task 10 已合入 `main`，其中 Task 6 至 Task 10 分别通过 PR #6、#7、#8、#10 和 #11 交付。当前中英文界面切换在独立分支 `feature/bilingual-ui` 开发；完整构建和 CTest 12/12 通过。窗口交互经 Qt 鼠标事件和自动化测试核验，候选差异经 API 验证；完整图形化生成/审核工作流仍待接入。

## MVP 工作流

```text
创建项目 → 绘制并校验蓝图 → 编译 IR 与提示词
        → 逐模块生成候选 → 人工审核 → 构建验证 → 导出项目
```

蓝图是架构和模块契约的唯一主数据源。布局信息独立保存。完整工作流中，AI 结果经校验后先进入候选目录，未经用户确认不得覆盖项目文件。

## 环境要求

- Windows 10/11
- Qt 6 Widgets、Network、Qt Test 和 LinguistTools（当前验证版本：Qt 6.9.3）
- 支持 C++17 的编译器（当前验证版本：MinGW 13.1）
- CMake 3.22 或更高版本（当前验证版本：4.3.2）
- Ninja

## 构建与测试

首次安装时，在 Qt 安装器中选择 Qt 6 的 MinGW 64-bit 套件及对应 MinGW 工具链，准备 CMake 和 Ninja。将私有仓库克隆到本机后，在仓库根目录执行以下命令。示例不需要 AI 服务商账号或 API 密钥；Qt Widgets、Network 和 Test 模块都需要可用。

以下 PowerShell 示例使用本机已验证的 Qt 路径；如果安装位置不同，请相应调整：

```powershell
$env:PATH = "E:\Qt\6.9.3\mingw_64\bin;E:\Qt\Tools\mingw1310_64\bin;E:\Qt\Tools\Ninja;$env:PATH"

cmake -S . -B build -G Ninja `
  -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH=E:/Qt/6.9.3/mingw_64
cmake --build build
ctest --test-dir build -C Debug --output-on-failure
```

运行编辑器：

```powershell
.\build\BlueprintEditor.exe
```

请优先通过 CTest 运行测试。直接双击单个 `tst_*.exe` 时，如果 Qt 的 `bin` 目录不在 `PATH` 中，Windows 会提示缺少 `Qt6Test.dll`。

## 界面语言

菜单栏的 **Language / 语言** 菜单提供 **English / 英语**（语言代码 `en`）和 **Chinese / 中文**（语言代码 `zh_CN`）。选择后窗口标题、菜单、工具栏、属性面板、构建与导出面板及后续状态信息会立即切换，无需重启；选择保存在本机 `QSettings` 的 `ui/language` 项，下次启动时自动恢复。仅使用默认英文时不会创建该设置项。

英文是源码语言，简体中文由 `translations/BlueprintEditor_zh_CN.ts` 提供。语言切换只作用于编辑器自身界面，不修改已创建节点的名称和说明，不翻译蓝图 JSON、提示词、AI 返回内容、构建输出或生成项目源码。节点在创建时使用当前界面的默认类型名称，创建后即作为用户数据保留。

可单独运行语言切换回归测试：

```powershell
ctest --test-dir build -C Debug -R '^language_switch$' --output-on-failure
```

## 离线登录示例与验收（Task 10）

`tests/fixtures/login-demo/blueprint.json` 是固定登录蓝图，`fake-ai-response.json` 是 JSON 数组，每项保存一个模块的 `nodeId`、摘要和候选文件。测试使用 `FakeAiClient` 逐模块生成候选，预览并核对候选内容，再显式接受、导出；随后用真实 CMake 配置和编译导出项目，并执行项目自身的 Qt Test。默认临时目录在测试结束后清理，不调用外部模型。

示例包含登录页面、凭据检查和认证分支三个生成模块。`demo / secret` 是公开的离线测试凭据：正确输入进入 success，错误或空输入进入 failure；页面密码框使用掩码显示。该示例不连接认证服务，不作为生产账号系统使用。

配置并构建编辑器后，可单独运行：

```powershell
ctest --test-dir build -C Debug -R '^end_to_end$' --output-on-failure
```

如需保留演示导出文件，先准备一个新的空目录，并仅在本次测试设置输出目录：

```powershell
$demoPath = 'C:/bp-login-demo'
New-Item -ItemType Directory -Path $demoPath -ErrorAction Stop | Out-Null
$env:BLUEPRINT_DEMO_OUTPUT_DIR = $demoPath
try {
    ctest --test-dir build -C Debug -R '^end_to_end$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Login demo acceptance failed' }
} finally {
    Remove-Item Env:BLUEPRINT_DEMO_OUTPUT_DIR -ErrorAction SilentlyContinue
}
```

`BLUEPRINT_DEMO_OUTPUT_DIR` 必须指向已存在的绝对空目录；同一非空目录不能被后续运行覆盖。它只控制演示导出位置，不是 API 密钥或模型配置。导出的 `generation-manifest.json` 保留生成与接受记录，`blueprint.json` 保留原始语义蓝图，候选目录不随工程导出。

需要在测试之外重新构建导出项目时，可运行：

```powershell
cmake -S C:/bp-login-demo -B C:/bp-login-build -G Ninja `
  -DBUILD_TESTING=ON -DCMAKE_PREFIX_PATH=E:/Qt/6.9.3/mingw_64
cmake --build C:/bp-login-build
ctest --test-dir C:/bp-login-build -C Debug --output-on-failure --no-tests=error
```

窗口交互复查步骤：

1. 从 Add node 菜单创建 Start、UI Page、End，拖动页面节点；确认连线锚点跟随节点移动。
2. 点击 Connect，依次选择来源和目标，连接 Start → UI Page → End。
3. 选中页面，在 Properties 修改名称与说明并 Apply，再用 Undo/Redo 核对字段和节点标题同步。
4. 在 Build and export 面板填入已有工作目录、独立构建目录和 CMake 参数，点击 Build，确认日志显示 configure/build 退出码和最终状态；选择现有空目录点击 Export。

当前候选差异、生成与接受操作通过服务 API 和上述测试验收，尚无候选审核窗口。生成工程入口仍是骨架占位窗口；登录模块行为由导出项目内的测试调用验证，尚未将蓝图业务流程自动连接到应用入口。真实服务商联调、完整图形化工作流和后续多语言方案不属于这次离线验收结果。

## AI 生成服务（Task 6）

`GenerationService::generate()` 接收模块提示词、预期节点 ID、绝对候选目录和可选限额，返回请求 UUID，通过成功或失败信号异步通知调用者。成功结果包含摘要、文件内容、规范化相对路径和候选目标路径；任何文件未通过校验时，整个响应都会被拒绝。

- `FakeAiClient` 可预设成功响应或错误，用于离线测试与演示。
- `OpenAiCompatibleClient` 构造时接收完整的 HTTPS Chat Completions 地址和模型名称；每次请求从本机 `BLUEPRINT_AI_API_KEY` 环境变量读取密钥。当前没有设置界面或配置持久化，也尚未进行真实服务商联调。
- 默认限额：模型 JSON 1 MiB、单文件 UTF-8 内容 256 KiB、总文件内容 1 MiB、最多 32 个文件、HTTP 响应体 2 MiB；默认绝对超时为 30 秒。
- 允许 `.h`、`.hpp`、`.cpp`、`.cc`，拒绝绝对路径、穿越、重复路径、非法字段及现有目录链接越界。Task 7 的写盘操作会再次校验目录边界和节点归属。

AI 相关自动化测试使用 Fake 或离线网络替身，不需要 API 密钥，也不产生模型调用费用。单独运行：

```powershell
ctest --test-dir build -C Debug -R '^generation_service$' --output-on-failure
```

## 工程骨架与候选流程（Task 7）

Task 7 的核心服务按以下顺序使用；生成和审核操作尚未接入窗口界面：

1. 先校验蓝图并准备好本机工作目录，再调用 `ProjectScaffolder::create(document, absoluteWorkspace, &error)`，创建确定性的 Qt 工程骨架和生成记录。相同蓝图可以重复调用；蓝图发生变化时需要选择新工作目录，本阶段不自动迁移已有工程。
2. 使用 `GenerationService::generate()` 取得已校验的内存结果，再调用 `persistCandidate()` 保存批次、模型名称、提示词 SHA-256 和候选文件。模型密钥不属于这些参数。
3. 通过 `previewCandidate()` 读取当前文件与候选内容及哈希，供调用者展示差异。
4. 用户逐文件确认后调用 `acceptCandidate()`；也可传入修改后的候选内容。存在人工修改时默认拒绝覆盖，即使用户明确确认覆盖，也要重新核对预览对应的文件状态。
5. 使用 `rejectCandidate()` 拒绝单个文件，或 `cancelCandidate()` 取消该节点的剩余候选。候选和处理状态保留作为生成记录，不因拒绝或取消删除当前工程文件。

工作目录中，`generated-project/` 保存当前工程，`candidates/<generation-id>/<node-id>/` 保存候选，`generation-manifest.json` 保存生成记录。AI 文件仅允许写入 `src/modules/<node-id>/implementation/` 或 `tests/<node-id>/` 下的 `.h`、`.hpp`、`.cpp`、`.cc` 文件；确定性骨架、公共契约、其他节点文件和外部代码不能被 AI 候选覆盖。

落盘使用的节点 ID 和批次 ID 必须是 1–80 个 ASCII 字母、数字、下划线或连字符，首字符为字母或数字，且不是 Windows 保留名称。候选路径段只使用 ASCII 字母、数字、下划线、连字符和点，不接受大小写别名、目录链接或 reparse point。显示名称和文字说明仍可使用中文。

调用模型前，调用者需要把生成的公共类型 `src/contracts/types.h` 和本模块 `src/modules/<node-id>/contract.h` 一并提供为只读上下文，并说明上述输出目录。实际 C++ 命名空间在 Task 5 IR 名称前加 `Blueprint_` 前缀，避免与 Qt 类或宏冲突；该实际值记录在生成工程 `src/contracts/blueprint.json` 的 `project.namespace` 中。应使用这份生成 IR 编译提示词，并以具体契约为准，不能再用原始 Task 5 IR 覆盖命名空间。通用提示词编译器尚不自动读取这些骨架文件。

生成骨架使用 `QVariantMap` 表示输入输出，自定义端口类型保留为元数据，不直接拼接进 C++。UiPage、LogicModule 和 Decision 分别提供 QWidget、QObject 和布尔判断接口。应用入口仍是占位窗口，尚未自动连接蓝图业务流程；接受候选也不等于候选已通过编译或满足接口契约。

生成工程中的每个节点测试 `.cpp`/`.cc` 都应自带测试入口；CMake 会将它们注册为独立 CTest 项。Windows 下建议使用较短的构建目录，避免测试目标和自动 MOC 路径触及工具链的路径长度限制。

这些操作面向可信本机、单写入者工作目录。每次操作都会重新检查路径和文件状态，但不把 Qt 的路径式文件操作当作针对其他进程恶意并发替换目录的安全沙箱。多文件写入在普通 I/O 失败时尝试回滚，不提供断电或进程崩溃时的整体原子性保证。生成代码仍须人工审查；写入或接受不会自动执行模型代码。

## 外部代码黑盒导入（Task 8）

`ExternalCodeImporter::importFiles()` 接收一个已由用户填写名称、说明和输入输出的 `ExternalCode` 节点、源目录、明确选择的相对文件列表及工作目录。它只复制 `.h`、`.hpp`、`.cpp`、`.cc` 文件，并保留所选文件的嵌套目录和原始字节；未选文件不会被隐式导入。

导入结果位于 `external/<node-id>/`，其中 `import-manifest.json` 保存完整节点契约、契约 SHA-256、逐文件 SHA-256 和 `readOnly` 生成策略。`ExternalCodeImporter::verifyImport()` 会重新检查契约、文件哈希及完整目录清单；文件、清单或契约缺失、替换、篡改时，蓝图校验返回 `external_code.import.invalid`。相同契约和文件可幂等重复导入，但本阶段不提供覆盖、替换或删除 API。

源目录、工作目录和文件路径均需通过目录链接、Windows reparse point、大小写别名、穿越、重复及前缀冲突检查。节点 ID 和相对路径沿用可移植 ASCII 规则。导入是面向可信本机单写入者的文件事务，不修改源文件权限，也不承诺抵御其他进程并发替换路径或断电。

外部节点不会进入可生成 IR 模块或生成清单的模块列表；相邻模块的提示词只包含蓝图中人工填写的外部接口契约，不读取外部源码。AI 候选仍只能写入可生成节点自己的实现和测试目录，不能覆盖 `external/`。Task 9 的导出服务会复验并携带外部代码；导入操作本身尚未接入窗口。

单独运行导入测试：

```powershell
ctest --test-dir build -C Debug -R '^external_code_importer$' --output-on-failure
```

## 构建与导出（Task 9）

窗口底部的 **Build and export** 面板可填写工作目录、独立构建目录、CMake 程序、配置参数及现有空导出目录，然后点击 Build 或 Export。日志面板显示构建输出、各阶段退出码及失败原因。工作目录是包含 `generated-project/` 的目录，不是 `generated-project/` 本身；当前画布不会在点击 Build 时自动保存或生成工程。

本机 MinGW/Ninja 环境的配置参数示例为 `-G Ninja -DCMAKE_PREFIX_PATH=E:/Qt/6.9.3/mingw_64`，CMake 程序可填写 `cmake` 或完整路径。启动编辑器前仍需按上述 PowerShell 示例设置 Qt、MinGW、Ninja 的 `PATH`；路径含空格的配置参数请加双引号。

`BuildService::start()` 接收源码目录、独立构建目录、CMake 程序及配置/构建参数，异步依次执行 CMake 配置和构建。配置失败时不会继续构建；服务提供分阶段 stdout、stderr、退出码和最终结果，拒绝并发启动，不自动运行生成的程序。源码目录必须已存在，构建目录可尚未创建，但二者均须为绝对路径且不能互相包含。源码/构建目录由专用字段控制，配置参数不能再次传入 `-S`、`-B`，也不能使用 `-P`、`-E`、`--build`、`--install` 等切换 CMake 执行模式的选项。

`ProjectExporter::exportProject(workspace, target, &error)` 将当前工作工程复制到一个已存在的空目录。导出布局如下：

| 工作目录中的来源 | 导出位置 |
|---|---|
| `generated-project/` 内容 | 导出目录根部 |
| `generation-manifest.json` | `generation-manifest.json` |
| `generated-project/src/contracts/source-blueprint.json` | 另存为根部 `blueprint.json` |
| `external/<node-id>/` | `src/external/<node-id>/` |

导出保留当前工程文件的原始字节，包括人工修改后的实现，不会把它们还原为上次生成版本。外部源码及其导入清单须通过复验；候选目录、工作目录下的构建产物及其他私有文件不在导出映射内。请将构建目录放在 `generated-project/` 之外，且不要在工程源目录内存放密钥或其他不应交付的文件。

复制先在目标目录同级的唯一临时目录中进行，并核对文件哈希；成功后通过目录重命名提交。非空目标、路径重叠、链接、非法路径和映射冲突会被拒绝，普通失败会尝试清理临时副本、恢复空目标。该保证面向可信本机单写入者，不涵盖断电、进程崩溃或恶意并发替换目录。

构建会执行工程中的 CMake 和编译步骤，应先人工审查来源。导出不等于编译或业务验收通过，也不会自动生成外部模块的调用连接。当前导出为同步操作，大工程复制期间界面可能短暂阻塞；尚不提供取消操作。Task 10 的登录示例通过独立测试验证构建和业务行为，不能据此推断任意模型输出都正确。

单独运行构建与导出测试：

```powershell
ctest --test-dir build -C Debug -R '^project_exporter$' --output-on-failure
```

## 蓝图模型

MVP 支持以下节点类型：

| 类型 | 用途 |
|---|---|
| `Start` | 唯一流程入口 |
| `End` | 流程出口 |
| `UiPage` | QWidget 页面或对话框 |
| `LogicModule` | 独立业务服务 |
| `Decision` | 带 `true` / `false` 标签的条件分支 |
| `ExternalCode` | 用户已有代码的只读黑盒模块 |

每个节点可定义名称、说明、输入、输出、约束和验收标准。首版流程要求为有向无环图。

## 项目结构

```text
project/
├─ CMakeLists.txt
├─ README.md
├─ doc/                 # MVP 设计、进度和后续规划
├─ src/
│  ├─ ai/               # 异步 AI 接口、Fake 与 HTTPS 客户端
│  ├─ app/              # 主窗口
│  ├─ blueprint/        # 领域模型、序列化和校验
│  ├─ editor/           # 蓝图场景、节点和连线图元
│  ├─ generation/       # IR、提示词、AI 响应校验、工程骨架与候选管理
│  └─ workspace/        # 外部代码导入、CMake 构建与项目导出
└─ tests/               # Qt Test 自动化测试
```

## 设计文档

- [MVP 实施计划](doc/mvp-implementation-plan.md)
- [实现进度](doc/implementation-progress.md)
- [多语言混合开发后续规划](doc/multilanguage-development-design.md)

多语言文档只描述 MVP 完成后的演进方向，不改变当前 Task 1 至 Task 10 的实现文件、技术路线或验收标准。

## 安全约束

- API 密钥仅从 `BLUEPRINT_AI_API_KEY` 读取，不写入项目文件或日志。
- AI 返回的文件只能进入候选目录，接受前必须再次校验相对路径和扩展名。
- 外部代码默认只向 AI 提供人工填写的接口摘要。
- 构建与运行必须由用户显式触发。
- 每个 Task 完成后的提交都必须同步更新 README 中受影响的项目状态、构建方式或使用说明。
- 每个 Task 完成后必须通过测试、创建独立提交并同步远端；禁止强制推送或改写远端历史。
- 从 Task 6 开始，每个 Task 均从最新远端 `main` 新建独立分支；前序 Task 合并后再开始依赖它的下一 Task。

## 许可

本仓库目前尚未添加开源许可证。未经仓库所有者明确授权，不代表允许复制、分发或商用。
