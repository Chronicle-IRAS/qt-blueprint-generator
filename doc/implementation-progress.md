# 实现进度

更新时间：2026-09-04（Asia/Shanghai）

## 当前状态

- 当前开发分支：`feature/blueprint-editor-mvp`
- 设计基线：`doc/mvp-implementation-plan.md`
- `doc/multilanguage-development-design.md` 为 MVP 完成后的后续规划，不影响当前 Task 1 至 Task 10。
- Task 1「建立可构建、可测试的 Qt 工程」已完成并通过规格与代码质量审查。
- Task 2「蓝图领域模型与 JSON 往返」已完成并通过规格与代码质量审查。
- Task 3「蓝图静态验证」已完成并通过规格与代码质量审查。
- Task 4「实现节点画布」已完成并通过规格与代码质量审查。
- 已按约定停在 Task 5 开始前，等待切换智能体模式后继续。
- Task 5 至 Task 10 尚未开始。

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

## 验证记录

- Qt：`E:\Qt\6.9.3\mingw_64`
- 编译器：`E:\Qt\Tools\mingw1310_64`
- 生成器：Ninja
- CMake 配置：通过。
- 完整构建：通过。
- CTest：`4/4 passed`（smoke、blueprint_document、blueprint_validator、blueprint_scene）。
- `blueprint_scene`：连续重复运行 20 次通过。
- 父进程 `PATH` 不包含 Qt 运行库目录时，CTest 仍可通过。
- `git diff --check`：通过。
- Task 3 最终质量复核：Approved（无 Critical、无 Important）。
- Task 4 规格复核：通过。
- Task 4 最终质量复核：Approved（无 Critical、无 Important）；剩余 3 项均为 Minor。

## Task 4 后续 Minor

- 让“开始/完成/取消连线”的状态提示与实际连线状态保持同步。
- 统一画布与 `BlueprintValidator` 对首尾空白 ID 的规范化策略。
- 加强多选属性禁用、真实框选，以及交错入边/出边删除顺序的 UI 回归测试。

## 恢复工作

1. 切换到 `feature/blueprint-editor-mvp`。
2. 在开始实施文档 Task 5 前切换智能体模式。
3. 从 Task 5「实现 IR 与提示词编译」开始，继续遵循测试先行流程。
4. 每个 Task 完成后运行相关测试与全量 CTest，创建独立提交，并同步当前开发分支到远端仓库。

## 约束提醒

- 每次改动必须同步新增或更新测试。
- 每个实施任务必须独立提交。
- 每个 Task 完成后必须同步远端；禁止强制推送或改写远端历史，同步受阻时记录 Issue 和进度说明。
- 不提交 `build/` 构建产物。
- API 密钥仅从 `BLUEPRINT_AI_API_KEY` 读取，不写入文件或日志。
