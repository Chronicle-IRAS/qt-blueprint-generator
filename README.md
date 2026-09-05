# Qt Blueprint Generator

一个面向 Qt 6 Widgets 项目的可视化蓝图编辑器。用户通过节点、端口和有向连线描述应用结构，系统对蓝图执行静态校验，并在后续阶段将其编译为稳定的中间表示和 AI 生成提示词，最终生成、审核、构建并导出 C++17/CMake 项目。

## 当前状态

项目当前已完成 MVP Task 1 至 Task 6：

- Qt 6 Widgets / C++17 / CMake 工程骨架和 Qt Test 测试环境。
- 蓝图领域模型及 `blueprint.json` 序列化往返。
- 节点 ID、流程结构、可达性、环、Decision 分支和外部代码路径校验。
- 六类节点的交互式画布、端口、有向连线、Decision 标签、属性编辑、缩放、框选及撤销/重做。
- 确定性 IR 编译、合法 C++ 命名空间、最小邻接上下文，以及项目级和模块级提示词模板。
- 异步 AI 客户端、离线 Fake、OpenAI-compatible HTTPS 请求，以及模型响应的 JSON、路径、扩展名和大小校验。

Task 7 至 Task 10 尚未开始。Task 6 提供核心服务 API，尚未接入生成操作界面；候选结果目前只保存在内存中。候选文件落盘与逐文件接受、工程骨架、构建验证和导出将在后续 Task 实现。

## MVP 工作流

```text
创建项目 → 绘制并校验蓝图 → 编译 IR 与提示词
        → 逐模块生成候选 → 人工审核 → 构建验证 → 导出项目
```

蓝图是架构和模块契约的唯一主数据源。布局信息独立保存。完整工作流中，AI 结果经校验后先进入候选目录，未经用户确认不得覆盖项目文件。

## 环境要求

- Windows 10/11
- Qt 6 Widgets、Network 和 Qt Test（当前验证版本：Qt 6.9.3）
- 支持 C++17 的编译器（当前验证版本：MinGW 13.1）
- CMake 3.22 或更高版本
- Ninja

## 构建与测试

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

## AI 生成服务（Task 6）

`GenerationService::generate()` 接收模块提示词、预期节点 ID、绝对候选目录和可选限额，返回请求 UUID，通过成功或失败信号异步通知调用者。成功结果包含摘要、文件内容、规范化相对路径和候选目标路径；任何文件未通过校验时，整个响应都会被拒绝。

- `FakeAiClient` 可预设成功响应或错误，用于离线测试与演示。
- `OpenAiCompatibleClient` 构造时接收完整的 HTTPS Chat Completions 地址和模型名称；每次请求从本机 `BLUEPRINT_AI_API_KEY` 环境变量读取密钥。当前没有设置界面或配置持久化，也尚未进行真实服务商联调。
- 默认限额：模型 JSON 1 MiB、单文件 UTF-8 内容 256 KiB、总文件内容 1 MiB、最多 32 个文件、HTTP 响应体 2 MiB；默认绝对超时为 30 秒。
- 允许 `.h`、`.hpp`、`.cpp`、`.cc`，拒绝绝对路径、穿越、重复路径、非法字段及现有目录链接越界。Task 7 写盘前必须再次校验物理目录边界。

全部自动化测试使用 Fake 或离线网络替身，不需要 API 密钥，也不产生模型调用费用。单独运行：

```powershell
ctest --test-dir build -C Debug -R '^generation_service$' --output-on-failure
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
│  └─ generation/       # IR、提示词编译与安全响应解析
└─ tests/               # Qt Test 自动化测试
```

`src/workspace/` 将随后续 MVP Task 逐步加入。

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
