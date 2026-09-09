# 项目说明与使用指南

## 1. 项目用途

Qt Blueprint Generator 是一个 Qt 6 Widgets 蓝图编辑器。它用节点和有向连线描述桌面应用的页面、业务模块、条件分支及已有代码，再把蓝图编译成稳定的中间表示和 AI 提示词。生成结果先进入候选区，人工确认后才能写入工程。

当前 MVP 使用 C++17、Qt 6 和 CMake。它已经具备蓝图模型、静态校验、交互画布、提示词编译、OpenAI-compatible 客户端、工程骨架、候选审核、外部代码导入、构建和导出的核心实现，并有一条离线登录示例验证完整链路。

## 2. 当前能直接使用的功能

| 功能 | 当前入口 | 状态 |
|---|---|---|
| 创建、移动和删除节点 | 编辑器窗口 | 可直接使用 |
| 创建带标签的有向连线 | 编辑器工具栏 | 可直接使用 |
| 编辑节点名称、说明、端口、约束和验收标准 | 右侧属性面板 | 可直接使用 |
| 撤销、重做、框选和缩放 | 编辑器窗口 | 可直接使用 |
| 恢复属性与构建/导出面板 | `View / 视图` 菜单 | 可直接使用 |
| 中英文界面切换 | `Language / 语言` 菜单 | 可直接使用并记住选择 |
| 构建已准备好的工作目录 | 底部构建面板 | 可直接使用 |
| 导出已准备好的工作目录 | 底部导出面板 | 可直接使用 |
| 蓝图 JSON 读写与校验 | C++ API、自动化测试 | 尚未接入打开/保存按钮 |
| AI 请求、候选预览和接受/拒绝 | C++ API、自动化测试 | 尚未接入窗口 |
| 工程骨架创建、外部代码导入 | C++ API、自动化测试 | 尚未接入窗口 |

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

正常情况下会运行 12 项 CTest。测试使用 Fake AI 或离线网络替身，不读取真实 API 密钥，也不会产生模型费用。

以后重新编译只需在已经设置好 `PATH` 的终端运行：

```powershell
cmake --build $buildRoot
& (Join-Path $buildRoot 'BlueprintEditor.exe')
```

## 5. 编辑蓝图

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

单击一个节点，右侧 `Properties / 属性` 面板会显示可编辑字段。修改后点击 `Apply / 应用`。同时选中多个节点时，属性面板不会修改其中任何一个节点。

输入和输出必须是 JSON 数组，每个端口都要包含 `name`、`type` 和 `description`：

```json
[
  {
    "name": "username",
    "type": "QString",
    "description": "用户名"
  }
]
```

约束和验收标准也是 JSON 数组，但数组元素是字符串：

```json
[
  "只使用 Qt 6 Widgets",
  "不得记录明文密码"
]
```

字段留空表示空数组。JSON 格式错误时，状态栏会显示提示，修改不会写入节点。

### 5.3 创建连线

1. 如果需要标签，先在工具栏的 `Edge label / 连线标签` 输入框填写。
2. 点击 `Connect / 连接`。
3. 先点击来源节点，再点击目标节点。
4. 按 Esc 或点击 `Cancel connection / 取消连接` 可以退出连接模式。

连线方向由点击顺序决定。条件分支的两条出边应分别使用小写 `true` 和 `false`；其他连线通常留空。系统拒绝自连接，完整流程是否合法需要再由蓝图校验器判断。

### 5.4 删除与撤销

选中一个或多个节点后点击 `Delete / 删除`。删除节点时，与它相连的边也会删除。工具栏和 `Edit / 编辑` 菜单都提供 Undo、Redo；移动节点、修改属性、创建连线和删除节点均可撤销。

### 5.5 切换语言

打开 `Language / 语言` 菜单：

- `English / 英语` 对应语言代码 `en`
- `Chinese / 中文` 对应语言代码 `zh_CN`

切换立即生效，不需要重启。显式选择会写入本机 `QSettings`，下次启动自动恢复。切换语言只修改编辑器界面，不会翻译已有节点名称、蓝图 JSON、提示词、模型回复、构建日志或生成源码。

### 5.6 恢复面板与使用工具栏

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

## 8. AI 服务的使用边界

窗口目前没有“生成代码”按钮、服务商设置或候选差异窗口。AI 功能已经作为核心 API 实现，主要入口是：

- `OpenAiCompatibleClient`：向 HTTPS Chat Completions 接口发送请求。
- `GenerationService::generate()`：发起异步生成并校验响应。
- `GenerationService::persistCandidate()`：把有效结果保存为候选批次。
- `previewCandidate()`、`acceptCandidate()`、`rejectCandidate()` 和 `cancelCandidate()`：完成候选审核。

真实模型密钥只从当前进程的 `BLUEPRINT_AI_API_KEY` 环境变量读取：

```powershell
$env:BLUEPRINT_AI_API_KEY = '<API key>'
```

接口地址和模型名由调用代码传给 `OpenAiCompatibleClient`，必须使用有效 HTTPS 地址。密钥不会写入蓝图、候选清单或日志。使用 DeepSeek 等 OpenAI-compatible 服务时，应从服务商当前文档取得 Chat Completions 地址和模型名；本项目没有把这些值硬编码在仓库中。

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

### 设置了密钥但窗口里没有 AI 按钮

这是当前版本的正常限制。设置环境变量只为 `OpenAiCompatibleClient` 提供密钥，窗口尚未连接 AI 和候选审核服务。

## 12. 相关文档

- [README](../README.md)
- [MVP 实施计划](mvp-implementation-plan.md)
- [实现进度](implementation-progress.md)
- [中英文界面切换计划](plans/2026-09-09-bilingual-ui.md)
- [C++/Python 混合开发后续规划](multilanguage-development-design.md)
