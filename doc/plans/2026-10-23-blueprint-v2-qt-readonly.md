# P1.2C Qt Blueprint v2 Read-only Integration Plan

> Execution: one cohesive implementation worker, independent read-only specification and final code review, primary integration and Windows GUI verification. The user has authorized implementation through PR delivery. Steps use checkboxes.

**Goal:** Open a Blueprint v2 project in the real Qt desktop application and inspect KnownNode/UnknownNode, ports, connections, properties and diagnostics safely.

**Architecture:** A bounded one-shot Node child accepts a JSON request on stdin and returns a versioned JSON reply on stdout. It constructs the existing Plugin Registry from explicit host-provided trusted declarations and calls the existing Reader. Qt renders a separate read-only scene; Schema 1 remains owned by its current document/scene. A successful, current reply is prepared before any project transition; failed parsing or transport never replaces the current project.

**Tech stack:** C++17/Qt 6 Widgets, QProcess, existing TypeScript/Node 24/Ajv/Vitest and CMake. No new npm dependency, server, plugin execution or framework.

**Spec:** User P1.2C request, [roadmap](../project-roadmap.md), [v2 Draft](../architecture/blueprint-schema-v2.md), [platform](../architecture/plugin-platform.md), existing Reader/Registry and [API](../../tools/schema-contracts/README.md).

## Global Constraints

- Start from PR #60 merged main `467f8e0a0c8bc6b78bab613f4cb64dab8eed176b`; branch `feature/p1.2c-blueprint-v2-qt-readonly`, isolated worktree. Do not merge automatically, force push or change Tag/Release.
- Reuse readBlueprintDocument and PluginRegistry.register/validate. Qt checks transport DTO shape, never reimplements Blueprint Schema or semantics. Known/Unknown recognition and diagnostics come from TS.
- V2 is entirely read-only: no editing, saving, generation, Candidate acceptance, Build/Export or external-code management. Legacy v1/Runtime v2 remain intact. No Cordis, Host, dynamic ports, Core IR, migration or new target.
- Do not trust descriptors from a blueprint or derive installed trusted pins from document locks. Default Registry is empty. The user may explicitly select a separate `{descriptors,trustedLocks}` JSON bundle at the v2-open dialog. It is trusted declaration metadata, not package/hash verification or plugin installation/execution; malformed configured declarations are an error, not silent zero-plugin fallback.
- Support Chinese paths/text, both editor languages and themes. Read-only/Unknown/error states have text labels, not only colors. Properties and diagnostics support selecting/copying text.
- Root owns product/API docs, plan, integration, Git and Windows GUI acceptance; worker owns production code, CMake, translations, examples and tests. No concurrent edits of shared files.

## Interfaces and decisions

- New TS `blueprint-reader-protocol.ts` exports a pure `handleReaderRequest(input: unknown)`; CLI `blueprint-reader-cli.ts` only handles bounded UTF-8 stdin/stdout. Request `{protocolVersion:1,requestId:string,sourceText:string,registry:{descriptors:unknown,trustedLocks:unknown}}`; only operation is reading. Reply echoes version/id, keeps `canGenerate:false`, gives `status:read|rejected|error`, diagnostics and the Reader display data/document. Exact DTO must be exported/documented and consumed by Qt; no shell command or document path is accepted by Node.
- Source UTF-8 limit 8 MiB; stdin/reply limit 32 MiB; process deadline 15 seconds; bounded stderr. Oversize, wrong correlation/version, malformed/multiple replies, nonzero exit, missing runtime, timeout and crash fail closed. No environment credential output/logging. Child environment should not forward unrelated AI keys. Keep GUI event loop responsive; cancel/ignore superseded requests and on close.
- A small build script/tsconfig compiles the existing production TS dependency closure and stages the unchanged machine Schemas and existing Ajv runtime dependency closure under `build/v2-reader`. Preserve relative resource/import paths and package type. CMake adds a local reader-build target when Node and already-installed dependencies exist, never downloads packages. If unavailable, Schema 1 still builds/runs and v2 open reports actionable setup diagnostics. No reliance on `.ts` execution or globally installed npm packages. Keep runtime service relocatable beside the executable where possible and document source-build setup.
- Qt `BlueprintV2Reader` owns one QProcess and returns only validated transport results; defaults resolve the built reader and Node. A production `setV2ReaderConfiguration(nodeExecutable,entryPoint,timeoutMs)` supports explicit runtime setup and failure tests without test-only hooks.
- MainWindow `openV2Project(directory, trustedBundlePath={})` asynchronously loads `<directory>/blueprint.json`; `v2LoadFinished(bool)` and `isV2Mode()` expose real lifecycle. A File menu v2-open dialog selects a project directory and optional trusted bundle. A visible exit-to-Schema1 action returns to the retained legacy project. Opening New/Schema1 also leaves v2 only after success.
- Retain the legacy project for return from v2. Use existing Save/Discard/Cancel prompts for dirty blueprint and source; Cancel aborts the transition. Save writes normally. Discard restores the last clean document/layout rather than marking unsaved content as saved; resetting discarded undo history is allowed. Otherwise preserve the legacy document/layout/history while v2 is visible. Disallow switching during an active Build; cancel generation and dismiss stale Candidate/External dialogs through existing invalidation (do not reject persisted candidates or write them). On exit restore coherent legacy UI. A failed/rejected/cancelled v2 load never changes the active scene, title, dirty state or candidates.
- V2 uses a separate QGraphicsScene/View in a stack plus read-only properties/diagnostics. Deterministic display-only layout, no layout file writes. Show every node by sourceIndex, including duplicate IDs; display original IDs, unit/type/definition version and full port snapshots. Only draw endpoints that resolve uniquely; ambiguous/dangling connections remain listed with their identity and explanation, never guessed or silently dropped. Show channels distinctly as declared relationships, without implying communication.
- Guard mutating entry points as well as widgets/actions. MainWindow callbacks (undo availability, generation state, Build finished, language/theme changes) must not re-enable v2 writes. WorkspaceBrowserWidget needs a narrow read-only mode guard at editor/save entry, preserving ordinary behavior outside v2. Retained legacy scene cannot receive keyboard/context/drop mutation while hidden. No blanket disabling that prevents v2 selection, copying or navigation.

