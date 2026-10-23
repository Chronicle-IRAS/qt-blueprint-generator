# P1.2B2 Restricted Blueprint Edits Implementation Plan

> **For agentic workers:** Use subagent-driven-development: one cohesive implementation worker, one independent read-only reviewer, primary integration. User explicitly authorizes design and implementation together without a separate approval gate. Steps use checkboxes.

**Goal:** Edit three authorized kinds of scalar values in original Blueprint v2 text, preserving every other UTF-8 fragment, and save through the existing guarded Store.

**Architecture:** Strict JSON and Blueprint acceptance remain Reader/Registry responsibilities. A bounded iterative JSON span index detects duplicate decoded keys and resolves exact array/object positions, without serializing the document. The editor authorizes targets from current trusted declarations, validates a whole candidate, and sends only valid candidates with their original-byte baseline to the existing Store. An optional Registry snapshot condition in that transaction prevents stale edit authority during asynchronous saving.

**Tech Stack:** Existing TypeScript, Node 24, Ajv and Vitest; no new dependency. A general JSON AST dependency would add another parser dependency for a scalar-only edit surface. A narrow iterative span index checks resource and ambiguity bounds before the Reader's strict JSON.parse and Schema gate; no indexed source is authorized without that gate. It is not a second Blueprint Schema validator.

**Spec:** User P1.2B2 request; [v2 Draft](../architecture/blueprint-schema-v2.md), [roadmap](../project-roadmap.md), existing Reader/Store and P0.3 descriptor validation.

## Constraints and decisions

