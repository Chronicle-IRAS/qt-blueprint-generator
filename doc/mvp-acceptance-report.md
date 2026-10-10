# MVP 验收报告

验收日期：2026-10-10（Asia/Shanghai）。当前验收基线：远端 `main`，`33503f85308a93d7ab68b96db9031faa42449f1d`。PR #45、#46、#47、#48 已合并；相关 Issues #17、#18、#32 已关闭。

验收依据为 [MVP 实施计划](mvp-implementation-plan.md) 第 10 节、[使用指南](project-usage-guide.md) 与已合并功能。本次范围是原有工程骨架、模块生成和人工审核闭环；下一阶段范围见 [项目路线](project-roadmap.md)。

结论：原 MVP 的 14 项验收标准已通过。Candidate Review Unicode 缺陷已修复，随后一个简单 LogicModule 的真实 DeepSeek 生成、候选接受、工作区构建与测试、导出及导出工程复验全部成功，未发现阻断进入 MVP 发布流程的问题。`v0.1.0-mvp` 的发布收尾 PR、最终检查和人工合并仍是创建 Tag / Pre-release 的前置条件；本报告不表示版本已发布。

## 原 MVP 标准逐项核对

| # | 原标准 | 实现与测试证据 | 当前状态 |
|---|---|---|---|
| 1 | 六类节点和文字字段编辑 | `blueprint_scene` 的 `mainWindowCreatesEveryNodeType`、`editingAllNodeFieldsIsOneUndoableCommand`，以及 `node_properties_editor` 字段往返 | 通过 |
| 2 | 创建、删除和显示有向连线 | `blueprint_scene` 的 `connectingNodesUpdatesDocumentAndSupportsUndo`、`edgeShowsDirectedLabelAndUsesPortAnchors`、`clickingAnEdgeSelectsItAndDeleteKeepsItsNodes` | 通过 |
| 3 | 保存并无损重新加载 | `blueprint_document` 完整 JSON 往返；`blueprint_project_store` 的 `roundtripPreservesSemanticsAndPositions`；`blueprint_persistence` 的 `roundTripRestoresGraphPropertiesAndLayout` | 通过 |
| 4 | 发现全部最小结构错误 | `blueprint_validator` 覆盖 ID、Start/End、度数、引用、可达性、环、Decision 分支、模块说明和 ExternalCode 完整性 | 通过 |
| 5 | 不同布局下 IR 一致 | `ir_compiler` 的 `layoutChangesDoNotAffectIr`、排序测试；`blueprint_project_store` 的 `layoutSignalsAndUndoNeverChangeSemanticIr` | 通过 |
| 6 | Fake AI 离线端到端 | `end_to_end` 实际加载、校验、编译 IR、生成三个模块、接受六个文件并导出 | 通过 |
| 7 | 真实客户端失败不破坏工程 | `openai_compatible_client` 离线网络替身验证认证、超时、HTTP 和格式错误；`generation_service` 验证失败不写候选；Controller 隔离失败与过期响应，`end_to_end` 验证工作区快照不变 | 通过 |
| 8 | 可生成节点独立候选目录 | `project_scaffolder` 候选批次/节点目录测试；`end_to_end` 按模块持久化并复核候选内容及 manifest | 通过 |
| 9 | 人工修改不被静默覆盖 | `candidate_review_dialog` 冲突确认、旧预览失效及 Unicode 草稿/差异回归；`project_scaffolder` 和 `end_to_end` 哈希保护；Workspace 保存前复查磁盘身份和哈希 | 通过 |
| 10 | 外部黑盒导入并随工程导出 | `external_code_importer` 字节、SHA-256、接口契约与 AI 隔离；`external_code_gui` 导入/重新导入/只读查看；`project_exporter` 外部文件字节一致 | 通过 |
| 11 | 显示 CMake 配置和构建日志 | `project_exporter` 的 BuildService 与 GUI 测试；`end_to_end` 验证两个构建阶段、输出信号和退出码 | 通过 |
| 12 | 完整工程导出到空目录 | `project_exporter` 验证空目录导出、非空/重叠目录拒绝、失败恢复和哈希；`end_to_end` 核对完整导出快照 | 通过 |
| 13 | 编辑器全部 QtTest 和端到端测试通过 | 验收基线 CTest 30/30（240.51 秒）；PR #48 修复后完整构建、CTest 30/30（237.78 秒），Candidate Review 24 项通过；发布收尾复验见下文 | 通过 |
| 14 | 导出示例配置、编译并通过测试 | `end_to_end` 真正调用 CMake 和 CTest，发现并运行导出登录工程的四个测试：4/4 | 通过 |

