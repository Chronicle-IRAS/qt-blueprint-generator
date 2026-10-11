# Runtime Assembly Phase 1 Implementation Plan

> **For agentic workers:** Use subagent-driven-development for the implementation and separate specification and quality reviews. Steps use checkbox tracking; Git operations remain owned by the primary agent.

**Goal:** Generate an application that executes a linear blueprint through stable module factories, passes declared Qt values, and displays the terminal page instead of a placeholder window.

**Architecture:** Preserve the MVP module-only API and version-1 workspaces. Add an explicit linear-runtime scaffold with a version-2 manifest of the same six-field shape, protected fixed factory contracts, and an unrolled WorkflowController. The production GUI defaults to runtime generation and offers an explicit legacy module-only action; generation and Candidate Review pin the chosen scaffold mode throughout their existing revalidation lifecycle.

**Tech Stack:** C++17, Qt 6 Widgets/Test, QVariantMap, CMake/Ninja, existing manifest/transaction/candidate APIs. Base: `main bb7fc4bb5e6e691e007f1bfea25445faa4ae29fc`. Preserve annotated MVP tag object `95e1dd4be539e56778db079456c1f15076013036`.

## Architecture findings and compatibility

- `IrCompiler::compile()` is deterministic module IR, sorted by ID, not execution order. It remains unchanged. A dedicated runtime assembler consumes semantic `BlueprintDocument::edges`, walking from Start; it never reads canvas layout.
- Flow edges contain only source/target/label, not port mapping. Phase 1 binds an input only to the immediately preceding module's identically named output with the same declared type. There is no renaming, implicit input source, accumulated global context, or conversion.
- Existing contracts have hashed class names, but no implementation factory. Use `create` + the existing protected class name, not an AI class name. Explicit references pull factory implementation translation units out of `blueprint_modules STATIC`; no registration registry is needed.
- `ProjectScaffolder::create()` retains byte-compatible module-only output. `createRuntime()` produces new runtime workspaces. Version-1 and version-2 workspaces are not converted or overwritten by the other entry point; errors direct users to a new workspace.
- Manifest keeps exactly `version`, `blueprintSha256`, `modules`, `protectedFiles`, `files`, and `batches`. Explicitly accept only versions 1 and 2; retain every existing hash, ownership, collision, link, and schema check. Candidate response JSON and persisted batch schema do not change.
- Runtime source, CMake, main, and contracts are protected. AI still writes only its own implementation/tests roots. Missing factories must fail the full application build: no stub factory, guessed class, or fallback blank page.
- Existing Build/Export do not recheck every protected-file hash. This change preserves their actual protection, and does not claim or implement an unrelated all-file validation redesign.

## Phase 1 runtime semantics

Supported: `Start → zero or more LogicModule → optional single terminal UiPage → End`. This includes the requested `Start → LogicModule → UiPage → End` and multiple sequential logic modules. Logic-only chains finish without entering an empty GUI event loop; a displayed page remains in Qt's event loop until closed. End marks completion of startup assembly, not immediate closure of the page.

Before any workspace write, reject unsupported node types, multiple Start/End, fan-in/out, cycles, unreachable or disconnected nodes, nonterminal/multiple pages, and any labelled flow. Reject Start/End data ports and UiPage output ports, which have no implemented runtime meaning. Preserve the general validator's MVP rules rather than tightening its global semantics.

Reject empty/duplicate port names, unavailable predecessor outputs, and mismatched declarations. First module inputs must be empty. Phase 1 supports exact Qt QVariant types `bool`, `int`, `double`, `QString`, `QByteArray`, `QStringList`, `QVariantList`, `QVariantMap`, plus any valid value for `QVariant`. Reject unknown type declarations; never interpolate arbitrary type text into C++.

At runtime validate returned output keys, validity and declared types; bind only the downstream declared keys. Missing, extra or mistyped outputs fail with the node and port identified. Do not substitute defaults or convert values. A null factory or implementation exception produces an explicit startup failure, not a success placeholder.

### Fixed contracts and lifetime

The namespace remains the existing deterministic `Blueprint_...` identifier. For a protected `Module_HASH` contract:

```cpp
// LogicModule: make execute an implemented contract in runtime mode only.
virtual Outputs execute(const Inputs &inputs) = 0;
std::unique_ptr<Module_HASH> createModule_HASH(QObject *parent = nullptr);

// UiPage: retain its QWidget base; pass initial values to the factory.
std::unique_ptr<Module_HASH> createModule_HASH(
    const Inputs &inputs, QWidget *parent = nullptr);
```