- Base latest origin/main `d0eb638d1fb989a5d7e74228e3280278a260bc88` (PR #59 merged); branch `feature/p1.2b2-blueprint-restricted-edits` in independent worktree. Baseline 373/373 and typecheck passed.
- Pure `prepareBlueprintEdits(sourceText, edits, registry)` accepts explicit discriminated operations: `{kind:"project-name",value:string}`, `{kind:"node-property",sourceIndex:number,property:string,value:Scalar}`, `{kind:"port-name",sourceIndex:number,portIndex:number,portId:string,value:string}`. Scalar is string/boolean/finite number/null. No arbitrary paths, insertion or deletion.
- A node property must already be a direct scalar own member of an object, explicitly declared in the trusted definition's root `propertiesSchema.properties` map. Pattern-only, conditional-only or reference-only declarations do not grant edit authority. No nested editing or unknown property creation. Special property names remain data, using own-member/Map lookups.
- Evaluator boundary discovered by behavior tests: current Ajv skips a `properties.__proto__` constraint. That node-property target remains read-only with an explicit refusal, including apparently valid replacements; do not change P0.3 behavior or introduce another validator. Other edits retain its original bytes. Declared `constructor` and slash/tilde names remain supported.
- Node targets use sourceIndex, never ID resolution. KnownNode means exact definition plus matching trusted lock as decided by Reader. Port target additionally matches original array position, explicit portId, unique instance/declaration ID and a static declaration. Display names never identify ports.
- Entire source must be lossless UTF-8, strictly valid v2, and free of duplicate decoded JSON keys anywhere, including opaque data. Escaped aliases of the same key are duplicates. Refuse ambiguity or overlapping/repeated edit ranges, including identical repeated operations. An empty edit request is rejected explicitly.
- Iterative scanner has explicit bounds: 8 Mi UTF-16 code units of source/candidate, 250,000 indexed JSON values, 16,384 container depth, 256 edits. Limits reject with structured diagnostics; no silent truncation. Include a 10,000-level opaque fixture that succeeds without recursive traversal. No regex/search chooses JSON locations. JSON.stringify is allowed only for one replacement scalar; preserve edited -0 explicitly.
- Apply disjoint replacements in descending offset order. Immutable results include current Reader diagnostics and canGenerate:false. Failures contain no candidate sourceText/read result that could be mistaken for approved edits.
- Re-run Reader/Registry on candidate. Existing unrelated semantic diagnostics may remain. For edited node properties, compare detailed before/after Ajv errors from the same P0.3 property-schema compiler/evaluator, not aggregate NODE_PROPERTIES codes alone; reject new contract failures and evaluation uncertainty. Reuse the existing compiler via a minimal shared helper if necessary; retain P0.3 public diagnostics/behavior. Explicitly test unrelated invalid sibling properties and new conditional/cross-property errors.
- `saveEditedBlueprintDocument({directory,sourceText,expectedSha256,edits},registry)` re-prepares with current Registry. Verify the lossless original UTF-8 hash equals the supplied baseline before any I/O. Never accept a fabricated/stale prepared result as authorization. Use Store overwrite with the unchanged baseline and original directory; Store owns all disk/path/temp/publication/cleanup logic.
- Capture request scalar fields and edit data synchronously before the first await. Add the smallest optional Store Registry-snapshot guard, checked at entry, immediately before publication and after re-read; normal B1 callers keep existing behavior. Registry changes before commit refuse, changes observed after commit report committed-unverified. No second file transaction or file policy.
- No new deps, machine Schema, Qt/C++, Schema 1/ProjectStore, generation/Candidate/manifest, plugin execution, GUI/layout, arrays/nodes/ports creation/deletion, migration, Core IR or paid AI.

## Files and ownership

- Worker: new `tools/schema-contracts/json-value-spans.ts`, `blueprint-document-editor.ts`, `blueprint-document-editor.test.ts`; a separate focused span test file if needed. Minimal shared property-evaluation helper/change in `plugin-descriptors.ts` and optional Store guard with focused tests; no Registry behavior changes.
- Primary: tool README, necessary architecture status, roadmap/progress and this plan; Git remote delivery.
- Reviewer: whole branch read-only, no overlapping edits or additional agents.

## Review focus

1. Duplicate escaped keys and special names such as __proto__/constructor cannot redirect authorization or choose a different range.
2. Invalid sibling properties or aggregate existing NODE_PROPERTIES must not hide a newly introduced field/conditional error, nor prohibit a valid edit elsewhere.
3. UTF-16 offsets, astral characters, Unicode escapes, unusual number spelling and CRLF preserve untouched UTF-8 bytes exactly.
4. Registry removal/replacement between prepare/save or during staging cannot retain stale node edit authority; project-name editing still allows unavailable plugins.
5. Stale source/hash pairs, late disk conflicts and postcommit failures must retain existing Store safety semantics without partial edits or misleading success.

## Task 1: Bounded editing and Store integration

- [x] Step 1: Add importable minimal API stubs and behavioral tests for literal project-name replacement and KnownNode property/port batch. Observe assertion RED, not missing-import failure; implement first GREEN.
- [x] Step 2: Add failing ambiguity/protection cases: duplicate keys including escaped aliases/opaque, overlapping targets, special keys, duplicate node IDs, wrong port position/ID, missing definition/untrusted lock, undeclared/nested/nonfinite values. Implement precise authorization and iterative indexing.
- [x] Step 3: Add preservation tests using hand-checked source literals with Chinese/astral text, escaped keys, CRLF, unknown nodes, opaque nesting and unusual numeric literals. Cover scanner resource bounds/depth without stack overflow and input/result immutability.
- [x] Step 4: Add property-schema failure tests, unrelated errors, conditional/cross-property failures and Registry state transitions. Reuse existing Ajv rules and Reader, reject new errors atomically with no approved text on failure.
- [x] Step 5: Add real temporary-directory save/reload/re-edit tests, source/hash mismatch, late disk conflict, write/rename/postcommit faults and mutable request/Registry changes. Delegate all file writes to existing Store and verify actual disk bytes; only narrow low-level fault injection.
- [x] Step 6: Full Vitest, typecheck and diff checks; record exact totals, platform skips and real RED/GREEN evidence. Existing 373 tests stay intact. No CMake/CTest needed for untouched Qt paths; report this explicitly.
- [x] Step 7: Update API/examples, editable/read-only fields, precise byte guarantee, resource bounds and Store concurrency/crash limitations. Update roadmap and progress without rewriting historical acceptance evidence.
- [ ] Step 8: Independent read-only spec/security/quality review, regression fixes and focused re-review. Fetch latest main, commit/push and create one PR base main; verify clean/matching HEAD, no merge or tag/release changes, then stop.

## Self-review

The supported edit set is deliberately smaller than JSON Schema's expressive surface. Direct declarations grant edit authority; existing schema evaluation supplies validity. Strict JSON parsing and the authoritative machine Schema remain unchanged. The Store receives the same original disk baseline, with only an additive in-memory authority condition. Every review focus has an explicit regression step, and no future GUI or generation stage is implied.
