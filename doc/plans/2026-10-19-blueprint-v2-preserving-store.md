# P1.2B1 Preserving Document Store Implementation Plan

> **For agentic workers:** Use subagent-driven-development for this cohesive task: one implementer, one independent read-only reviewer, primary integration. Steps use checkboxes.

**Goal:** Safely persist and reload unchanged Blueprint v2 JSON text in a separate project directory, including semantically invalid documents.

**Architecture:** A small asynchronous Node store uses the existing Reader as its only structure/semantic gate. It writes the original string's UTF-8 bytes to an exclusive same-directory temporary file, verifies staging, then publishes with explicit create/save-as or SHA-256 guarded overwrite. A separate path-policy helper owns platform safety checks; neither module knows target-specific node semantics.

**Tech Stack:** Existing TypeScript/Node 24, Vitest; no package dependencies. On Windows, a fixed noninteractive Windows PowerShell command reads native FileAttributes.ReparsePoint (path data passed separately, never interpolated into code). Fail closed if this check is unavailable. This is a local file-attribute adapter, not RPC or a plugin process.

**Spec:** User P1.2B1 request; [v2 Draft](../architecture/blueprint-schema-v2.md), [platform](../architecture/plugin-platform.md), existing [Reader](../../tools/schema-contracts/blueprint-document-reader.ts), read-only reference [Qt ProjectStore](../../src/blueprint/blueprint_project_store.cpp).

## Global constraints and decisions

