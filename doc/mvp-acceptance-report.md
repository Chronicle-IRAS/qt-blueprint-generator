# MVP 验收报告

验收日期：2026-10-10（Asia/Shanghai）。基线：远端 `main`，`c009bab882242cc464dc5480c45e2e3c355eb559`。PR #45、#46、#47 已合并；相关 Issues #17、#18、#32 已关闭。

验收依据为 [MVP 实施计划](mvp-implementation-plan.md) 第 10 节、[使用指南](project-usage-guide.md) 与已合并功能。本次范围是原有工程骨架、模块生成和人工审核闭环；下一阶段范围见 [项目路线](project-roadmap.md)。

## 原 MVP 标准逐项核对

| # | 原标准 | 实现与测试证据 | 基线状态 |
|---|---|---|---|
| 1 | 六类节点和文字字段编辑 | `blueprint_scene` 的 `mainWindowCreatesEveryNodeType`、`editingAllNodeFieldsIsOneUndoableCommand`，以及 `node_properties_editor` 字段往返 | 通过 |
| 2 | 创建、删除和显示有向连线 | `blueprint_scene` 的 `connectingNodesUpdatesDocumentAndSupportsUndo`、`edgeShowsDirectedLabelAndUsesPortAnchors`、`clickingAnEdgeSelectsItAndDeleteKeepsItsNodes` | 通过 |
| 3 | 保存并无损重新加载 | `blueprint_document` 完整 JSON 往返；`blueprint_project_store` 的 `roundtripPreservesSemanticsAndPositions`；`blueprint_persistence` 的 `roundTripRestoresGraphPropertiesAndLayout` | 通过 |
| 4 | 发现全部最小结构错误 | `blueprint_validator` 覆盖 ID、Start/End、度数、引用、可达性、环、Decision 分支、模块说明和 ExternalCode 完整性 | 通过 |
| 5 | 不同布局下 IR 一致 | `ir_compiler` 的 `layoutChangesDoNotAffectIr`、排序测试；`blueprint_project_store` 的 `layoutSignalsAndUndoNeverChangeSemanticIr` | 通过 |
| 6 | Fake AI 离线端到端 | `end_to_end` 实际加载、校验、编译 IR、生成三个模块、接受六个文件并导出 | 通过 |
| 7 | 真实客户端失败不破坏工程 | `openai_compatible_client` 离线网络替身验证认证、超时、HTTP 和格式错误；`generation_service` 验证失败不写候选；Controller 隔离失败与过期响应，`end_to_end` 验证工作区快照不变 | 通过 |
| 8 | 可生成节点独立候选目录 | `project_scaffolder` 候选批次/节点目录测试；`end_to_end` 按模块持久化并复核候选内容及 manifest | 通过 |
| 9 | 人工修改不被静默覆盖 | `candidate_review_dialog` 冲突确认、旧预览失效；`project_scaffolder` 和 `end_to_end` 哈希保护；Workspace 保存前复查磁盘身份和哈希 | 通过；候选编辑另有下述缺陷 |
| 10 | 外部黑盒导入并随工程导出 | `external_code_importer` 字节、SHA-256、接口契约与 AI 隔离；`external_code_gui` 导入/重新导入/只读查看；`project_exporter` 外部文件字节一致 | 通过 |
| 11 | 显示 CMake 配置和构建日志 | `project_exporter` 的 BuildService 与 GUI 测试；`end_to_end` 验证两个构建阶段、输出信号和退出码 | 通过 |
| 12 | 完整工程导出到空目录 | `project_exporter` 验证空目录导出、非空/重叠目录拒绝、失败恢复和哈希；`end_to_end` 核对完整导出快照 | 通过 |
| 13 | 编辑器全部 QtTest 和端到端测试通过 | 基线完整构建及全部 CTest：30/30，240.51 秒；测试明细检查见下文 | 通过；新增复现检出覆盖缺口 |
| 14 | 导出示例配置、编译并通过测试 | `end_to_end` 真正调用 CMake 和 CTest，发现并运行导出登录工程的四个测试：4/4 | 通过 |

## 当前 GUI 与生命周期检查

