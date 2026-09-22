# 项目说明与使用指南

## 1. 项目用途

Qt Blueprint Generator 是一个 Qt 6 Widgets 蓝图编辑器。它用节点和有向连线描述桌面应用的页面、业务模块、条件分支及已有代码，再把蓝图编译成稳定的中间表示和 AI 提示词。生成结果先进入候选区，人工确认后才能写入工程。

当前 MVP 使用 C++17、Qt 6 和 CMake。它已经具备蓝图模型、静态校验、交互画布、提示词编译、OpenAI-compatible 客户端、工程骨架、候选审核、外部代码导入、构建和导出的核心实现，并有一条离线登录示例验证完整链路。

## 2. 当前能直接使用的功能

| 功能 | 当前入口 | 状态 |
|---|---|---|
| 创建、移动和删除节点 | 编辑器窗口 | 可直接使用 |
| 创建带标签的有向连线 | 编辑器工具栏 | 可直接使用 |
| 编辑节点名称、说明、端口、约束和验收标准 | 双击节点或右侧属性面板 | 可直接使用 |
| 撤销、重做、框选和缩放 | 编辑器窗口 | 可直接使用 |
| 恢复属性与构建/导出面板 | `View / 视图` 菜单 | 可直接使用 |
| 画布、节点和连线的右键菜单（含全选、适应视图、重置视图） | 画布空白处、节点或连线右键 | 可直接使用 |
| 中英文界面切换 | `Language / 语言` 菜单 | 可直接使用并记住选择 |
| AI 服务参数和连接测试 | `AI > AI Settings... / AI 设置...` | 可直接使用 |
| 构建已准备好的工作目录 | 底部构建面板 | 可直接使用 |
| 导出已准备好的工作目录 | 底部导出面板 | 可直接使用 |
| 蓝图 JSON 读写与校验 | C++ API、自动化测试 | 尚未接入打开/保存按钮 |
| AI 代码生成、候选对照、编辑和接受/拒绝 | 工具栏、AI 菜单及候选审核窗口 | 可直接使用 |
| 工程骨架创建与一致性复验 | GUI 生成流程自动调用 | 可直接使用 |
| 外部代码导入 | C++ API、自动化测试 | 尚未接入窗口 |

关闭编辑器会丢失当前画布中的内容，因为窗口暂时没有打开、保存和自动恢复蓝图的功能。需要长期保存的数据应先通过 `BlueprintSerializer::toJson()` 序列化，再由调用方写入 `blueprint.json`；也可以先使用测试夹具和核心服务验证流程。

## 3. 环境准备

推荐环境：

- Windows 10 或 Windows 11
- Qt 6.9.3 MinGW 64-bit，安装 Widgets、Network、Test 和 LinguistTools
- MinGW 13.1
- CMake 3.22 或更高版本
- Ninja

下面的命令使用本项目已经验证过的 Qt 安装位置。如果 Qt 安装在其他目录，需要修改 `$qtRoot`、`$mingwRoot` 和 `$ninjaRoot`。

```powershell
Set-Location "C:\path\to\project"

$qtRoot = 'E:\Qt\6.9.3\mingw_64'
$mingwRoot = 'E:\Qt\Tools\mingw1310_64'
$ninjaRoot = 'E:\Qt\Tools\Ninja'
$buildRoot = Join-Path $env:LOCALAPPDATA 'QtBlueprintGenerator\build'
$env:PATH = "$qtRoot\bin;$mingwRoot\bin;$ninjaRoot;$env:PATH"
```

项目路径可以包含中文，但建议把构建目录放在纯 ASCII 路径，例如上面的 `%LOCALAPPDATA%\QtBlueprintGenerator\build`。MinGW 和 Qt AutoMOC 在部分中文构建路径下可能无法正确生成文件。

## 4. 构建、测试和启动

在同一个 PowerShell 窗口执行：

```powershell
cmake -S . -B $buildRoot -G Ninja `
  -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH="$qtRoot"

