# P1.1 Minimal Plugin Registry Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.
>
> User-selected execution: Codex implements and integrates natively; one independent final reviewer. No complex multi-agent orchestration. If the native execution skill is unavailable, follow this checklist with the available TDD and verification skills.

**Goal:** Register, discover, query and unregister trusted declarative plugins atomically, feeding the existing Blueprint v2 validators without executing plugin code.

**Architecture:** A synchronous in-memory `PluginRegistry` owns an isolated snapshot of caller-vetted Descriptor JSON and matching independent locks. Every mutation prepares a complete candidate set and reuses P0.3 `assembleTrustedContext`; publish only a fully valid snapshot. Graph validation delegates to `validateBlueprintWithDescriptors`, never filters or edits the graph.

**Tech Stack:** Existing TypeScript 5.9.3 / Node 24.18.1 / Ajv 8.20.0 / Vitest 5.0.4 package under `tools/schema-contracts`; no new dependency or service.

**Spec:** User's P1.1 request; `doc/architecture/plugin-platform.md`, `plugin-api-v1.md`, `plugin-descriptor-contracts.md`, `blueprint-schema-v2.md`, and `doc/project-roadmap.md`. The P0.3 static, exact-version subset is implemented; executable lifecycle interfaces in the broader Draft remain proposals.

## Global Constraints

- Base: remote `main` at `56d9183fd9889b962a3cffdca41aab2872e673db`, merged PR #54; branch `feature/plugin-registry` in its own worktree.
- Preserve zero-plugin startup and valid dependency-free empty blueprints. Missing plugins must still produce diagnostics and leave parsed document data unchanged.
- JSON Schema remains structural authority. TypeScript declarations are readonly views of validated data, not a divergent runtime schema.
- Trust comes from the caller's independent locks and verified descriptor provenance. Neither manifest capabilities nor matching metadata grant permissions or verify package bytes.
- No automatic download, version upgrade/replacement, cascade removal, plugin callbacks, dynamic execution, Cordis, GUI, Graph IR, generation, paid AI, or C++/Qt production changes.
- Do not rewrite history, merge PRs, or modify the MVP tag. Do not implement the next stage.

## Review Focus

1. A failed batch must not leak its earlier valid declarations or trust pins into subsequent queries/validation (Task 1).
2. Removing a provider may break supportedTarget references even without a reverse manifest dependency; revalidate the complete remainder, not just the dependency graph (Task 2).
3. Mutating registration input or returned nested metadata must not mutate Registry state; unknown JSON extensions must survive snapshots (Task 1).
4. Independent pins without descriptors do not install plugins or make missing-plugin blueprints valid (Tasks 1 and 3).
5. Exact node-definition and Bridge versions must not collapse to a guessed latest version; discovery order must be stable (Task 1).

## Task 1: Atomic registration and unified discovery

**Files:** Create `tools/schema-contracts/plugin-registry.ts` and `plugin-registry.test.ts`. Modify `plugin-descriptors.ts` only to expose/complete readonly descriptor type views; preserve its runtime behavior and original tests.

**Interfaces:** Export `PluginRegistry`, `RegistryResult { diagnostics: readonly Diagnostic[] }`, `RegisteredPlugin { descriptor, lock }`, `Declaration<T> { pluginId, declaration }`, and `RegistrySnapshot { plugins, nodes, types, targets, bridges, context }`. Snapshot `context` is the existing `TrustedSemanticContext`; declaration arrays contain the corresponding P0.3 metadata, including full static-port/property and extension data.

- [x] Add behavioral tests for `new PluginRegistry().snapshot()` and `register(descriptors: unknown, trustedLocks: unknown): RegistryResult`. First use a minimal testable stub to observe a real assertion failure rather than just an unresolved import.
- [x] Implement batch registration against current state plus proposed additions, validating the whole candidate set through P0.3. Empty arrays succeed. Missing/mismatched locks, malformed records, duplicates, dependencies, versions, cycles, ownership and cross-references fail without state publication.
- [x] Keep a single installed package per plugin ID; repeated IDs fail, never silently replace. Accept bundles because closed-set Target/node advertisements may require several descriptors to arrive together. Do not require an arbitrary sequential installation order.
- [x] Store only locks corresponding to installed descriptors; independent pins alone do not install a provider. A later registration must provide its own pins. Existing installed locks cannot be overwritten by a registration.
- [x] Implement deterministic `snapshot()` and exact `getPlugin(pluginId)`, `getNode(typeId, nodeDefinitionVersion)`, `getType(typeId)`, `getTarget(targetId)`, `getBridge(pluginId, adapterVersion)` lookups; unknown keys return `undefined`. Snapshot arrays are unified discovery, sorted by identity/version with provider ownership attached.
- [x] Test a valid two-provider bundle, multiple definition/Bridge versions, input order changes, a wholly renamed domain, unknown extension retention, and nested input/output mutation isolation. Use detached recursively frozen JSON snapshots; handle snapshot preparation failures without mutating installed state. This is not a sandbox for arbitrary JavaScript objects or hostile schemas.
- [x] Run `npm test -- --run plugin-registry.test.ts`; confirm RED then GREEN for each new behavior before proceeding. Changes are delivered as one coherent final P1.1 commit with README, not separate subtask commits.