- `blueprint_persistence` 验证 New/Open/Save/Save As 快捷键、节点位置、Undo/Redo 回到保存内容后的 clean state、非法文件和保存失败保留当前蓝图。
- `generation_workflow` 与 `candidate_review_dialog` 验证 Generate、双栏比较、逐文件接受/拒绝、草稿编辑及语义/工作区变化后的上下文失效。
- `workspace_browser` 与 `workspace_main_integration` 验证普通源码编辑与安全保存、普通 Backspace 恢复 clean、Save/Discard/Cancel、蓝图和源码两套未保存提示、Build/Export 预检、失败后继续修改重建、项目切换和 ExternalCode 选择框上下文。
- `language_switch`、`theme_switch` 及各功能 GUI 测试验证中英文和亮暗主题切换；翻译构建完成 249 条，无未完成项。画布局部重绘与 150% 分数 DPI 像素回归实际执行、无跳过。

## 发现的真实缺陷

候选审核窗口用 `QPlainTextEdit::toPlainText()` 更新整份草稿，导致候选字符串中的 NBSP（U+00A0）在用户仅于其他位置追加 `//x` 后被改为普通空格。点击 Accept edited draft 会把该变化写入正式工程，原候选仍保持不变。

独立 GUI probe 在基线稳定复现：2 passed、1 failed、0 skipped，接受后的字节 `c2 a0` 变为 `20`。因此即使既有 CTest 全部通过，也不能把本次基线判为无缺陷完成。

修复分支：`fix/mvp-candidate-unicode`，从上述最新 `main` 创建。草稿更新与 Diff 使用原始文本读取，保留 NBSP 和 U+2028，沿用 Qt 段落边界转 LF 的既有约定；候选接受、哈希和冲突确认流程没有变化。

新增六条数据回归，覆盖真实键盘编辑、切换文件/语言后接受、接受原候选、原候选字节保留及 Unicode 差异高亮。测试先检出四处失败，修复后新增用例全部通过；完整 Candidate Review 测试 24 passed、0 failed、0 skipped。修复分支完整构建成功，全部 CTest 30/30 通过（237.78 秒），导出登录示例自身测试再次 4/4 通过。独立 Review 结论 Ready，无未解决问题。最终 30 份 QtTest 日志中无失败，仅有与基线相同的 Windows 大小写别名 GUI 用例跳过；在修复 PR 合并前，`main` 的最终验收结论仍为待收口。

## 证据边界与后续

- 全量测试使用 Fake AI 或离线网络替身，本轮没有调用真实服务。真实 DeepSeek 集成已于 2026-09-15 和 2026-09-21 验证并记录在实现进度；这属于历史证据，不代表本轮重测，也不保证任意模型代码可编译或业务正确。
- 基线明细仅跳过 `external_code_gui::pendingCaseAliasesReachImporterValidation`：本机 Windows 文件系统不能创建两个仅大小写不同的文件。后端大小写别名拒绝测试实际执行通过；链接、文件身份及哈希保护测试没有跳过。
- QtTest 合成鼠标/键盘验证交互；原生目录选择框、实际显示器缩放和视觉观感仍需人工检查。不能把离屏测试称为真人桌面验收。
- 蓝图语义和布局分别使用安全写入，混合版本会被 hash 检出；不承诺两个文件在断电时整体原子提交。
- 应用入口仍为占位窗口，运行时模块装配、行级评论、AI 返工及 Candidate Accept 自动更新 Workspace 属于后续路线。原 MVP 不要求这些能力。
- 当前 `main` 尚不能正式标记完成，须先合并候选编辑修复。其余原 MVP 标准没有发现阻断问题；进入“可运行原型生成”前应先完成该修复的审核与合并。

## 建议人工补充验收

1. 在中文目录中打开、另存为蓝图，移动节点并重开，检查连线、属性和位置；同时修改蓝图与源码，依次验证关闭提示的保存、放弃、取消。
2. 使用原生 Browse 选择中文路径和空目录，再取消一次切换；确认工作区、未保存源码及受保护文件状态保持一致。
3. 在英文/中文和亮色/暗色下检查画布拖拽、实际屏幕缩放、候选双栏差异与构建日志；完整操作一次接受、人工改源码、Build、Export。