cmake --build $buildRoot
ctest --test-dir $buildRoot -C Debug --output-on-failure
& (Join-Path $buildRoot 'BlueprintEditor.exe')
```

正常情况下会运行全部已注册的 CTest。测试使用 Fake AI 或离线网络替身，不读取真实 API 密钥，也不会产生模型费用。人工真实集成程序从不注册为 CTest。

以后重新编译只需在已经设置好 `PATH` 的终端运行：

```powershell
cmake --build $buildRoot
& (Join-Path $buildRoot 'BlueprintEditor.exe')
```

## 5. 编辑蓝图

### 界面与布局

编辑器使用统一浅色主题，菜单、工具栏、属性面板、构建面板与 AI 对话框遵循相同配色和控件状态。当前不提供明暗主题切换。界面使用系统字体，代码和构建日志使用系统等宽字体，不需要下载额外字体。

窗口较小时，工具栏中的部分操作会收进溢出入口。属性表格和底部构建表单可滚动；可以拖动面板边界调整空间，也可以从 `View / 视图` 菜单执行 `Reset Layout / 重置布局` 恢复布局。高 DPI 下建议按显示器的实际可用空间调整窗口，不需要修改系统缩放。

样式、键盘焦点、中英文及缩放检查方法见[界面验收指南](visual-style-verification.md)。

### 5.1 添加节点

点击工具栏的 `Add node / 添加节点`，选择节点类型：

| 节点 | 用途 |
|---|---|
| `Start / 开始` | 流程入口；一张有效蓝图只能有一个 |
| `End / 结束` | 流程出口，可以有多个 |
| `UI Page / 界面页面` | QWidget 页面或对话框 |
| `Logic Module / 逻辑模块` | 独立业务服务 |
| `Decision / 条件分支` | 根据布尔结果选择后续路径 |
| `External Code / 外部代码` | 已有 C/C++ 代码的只读黑盒模块 |

新节点会出现在画布中央附近。拖动节点可调整位置；按住 Ctrl 或 Shift 可以增加或减少选择，拖动画布空白区域可框选多个节点。鼠标滚轮控制缩放，范围为 25% 到 300%。

### 5.2 编辑属性

双击画布中的节点会打开 `Edit node / 编辑节点` 对话框，可以在画布上下文中修改名称、说明、输入、输出、约束和验收标准。点击 `Save / 保存` 后，蓝图文档、节点标题和右侧属性面板会立即同步；点击 `Cancel / 取消` 不会修改蓝图。直接编辑产生一条撤销命令，可以使用 Undo / Redo 完整撤销或恢复。

单击一个节点时，右侧 `Properties / 属性` 面板仍会显示同一组字段，可作为 Inspector 和辅助编辑入口；修改后点击 `Apply / 应用`。同时选中多个节点时，属性面板不会修改其中任何一个节点。

输入和输出区域使用表格，每行是一个端口，依次填写名称、类型和说明。点击 `Add input / 添加输入` 或 `Add output / 添加输出` 新增一行，点击该行的 `Delete / 删除` 移除。类型是蓝图中的接口文本，例如 `QString`、`bool` 或项目自定义类型，不要求在编辑时解析为 C++ 类型。

约束和验收标准区域同样按行编辑。点击 `Add constraint / 添加约束` 或 `Add criterion / 添加验收标准` 新增一项，再直接填写文本。空列表、空字符串、重复项、排列顺序和首尾空白都会原样保留；编辑器不会擅自整理或去重。

两种入口只在点击 `Apply / 应用` 或 `Save / 保存` 时写入蓝图，并各自产生一条完整的撤销命令。切换中英文不会丢失尚未提交的表格内容。

### 5.3 创建连线

直接拖拽是画布上的主要操作方式：

1. 如果需要标签，先在工具栏的 `Edge label / 连线标签` 输入框填写。
2. 按住来源节点右侧的输出端口并拖动，蓝色虚线会跟随鼠标。
3. 移到另一节点左侧的有效输入端口；端口变为绿色表示可以连接。
4. 松开鼠标创建有向连线。松开在无效区域不会修改蓝图，也不会增加撤销记录。

拖拽过程中按 Esc 或点击鼠标右键会取消操作。工具栏中的 `Connect / 连接` 仍然保留：点击后依次点击来源节点和目标节点即可；按 Esc 或点击 `Cancel connection / 取消连接` 可以退出该模式。

连线方向始终从输出端口指向输入端口；使用 `Connect / 连接` 时则由点击顺序决定。条件分支的两条出边应分别使用小写 `true` 和 `false`；其他连线通常留空。系统拒绝自连接，完整流程是否合法需要再由蓝图校验器判断。节点移动后，已有连线会自动跟随。

连接模式会优先解释节点点击；在该模式下双击不会同时打开节点编辑器。需要直接编辑节点时，先按 Esc 或点击 `Cancel connection / 取消连接` 退出连接模式，再双击节点。

### 5.4 删除与撤销

选中一个或多个节点后，点击工具栏的 `Delete / 删除` 或直接按键盘 `Delete` 键即可删除；删除节点时，与它相连的边也会删除。

连线同样可以被选中：单击连线即可选中，不需要精确点在线上，选中的连线使用更粗的高亮颜色显示。此时按 `Delete` 只删除这条连线，两端节点保持不变。

节点和连线可以同时选中，此时 `Delete` 会删除全部选中的对象；其中同时被选中的连线只删除一次，不会与节点删除重复处理。每个被删除的对象各产生一条撤销命令，依次 `Undo` 可以逐条恢复。

只有画布拥有焦点时 `Delete` 才会删除画布对象。焦点在 `Name`、`Description`、连线标签、端口表格或其他文本输入控件中时，`Delete` 仍然是正常的文本编辑键，不会影响画布。

工具栏和 `Edit / 编辑` 菜单都提供 Undo、Redo；移动节点、修改属性、创建连线、删除节点和删除连线均可撤销。

### 5.5 切换语言

打开 `Language / 语言` 菜单：

- `English / 英语` 对应语言代码 `en`
- `Chinese / 中文` 对应语言代码 `zh_CN`

切换立即生效，不需要重启。显式选择会写入本机 `QSettings`，下次启动自动恢复。切换语言只修改编辑器界面，不会翻译已有节点名称、蓝图 JSON、提示词、模型回复、构建日志或生成源码。

### 5.6 右键菜单

画布上右键会按点击位置给出对应菜单，顶部工具栏和菜单栏保持不变：

| 右键位置 | 菜单项 |
|---|---|
| 空白画布 | `Add node / 添加节点`（与工具栏相同的节点类型）、`Select All / 全选`、`Fit View / 适应视图`、`Reset View / 重置视图` |
| 节点 | `Edit node / 编辑节点`、`Generate selected node / 生成选中节点`、`Delete / 删除` |
| 连线 | `Delete / 删除` |

- 右键一个尚未选中的节点或连线时，它会成为当前操作对象；右键已经在多选中的对象时保留原有多选，因此 `Delete` 仍然删除全部选中对象，而 `Edit node`、`Generate selected node` 只针对右键的那个节点。右键空白画布不会改变当前选择。
- `Generate selected node` 与工具栏共用同一个动作，所以只有在恰好选中一个 `UI Page`、`Logic Module` 或 `Decision` 节点时才可用，其他类型显示为不可用。
- 从空白画布菜单添加节点时，新节点出现在右键点击的位置，而不是画布中央。
- `Select All` 选中画布上的全部节点和连线；`Fit View` 按全部节点范围调整视图并保留 25%–300% 的缩放限制；`Reset View` 恢复 100% 缩放和初始位置。

### 5.7 恢复面板与使用工具栏

关闭 `Properties / 属性` 或 `Build and Export / 构建与导出` 面板后，可以从 `View / 视图` 菜单重新显示。`Reset Layout / 重置布局` 会将属性面板恢复到右侧、构建与导出面板恢复到底部，并显示这两个面板；该操作不会修改蓝图内容或当前界面语言。

主工具栏中的 `Build / 构建` 和 `Export / 导出` 与底部面板按钮执行同一操作，使用相同的工作目录、构建目录、导出目录和 CMake 参数。

## 6. 运行离线登录示例

离线示例是目前验证完整生成链路最直接的方法。它会读取 `tests/fixtures/login-demo/blueprint.json` 和固定 Fake AI 响应，创建工程、接受候选、构建、导出并运行导出项目自己的测试，全程不访问模型服务。

只做临时验收：

```powershell
ctest --test-dir $buildRoot -C Debug -R '^end_to_end$' --output-on-failure
```

要保留导出结果，准备一个已存在的空目录：

```powershell
$demoOutput = 'C:\bp-login-demo'
New-Item -ItemType Directory -Path $demoOutput -ErrorAction Stop | Out-Null
$env:BLUEPRINT_DEMO_OUTPUT_DIR = $demoOutput

