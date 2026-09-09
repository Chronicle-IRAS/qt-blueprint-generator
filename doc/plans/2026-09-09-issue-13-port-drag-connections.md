# Issue #13 Port Drag Connections Implementation Plan

**Goal:** Make output and input ports the primary direct-mouse connection gesture while preserving the existing Connect action, FlowEdge schema, labels, and undo history.

**Base:** origin/main at 4f0bce6 (Issue #16 PR #21).

**Branch:** feature/issue-13-port-drag-connections.

## Scope and design

- Pressing a visible output port starts a drag owned by BlueprintScene.
- A dashed temporary path follows the pointer from the exact output anchor.
- Hovering a valid input port highlights that port; self-targets and non-input areas are invalid.
- Releasing on a valid input creates the existing node-level FlowEdge through connectNodes.
- Releasing elsewhere, pressing Esc, or right-clicking cancels without changing BlueprintDocument.
- The toolbar Edge label value is reused for drag-created edges, preserving Decision true/false labels.
- Existing Connect source/target mode stays available and keeps its tests.
- No BlueprintDocument or JSON schema change is allowed.

## Task 1: Establish failing GUI contracts

**Files:**
- Modify: tests/tst_blueprint_scene.cpp

- [x] Test output-port press and mouse movement create a temporary path whose end follows the pointer.
- [x] Test valid input hover feedback and release create the expected directed FlowEdge.
- [x] Test invalid release leaves the document and undo stack unchanged.
- [x] Test Esc and right-click cancel the temporary path and clear hover state.
- [x] Test Undo/Redo, node movement, drag labels, Decision true/false labels, and the existing Connect action.
- [x] Run blueprint_scene and record the expected RED result.

## Task 2: Implement port drag interaction

**Files:**
- Modify: src/editor/node_item.h
- Modify: src/editor/node_item.cpp
- Modify: src/editor/blueprint_scene.h
- Modify: src/editor/blueprint_scene.cpp
- Modify: src/app/main_window.cpp

- [ ] Add port hit testing and highlighted-input state to NodeItem.
- [ ] Route output-port press/move/release callbacks to BlueprintScene without initiating node movement.
- [ ] Render and update a dashed temporary connection path.
- [ ] Detect valid input-port targets and update hover feedback.
- [ ] Create an existing FlowEdge only after valid release; cleanly cancel all other endings.
- [ ] Reuse the toolbar Edge label for drag connections and keep button-based connection unchanged.
- [ ] Run focused tests to GREEN.

## Task 3: Document, verify, review, and deliver

**Files:**
- Modify: README.md
- Modify: doc/project-usage-guide.md
- Modify: doc/implementation-progress.md

- [ ] Update stable user documentation and README for port dragging and cancellation.
- [ ] Run complete build, full CTest, startup probe, link check, and git diff --check.
- [ ] Perform quality review and resolve all Critical, Important, and Minor findings.
- [ ] Commit each completed change group and normally push the branch.
- [ ] Create a prefixed PR title containing (#13) and a body containing Closes #13. Do not force-push or auto-merge.

## Self-review checklist

- Temporary UI state never enters BlueprintDocument.
- Invalid and cancelled drags add no undo command.
- Valid release creates exactly one existing FlowEdge command.
- Input feedback clears after every finish and cancellation path.
- Existing edges still follow moved nodes.
- Existing Connect and Decision label workflows do not regress.
