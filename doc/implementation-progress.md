# 实现进度

更新时间：2026-09-09（Asia/Shanghai）

## 当前状态

- 当前开发分支：`feature/issue-13-port-drag-connections`，基于远端 `main` 的 `4f0bce6`（Issue #16 PR #21）。
- 当前项目路径：`C:\Users\Lenovo\DeskBox\毕业设计相关\毕设\project`。
- MVP 基线：`doc/mvp-implementation-plan.md`；当前实施计划：`doc/plans/2026-09-09-issue-13-port-drag-connections.md`；完整使用说明：`doc/project-usage-guide.md`。
- `doc/multilanguage-development-design.md` 描述 C++/Python 混合实现的后续规划；当前任务和使用指南只说明已经实现的功能。
- Task 1「建立可构建、可测试的 Qt 工程」已完成并通过规格与代码质量审查。
- Task 2「蓝图领域模型与 JSON 往返」已完成并通过规格与代码质量审查。
- Task 3「蓝图静态验证」已完成并通过规格与代码质量审查。
- Task 4「实现节点画布」已完成并通过规格与代码质量审查。
- Task 5「实现 IR 与提示词编译」已完成并通过规格与代码质量审查。
- Task 6「实现 AI 客户端与安全响应解析」已完成并通过规格与代码质量复核。
- Task 6 的 PR #6 已经用户授权合入 `main`。
- Task 7「实现确定性工程骨架与候选代码流程」已完成，通过规格和最终质量复核，完整构建与 CTest `8/8 passed`。
- Task 7 的 PR #7 已由用户合入 `main`，已拉取并确认合并提交 `dda7179`。
- Task 8「实现外部代码黑盒导入」已完成，通过规格和最终质量复核；完整构建、CTest `9/9 passed` 和独立集成链路均通过。
- Task 8 的 PR #8 已由用户合入 `main`，已拉取并确认合并提交 `70a3d9f`。
- Task 9「实现构建验证与项目导出」已完成；主实现为 `6519955`，质量复核修复为 `4bc109e` 和 `c56c843`，规格与最终质量复核均通过。最终专项 Qt Test `28 passed`，最终完整构建和 CTest `10/10 passed`（45.53 秒），独立导出/构建集成验证退出码 0。
- Task 9 的 PR #10 已由用户合并，已确认合并提交 `b89202c`。
- Task 10 离线登录端到端验收已完成，主实现 `42cdd6e`、质量调整 `150c044`；规格与最终质量复核均通过。完整构建与 CTest `11/11 passed`（66.54 秒），导出示例测试 `4/4 passed`；质量调整后的两种导出专项均通过。
- Task 10 的 PR #11 已于 2026-09-09 合入远端 `main`，合并提交为 `43ecdb3`。
- 中英文界面切换已在 `feature/bilingual-ui` 实现：运行时切换、选择持久化、稳定控件标识和完整当前界面翻译均通过测试。
- 中英文界面 PR #12 已合入远端 `main`，合并提交为 `4e2378e`。
- 项目使用说明 PR #20 已合入远端 `main`，合并提交为 `6e16bac`。
- 远端 Issue 按依赖、关键程度和更改幅度排序为 #16、#14、#13、#15、#17、#18、#19；均已标记 `enhancement` 和 `ready-for-agent`。
- #16 已通过 PR #21 合入远端 `main`，合并提交为 `4f0bce6`。
- #14 已通过独立分支创建 PR #22，标题含 `[feat]` 与 `(#14)`，正文含 `Closes #14`；当前等待用户合并。
- #13 已从最新远端 `main` 新建独立分支，开始实现从 Output Port 拖拽到 Input Port 的直接连线交互。

## 已完成提交

- `b31d446 docs: plan issue 16 dock recovery`
  - 记录 #16 的测试先行步骤、实现范围、双语文本和交付约束。
- `2165f85 feat: restore editor docks from view menu`
  - 新增 View 菜单、两项 Dock 恢复入口、默认布局复位和 Build/Export 工具栏入口。
  - Qt Linguist 目录扩展到 60 条完成翻译；GUI 测试覆盖可见性、勾选状态、布局区域、动作复用和中英文切换。
- `75cc61e test: cover dock reset and toolbar build state`
  - 补充 Reset Layout 从隐藏/错位状态恢复，以及工具栏 Build 在同步拒绝、异步运行和失败后的状态同步测试。
  - 突变验证确认删除 Dock `show()` 或异步完成后的 action 恢复都会使测试失败。
- `b25ef46 docs: add project usage guide`
  - 新增 `doc/project-usage-guide.md`，覆盖环境、构建、启动、画布操作、离线示例、工作目录、AI/外部代码使用边界及常见问题。
  - README 改为稳定项目首页，删除 Task、分支、PR 和测试耗时等开发过程记录。