try {
    ctest --test-dir $buildRoot -C Debug -R '^end_to_end$' --output-on-failure
    if ($LASTEXITCODE -ne 0) { throw 'Login demo acceptance failed' }
} finally {
    Remove-Item Env:BLUEPRINT_DEMO_OUTPUT_DIR -ErrorAction SilentlyContinue
}
```

同一个目录不能重复用于导出。需要再次运行时，应删除旧目录后重新创建，或者换一个新的空目录。导出成功后可以单独构建：

```powershell
$demoBuild = 'C:\bp-login-build'
cmake -S $demoOutput -B $demoBuild -G Ninja `
  -DBUILD_TESTING=ON `
  -DCMAKE_PREFIX_PATH="$qtRoot"
cmake --build $demoBuild
ctest --test-dir $demoBuild -C Debug --output-on-failure --no-tests=error
```

示例账号是 `demo`，密码是 `secret`。它只用于离线测试，不连接真实认证服务。

## 7. 构建和导出已有工作目录

底部 `Build and export / 构建与导出` 面板用于处理已经由 `ProjectScaffolder` 创建好的工作目录。工作目录至少包含 `generated-project/` 和 `generation-manifest.json`；产生候选或导入外部代码后，还会出现相应目录：

```text
workspace/
├─ generated-project/
├─ candidates/             # 有候选批次时出现
├─ external/               # 导入外部代码后出现
└─ generation-manifest.json
```

面板字段的含义：

| 字段 | 填写内容 |
|---|---|
| Workspace root | 包含 `generated-project/` 的工作目录，不是 `generated-project/` 本身 |
| Build directory | 独立的绝对构建目录；不要放在源码目录内部 |
| Empty export directory | 已存在的绝对空目录 |
| CMake executable | `cmake` 或 `cmake.exe` 的完整路径 |
| Configure arguments | CMake 生成器和 Qt 路径等参数 |

MinGW/Ninja 示例参数：

```text
-G Ninja -DCMAKE_PREFIX_PATH=E:/Qt/6.9.3/mingw_64
```

点击 `Build / 构建` 后，程序依次执行 CMake 配置和编译，日志区显示标准输出、错误输出和退出码。点击 `Export / 导出` 会把当前工程复制到空目录，并携带 `generation-manifest.json`、蓝图快照和已复验的外部代码。候选目录和工作目录中的构建产物不会导出。

构建操作会执行工作目录中的 CMake 和源码。来源不明的生成代码应先人工审查。

## 8. AI 设置与人工真实集成

### 8.1 GUI 设置与连接测试

从主窗口打开 `AI > AI Settings... / AI 设置...`。Provider 固定为 `openai-compatible`；默认端点为 `https://api.deepseek.com/chat/completions`，默认模型为 `deepseek-flash`。端点必须是没有用户名、密码、查询串或片段的 HTTPS URL。修改后可先点 `Test Connection / 测试连接`，再决定是否保存非敏感设置。