## Task 2: Dependency-safe unregister

**Files:** Extend the same Registry and test file; no separate lifecycle subsystem.

**Interfaces:** `unregister(pluginIds: readonly string[]): RegistryResult`. Empty removal succeeds; unknown plugin IDs produce `REGISTRY_NOT_FOUND`. Removal is explicit and batch-based, with no cascade. `getPlugin(id).descriptor.manifest.dependencies` exposes declared direct requirements; `getDependents(pluginId): readonly string[]` lists installed direct dependents, sorted.

- [x] Write failing tests: remove a leaf; refuse a still-required provider; refuse removal that leaves a dangling Target advertisement; remove a complete bundle atomically; unknown IDs and a partly valid removal batch leave state unchanged.
- [x] Stage the remainder and revalidate all surviving descriptors and locks through the same P0.3 gate, then publish once. Reuse dependency/version diagnostics rather than building a second dependency resolver or executing a topological activation plan.
- [x] Test direct and transitive dependency effects and equality of every public snapshot/query before and after failed operations.
- [x] Run focused tests and typecheck; commit coherent behavior only after GREEN.

## Task 3: Existing validator integration and delivery

**Files:** Registry/tests; update `tools/schema-contracts/README.md`, root `README.md`, `doc/architecture/plugin-platform.md`, `plugin-api-v1.md`, `plugin-descriptor-contracts.md`, `doc/project-roadmap.md`, and prepend current results to `doc/implementation-progress.md`. Keep historical acceptance records intact. No new protocol/schema format.

**Interfaces:** `validate(blueprint: unknown): Diagnostic[]` uses the current installed descriptors and their locks. `snapshot().context` can be passed directly to existing P0.2 `validateBlueprintSemantics`; descriptor-aware checks stay in P0.3.

- [x] Write failing tests for empty startup/empty graph, invalid graph structure, missing plugins before registration, valid graph after registration, diagnostics after explicit bundle removal, and successful re-registration of the same plugins against the unchanged graph.
- [x] Cover changed static ports/properties/cardinality, dependency lock closure and lock metadata mismatch via the Registry entry point. Compare to the existing descriptor-aware validator, with independently specified diagnostic codes and locations. Freeze graph input and verify all unknown parsed JSON data is retained.
- [x] Document actual APIs and an understandable offline register → query → validate → unregister example. Distinguish declarative metadata registration from installation, activation, GUI placeholder/persistence, and actual execution.
- [x] Run original P0.1–P0.3 plus new P1.1 tests with `npm test`, then `npm run typecheck`. Baseline personally run: 219/219 and typecheck exit 0. Final run: 265/265 and typecheck exit 0. Report actual runs; no CTest claim when C++ is unchanged.
- [x] Perform one independent specification/quality Review; fix real findings with regressions and use the same reviewer for focused rechecks. A sparse-array numeric-key boundary was reproduced by three failing tests and fixed; reviewer rechecked 46/46 and reported Ready, no remaining findings. Root scope/link/history/tag checks pass; P0.3 emitted JS is unchanged.
- [ ] Commit, push `feature/plugin-registry`, create PR with base `main`, attach it to the chat, verify remote/local/PR SHA, no auto-merge, then stop. No next-stage implementation.

## Preparation status

- [x] Read architecture documents and P0.1–P0.3 implementation/API boundaries.
- [x] Confirm PR #54 merged, fetch latest remote main, create clean isolated branch/worktree without touching the old workspaces.
- [x] Install existing pinned dependencies offline (`npm ci --offline --ignore-scripts --no-audit --no-fund`); baseline 219/219 and typecheck exit 0.
- [x] User confirms this plan before implementation, as required by the writing-plans workflow (2026-10-19).