## Files and ownership

- Worker TS: new protocol/CLI/build script/reader tsconfig and tests in tools/schema-contracts; add build script entry only, no dependency change or existing Reader/Registry behavior change.
- Worker Qt: new src/app/blueprint_v2_reader.*, blueprint_v2_view.* and optionally small v2-open dialog; MainWindow/WorkspaceBrowser narrow integration; CMakeLists/tests CMake/translations; new tests/tst_blueprint_v2_gui.cpp. Add real fixture-based examples under examples/blueprint-v2-readonly with README data notes (root may polish its prose).
- Primary docs: root README introduction, usage guide, tool API README, architecture status, roadmap, implementation progress and this plan. Existing historical reports are untouched.

## Review Focus

1. Failed or stale replies and Save/Discard/Cancel must not replace or silently discard the current legacy/v2 project; test both asynchronous completion and user cancellation.
2. Duplicate node/port IDs must not collapse records or connect the wrong node; test sourceIndex selection, unresolved edge/channel listing and distinct properties.
3. Registry trust must remain host-provided; test zero plugins, exact locks, missing definitions, mismatched versions and invalid declaration configuration through real TS and GUI.
4. Menu, keyboard, callbacks and public save/build/generation/workspace entry points must remain blocked in v2, including after retranslation/theme/generation completion.
5. Packaged Node entry must actually run with real fixtures on Windows, including Unicode paths, no shell quoting assumptions, bounded failures and process cleanup. Full CTest and a real visible application are mandatory.

## Task 1: End-to-end Qt v2 read-only view

- [x] Step 1: Add importable protocol/API skeletons and behavioral tests for real fixture reads, zero plugins/empty graph, Known/Unknown and rejection; observe assertion RED before implementation, not missing-import failures. Implement minimum TS wrapper using existing Registry and Reader; test malformed protocol and independent trusted locks.
- [x] Step 2: Implement/build the one-shot CLI and real process tests. Test stdin EOF, non-ASCII, limits, malformed request and compiled runtime relocation. Add CMake reader staging without network or new dependencies. Keep original 459 TS tests intact.
- [x] Step 3: Add Qt transport and failing real-process GUI tests; implement async bounded QProcess lifecycle, version/id checking, error reporting, timeout/cancellation and no shell. Tests for communication failure must preserve prior project.
- [x] Step 4: Add v2-open dialog/menu and read-only canvas/properties/diagnostics. Use actual descriptor Blueprint fixture and independent trust bundle; show all three nodes, normal edge and declared channel. Test missing plugins, wrong version, duplicated IDs, selection/property display, language/theme and unchanged input files.
- [x] Step 5: Add transition/lifecycle regressions (dirty legacy/source Cancel/Discard, generation/candidate invalidation, load failure, stale completion, return-to-legacy). Implement unified permission guards including callbacks and Workspace save; verify no generated/export/saved files arise in v2.
- [x] Step 6: Run focused tests during TDD, then final full TS tests/typecheck, complete CMake build and full CTest. Record commands, exact counts/skips and actual RED evidence in task report. Do not claim GUI launch until primary performs visible Windows acceptance.
- [ ] Step 7: Primary updates docs and performs Windows visible launch/open/select/diagnostics/disabled actions/failure preservation/legacy-return checks. Independent reviewer receives full diff, task brief, report and GUI evidence, checks spec and quality; fix confirmed issues with regressions and scoped re-review.
- [ ] Step 8: Fetch latest main, integrate any changes normally if needed; commit, push and create PR base main. Verify matching local/remote HEAD, clean worktree and no auto-merge. Stop after delivery.

## Self-review

One cohesive deliverable owns the process DTO, actual view and its mode guards; no intermediate bottom-layer-only PR. Structural and semantic decisions have a single authority. KnownNode demo input requires an explicit independent trust bundle. Deterministic display layout is not persistence or IR. Every review focus has a behavior test plus visible acceptance, and neither successful parsing nor an optional local child process authorizes generation.