连接测试只验证当前端点、模型和凭据是否能够完成一个受限请求。成功不表示后续代码一定正确；失败可能来自网络、超时、认证、余额或计费、限流、端点、模型、响应格式或服务暂时不可用。界面不会显示原始服务商响应，也不会保存密钥。

GUI 生成与候选审核复用以下核心入口：

- `OpenAiCompatibleClient`：向 HTTPS Chat Completions 接口发送请求。
- `GenerationService::generate()`：发起异步生成并校验响应。
- `GenerationService::persistCandidate()`：把有效结果保存为候选批次。
- `previewCandidate()`、`acceptCandidate()`、`rejectCandidate()` 和 `cancelCandidate()`：完成候选审核。

真实模型密钥只从当前进程的 `BLUEPRINT_AI_API_KEY` 环境变量读取。在启动编辑器或人工工具的同一个 PowerShell 窗口中临时设置：

```powershell
$env:BLUEPRINT_AI_API_KEY = '<paste the key only in this terminal>'
```

不要把密钥写入 `.env`、PowerShell/CMD 启动脚本、CMake 参数、蓝图、生成工程、候选、测试夹具或其他项目文件。使用完毕后执行 `Remove-Item Env:BLUEPRINT_AI_API_KEY`，关闭终端也会清除该进程环境。密钥疑似泄露时，应立即在服务商控制台撤销并创建新密钥；不要只修改本地字符串。

### 8.2 从窗口生成与审核代码