## 当前 GUI 与生命周期检查

- `blueprint_persistence` 验证 New/Open/Save/Save As 快捷键、节点位置、Undo/Redo 回到保存内容后的 clean state、非法文件和保存失败保留当前蓝图。
- `generation_workflow` 与 `candidate_review_dialog` 验证 Generate、双栏比较、逐文件接受/拒绝、草稿编辑及语义/工作区变化后的上下文失效。
- `workspace_browser` 与 `workspace_main_integration` 验证普通源码编辑与安全保存、普通 Backspace 恢复 clean、Save/Discard/Cancel、蓝图和源码两套未保存提示、Build/Export 预检、失败后继续修改重建、项目切换和 ExternalCode 选择框上下文。
- `language_switch`、`theme_switch` 及各功能 GUI 测试验证中英文和亮暗主题切换；翻译构建完成 249 条，无未完成项。画布局部重绘与 150% 分数 DPI 像素回归实际执行、无跳过。

## 已修复的 Candidate Review Unicode 缺陷

修复前，候选审核窗口用 `QPlainTextEdit::toPlainText()` 更新整份草稿，导致候选字符串中的 NBSP（U+00A0）在用户仅于其他位置追加 `//x` 后被改为普通空格。点击 Accept edited draft 会把该变化写入正式工程，原候选仍保持不变。

独立 GUI probe 在原验收基线 `c009bab882242cc464dc5480c45e2e3c355eb559` 稳定复现：2 passed、1 failed、0 skipped，接受后的字节 `c2 a0` 变为 `20`。这是初轮验收发现的真实缺陷，现已通过 PR #48 修复并合入 `main`。

修复分支：`fix/mvp-candidate-unicode`，从原验收基线创建。草稿更新与 Diff 使用原始文本读取，保留 NBSP 和 U+2028，沿用 Qt 段落边界转 LF 的既有约定；候选接受、哈希和冲突确认流程没有变化。PR #48 的 `main` 合并提交为 `33503f85308a93d7ab68b96db9031faa42449f1d`。

新增六条数据回归，覆盖真实键盘编辑、切换文件/语言后接受、接受原候选、原候选字节保留及 Unicode 差异高亮。测试先检出四处失败，修复后新增用例全部通过；完整 Candidate Review 测试 24 passed、0 failed、0 skipped。修复分支完整构建成功，全部 CTest 30/30 通过（237.78 秒），导出登录示例自身测试再次 4/4 通过。独立 Review 结论 Ready，无未解决问题。最终 30 份 QtTest 日志中无失败，仅有与基线相同的 Windows 大小写别名 GUI 用例跳过。

## 真实 DeepSeek 端到端验收

2026-10-10，在已合并 PR #48 的 `main 33503f8` 上使用独立临时 Workspace 实测。生产 `OpenAiCompatibleClient` 调用 `https://api.deepseek.com/chat/completions`；配置和响应模型均为 `deepseek-flash`，HTTP 200，`finish_reason=stop`。没有使用 Fake AI 或 Mock，也没有修改模型返回的代码。

| 阶段 | 结果与证据 |
|---|---|
| 有效蓝图 | `Start → LogicModule(add_one) → End` 通过生产 BlueprintValidator；仅测试一个简单模块 |
| 真实生成与解析 | 生产 PromptCompiler、脚手架契约、OpenAiCompatibleClient 和 GenerationService 完成一次生成，严格 JSON 校验通过 |
| Candidate / Manifest | 生成 `add_one.h`、`add_one.cpp`、`tst_add_one.cpp` 三个文件；候选原始字节、SHA-256、节点和提示词摘要一致，初始状态为 pending |
| Candidate Review / Accept | 审阅代码后通过生产 CandidateReviewDialog 的 Accept original 控件接受三个文件；正式文件保留原字节，Manifest 三项均为 accepted |
| 工作区 Build / CTest | 生产 BuildService 实际调用 CMake、MinGW/Ninja，配置和编译退出码均为 0；CTest 2/2 通过，包括骨架烟测和模型提供的功能测试 |
| Export / 独立复验 | ProjectExporter 导出到独立空目录，工程文件、蓝图快照和 Manifest 哈希一致；导出工程独立配置、编译退出码均为 0，CTest 再次 2/2 通过 |
| 文件保护 | 受保护文件 SHA-256 未变，导出未混入候选目录或工作区构建产物 |