Declarations reside at namespace scope after their classes. AI defines these exact functions, without repeating default arguments, and may choose any internal implementation class name. The runtime passes nullptr parents and owns returned objects. It keeps the page alive across `app.exec()` and prevents delete-on-close from conflicting with its ownership. QApplication exists before any factory creates widgets; controller destruction occurs before QApplication destruction.

## Implementation boundaries

Create:

- `src/generation/runtime_assembler.h/.cpp`: strict linear validation, flow order, safe literal emission, and generated `src/runtime/workflow_controller.h/.cpp` plus `src/main.cpp`. No scheduler or registry.
- `tests/tst_runtime_assembly.cpp`: deterministic generation, graph/data rejection, scaffold versions, factory/protected-file/manifest checks.
- `tests/tst_runtime_end_to_end.cpp` and `tests/fixtures/runtime-linear/`: offline real application build/run/Export evidence. Arbitrary implementation class names; stable factories only.

Modify narrowly:

- `project_scaffolder.h/.cpp`: explicit runtime API, v2 factories/main/controller/CMake/README, unchanged v1 output.
- `workspace_io.cpp`: explicit v1/v2 recognition with all six-field schema and record checks retained.
- `generation_controller.h/.cpp`: runtime entry point, shared implementation, captured mode in both scaffold revalidation points, in-memory CandidateBatch mode, exact factory responsibility in the actual prompt.
- `candidate_review_dialog.h/.cpp`: optional appended mode argument with legacy default; both pre-accept scaffold revalidation points use that pinned mode.
- `main_window.cpp`: default Generate calls the runtime entry point; the explicit legacy action calls the module-only entry point. Pass batch mode into Candidate Review, with no automatic fallback, persisted mode switch or unrelated lifecycle changes.
- `CMakeLists.txt`, `tests/CMakeLists.txt`: source and tests registration; retain all translation targets.
- `tests/tst_generation_workflow.cpp`: runtime preparations for existing GUI protection tests, plus unsupported topology/old workspace rejection; preserve all original assertions.
- `tests/tst_blueprint_persistence.cpp`, `tests/tst_workspace_main_integration.cpp`: adapt only generation lifecycle fixtures to runtime-valid inputs and v2 scaffolds. Keep generic persistence metadata, legacy browsing/External fixtures, and all existing assertions; add GUI rejection checks with zero provider calls and unchanged workspace/source state.
- `README.md`, `doc/project-usage-guide.md`, `doc/project-roadmap.md`, `doc/implementation-progress.md`: product usage, exact supported semantics, legacy workspace boundary, and final evidence. Do not rewrite MVP acceptance history or release notes.

## Task 1: Red tests before runtime implementation

- [x] Write runtime scaffold/generator and offline end-to-end tests first. Establish only the minimum compilable API boundary, then observe behavioral failures for missing assembly before adding implementation.
- [x] Assert fixed factory signatures and protected runtime/main files instead of searching for any guessed implementation class.

```cpp
BlueprintDocument linearDocument()
{
    BlueprintDocument document;
    document.projectId = "runtime-phase1";
    document.projectName = "Runtime Phase 1";
    document.target = "qt6-widgets";
    for (const auto &entry : {
             qMakePair(QString("start"), NodeType::Start),
             qMakePair(QString("logic"), NodeType::LogicModule),
             qMakePair(QString("ui"), NodeType::UiPage),
             qMakePair(QString("end"), NodeType::End)}) {
        BlueprintNode node;
        node.id = node.name = entry.first;
        node.type = entry.second;
        node.description = "Runtime module contract";
        if (node.type == NodeType::LogicModule)
            node.outputs.append({"message", "QString", "Displayed result"});
        if (node.type == NodeType::UiPage)
            node.inputs.append({"message", "QString", "Displayed result"});
        document.nodes.append(node);
    }
    document.edges = {{"e1", "start", "logic", {}},
                      {"e2", "logic", "ui", {}},
                      {"e3", "ui", "end", {}}};
    return document;
}

QByteArray readFile(const QString &workspace, const QString &relative)
{
    QFile file(QDir(workspace).filePath(relative));
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

void runtimeScaffoldHasProtectedEntry()
{
    QTemporaryDir workspace;
    QString error;
    QVERIFY2(ProjectScaffolder::createRuntime(linearDocument(), workspace.path(), &error),
             qPrintable(error));
    QVERIFY(readFile(workspace.path(), "generated-project/src/main.cpp")
                .contains("WorkflowController"));
    const QJsonObject manifest = QJsonDocument::fromJson(
        readFile(workspace.path(), "generation-manifest.json")).object();
    QCOMPARE(manifest.value("version").toInt(), 2);
}
```

