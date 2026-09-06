# 实现进度

更新时间：2026-09-06（Asia/Shanghai）

## 当前状态

- 当前开发分支：`feature/task7-scaffold-candidates`，直接基于远端 `main` 的 `a55e5ee`（Task 6 / PR #6）。
- 当前 worktree：`C:\Users\Lenovo\.config\superpowers\worktrees\project\task7-scaffold-candidates`。
- 设计基线：`doc/mvp-implementation-plan.md`
- `doc/multilanguage-development-design.md` 为 MVP 完成后的后续规划，不影响当前 Task 1 至 Task 10。
- Task 1「建立可构建、可测试的 Qt 工程」已完成并通过规格与代码质量审查。
- Task 2「蓝图领域模型与 JSON 往返」已完成并通过规格与代码质量审查。
- Task 3「蓝图静态验证」已完成并通过规格与代码质量审查。
- Task 4「实现节点画布」已完成并通过规格与代码质量审查。
- Task 5「实现 IR 与提示词编译」已完成并通过规格与代码质量审查。
- Task 6「实现 AI 客户端与安全响应解析」已完成并通过规格与代码质量复核。
- Task 6 的 PR #6 已经用户授权合入 `main`。
- Task 7「实现确定性工程骨架与候选代码流程」主体实现完成，正在执行边界测试与独立复核。
- 同步目标：`origin/feature/task7-scaffold-candidates`；通过普通推送和独立 PR 交付。
- Task 8 至 Task 10 尚未开始。开始 Task 10 前必须暂停并提醒用户切换 Agent 模式。

## 已完成提交

- `ee3bf88 chore: scaffold Qt blueprint editor`
  - Qt 6 Widgets / C++17 / CMake 工程骨架。
  - 空 `MainWindow`、应用入口与 Qt Test 烟雾测试。
  - 中文路径下使用相对输出路径生成测试 MOC 文件。
- `3cdc402 fix: provide Qt runtime for tests`
  - CTest 自动把 Qt 运行库目录前置到测试进程的 `PATH`。
  - 修复直接执行测试时找不到 `Qt6Test.dll` 的问题。
- `0db22cf feat: add blueprint model and serialization`
  - 实现蓝图节点、边、文档模型及 JSON 序列化往返。
- `239e228 fix: reject unsupported blueprint schema versions`
  - 严格拒绝不受支持的蓝图 schema 版本。
- `9b175fe feat: validate blueprint structure`
  - 实现节点、边、起止节点、度数、可达性、环、Decision 分支与外部代码校验。
- `af7d876 test: cover non-end outgoing validation`
  - 补充非 End 节点出边规则的规格覆盖。
- `5f7f9b5 fix: harden blueprint graph validation`
  - 修复悬空边污染拓扑、重复 ID 归属和递归 DFS 栈溢出。
  - 增加长链图及 Windows junction 越界回归测试。
- `664e51b fix: close external code path escapes`
  - 校验项目根、`external` 根和节点目录的最终物理路径。
  - 扫描全部受支持源码，拒绝任一越界符号链接。
- `e4e6417 docs: allow hybrid-language implementation`
  - 补充 MVP 完成后的多语言演进设计，明确 C++ 核心与外部语言扩展的边界。
- `5e87e23 feat: add interactive blueprint canvas`
  - 实现节点、端口、连线、缩放、框选、属性编辑和撤销栈的画布基础能力。
- `f0e2691 fix: complete blueprint canvas interactions`
  - 补齐六类节点、显式源到目标连线、箭头标签与完整属性字段。
- `0fcbff7 fix: support labeled canvas connections`
  - 支持从界面创建带标签的 Decision `true` / `false` 分支。
- `5838e15 fix: harden blueprint canvas state`
  - 修复边坐标刷新、多选拖动、图元边界、撤销后属性同步及不可表示状态处理。
- `5e00d38 fix: preserve canvas interaction invariants`
  - 加固所有画布变更入口、修复 Ctrl/Shift 拖动撤销语义，并隔离布局变化与属性草稿刷新。