实际生成请求 1 次，无额外连接探针、自动重试或第二次生成。测试请求限定 `max_tokens=1800`、`thinking.type=disabled`。Provider 返回输入 810 tokens（缓存命中 0、未命中 810），输出 578 tokens，总计 1388 tokens；未查询账单，不能把计价估算视为实际扣费。

模型实现输入加一以及 QVariantMap 的 `execute()` 输入输出，模型提供的 QtTest 覆盖正数、零和执行接口。Candidate Review 使用 QtTest 驱动真实离屏 GUI 控件；这证明生产候选审核流程有效，不代表真人点验 MainWindow Generate 或原生桌面完整流程。生产路径可定位到 `src/ai/openai_compatible_client.cpp`、`src/generation/generation_service.cpp`、`src/app/candidate_review_dialog.cpp`、`src/workspace/build_service.cpp` 和 `src/workspace/project_exporter.cpp`。

本地保留证据包括结构化验收结果、generation manifest、工作区和导出工程构建/CTest 日志、文件摘要及独立审查结果。原始模型内容、一次性验收工具、日志和凭据不随仓库提交或源码版本发布；本报告仅记录可公开的结果摘要。本次无接口、解析、候选、编译或测试失败。一次简单 LogicModule 成功不保证任意蓝图、任意模型输出均能编译或满足业务要求。

## 发布收尾检查

- 基于上述 main 的独立 worktree 完成全新 Debug 配置和完整构建，最终 CTest 30/30 通过（234.22 秒）。逐份核对 30 份 QtTest XML：939 个通过记录、0 个失败记录，仅跳过 Windows 无法创建大小写别名文件的 GUI fixture；Candidate Review 24 项通过，100% 与 150% 缩放画布回归均实际执行，导出登录示例独立测试 4/4 通过。本轮使用离线测试，没有额外调用付费 API。
- `.gitignore` 显式忽略根目录 `apikey.txt`，该文件未被 Git 跟踪。Git 跟踪文件和待提交源码 ZIP 的 120 个文件清单一致，无缺失或额外项；已知本机凭据的精确匹配及通用密钥模式扫描均无命中，未发现凭据文件、二进制、构建产物或本机实测日志。源码预览只用于本地复核，不作为 Release 附件上传。
- 发布目标为 `v0.1.0-mvp` 源码 Pre-release；收尾 PR 人工合并且最终 `main` 验证完成后，才可创建 annotated Tag 和 GitHub Release。发布说明见 [v0.1.0-mvp](releases/v0.1.0-mvp.md)。

## 证据边界与后续

- 全量 CTest 使用 Fake AI 或离线网络替身。2026-09-15 和 2026-09-21 的真实 DeepSeek 集成属于历史证据；本报告另记录 2026-10-10 的一次真实模块端到端验收，两类证据分别保留其范围。
- 基线明细仅跳过 `external_code_gui::pendingCaseAliasesReachImporterValidation`：本机 Windows 文件系统不能创建两个仅大小写不同的文件。后端大小写别名拒绝测试实际执行通过；链接、文件身份及哈希保护测试没有跳过。
- QtTest 合成鼠标/键盘验证交互；原生目录选择框、实际显示器缩放和视觉观感仍需人工检查。不能把离屏测试称为真人桌面验收。
- 蓝图语义和布局分别使用安全写入，混合版本会被 hash 检出；不承诺两个文件在断电时整体原子提交。
- 应用入口仍为占位窗口，Runtime Assembly、行级 Code Review、AI 返工及 Candidate Accept 自动更新 Workspace 尚未实现，属于后续路线。原 MVP 不要求这些能力；当前验收没有启动这些阶段。
- 原 MVP 可标记完成，未发现阻断进入“可运行原型生成”的程序问题；正式版本发布须先完成本次收尾检查与 PR 人工审核。源码 Pre-release 不包含未经独立验证的二进制或 Windows 安装包。

## 建议人工补充验收

1. 在中文目录中打开、另存为蓝图，移动节点并重开，检查连线、属性和位置；同时修改蓝图与源码，依次验证关闭提示的保存、放弃、取消。
2. 使用原生 Browse 选择中文路径和空目录，再取消一次切换；确认工作区、未保存源码及受保护文件状态保持一致。
3. 在英文/中文和亮色/暗色下检查画布拖拽、实际屏幕缩放、候选双栏差异与构建日志；完整操作一次接受、人工改源码、Build、Export。