- `8935068 docs: clarify blueprint serialization usage`
  - 明确 `BlueprintSerializer::toJson()` 只负责序列化，文件写入由调用方完成。
- `d1a13c2 docs: plan bilingual UI switching`
  - 记录中英文运行时切换、持久化、翻译资源、测试和交付范围。
- `d281bfb feat: add English and Chinese UI switching`
  - 使用 Qt LinguistTools 和内嵌 `.qm` 资源实现英文与简体中文即时切换。
  - `QSettings` 保存选择；语言变化只重译界面，不改写现有蓝图数据。
  - 新增 `language_switch` 测试，覆盖菜单触发、控件刷新、节点数据保持、重启恢复和非法语言拒绝。
- `676e666 fix: avoid writing default language settings`
  - 启动时只读取并应用已保存语言，不再把未显式选择的默认英文写入设置。
  - 回归测试覆盖默认设置缺席，以及非法语言不会覆盖最后一次有效选择。
- `8456049 docs: record Task 10 acceptance kickoff`
  - 基于 Task 9 合并提交 `b89202c` 建立 Task 10 独立分支并记录基线。
- `42cdd6e test: verify end-to-end blueprint generation`
  - 新增六节点登录蓝图、三模块 Fake 响应和真实构建/测试的端到端验收。
  - 覆盖模型失败不写盘、人工修改默认覆盖拒绝，以及可选的演示导出目录。
- `150c044 test: avoid inspecting rejected demo export destinations`
  - 可选演示导出先由导出器校验目标，不再提前遍历可能被拒绝的目录；成功导出后仍核对全部文件字节。
  - 默认临时导出专项 `1/1 passed`（20.50 秒），指定新空目录导出专项 `1/1 passed`（21.83 秒）。
- `eee76c9 docs: record Task 9 kickoff from merged Task 8`
  - 基于已合并 PR #8 创建 Task 9 独立分支并记录基线。
- `6519955 feat: build and export generated Qt projects`
  - 异步 CMake 服务、构建日志面板、工具链参数输入和空目录暂存导出。
  - 外部源码映射、人工修改保留、路径/清单验证和 Windows 重命名失败清理测试。
- `4bc109e fix: protect build invocation controls`
  - 拒绝覆盖服务所有的 CMake 源码/构建目录和切换执行模式的配置参数。
  - 修复 CMake 程序无法启动时构建按钮被永久禁用，并增加同步失败回归测试。
- `c56c843 fix: retain failed export recovery artifacts`
  - 导出恢复失败时保留原空目标备份和已完成导出，并在错误中给出准确恢复路径。
  - 增加内部恢复状态决策的 RED→GREEN 回归测试。
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
- `3fc458b feat: import external code as black-box modules`
  - 明确选择并复制 `.h`、`.hpp`、`.cpp`、`.cc`，保留嵌套路径和原始字节。
  - 按节点保存完整人工接口契约、契约 SHA-256、逐文件 SHA-256 和只读生成策略。
  - 拒绝路径穿越、大小写别名、链接、未跟踪目标、替换及清单篡改；外部节点不进入 AI 候选覆盖范围。
- `47d346a fix: require verified external import manifests`
  - 外部节点蓝图校验一律要求有效导入清单，不能通过删除清单降级到无哈希校验。
  - 增加“删除清单”和“删除清单后替换源码”的 RED→GREEN 回归测试。

## 验证记录