1. 在底部构建面板填写工作目录的绝对路径。目录必须预先存在；首次使用建议选择专门的空目录，不要使用源码仓库或存放其他重要文件的目录。
2. 完成有效蓝图，例如 `Start → Logic Module → End`，填写模块说明并选中一个可生成节点。可生成类型为 UI Page、Logic Module、Decision；未选中、选中多个节点或选中 Start、End、External Code 时，生成入口不可用。
3. 通过工具栏或 AI 菜单发起生成。窗口显示当前节点和请求状态；生成期间可以取消。真实生成最多等待 180 秒，失败时显示经过脱敏的错误类别，不显示服务商原始响应。
4. 生成成功后会打开候选审核窗口。左侧列出本次返回的文件，右侧对照当前工程内容与候选内容，并以基础逐行高亮标记差异。这不是专业语义 Diff；插入一行也可能使后续多行标为不同。
5. 逐个选择文件：可以接受原候选、编辑候选后接受，或拒绝文件。拒绝不会改写当前工程。接受仅写入所选文件，并记录候选状态和文件哈希。
6. 如果显示 Conflict，说明当前文件与生成基线不一致。仔细检查手工修改，只有明确确认后才会覆盖。审核后文件又被其他程序修改时，旧预览不能继续接受；先刷新预览，再重新检查和确认。
7. 取消剩余候选不会回滚已经接受的文件。候选内容和审核记录保留用于追溯。接受后可按第 7 节自行构建与导出；编辑器不会自动运行模型返回的代码。

生成请求固定绑定发起时的蓝图、节点和工作目录，之后切换选择不会把结果写到另一个节点。修改蓝图语义或工作目录会使进行中的请求和打开的审核失效。仅拖动节点或切换界面语言不会改变生成内容。

工作目录中的骨架和契约必须与蓝图匹配。蓝图变更后旧工作目录不会自动迁移；出现不匹配提示时，请使用新的工作目录。不要手工修改受保护的契约、占位文件或工程骨架以绕过校验。

当前审核窗口面向本次生成，不提供重启后重新加载历史候选的入口。关闭编辑器前应完成审核；未处理候选虽然仍在磁盘，但需要核心 API 才能继续处理。生成工程目前只提供占位主窗口，不会自动连接和执行业务节点。

### 8.3 构建和运行人工集成工具

`manual_ai_integration` 是真实请求的人工验收路径，默认 `BUILD_MANUAL_AI_INTEGRATION=OFF`，因此普通配置和构建不会创建它。准备一个独立的 ASCII 构建目录，并显式开启：

```powershell
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

默认使用 DeepSeek 端点和 `deepseek-flash`。可按服务商当前文档覆盖端点、模型和总等待时间：

```powershell
$manualWorkspace = 'C:\existing-empty-manual-workspace'
New-Item -ItemType Directory -Path $manualWorkspace -ErrorAction Stop | Out-Null
& (Join-Path $manualBuild 'manual_ai_integration.exe') `
  --endpoint 'https://provider.example/v1/chat/completions' `
  --model 'provider-model' `
  --timeout-seconds 90 `
  --workspace $manualWorkspace