- `b38117c feat: compile blueprints into generation prompts`
  - 实现确定性 IR、稳定模块与邻接排序、最小直接邻接上下文，以及项目级和模块级提示词。
- `44930cd fix: harden IR and prompt contracts`
  - 生成合法且非保留的 C++ 命名空间，补全 ExternalCode 邻接说明，并收紧提示词上下文。
- `13747d7 feat: add safe modular AI generation`
  - 异步 `IAiClient`、离线 Fake、OpenAI-compatible HTTPS 客户端和安全响应解析。
  - 严格字段、节点 ID、可移植路径、扩展名、重复路径、UTF-8 字节限额及现有目录链接边界检查。
  - 逐请求绝对超时、网络响应限额和客户端/网络管理器销毁时的请求清理。
- `1b135c9 fix: guard asynchronous AI lifetimes`
  - 为失败信号与网络 abort 的同步重入增加对象存活保护，并修正未知字段测试的假阳性。
- `eb6f8b7 test: make AI lifetime regressions deterministic`
  - 用固定存储重建测试对象，消除回归测试对分配器地址复用的依赖，并保留旧对象失效和禁止多余信号的断言。
- `12e23e5 feat: scaffold projects and manage candidates`
  - 确定性工程骨架、公共契约、完整蓝图快照、生成清单与候选持久化。
  - 逐文件预览/接受/拒绝/取消、人工修改和过期预览保护、写盘前路径与清单验证。
- `65bab28 fix: isolate scaffold namespaces from Qt identifiers`
  - 实际命名空间加 `Blueprint_` 前缀，并同步写入生成 IR，避免 `QWidget` 类和 `signals` 宏冲突。
  - 增加两个实际生成工程的编译回归，以及后续文件提交失败后的回滚验证。

## 验证记录

- Qt：`E:\Qt\6.9.3\mingw_64`
- 编译器：`E:\Qt\Tools\mingw1310_64`
- 生成器：Ninja
- CMake 配置：通过。
- 完整构建：通过。
- 当前 CTest：`8/8 passed`（project_scaffolder、smoke、blueprint_document、blueprint_validator、blueprint_scene、ir_compiler、prompt_compiler、generation_service）。
- Task 6：测试先行，接口缺失、客户端销毁、网络管理器销毁、绝对超时、junction 越界及同步重入场景均有 RED→GREEN 记录。
- `generation_service`：在 `1b135c9` 上连续运行 20 次通过；真实客户端使用离线网络替身测试，未调用外部模型。
- 固定存储测试调整后，在 `eb6f8b7` 及最终 API 注释上重新完整构建并执行 CTest：`7/7 passed`。
- `ir_compiler` 与 `prompt_compiler`：分别连续重复运行 20 次通过。
- `blueprint_scene`：连续重复运行 20 次通过。
- 父进程 `PATH` 不包含 Qt 运行库目录时，CTest 仍可通过。
- `git diff --check`：通过。
- Task 3 最终质量复核：Approved（无 Critical、无 Important）。
- Task 4 规格复核：通过。
- Task 4 最终质量复核：Approved（无 Critical、无 Important）；剩余 3 项均为 Minor。
- Task 5 最终代码复核：Ready to merge（无 Critical、无 Important、无 Minor）。
- Task 6 规格复核：SPEC COMPLIANT。
- Task 6 最终质量复核：两处同步重入存活保护、未知字段测试及固定存储回归均通过，限定复核范围无剩余问题。
- Task 7 规格复核：在 `65bab28` 上 SPEC COMPLIANT；命名空间冲突通过实际 CMake 构建 RED→GREEN 修复。
- Task 7 回滚验证：Windows 下保持第二个目标文件的无删除共享句柄，使后续提交失败；验证前一文件恢复且两个原文件内容不变。
- Task 7 最终独立验证：在 `65bab28` 上完整构建、CTest `8/8 passed`（35.71 秒）；`QWidget` 与 `signals` 两个项目分别实际生成并接受源码及 Qt Test，再配置/构建，各自 CTest `2/2 passed`。