The fixture helper returns a fully valid Start/Logic/Ui/End document with no initial input and a QString `message` output/input pair. Test helpers remain in tests, not production. Runtime scaffold API returns failure before the feature is implemented, producing a real assertion failure.

- [x] Run focused red tests in `C:/bp-runtime-assembly-build`; save QtTest XML and distinguish expected feature failures from setup/compiler failures. Both new targets compiled successfully. Runtime red XML recorded 26 pass / 10 fail; end-to-end red XML recorded 2 pass / 2 fail. Assertions detected the missing runtime implementation and incorrect mode propagation; the frozen v1 golden already passed. Retained red evidence is separate from final suite logs.

```powershell
cmake --build C:/bp-runtime-assembly-build --parallel 4
ctest --test-dir C:/bp-runtime-assembly-build -R '^runtime_(assembly|end_to_end)$' --output-on-failure
```

## Task 2: Implement the smallest deterministic assembly

- [x] Walk the single flow, not ID-sorted modules, and validate topology/ports before the transaction. Generate protected source from that order with safely escaped node/port literals.
- [x] Generate fixed factories in runtime contracts. Main creates QApplication, runs the controller, reports failures, and enters the event loop only for a displayed page.
- [x] CMake links the generated controller into `generated_app`, including explicit references to every fixed factory. An empty/partial/legacy-only candidate set must not link successfully.
- [x] Retain exact module-only output and explicitly reject cross-version scaffold requests without changing any workspace bytes.
- [x] Route default GUI requests through `startRuntime`, keep old `start` as the legacy API, and pin mode in the success callback, CandidateBatch and both Candidate Review accept rechecks.
- [x] Prompts explicitly require definitions of protected factory signatures, not just derived classes. The output JSON remains unchanged.
- [x] Run focused runtime and generation lifecycle tests, then the complete suite including workflow/controller, Candidate Review, workspace protection, scaffold and manifest tests. Keep every pre-existing protection assertion.

## Task 3: Offline real application evidence and regression guards

- [x] Use a new fake response fixture through the real generation/persist/Candidate Review flow; do not alter FakeAiClient to implement runtime behavior.
- [x] Use reverse-sorted node IDs and two logic modules. First execute returns `message`; second consumes and transforms it. The page factory consumes the transformed value and displays it in a QLabel.
- [x] On the fixture page's queued show observation, verify `isVisible()`, the real QLabel value, and execution order. Print a fixture success marker and quit normally. This code is only in the accepted offline fixture; production main has no testing flag or automatic exit.
- [x] Build with production BuildService; launch the actual `generated_app` using QProcess under offscreen Qt, with finite timeout, NormalExit, exit code 0 and expected stdout marker assertions.

```cpp
process.start(generatedExecutable);
QVERIFY(process.waitForStarted(10000));
QVERIFY(process.waitForFinished(20000));
QCOMPARE(process.exitStatus(), QProcess::NormalExit);
QCOMPARE(process.exitCode(), 0);
QVERIFY(process.readAllStandardOutput().contains("RUNTIME_CHAIN_OK"));
```

- [x] Export using ProjectExporter, independently configure/build/test the exported project, and launch its `generated_app` with the same assertions. Runtime protected files and accepted byte hashes must survive export.
- [x] Demonstrate test sensitivity by mutations confined to temporary generated artifacts: omit execute, wrong data binding, or omit page.show. Each must fail; restore the artifacts. Do not mutate or reset user files.
- [x] Test null factory, exceptions, missing/mistyped/extra outputs, unavailable/unknown inputs, missing factory link, v1↔v2 refusal, runtime protected candidate targets, manual scaffold changes, stale Candidate Review and mode changes.
- [x] Prove deterministic files for repeated/reordered semantic input and layout-independent behavior; retain the old login-demo/Decision module-only end-to-end test unchanged.

## Task 4: Review and delivery

- [x] Independent specification review against the user requirements; fix missing or extra behavior before quality review.
- [x] Independent quality/security review, including adapted GUI fixtures; no unresolved Critical or Important findings.
- [x] Full build and CTest, inspect every QtTest XML including skips and generated-project tests. No paid API calls.
- [x] Update product docs and progress with actual results and unsupported semantics; don't imply Decision, interactive downstream page outputs, branches, parallelism, arbitrary type conversion or AI rework are implemented.

Delivery gate after verification: check the staged diff/status and sensitive patterns, verify local/remote MVP annotated object and peeled commit remain unchanged, commit/push only `feature/runtime-assembly-phase-1`, create a main-base PR, attach it and stop. Record the resulting commit and PR in the final handoff. Never merge or force push.

## Verification environment

