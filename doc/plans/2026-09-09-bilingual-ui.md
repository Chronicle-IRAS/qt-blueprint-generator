# Chinese and English UI Switching Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:test-driven-development to implement this plan step-by-step. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Add an in-application language menu that switches the existing Qt Widgets interface between English and Simplified Chinese at runtime and remembers the selection.

**Architecture:** Keep English source strings in `tr()` calls and compile one Simplified Chinese Qt Linguist catalog into the editor and its localization test. `MainWindow` owns the translator, exposes the current language for verification, retranslates existing widgets without rebuilding the document, and stores the choice with `QSettings`. User-authored blueprint values remain unchanged.

**Tech Stack:** Qt 6 Widgets, Qt LinguistTools, QTranslator, QSettings, Qt Test, CMake/Ninja.

---

### Task 1: Specify runtime language behavior

**Files:**
- Create: `tests/tst_language_switch.cpp`
- Modify: `tests/CMakeLists.txt`

- [ ] **Step 1: Write the failing test**

Create a Qt Test that constructs `MainWindow`, locates `languageMenu`, `languageEnglishAction`, and `languageChineseAction`, triggers Chinese, and expects the title, property dock, build button, and newly created node name to become Chinese. Trigger English and expect the controls to return to English while the existing user-visible node name remains unchanged. Reconstruct the window to verify the saved selection.

- [ ] **Step 2: Run the test to verify RED**

Run:

```powershell
cmake --build C:/Users/Lenovo/AppData/Local/QtBlueprintGenerator/bilingual-build
ctest --test-dir C:/Users/Lenovo/AppData/Local/QtBlueprintGenerator/bilingual-build -R '^language_switch$' --output-on-failure
```

Expected: compilation fails because `MainWindow::currentLanguage()` and language actions do not exist.

### Task 2: Implement standard Qt localization

**Files:**
- Create: `translations/BlueprintEditor_zh_CN.ts`
- Modify: `CMakeLists.txt`
- Modify: `src/main.cpp`
- Modify: `src/app/main_window.h`
- Modify: `src/app/main_window.cpp`

- [ ] **Step 1: Add the translation catalog**

Use `find_package(Qt6 REQUIRED COMPONENTS LinguistTools Network Widgets)` and:

```cmake
qt_add_translations(
    TARGETS BlueprintEditor tst_language_switch
    SOURCE_TARGETS BlueprintEditorCore
    TS_FILES translations/BlueprintEditor_zh_CN.ts
    RESOURCE_PREFIX "/i18n"
)
```

English remains the source language; the `.ts` file supplies Chinese translations for current menus, toolbars, docks, form labels, buttons, node types, port captions, status messages, and build/export messages.

- [ ] **Step 2: Add runtime switching**

Add these public methods to `MainWindow`:

```cpp
QString currentLanguage() const;
bool setLanguage(const QString &languageCode);
```

Store stable pointers to the widgets/actions whose text must change. `setLanguage()` accepts only `en` and `zh_CN`, loads `:/i18n/BlueprintEditor_zh_CN.qm` for Chinese, removes the translator for English, updates checked actions, calls `retranslateUi()`, repaints the scene, and writes `ui/language` with `QSettings`. Unsupported codes return `false` without changing the current language.

- [ ] **Step 3: Give settings stable application identity**

Before constructing `MainWindow`, set:

```cpp
QCoreApplication::setOrganizationName(QStringLiteral("QtBlueprintGenerator"));
QCoreApplication::setApplicationName(QStringLiteral("BlueprintEditor"));
```

- [ ] **Step 4: Verify GREEN and regressions**

Run the focused test, then the complete build and CTest suite. Expected: `language_switch` passes and all pre-existing tests remain green.

### Task 3: Document and deliver the new task

**Files:**
- Modify: `README.md`
- Modify: `doc/implementation-progress.md`

- [ ] **Step 1: Document usage and limits**

Explain the Language menu, runtime switching, persistence, supported `en`/`zh_CN` values, and that blueprint content and generated code are not translated.

- [ ] **Step 2: Verify and commit**

Run `git diff --check`, the complete CTest suite, and a GUI smoke check. Commit the feature and final README/progress update without adding `start-blueprint-editor.cmd`.

- [ ] **Step 3: Sync the task branch**

Push `feature/bilingual-ui` normally and create a pull request against `main`; do not force-push or auto-merge.

## Self-review

- The plan covers switching, initial restoration, persistence, complete current UI translation, stable object names, preservation of blueprint data, automated tests, documentation, and branch delivery.
- No Python worker or generated-project language changes are included; those are separate meanings of “multi-language” and outside this UI-localization task.
- The public method names and persisted language codes are consistent across implementation and tests.