## Task 7 实现与边界

- `ProjectScaffolder::create()` 创建 `generated-project/` 下的 CMake、入口、公共类型、模块契约、测试配置与说明文件，以及工作目录中的 `generation-manifest.json`。
- 蓝图快照按节点与边 ID 排序，文件内容和 SHA-256 不依赖绝对路径或时间；改变蓝图内容后需使用新工作目录，本轮不做既有工程迁移。
- `GenerationService` 增加候选保存、当前/候选预览、逐文件接受（包括编辑后接受）、拒绝和取消 API；状态实现位于 `candidate_store.cpp`，文件访问与事务辅助位于 `workspace_io.cpp`。
- 保存候选时重新校验响应；接受时重新读取当前与候选文件并核对哈希。人工修改、旧候选和过期预览不得静默覆盖当前文件。
- 候选仅可进入节点自己的实现和测试子目录，不能覆盖公共契约、骨架、其他节点或外部代码。目录联接、reparse point、大小写别名、穿越和文件/目录前缀冲突均拒绝。
- 工作目录必须已存在；本地可信单写入者是前提。普通 I/O 失败尝试回滚，不承诺断电/进程崩溃时的多文件原子性，也不防御其他进程恶意并发替换目录。
- 输入输出使用 `QVariantMap`；任意自定义类型文本只作契约元数据。生成应用仍为占位窗口，不包含完整业务流程连接；候选审核 UI 和真实服务商联调尚未接入。
- 生成 IR 的 `project.namespace` 与实际 `Blueprint_` 前缀命名空间一致；后续提示词应使用生成 IR 和具体契约，不能用原始 Task 5 IR 覆盖实际命名空间。
- 独立验收已实际生成并编译六节点骨架；反转节点/边顺序时逐文件内容一致。接受新增源码及 Qt Test 后，生成工程再次配置/构建成功，CTest `2/2 passed`；人工编辑后第二批默认接受被拒绝，编辑内容保持不变。

## Task 6 原有边界

- `generate()` 与 `parseAndValidate()` 的结果仍仅保存在内存中；调用者显式调用 Task 7 的候选保存 API 才会写入文件。
- Task 7 已新增 `candidates/<generation-id>/<node-id>/` 路径组织、文件落盘、哈希与逐文件接受，并在写盘前重新检查目录边界。
- HTTPS 地址和模型由构造参数提供；配置界面、本地设置持久化和真实服务商联调尚未接入。API 密钥只从本机环境变量读取。
- JSON 类型和字段严格检查使用 Qt JSON 解析器；重复对象键沿用 Qt 的解析语义，当前不提供独立的重复键拒绝保证。

## Task 4 后续 Minor

- 让“开始/完成/取消连线”的状态提示与实际连线状态保持同步。
- 统一画布与 `BlueprintValidator` 对首尾空白 ID 的规范化策略。
- 加强多选属性禁用、真实框选，以及交错入边/出边删除顺序的 UI 回归测试。

## 恢复工作

1. Task 7 的代码与构建位于上述独立 worktree；原项目目录和 Task 6 worktree 保留不动。
2. Task 6 已合入远端 `main`；Task 7 分支已从最新 `origin/main` 创建。
3. 继续 Task 7「实现确定性工程骨架与候选代码流程」，遵循测试先行、规格复核和质量复核流程，不提前开始 Task 8。
4. 每个 Task 完成后更新 README 和进度，运行相关测试与全量 CTest，创建提交并同步对应分支到远端。
5. 开始 Task 10 前仍须暂停，提醒用户切换智能体模式。

## 约束提醒

- 每次改动必须同步新增或更新测试。
- 每个实施任务必须独立提交。
- 从 Task 6 开始，每个 Task 都必须从最新远端 `main` 新建独立开发分支。
- 每个 Task 完成后必须同步远端；禁止强制推送或改写远端历史，同步受阻时记录 Issue 和进度说明。
- 不提交 `build/` 构建产物。
- API 密钥仅从 `BLUEPRINT_AI_API_KEY` 读取，不写入文件或日志。