The independent worktree is `C:/Users/Lenovo/.config/superpowers/worktrees/project/runtime-assembly-phase-1`. Configure with Qt `E:/Qt/6.9.3/mingw_64` and compiler `E:/Qt/Tools/mingw1310_64/bin/g++.exe`; prepend Qt/MinGW/Ninja to PATH. Editor baseline fresh build and CTest passed 30/30 in 204.04 seconds before any implementation. A generated CTest XML logging helper lives outside Git; it does not change test selections or environments.

Final verification on 2026-10-11: full build succeeded; runtime focused CTest 2/2 (153.09 seconds), lifecycle focused CTest 2/2 (71.95 seconds), and final full CTest 32/32 (226.39 seconds) passed. All 32 detailed QtTest XML files parsed completely: 984 pass, zero fail, one pre-existing Windows case-alias picker skip. Generated and exported runtime project CTest each passed 4/4, and both executables were actually launched. An external logger helper's duplicate output arguments were corrected and the full suite rerun; no production code was changed for that tooling issue. Real paid API requests: zero.

## PR #50 compatibility review follow-up

The owner's review identified a real GUI compatibility gap: preserving legacy core APIs alone does not let ordinary users generate additional candidates in existing v1 workspaces. This follow-up restores that explicit GUI entry without changing runtime contracts or silently falling back after a runtime failure.

Files: `src/app/main_window.h/.cpp` for two named actions and explicit mode routing; `tests/tst_generation_workflow.cpp` for GUI mode/workspace/Candidate regressions; `tests/tst_blueprint_scene.cpp` and `tests/tst_language_switch.cpp` for shared menu actions, enablement and translations; `translations/BlueprintEditor_zh_CN.ts` for the two action labels. Product documentation is updated by the primary agent; Git operations and test runs remain centralized.

- [x] Add GUI tests before the fix. Look up an explicit `generateLegacyModuleAction`, trigger real QActions, and observe the missing entry as a behavioral assertion failure, not a compiler failure. Compiled Red reports: workflow 4 pass / 10 fail, node menu 2 pass / 1 fail, language 2 pass / 2 fail. Enhanced fresh/existing workspace continuation rows also failed at the missing action after their real persistence/accept preparations succeeded.
- [x] Verify runtime remains the preferred AI-menu/toolbar action (`generateSelectedNodeAction`). Add the legacy action to the AI menu and node context menu, not a global persisted mode or automatic mode detector. Labels must distinguish runtime generation from module-only legacy generation in English and Chinese.
- [x] Route each action to an explicitly chosen mode through a shared private method:

```cpp
void startGeneration(ProjectScaffolder::Mode mode);
// Runtime action: startGeneration(ProjectScaffolder::Mode::LinearRuntime)
// Legacy action:  startGeneration(ProjectScaffolder::Mode::ModulesOnly)
```

Use the corresponding action's enabled guard, capture the existing blueprint/node/workspace snapshot, and call only `startRuntime()` or `start()` for the chosen mode. Reuse the existing CandidateBatch mode and Candidate Review rechecks unchanged. Runtime targets LogicModule/UiPage, legacy targets LogicModule/UiPage/Decision; both retain exact-one-selection and generating/review guards.

- [x] Cover fresh and existing v1 legacy generation and Candidate Accept, v2 runtime generation, both cross-version refusals without provider calls or writes, and a generic-valid Decision blueprint explicitly supported by legacy. Runtime validation failures must never call legacy as a fallback. Existing-workspace fixtures preserve a real previously accepted file and its manifest/batch records across new GUI acceptance.
- [x] Preserve all previous protection, lifecycle and repaint assertions. Change menu label/ordering/type expectations only where the new explicit actions require it. Validate shared QAction identity and language switching, including a legacy Candidate still using v1 revalidation.
- [x] Run focused GUI/generation/runtime tests with exactly one XML logger per registration, then full CTest. Focused 6/6 (53.91 seconds) and full 32/32 (258.75 seconds) passed; all 32 XML reports parsed: 993 pass, zero fail, one existing platform skip. All 250 source translations match after XML newline normalization, with no missing, extra, unfinished or duplicate entries.
- [x] Independent specification review approved the targeted compatibility adjustment; no specification fixes required.
- [x] Complete independent quality/safety review and record its final assessment before Git delivery. Ready; no unresolved Critical, Important or Minor findings. The reviewer independently inspected the diff, both JUnit evidence and all 32 detailed reports; production APIs and mode-pinned safety checks remain unchanged.

After verification, append a normal fix commit and push to the existing PR #50 branch. Reply to the review comment with the actual changes and tests. Do not merge, rewrite history, alter the MVP Tag, request paid AI or start another runtime phase.