- Base latest origin/main `f52f15da42f4c4cedccae438a0d4b096f594353b` (PR #58 merged). Independent branch `feature/p1.2b1-blueprint-preserving-store` and worktree `blueprint-v2-preserving-store`; baseline 303/303 plus typecheck passed.
- Only fixed `blueprint.json` in an explicitly supplied existing absolute standalone directory. Caller creates/selects directory; no recursive directory creation. Create/save-as require an empty directory; overwrite requires only a regular existing structurally valid v2 `blueprint.json`. Unexpected entries, layout files and case aliases are refused without deletion. No directory/file name derived from document data.
- Check every ancestor for links/reparse points and generated markers (`generation-manifest.json`, or scaffold `src/contracts/blueprint.json` + `source-blueprint.json`), and nested blueprint projects. Reject destination Schema 1, unsupported/invalid existing documents, nonregular/multiply-linked targets, drive roots, UNC/device/drive-relative paths, dot traversal, ADS, reserved Windows names and trailing-dot/space aliases. Chinese names and ordinary spaces are valid.
- Preserve original string exactly; never serialize parsed document for saving. Refuse strings that cannot losslessly encode/decode UTF-8 (raw unpaired surrogates), invalid UTF-8 files and BOM. Existing escaped Unicode text remains untouched. Reuse Reader, no new schema/Registry rules.
- Semantic errors including missing plugins/definitions, bad locks and duplicate IDs allow saving and are returned with complete data. `canGenerate` is always false.
- No Qt/C++/ProjectStore, machine Schema, Registry/P0 API, manifest, Candidate, dependency/lockfile or old fixture changes. No editing, layout, migration, GUI, generation, paid AI or P1.2B2.

## API and transaction

- `loadBlueprintDocument(directory: string, registry: PluginRegistry)` returns a structured load result containing the Reader result and exact-byte SHA-256, or a structured I/O/path/encoding/Reader rejection.
- `saveBlueprintDocument(request: { directory: string; sourceText: string; mode: "create" | "save-as" } | { directory: string; sourceText: string; mode: "overwrite"; expectedSha256: string }, registry: PluginRegistry)` returns structured saved/failed result, Reader diagnostics and always-disabled generation. It accepts text, not mutable parsed objects; provides no editing operation.
- Overwrite requires a well-formed expected SHA-256 and compares actual disk bytes initially and immediately before commit; a missing/different file is a conflict. Existing file must independently pass Reader structure gate; semantic validity is not required. A final recheck also checks path identity and directory eligibility.
- Stage with exclusive `wx` open, write full buffer, sync, close and verify staged bytes before publish. Never truncate destination or delete it first. For create/save-as use no-clobber hard-link publication of the completed temp then remove owned temp (unsupported filesystem -> explicit failure, no unsafe fallback). Overwrite uses same-directory rename replacement. Serialize/reject overlapping operations in-process; do not claim interprocess compare-and-swap.
- Reopen after commit, compare exact bytes and invoke Reader again; return its parsed result. Post-commit verification/cleanup failures report a distinct committed-but-unverified/cleanup state, never pretend the old file was preserved or restore blindly over a concurrent writer.
- Pre-commit I/O failures preserve old destination and remove only the owned temporary file when safe. Cleanup errors identify retained temp; never delete unknown/stale files automatically. Crash recovery and directory fsync durability are not promised; process death can leave temp files. No multi-file transaction, no claim of race-free path binding with Node path APIs.

## Review focus

1. Same-content hashes with changed file identity, destination created during staging, and a stale overwrite baseline: recheck and preserve unrelated bytes.
2. Windows junction/reparse ancestor, case/ADS/reserved-name aliases and paths containing quotes/Chinese: native attribute checks cannot become command injection.
3. Partial write/sync/rename failure and cleanup failure: old file remains before commit; only owned temp may be removed.
4. Invalid UTF-8/unpaired raw surrogates, escaped Unicode and CRLF: refuse lossy encoding, preserve valid bytes exactly.
5. Post-commit verification failure or external replacement: report uncertainty, do not overwrite a third-party change while attempting rollback.

## Task 1: Store, safety regression coverage and delivery

**Files:** create `tools/schema-contracts/blueprint-document-store.ts`, `blueprint-store-paths.ts`, `blueprint-document-store.test.ts`; optionally a separate focused path test file if useful. Primary updates tool README, architecture status, roadmap, progress and this plan. No broad refactor.

- [x] Step 1: Write failing creation/load/save-as, original-byte preservation and invalid-structure tests with real temporary directories and real Registry; observe behavior assertion RED before implementation.
- [x] Step 2: Implement the minimal Reader-backed store and encoding boundary; observe GREEN.
- [x] Step 3: Add failing overwrite/hash conflict, wrong existing format, generated/legacy directory, unsafe path and Windows junction cases; implement path checks and publication without truncating final file.
- [x] Step 4: Add staged-write/sync/rename/post-commit/cleanup fault tests and concurrent operations; use narrow fs spies only for otherwise unreliable failures, assert real disk bytes and directory contents. Cover all review focus cases, unknown plugins/nodes/fields, duplicate IDs and fixture records for locks/ports/edges/channels/drivers.
- [x] Step 5: Run full Vitest, typecheck, diff check; report exact passed/skipped totals and observed RED evidence. Do not claim CTest was run.
- [x] Step 6: Update actual API and safety guarantees, explicitly distinguish ordinary failure, concurrent modification and system crash, and defer edited opaque preservation to P1.2B2.
- [x] Step 7: Independent read-only spec/security/quality review, fix confirmed findings with regression tests, rerun affected checks and obtain focused re-review.
- [x] Step 8: Fetch latest main, normal commit/push, independent PR base main; verify clean status and matching remote HEAD. No automatic merge, tag changes or next-stage work.

Delivered as [PR #59](https://github.com/Chronicle-IRAS/qt-blueprint-generator/pull/59), base `main`, with 373/373 Vitest tests and independent review Ready. Request-snapshot regression fix: `9a614038bb1a3e2761a4c94d32fe8d2979e9a569`. Awaiting human review; P1.2B2 is not started.

## Plan review

The same Reader is authoritative for source and existing target; no semantic diagnostic is promoted to a saving veto. Strict standalone directory policy avoids guessing whether unrelated user files belong to this format. Fixed target filename prevents path injection from JSON. Separate pre/post-commit outcomes avoid false rollback claims. Single-file publication and a documented final-check-to-rename race meet this stage without a new transaction format or native dependency. User authorized direct execution after this plan.