```

不传 `--workspace` 时，工具创建隔离的临时工作目录。显式覆盖时，目录必须在调用前已经存在、为空且为绝对路径；从该目录到文件系统根的任一级都不能是 symlink、Windows junction 或其他 reparse point。工具不会创建、清空或复用不合格的覆盖目录，拒绝时不会改动链接目标。成功后的目录不再为空，因此不能原样用于下一次运行。

工具先检查密钥和工作目录边界，再校验内置 Blueprint，调用 `ProjectScaffolder`，从脚手架 IR 和模块契约编译真实提示词；提示词明确把候选限制在 `src/modules/manual_logic/implementation/` 或 `tests/manual_logic/`，并只允许 `.h`、`.hpp`、`.cpp`、`.cc`。随后通过 `OpenAiCompatibleClient` 和 `GenerationService` 请求并严格校验响应，最后调用 `persistCandidate()`。成功时会保留并输出工作目录和候选批次 ID，方便人工检查。它只保存 `active/pending` 候选，从不调用 `acceptCandidate()`，所以 `generated-project` 不会因本次模型回复被自动覆盖。

固定退出码约定：`0` 成功；`2` 参数错误；`3` 缺少环境密钥；`4` 内置蓝图校验失败；`5` 工作目录或脚手架失败；`6` 提示词准备失败；`7` 非超时的网络或服务商失败；`8` 客户端超时、HTTP 408 或工具总等待超时；`9` 模型响应未通过严格校验；`10` 候选持久化失败。输出只包含固定安全摘要，不包含密钥、Authorization 头、完整提示词、原始响应正文，模型返回的节点 ID、文件路径或校验错误也不会回显。

真实请求可能产生服务商费用，即使最终响应校验失败也可能计费。运行前确认账户、模型价格、配额和数据处理政策；遇到认证、余额、限流、模型或端点错误时，先在 GUI 的连接测试中验证相同设置，再查服务商控制台。不要为了“跑通”而放宽 HTTPS、响应结构、路径、大小或候选边界。

模型返回内容必须是项目规定的 JSON 格式，并通过文件数量、大小、扩展名和相对路径校验。允许的源码扩展名是 `.h`、`.hpp`、`.cpp` 和 `.cc`。未经接受的候选不会覆盖工程文件，接受候选也不代表代码已经通过编译或业务验收。

开发者可从以下测试查看完整调用示例：

- `tests/tst_generation_service.cpp`：请求、网络响应和安全校验。
- `tests/tst_project_scaffolder.cpp`：工程骨架与候选生命周期。
- `tests/tst_end_to_end.cpp`：蓝图到导出工程的离线完整链路。

## 9. 外部代码导入

外部代码导入尚未接入窗口。`ExternalCodeImporter::importFiles()` 只复制用户明确选择的 `.h`、`.hpp`、`.cpp` 和 `.cc` 文件，并在 `external/<node-id>/import-manifest.json` 中记录节点契约与文件 SHA-256。

导入后应调用 `verifyImport()` 复验文件和契约。外部模块不会作为 AI 生成目标；相邻模块的提示词只包含人工填写的接口说明，不会把外部源码直接发送给模型。

专项测试：

```powershell
ctest --test-dir $buildRoot -C Debug -R '^external_code_importer$' --output-on-failure
```

## 10. 常用测试

```powershell
# 所有测试
ctest --test-dir $buildRoot -C Debug --output-on-failure

# 画布交互
ctest --test-dir $buildRoot -C Debug -R '^blueprint_scene$' --output-on-failure

# 结构化节点属性编辑
ctest --test-dir $buildRoot -C Debug -R '^node_properties_editor$' --output-on-failure

# 中英文切换
ctest --test-dir $buildRoot -C Debug -R '^language_switch$' --output-on-failure

# AI 请求与响应校验
ctest --test-dir $buildRoot -C Debug -R '^generation_service$' --output-on-failure

# 工程骨架与候选流程
ctest --test-dir $buildRoot -C Debug -R '^project_scaffolder$' --output-on-failure

# 构建、导出和恢复
ctest --test-dir $buildRoot -C Debug -R '^project_exporter$' --output-on-failure
```

## 11. 常见问题

### 提示缺少 `Qt6*.dll`

启动程序的终端没有包含 Qt 的 `bin` 目录。重新执行环境准备中的 `$env:PATH` 设置，再从同一终端启动。CTest 已自动为测试补充 Qt 运行库路径。

### AutoMOC 或编译器在中文路径报错

把构建目录改到纯 ASCII 路径，例如 `%LOCALAPPDATA%\QtBlueprintGenerator\build`。源码目录仍可保留中文路径。

### 点击 Build 后提示找不到源码

`Workspace root` 必须指向工作目录根部，其中要有 `generated-project/`。不能直接填写导出后的工程目录，也不能填写当前编辑器仓库。

### Export 拒绝目标目录

导出目录必须已存在、为空、使用绝对路径，且不能与工作目录重叠。为避免误覆盖，程序不会导出到非空目录。

### 无法生成，或提示工作目录与蓝图不匹配

先确认正在运行最新构建，并检查是否只选中了一个可生成节点、蓝图是否有效、工作目录是否为已存在的绝对路径。已有工作目录只接受与其契约一致的蓝图；修改蓝图后可改用新目录。认证失败或缺少密钥时，在设置好环境变量的终端中重新启动编辑器，再检查 AI 设置。

## 12. 相关文档

- [README](../README.md)
- [MVP 实施计划](mvp-implementation-plan.md)
- [实现进度](implementation-progress.md)
- [中英文界面切换计划](plans/2026-09-09-bilingual-ui.md)
- [C++/Python 混合开发后续规划](multilanguage-development-design.md)
