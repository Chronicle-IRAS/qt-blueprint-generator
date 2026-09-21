# Issue #18: GUI generation and candidate review

## Goal

Allow a user to select a generatable blueprint node, generate with saved provider settings, inspect candidate files, and explicitly accept, reject, edit-and-accept, or cancel them from the desktop GUI.

Base: `origin/main` merge `9959917`; branch: `feature/issue-18-ai-generation-review`.

## Architecture and boundaries

- Keep MainWindow responsible for selection, workspace selection, menu/toolbar entry, and visible status.
- Introduce a generation controller for provider construction, request snapshots, scaffold validation, contract-aware prompt compilation, generation, and candidate persistence.
- Introduce a candidate review widget/dialog with file list, read-only Current, editable Candidate, basic change highlighting, and explicit actions.
- Reuse GenerationService and all existing Candidate APIs. Preserve stale-preview hashes, manual-edit conflict confirmation, node ownership, and path checks.
- Capture the blueprint/node/workspace when starting; reject stale completion and review actions after incompatible document/workspace changes.
- Use the actual scaffold namespace and contracts in prompts, including exact allowed node implementation/test paths.
- UI strings support English and Simplified Chinese. Provider credentials continue to come from the environment.
- Do not add an agent loop, worker runtime, or automatically accept/run generated code.

## Implementation groups

### 1. Generation controller

- [x] Define offline behavior tests before implementation: valid selected module, invalid selection/configuration/blueprint, snapshot handling, provider failure, candidate-only success and lifetime cleanup.
- [x] Implement a controller and injected client factory, reusing provider settings, scaffold/prompt and GenerationService.
- [x] Show stable state transitions and safe error categories; prevent duplicate generation.
- [x] Run focused tests, obtain independent review, update README/progress and push completed group (`afdd055`).

### 2. Candidate review

Groups 2 and 3 share the GUI end-to-end acceptance boundary and will be delivered together after integrated review and verification.

- [x] Add GUI tests for file selection, Current/Candidate preview, diff, accept/reject/edit-and-accept/cancel.
- [x] Implement explicit manual conflict confirmation and stale-preview rejection through existing service checks.
- [x] Preserve drafts during translation and handle close/cancel without unintended acceptance.
- [ ] Verify focused tests, independent review, README/progress and push.

### 3. Main window integration and delivery

- [x] Add a visible Generate toolbar/menu entry for selected generatable nodes and workspace/status feedback.
- [x] Add FakeAiClient GUI end-to-end coverage from selection through review and conflict handling.
- [x] Complete translations and user instructions.
- [x] Run complete offline CTest and a user-authorized real provider GUI-path verification.
- [ ] Create `[feat] ... (#18)` PR referencing `Closes #18`; update existing Issue status and stop for review without auto-merging.

## Local changes to preserve

The user's `.gitignore` edit ignores `start-blueprint-editor.cmd`. Keep it intact and exclude it from task commits unless explicitly included by the user. Existing build directories and local launcher are not deliverables.
