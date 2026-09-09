# Issue 16 Dock Recovery and Toolbar Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development to implement this plan step-by-step. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add a standard View menu that restores closed docks and resets the default layout, while exposing existing Build and Export operations in the main toolbar with complete English and Simplified Chinese text.

**Architecture:** Reuse each `QDockWidget::toggleViewAction()` so visibility and checked state stay synchronized by Qt. `MainWindow` owns one Reset Layout action and restores the two existing docks to their original areas without persisting a second layout model. Toolbar actions call the same build/export methods as the existing dock buttons.

**Tech Stack:** C++17, Qt 6 Widgets, Qt LinguistTools, Qt Test, CMake/Ninja.

---

### Task 1: Specify dock recovery and toolbar behavior

**Files:**
- Modify: `tests/tst_blueprint_scene.cpp`
- Modify: `tests/tst_language_switch.cpp`

- [ ] **Step 1: Write the failing dock recovery test**

Add `mainWindowRestoresClosedDocksAndDefaultLayout()` to `BlueprintSceneTest`. Show the window, locate `propertiesDockAction`, `buildExportDockAction`, and `resetLayoutAction`, close both docks, verify the actions become unchecked, reopen them through the actions, then float the docks and trigger Reset Layout. Verify Properties returns to the right and Build/Export to the bottom, both docked and visible.

```cpp
MainWindow window;
window.show();
auto *propertiesDock = window.findChild<QDockWidget *>(QStringLiteral("propertiesDock"));
auto *buildDock = window.findChild<QDockWidget *>(QStringLiteral("buildExportDock"));
auto *propertiesAction = window.findChild<QAction *>(QStringLiteral("propertiesDockAction"));
auto *buildAction = window.findChild<QAction *>(QStringLiteral("buildExportDockAction"));
auto *resetAction = window.findChild<QAction *>(QStringLiteral("resetLayoutAction"));
QVERIFY(propertiesDock && buildDock && propertiesAction && buildAction && resetAction);
```

- [ ] **Step 2: Write failing toolbar and translation assertions**

Add `mainWindowExposesBuildAndExportInToolbar()` to `BlueprintSceneTest`. Locate `toolbarBuildAction` and `toolbarExportAction`, trigger them with intentionally incomplete fields, and verify the existing build log receives the corresponding rejection messages.

Extend `LanguageSwitchTest` to locate `viewMenu`, both dock actions, Reset Layout, toolbar Build, and toolbar Export. Assert the English labels, switch to Chinese, and assert `视图`、`属性`、`构建与导出`、`重置布局`、`构建`、`导出`.

- [ ] **Step 3: Run RED**

Run:

```powershell
cmake --build C:/Users/Lenovo/AppData/Local/QtBlueprintGenerator/issue-16-build `
  --target tst_blueprint_scene tst_language_switch
ctest --test-dir C:/Users/Lenovo/AppData/Local/QtBlueprintGenerator/issue-16-build `
  -C Debug -R '^(blueprint_scene|language_switch)$' --output-on-failure
```

Expected: assertions fail because the new menu and actions do not exist.

### Task 2: Implement the View menu and shared actions

**Files:**
- Modify: `src/app/main_window.h`
- Modify: `src/app/main_window.cpp`

- [ ] **Step 1: Add stable action members and the layout reset method**

```cpp
void resetWindowLayout();

QMenu *m_viewMenu = nullptr;
QAction *m_propertiesDockAction = nullptr;
QAction *m_buildDockAction = nullptr;
QAction *m_resetLayoutAction = nullptr;
QAction *m_toolbarBuildAction = nullptr;
QAction *m_toolbarExportAction = nullptr;
```

- [ ] **Step 2: Create and connect actions**

Create View before Language. After constructing both docks, reuse their toggle actions, assign stable object names, and add Reset Layout. Add Build and Export to the existing toolbar after a separator and connect them to `startBuild()` and `exportProject()`.

When a build starts, disable both the dock button and toolbar Build action. Re-enable both on immediate rejection or asynchronous completion.

- [ ] **Step 3: Restore the default layout**

Implement `resetWindowLayout()` by removing both existing docks, docking Properties at `Qt::RightDockWidgetArea` and Build/Export at `Qt::BottomDockWidgetArea`, showing both, and applying default horizontal/vertical sizes with `resizeDocks()`. The method must not alter the blueprint or current language.

- [ ] **Step 4: Retranslate all new labels**

In `retranslateUi()`, set View, both dock toggle actions, Reset Layout, toolbar Build, and toolbar Export from `tr()` strings. Existing Language behavior must remain unchanged.

- [ ] **Step 5: Run GREEN and affected regressions**

Build both affected test targets and run `blueprint_scene|language_switch`. Expected: both pass with the new interaction and translation coverage.

### Task 3: Update translations, documentation, and delivery state

**Files:**
- Modify: `translations/BlueprintEditor_zh_CN.ts`
- Modify: `README.md`
- Modify: `doc/implementation-progress.md`

- [ ] **Step 1: Update the Qt translation catalog**

Run `update_translations`, translate `View` as `视图` and `Reset Layout` as `重置布局`, then rebuild. Confirm Linguist reports no unfinished, vanished, or obsolete entries.

- [ ] **Step 2: Document stable behavior**

Document how to reopen Properties and Build/Export through View, how Reset Layout behaves, and that toolbar Build/Export reuse the existing operations. Do not add development-history details to README.

- [ ] **Step 3: Verify the complete task**

Run `git diff --check`, a complete build, and the full CTest suite. Start `BlueprintEditor.exe`, confirm it remains running, and stop only the process created by the probe.

- [ ] **Step 4: Commit and synchronize**

Push `feature/issue-16-dock-recovery` normally, create a PR against `main` with `Closes #16`, and do not force-push or auto-merge.

## Self-review

- Every Issue #16 acceptance criterion is covered without inventing New/Open/Save/Validate/Generate commands.
- `toggleViewAction()` remains the single source of truth for dock visibility and checked state.
- Reset Layout changes only dock placement and visibility.
- Toolbar and dock controls call the same build/export implementations.
