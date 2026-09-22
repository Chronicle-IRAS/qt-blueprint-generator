# Issue #19 编辑器视觉样式 Implementation Plan

> **For agentic workers:** 使用 subagent-driven-development，先测试后实现；实现和独立规格/质量审查分离。Git、文档和最终验收由主代理统一协调。

**Goal:** 在不改变蓝图、AI、审核及构建行为的前提下，统一 Qt 编辑器的视觉语言并改善中英文及缩放布局。

**Architecture:** 保留 QMainWindow/QDockWidget/QGraphicsView，新增集中式 `src/ui/theme.h/.cpp`，以 QPalette/QSS 服务标准控件，以语义颜色服务画布绘制。采用浅灰蓝背景、白色面板、深色文字和蓝色强调色；本次不增加暗色切换、外部字体、动画或新业务功能。

**Tech Stack:** C++17、Qt 6 Widgets、Qt Test、CMake/Ninja、MinGW。

## 边界与验收

- 基于远端 main `d94db17`，分支 `feature/issue-19-editor-visual-style`。
- 保留用户 `.gitignore`、启动脚本及现有本地构建目录；不提交构建产物。
- 系统字体及代码等宽字体；统一 4/8/16 间距、边框、圆角、hover/pressed/disabled/focus/selected 状态。
- 普通文字对背景对比度至少 4.5:1；不以颜色单独传递状态。
- 不改变对象名、翻译字符串含义、动作/快捷键、撤销、节点锚点与命中区域。
- 自动化测试与中英/100%、125%、150%、200% 缩放渲染检查互补；离屏渲染不等于真实显示器完整人工验收。

## Task 1：统一主题与标准控件布局

**Files:** 新增 `src/ui/theme.h/.cpp`、`tests/tst_editor_theme.cpp`；修改 `src/main.cpp`、`src/app/main_window.cpp`、`src/app/ai_settings_dialog.cpp`、`src/app/candidate_review_dialog.cpp`、`src/editor/node_properties_editor.cpp`、两级 `CMakeLists.txt`。

- [x] 添加主题回归测试，先用现有默认窗口确认配色/布局断言失败；主题 API 测试在声明可编译后验证其行为失败。
- [x] 集中主题 API 采用 `namespace EditorTheme`：`const Colors &colors()`、`void apply(QApplication &)`、`QFont codeFont()`；Colors 覆盖 surface/background/text/mutedText/border/accent/hover/selection/canvas/grid/node/port/edge/diff 语义。不向业务模块散布颜色字面量。
- [x] 应用入口调用一次 `EditorTheme::apply(app)`；测试显式调用同一入口，检查幂等、QPalette 和真实控件的有效字体/布局，而非只匹配 QSS 字符串。
- [x] 标准控件统一状态。主要按钮通过语义 property 获得强调样式；其余按钮保持清晰次级样式。菜单、工具栏和 Dock 保留原生操作。
- [x] 构建表单可滚动且日志仍可见；Inspector 表格长文本不撑宽面板，行高容纳按钮；AI 表单允许换行；候选五按钮分组排列，编辑器保持足够空间。
- [x] 聚焦测试通过后进行独立规格审查、质量审查；修复后再次运行。

测试命令（先将 Qt 和 MinGW 加入进程 PATH）：

```powershell
cmake -S . -B C:/bp-issue19-build -G Ninja -DCMAKE_PREFIX_PATH=E:/Qt/6.9.3/mingw_64 -DCMAKE_MAKE_PROGRAM=E:/Qt/Tools/Ninja/ninja.exe -DCMAKE_BUILD_TYPE=Debug
cmake --build C:/bp-issue19-build --parallel 2
ctest --test-dir C:/bp-issue19-build -R "editor_theme|node_properties_editor|ai_settings_dialog|candidate_review_dialog|language_switch|generation_workflow" --output-on-failure
```

## Task 2：画布视觉一致性

**Files:** 修改 `src/editor/node_item.cpp`、`src/editor/edge_item.cpp`、`src/editor/blueprint_scene.cpp/.h`、`tests/tst_blueprint_scene.cpp`。

- [x] 先添加画布背景/节点 hover 与选择绘制回归，确认旧实现失败。保留现有端口拖拽、路径端点、标签边界断言。
- [x] 使用 Task 1 的语义色。节点保留 180×96 主体与现有端口坐标；改善标题/正文层次，长文本省略但可查看完整内容。
- [x] 连线、箭头、标签和兼容端口高亮统一；不改变连接校验和路径语义。
- [x] `drawBackground(QPainter *, const QRectF &)` 绘制裁剪且随缩放控制密度的浅网格，不添加场景项，不影响命中测试。
- [x] 运行 `ctest --test-dir C:/bp-issue19-build -R blueprint_scene --output-on-failure`，独立规格/质量审查后修复复测。

## Task 3：验收与交付

**Files:** 更新 `README.md`、`doc/project-usage-guide.md`、`doc/implementation-progress.md`；新增 `doc/visual-style-verification.md`。

- [x] 完整构建与 `ctest --test-dir C:/bp-issue19-build --output-on-failure` 必须通过。
- [x] 使用虚构离线蓝图与候选，不调用真实模型。渲染中英主窗口、Inspector、AI 设置与候选审核；检查焦点、禁用、选中、长文本和小窗口。
- [x] 分别以 `QT_SCALE_FACTOR=1/1.25/1.5/2` 新进程检查布局；记录自动检查与待人工检查的边界。
- [x] README 仅补充已实现界面说明，不添加开发进度；进度写入独立文档。
- [x] 精确暂存本任务文件、提交推送、创建 `[feat] ... (#19)` PR，正文包含 `Closes #19`；Issue 标记 in-review，保持开启，停止等待用户审核，不自动合并。交付 PR #27，实现提交 `c82fba0`。