- #13 基线：全新 ASCII 构建目录配置成功，`blueprint_scene` `1/1 passed`（0.34 秒）。
- #13 RED：端口拖拽 GUI 合同测试因 `NodeItem::highlightedInputPort()` 与拖拽实现尚不存在而按预期编译失败。
- #16 基线专项：`language_switch|blueprint_scene` `2/2 passed`（1.84 秒）。
- #16 RED：新增 View/Dock/Toolbar 断言后两项测试均按预期失败；GREEN：`2/2 passed`（0.49 秒）。
- #16 Qt Linguist：60 条完成翻译，0 条 unfinished；无 vanished 或 obsolete 条目。
- #16 初次完整构建通过；CTest `12/12 passed`（79.46 秒）。
- #16 `BlueprintEditor.exe` 使用 offscreen 平台插件启动并保持运行 2 秒，启动探测退出码 0。
- #16 质量复核初次结论 Not ready：测试未证明隐藏 Dock 恢复和异步 Build action 状态；补强并完成两项突变验证后，复核结论为 Ready，无剩余 Critical、Important 或 Minor。
- #16 复核修复后的最终完整构建通过；CTest `12/12 passed`（71.88 秒）。
- #16 合入项目说明 PR #20 并解决文档冲突后，完整构建通过；CTest `12/12 passed`（68.03 秒）。
- 项目说明任务使用全新纯 ASCII 构建目录完成 CMake 配置和完整构建；CTest `12/12 passed`（83.13 秒）。
- README 与使用指南的本地 Markdown 链接全部可解析；`git diff --check` 通过。
- 项目说明最终只读复核结论为 Ready，无 Critical 或 Important；唯一序列化 API 表述 Minor 已修正。
- Qt：`E:\Qt\6.9.3\mingw_64`
- 编译器：`E:\Qt\Tools\mingw1310_64`
- 生成器：Ninja
- CMake 配置：通过。
- 完整构建：通过。
- Task 10 交付时 CTest：`11/11 passed`（66.54 秒），包含新增 end_to_end 与此前 10 项测试。
- 中英文界面任务基线：远端 `main` 完整构建和 CTest `11/11 passed`（83.88 秒）。
- `language_switch`：RED 阶段因 `MainWindow::currentLanguage()` 与 `setLanguage()` 不存在而编译失败；GREEN 阶段 `1/1 passed`（0.21 秒）。
- 默认语言设置副作用回归：新增断言后 RED，修复后 `language_switch` `1/1 passed`（0.21 秒）。
- Qt Linguist 源码扫描：发现 58 条当前界面文本，`58 finished`，无 unfinished、vanished 或 obsolete 条目。
- 中英文界面任务最终完整构建：通过；CTest `12/12 passed`（81.91 秒）。
- `BlueprintEditor.exe` 使用 `offscreen` 平台插件启动并保持运行 2 秒，启动探测退出码 0；仅终止本次探测创建的进程。
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
- Task 7 最终质量复核：Ready to merge，Critical 与 Important 均无；已知 Minor 为完整 SHA-256 测试目标名在 Windows 长构建目录中的路径长度警告，README 已说明使用较短构建目录。
- Task 8 初始 RED：占位接口下导入测试 `2 passed, 25 failed`；初始 GREEN 为 `27/27 passed`，补充六类 Windows junction 后为 `33/33 passed`。
- Task 8 清单降级回归：删除清单及删除后替换源码初始 `2 passed, 2 failed`，修复后导入测试 `35/35 passed`。
- Task 8 独立集成验收：导入 → 蓝图校验 → IR/提示词 → 工程骨架 → 普通候选接受完整链路退出码 0，外部源码及导入清单保持不变。
- Task 8 规格复核：SPEC COMPLIANT；确认缺失清单不再回退到未绑定的直接文件校验。
- Task 8 最终质量复核：Ready to merge，无 Critical、无 Important；非阻断 Minor 为幂等测试使用文件修改时间，在极粗时间分辨率文件系统上敏感度可能降低。
- Task 9 规格复核：SPEC COMPLIANT；确认构建、日志、导出映射、失败清理和范围边界符合计划。
- Task 9 质量复核 RED：14 类受保留 CMake 参数被错误接受，且不存在的 CMake 程序同步失败后构建按钮仍被禁用，共 `15` 个预期失败。
- Task 9 质量修复 GREEN：参数守卫专项 `16 passed`，缺失工具专项 `3 passed`，完整专项二进制 `27 passed, 0 failed, 0 skipped`；完整 CTest `10/10 passed`（46.12 秒）。
- Task 9 恢复分支 RED→GREEN：恢复失败场景最初错误启用导出副本删除，定向测试 `1 failed`；修复后定向 `3/3 passed`，完整专项 `28 passed, 0 failed, 0 skipped`。
- Task 9 最终质量复核：Ready to merge，无 Critical、无 Important、无 Minor；交付前重新完整构建并执行 CTest `10/10 passed`（45.53 秒），独立导出工程实际 CMake 构建退出码 0。

## Task 10 验收记录与边界

