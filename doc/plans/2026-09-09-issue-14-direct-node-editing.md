# Issue #14 Direct Node Editing Implementation Plan

**Goal:** Let users open a bilingual modal editor directly from a canvas node, edit all six supported property groups, and save through the existing undoable BlueprintScene edit path.

**Base:** origin/main at 6e16bac (project usage guide PR #20).

**Branch:** feature/issue-14-direct-node-editing.

## Scope and design

- Double-clicking a NodeItem requests editing for that node through BlueprintScene.
- MainWindow opens a modal editor containing Name, Description, Inputs, Outputs, Constraints, and Acceptance Criteria.
- Save validates the four JSON-array fields before closing, then calls BlueprintScene::editNode exactly once.
- Cancel closes without calling BlueprintScene::editNode.
- The existing Properties Dock remains available and refreshes through the existing semantic-change handler.
- No BlueprintDocument or JSON schema change is permitted.

## Task 1: Establish GUI contracts with failing tests

**Files:**
- Modify: tests/tst_blueprint_scene.cpp

- [x] Add a test that double-clicks a node, fills every dialog field, saves, and verifies the document, node title, Properties Dock, and one undo command.
- [x] Extend the test through Undo and Redo to verify document, canvas, and Properties Dock synchronization.
- [x] Add a Cancel test proving every document field and undo-stack count remain unchanged.
- [x] Add an invalid-JSON test proving Save keeps the dialog open and does not change the document.
- [x] Run the blueprint_scene test and record the expected RED result before production implementation.

## Task 2: Implement the direct editor

**Files:**
- Modify: src/editor/node_item.h
- Modify: src/editor/node_item.cpp
- Modify: src/editor/blueprint_scene.h
- Modify: src/editor/blueprint_scene.cpp
- Modify: src/app/main_window.h
- Modify: src/app/main_window.cpp

- [x] Add a dedicated double-click callback on NodeItem without changing single-click connection behavior.
- [x] Emit a BlueprintScene nodeEditRequested signal for representable, existing nodes.
- [x] Add a MainWindow modal node editor with stable object names for GUI tests.
- [x] Reuse the existing port/list JSON parsers and BlueprintScene::editNode command.
- [x] Keep invalid input in the dialog with a visible validation message.
- [x] Run the focused test to GREEN and check affected selection, connection, and property regressions.

## Task 3: Translate, document, verify, and deliver

**Files:**
- Modify: translations/BlueprintEditor_zh_CN.ts
- Modify: README.md
- Modify: doc/project-usage-guide.md
- Modify: doc/implementation-progress.md

- [x] Update the translation catalog and finish all new Simplified Chinese strings.
- [ ] Document double-click editing, Save/Cancel behavior, JSON requirements, Inspector synchronization, and Undo/Redo.
- [ ] Run translation checks, complete build, full CTest, startup probe, and git diff --check.
- [ ] Perform a quality review and address all Critical, Important, and Minor findings.
- [ ] Commit each completed change group and normally push the branch.
- [ ] Create a PR titled with an operation prefix and Issue reference, and include Closes #14 in the body. Do not force-push or auto-merge.

## Self-review checklist

- All acceptance fields are editable.
- Save updates BlueprintDocument once and refreshes the canvas immediately.
- Cancel and invalid JSON cannot mutate the document or undo stack.
- Undo/Redo keep the dialog result, canvas, and Properties Dock coherent.
- Double-click does not accidentally start or complete a connection.
- Existing Properties Dock remains an independent Inspector/editing entry.
