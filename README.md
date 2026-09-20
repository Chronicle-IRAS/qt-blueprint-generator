# Qt Blueprint Generator

Qt Blueprint Generator 是一个面向 Qt 6 Widgets 项目的可视化蓝图编辑器。用户通过节点、端口和有向连线描述应用结构，系统负责校验蓝图、编译中间表示和 AI 提示词，并管理生成工程、候选代码、构建与导出。

蓝图是项目结构和模块契约的主数据源。AI 返回的代码不会直接覆盖工程文件，而是先进入候选区，经过预览和人工确认后再写入。

## 核心能力

- 六类蓝图节点：Start、End、UI Page、Logic Module、Decision 和 External Code。
- 交互式画布：节点拖动、框选、缩放、端口拖拽有向连线、分支标签，以及通过结构化表格编辑节点属性并支持撤销/重做。
- 蓝图 JSON 序列化和静态校验，包括节点 ID、流程结构、可达性、环和 Decision 分支检查。
- 确定性中间表示、合法 C++ 命名空间，以及项目级和模块级提示词。
- OpenAI-compatible 异步客户端、模型响应校验和候选代码生命周期管理。
- 绑定蓝图、节点和工作目录快照的生成控制器，支持取消、脱敏错误和候选持久化，并拒绝过期结果。
- GUI 中可配置非敏感 AI 参数并执行低成本连接测试；默认兼容 DeepSeek Chat Completions。
- Qt/CMake 工程骨架、公共契约、生成记录及文件 SHA-256 校验。
- 外部 C/C++ 代码的黑盒导入、接口契约绑定和完整性复验。
- 独立 CMake 构建、日志采集和空目录导出。
- 英文与简体中文界面运行时切换。
- 可恢复的 Properties、Build and Export 面板，以及复用现有构建与导出操作的工具栏入口。

## 使用边界

编辑器窗口已经支持画布编辑、从输出端口拖拽创建连线、双击节点直接修改属性、右侧 Inspector 辅助编辑、语言切换、AI 服务设置和连接测试，以及对已准备工作目录的构建和导出。两处属性入口使用相同的结构化控件，输入和输出端口可以逐行填写名称、类型和说明，约束与验收标准可以逐项增删。View 菜单可以重新显示 Properties、Build and Export 面板，Reset Layout 可恢复默认停靠区域和可见性。

蓝图打开和保存、工程骨架创建、代码生成、候选差异审核及外部代码导入已有核心 API 和自动化测试，但尚未接入窗口。因此，关闭编辑器会丢失当前画布内容；AI Settings 只负责非敏感参数和连接测试，不会让窗口出现代码生成或自动接受按钮。

项目目前生成 C++17、Qt 6 Widgets 和 CMake 工程。Python 等其他实现语言属于后续演进范围。

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
│  └─ workspace/        # 外部代码导入、构建和导出
├─ tools/               # 需显式启用的人工 AI 集成工具
└─ tests/               # Qt Test 自动化测试和离线示例
~~~

## AI 设置与人工集成

从窗口的 `AI > AI Settings...` 可设置 OpenAI-compatible HTTPS 端点与模型，并用 `Test Connection` 做一次受限连接测试。默认端点是 `https://api.deepseek.com/chat/completions`，默认模型是 `deepseek-flash`。API 密钥只从启动进程的 `BLUEPRINT_AI_API_KEY` 环境变量读取；不要把密钥放入 `.env`、脚本、蓝图、项目文件或 CMake 参数。

真实代码生成仅提供显式启用的命令行验收路径，默认构建不会产生该程序：

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
- [MVP 实施计划](doc/mvp-implementation-plan.md)
- [实现进度](doc/implementation-progress.md)
- [中英文界面切换计划](doc/plans/2026-09-09-bilingual-ui.md)
- [C++/Python 混合开发后续规划](doc/multilanguage-development-design.md)

## 许可

本仓库目前尚未添加开源许可证。未经仓库所有者明确授权，不代表允许复制、分发或商用。