- 新 worktree 基线：`b89202c`；完整配置、构建和 CTest `10/10 passed`（46.52 秒）。
- Task 10 专项 Qt Test：3 个业务用例与 init/cleanup 共 `5 passed`，无失败、无跳过；XML 证据位于 `build/task10-end-to-end-results.xml`。
- 实际执行加载 → 校验 → IR/契约提示词 → Fake 异步生成 → 预览 → 逐文件接受 → 导出 → CMake 配置/构建 → 导出项目 CTest；准确发现 3 项模块 Qt Test 和 1 项骨架测试，全部通过。
- 全量构建与 CTest `11/11 passed`（66.54 秒）。设置 `BLUEPRINT_DEMO_OUTPUT_DIR` 后保留的导出位于 `build/login-demo-export`；再次导出到该非空目录得到预期拒绝，24 个文件的 SHA-256 均不变，负向测试日志位于 `build/task10-nonempty-results.xml`。
- 随后的 `150c044` 仅移除测试在目标校验前的多余目录快照；重新构建成功，默认临时导出与指定空目录 `build/task10-reviewed-export` 的两次专项测试均通过，各自实际构建导出项目并执行其测试。
- 规格复核：SPEC COMPLIANT。最终质量复核：Ready to merge，唯一 Minor 已在 `150c044` 解决，无剩余 Critical、Important 或 Minor。
- 窗口交互探针使用 Qt 鼠标事件验证节点拖拽、工具栏按来源/目标连线、属性 Apply 与 Undo/Redo；随后通过窗口按钮实际配置、构建和导出，全部断言通过，退出码 0。
- 已查看窗口渲染截图，节点标题、连线箭头、属性面板和构建/导出成功日志显示正常。截图位于忽略的 `build/task10-ui-check/editor-acceptance.png`，不是已提交的产品资源。
- 候选预览验证当前文件缺失与候选内容的差异，确认接受前工程文件未写入，显式接受后构建成功；候选审核仍是 API 功能，尚无审核窗口，不能据此宣称完成该窗口的人工验收。
- 登录示例使用固定 Fake 响应；真实模型联调和应用入口自动串接业务流程不属于本次验收结果。

## Task 9 实现与边界

- `BuildService` 使用独立 `QProcess` 异步执行 CMake 配置、构建，分别收集 stdout、stderr 和退出码；配置失败不启动构建，同一实例拒绝并发请求，不自动运行生成应用。
- 主窗口新增构建/导出面板，工作目录指向包含 `generated-project/` 的根目录；构建输出需放在源码树之外。构建是显式执行受信任工程，不是模型代码沙箱。
- 导出将 `generated-project/` 展平至目标根部，另带生成清单及原始蓝图；经过契约、哈希和目录清单复验的外部代码映射到 `src/external/<node-id>/`。
- 当前实现文件的人工修改按原始字节保留；生成清单作为历史记录复制，不冒充当前文件全部重新生成或已通过业务验收。候选、工作目录构建产物和其他非映射文件不参与导出。
- 目标必须是现有空目录，且不得与工作目录重叠。先在同级唯一临时目录复制并验证哈希，再通过目录重命名提交；普通失败清理暂存并尝试恢复目标。可信本机单写入者、非断电原子性边界与既有工作目录服务一致。
- Task 9 交付时已按用户要求暂停并提醒切换模式；用户确认合并后，于 2026-09-08 明确要求开始 Task 10。

## Task 8 实现与边界

- `ExternalCodeImporter::importFiles()` 仅复制调用者明确选择的可移植相对路径，接受 `.h`、`.hpp`、`.cpp`、`.cc`，保留嵌套目录及每个文件的原始字节。
- `external/<node-id>/import-manifest.json` 严格记录完整 `ExternalCode` 节点契约、契约哈希、文件清单与哈希，以及 `readOnly` 生成策略；同内容重复导入不写盘。
- `verifyImport()` 重新检查绑定契约、全部文件哈希和目录清单。缺失、额外、替换或篡改内容，以及链接、reparse point 和大小写别名都会使蓝图校验失败。
- Task 8 起外部节点必须由导入器建立并保留有效清单；单独放置源文件不再构成有效外部节点。删除或替换操作由后续调用方显式处理，并须重新导入、重新校验。
- 外部节点仍不属于可生成模块，AI 提示词只接收蓝图中的人工接口契约。生成候选的节点和路径白名单不会接受外部节点或 `external/` 目标。
- 当前仅提供核心服务 API，尚未接入窗口；不自动分析外部源码、修改源文件权限、覆盖已有导入，亦不负责 Task 9 的构建和导出。

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

1. 当前项目以 `C:\Users\Lenovo\DeskBox\毕业设计相关\毕设\project` 为准，分支为 `feature/issue-16-dock-recovery`。
2. 项目说明 PR #20 已合入远端 `main`（`6e16bac`）；#16 分支已普通合并该提交并处理文档冲突。
3. #16 已通过 PR #21 交付，等待用户审阅与合并；后续 Issue 使用各自独立分支。
4. 每个 Task 完成后更新 README 和进度，运行相关测试与全量 CTest，创建提交并同步对应分支到远端。
5. 不为 Git 身份名称差异再次创建 Issue；原 Issue 由用户主动删除，已明确要求不要重建。

## 约束提醒

- 每次改动必须同步新增或更新测试。
- 每个实施任务必须独立提交。
- 从 Task 6 开始，每个 Task 都必须从最新远端 `main` 新建独立开发分支。
- 每个 Task 完成后必须同步远端；禁止强制推送或改写远端历史，同步受阻时记录 Issue 和进度说明。
- 不提交 `build/` 构建产物。
- API 密钥仅从 `BLUEPRINT_AI_API_KEY` 读取，不写入文件或日志。
