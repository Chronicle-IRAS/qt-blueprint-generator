# Issue #15 Structured Node Editor Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use test-driven development and execute this plan task-by-task. The delegated implementation worker became unavailable, so the primary agent owns integration while preserving independent final review.

**Goal:** Replace user-authored JSON in both node property entry points with reusable structured port and string-list editors without changing the blueprint schema.

**Architecture:** Add one `NodePropertiesEditor` widget used by the Properties Inspector and the canvas double-click dialog. It owns draft UI state and converts directly between widgets and the existing `BlueprintNode` fields; the two explicit submit buttons remain the only calls to `BlueprintScene::editNode()`, preserving one-command Undo/Redo.

**Tech Stack:** C++17, Qt 6 Widgets, Qt Test, CMake, Qt Linguist.

---

## File map

- Create `src/editor/node_properties_editor.h/.cpp`: reusable six-field editor with two port tables and two string-list tables.
- Modify `src/app/main_window.h/.cpp`: replace duplicated JSON editors with the shared widget in Inspector and direct dialog.
- Modify `CMakeLists.txt`: compile the new editor into `BlueprintEditorCore`.
- Modify `tests/tst_blueprint_scene.cpp`: migrate JSON-dependent integration tests and cover both structured entry points, serializer compatibility, Undo/Redo, Cancel, drafts and multi-selection.
- Modify `tests/tst_language_switch.cpp`: verify translated structured controls without losing drafts.
- Modify `translations/BlueprintEditor_zh_CN.ts`: complete all new visible strings and remove obsolete JSON labels.
- Modify `README.md`, `doc/project-usage-guide.md`, `doc/implementation-progress.md`: document stable usage and verification.

## Task 1: Establish failing structured-editor contracts

- [x] Add `NodePropertiesEditor`-facing Qt tests before the production header exists.
- [x] Require stable roots `inspectorNodePropertiesEditor` and `directNodePropertiesEditor`.
- [x] Require scoped port areas with `portItemsTable`, `addPortButton`, and per-row Delete buttons.
- [x] Require scoped string areas with `stringItemsTable`, `addStringItemButton`, and per-row Delete buttons.
- [x] Cover add/edit/delete, Unicode and special characters, empty collections, ordering and duplicate preservation.
- [x] Migrate MainWindow tests away from `nodeInputsEdit`, `directNodeInputsEdit` and other JSON text boxes.
- [x] Add GUI-to-`BlueprintSerializer` round-trip checks for schema version 1, object arrays and string arrays.
- [x] Run the focused target and record RED because `NodePropertiesEditor` and structured controls do not exist.

Expected test-facing interface:

```cpp
class NodePropertiesEditor final : public QWidget
{
public:
    explicit NodePropertiesEditor(const QString &objectNamePrefix, QWidget *parent = nullptr);
    void setNode(const BlueprintNode &node);
    void applyTo(BlueprintNode *node) const;
    void clear();
    void setEditorEnabled(bool enabled);
    void retranslateUi();
};
```

## Task 2: Implement the reusable editor

- [x] Add Name and Description controls while retaining the established scoped names `nodeNameEdit` / `directNodeNameEdit` and description equivalents.
- [x] Add Inputs and Outputs tables with Name, Type, Description and Delete columns plus Add buttons.
- [x] Add Constraints and Acceptance Criteria tables with Text and Delete columns plus Add buttons.
- [x] Compute the current row at deletion time rather than capturing a stale row index.
- [x] Treat absent cells as empty strings; preserve whitespace, empty values, duplicates and order exactly.
- [x] Make `applyTo()` update only the six editable fields and never mutate node `id` or `type`.
- [x] Make `retranslateUi()` update labels, headers and dynamic buttons without rebuilding rows or discarding drafts.
- [x] Run the component-focused tests to GREEN.

## Task 3: Integrate both explicit submit paths

- [x] Put the Inspector editor in a scroll area and call `setNode`, `clear`, and `setEditorEnabled` from existing selection synchronization.
- [x] Put the same editor class in `NodeEditDialog`; Save applies to a copy and Accepts, Cancel never calls `editNode()`.
- [x] Keep Inspector Apply and dialog Save to one `BlueprintScene::editNode()` call each.
- [x] Remove GUI JSON conversion helpers and the four JSON `QPlainTextEdit` members.
- [x] Keep layout-only changes from refreshing Inspector drafts; keep Undo/Redo semantic refresh.
- [x] Run `blueprint_scene` to GREEN and verify existing direct-edit and connection tests remain passing.

## Task 4: Translate, document and deliver

- [x] Replace visible `(JSON)` labels with structured labels and add translations for Add/Delete and table headers.
- [x] Verify the language switch preserves structured draft rows.
- [x] Update README and the project usage guide without adding development-history prose to README.
- [x] Update implementation progress with branch, commits, RED/GREEN and final checks.
- [x] Run complete build, full CTest, translation hygiene, startup probe, Markdown links and `git diff --check`.
- [x] Obtain independent code review and resolve every Critical, Important and Minor finding.
- [x] Commit each completed group, push normally, and create `[feat] ... (#15)` PR with `Closes #15`; never force-push or auto-merge.

## Scope guard

- Do not modify `BlueprintDocument`, schema version, `BlueprintSerializer`, `BlueprintValidator`, IR or AI code.
- Do not add new validation semantics, trimming, sorting or deduplication.
- Do not commit `start-blueprint-editor.cmd` or build output.
