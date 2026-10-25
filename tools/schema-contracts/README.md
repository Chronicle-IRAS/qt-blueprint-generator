# Blueprint contracts, Plugin Registry and preserving documents — Draft

This private, standalone TypeScript package validates Blueprint v2 offline. [The machine JSON Schema](../../schemas/blueprint-v2.schema.json) remains the sole structural authority. The current Qt application still reads Schema 1; this tool is not integrated into the GUI, CMake, generation or Runtime paths. Setup and existing test/typecheck commands are in [schemas/README](../../schemas/README.md).

## Read-only API

[semantic-validator.ts](semantic-validator.ts) exports:

```typescript
validateBlueprintSemantics(input: unknown, context: TrustedSemanticContext): Diagnostic[]
```

The synchronous function first applies the existing Ajv structural validator. Structural failures return `STRUCTURE_INVALID` with Ajv's instance path and stop semantic checking. A structurally valid document is checked against caller-supplied, trusted, read-only descriptions. Neither input nor context is modified. Empty diagnostics mean the implemented checks passed; they do not authorize generation or prove executability.

`TrustedSemanticContext` is plain data, not a Registry, PluginHost, manifest loader or plugin callback API. The caller must supply it independently of the untrusted blueprint; copying a document's declarations into this context would defeat lock and definition checks. The TypeScript context is a caller contract, not an arbitrary-JSON context parser. Duplicate or inconsistent descriptions are rejected with `CONTEXT_INVALID`.

For example, inside this package's TypeScript/Vitest environment:

```typescript
import { readFileSync } from "node:fs";
import { validateBlueprintSemantics } from "./semantic-validator.js";
import { trustedContext } from "./semantic-context.fixture.js";

const input: unknown = JSON.parse(readFileSync(new URL(
  "../../tests/fixtures/blueprint-v2/valid/single-qt.json", import.meta.url,
), "utf8"));
const diagnostics = validateBlueprintSemantics(input, trustedContext);
// []: this fixture passes with these independently authored test descriptions.
```

`semantic-context.fixture.ts` is test data. A future caller supplies its own vetted descriptions through the same API.

| Context field | Meaning |
| --- | --- |
| `pluginLocks` | Trusted `pluginId/package/version/sha256/pluginApiVersion`, compared exactly to document metadata |
| `nodeDefinitions` | `typeId/version/pluginId/supportedTargets`; definition version is separate from package version |
| `targets` | Explicit `targetId/pluginId` provider descriptions |
| `portTypes` | Registered native port type IDs |
| `compatibility` | Explicit `sourceTypeId/targetTypeId` pairs for ordinary edges; directional, with no reverse or transitive inference |
| `bridges` | `pluginId/version`, explicit `mappings` and per-side `requiredDriverCount`; adapter version is independent of package version |
| `portCardinality` | Instance `nodeId/portId` with optional nonnegative integer `min/max`; no rule means no cardinality restriction |

Each Bridge mapping names `sourceTargetId`, `targetTargetId`, `sourceTypeId`, `targetTypeId` and `wireTypeId`. Native types need not equal the wire type; an exact declared mapping is required. Native-to-native ordinary-edge compatibility does not replace a Bridge mapping. Each side's required-driver count is satisfied only by existing, unambiguous bindings owned by that endpoint's valid Unit. Omitting the optional binding list is legal only when the policy does not require drivers on that side.

Port cardinality counts declared endpoint occurrences across both edges and channels when the endpoint resolves. An invalid connection can still occupy its resolved port and also produces its own diagnostic; counts are not a claim that the connection can execute. Duplicate IDs are diagnosed without choosing a first/last record to resolve ambiguity. Port IDs are unique within a node, not across the entire document.

## Diagnostics

Every diagnostic has stable `code`, `severity: "error"`, a readable `message`, and `path`. Relevant locations also carry `nodeId`, `portId`, `edgeId`, `channelId`, `unitId`, `driverBindingId` or `pluginId`. Consumers should branch on code and location, not parse the message. Paths refer to the parsed input, except `/context` and `/context/portCardinality` for context-related errors.

| Codes | Check |
| --- | --- |
| `STRUCTURE_INVALID`, `CONTEXT_INVALID` | Structure gate or conflicting trusted descriptions |
| `DUPLICATE_UNIT_ID`, `DUPLICATE_NODE_ID`, `DUPLICATE_PORT_ID`, `DUPLICATE_EDGE_ID`, `DUPLICATE_CHANNEL_ID`, `DUPLICATE_DRIVER_BINDING_ID`, `DUPLICATE_PLUGIN_LOCK` | Identity uniqueness |
| `UNIT_NOT_FOUND`, `NODE_NOT_FOUND`, `PORT_NOT_FOUND` | Missing or ambiguous references |
| `PLUGIN_LOCK_MISSING`, `PLUGIN_UNTRUSTED`, `PLUGIN_LOCK_MISMATCH` | Provider lock and trusted metadata |
| `TARGET_UNKNOWN`, `TARGET_PROVIDER_MISMATCH`, `NODE_DEFINITION_UNKNOWN`, `NODE_TARGET_UNSUPPORTED` | Explicit definitions, versions and target support |
| `PORT_TYPE_UNKNOWN`, `PORT_DIRECTION`, `EDGE_KIND`, `EDGE_TYPE`, `EDGE_CROSS_UNIT` | Registered types, output-to-input edges, kind and Unit boundaries |
| `CHANNEL_UNIT_MISMATCH`, `CHANNEL_KIND`, `BRIDGE_UNKNOWN`, `BRIDGE_MAPPING_UNSUPPORTED` | Channel endpoints and explicit Bridge policy |
| `DRIVER_NOT_FOUND`, `DRIVER_WRONG_UNIT`, `BRIDGE_DRIVER_REQUIRED` | Binding resolution, ownership and required count |
| `CARDINALITY_ENDPOINT_UNKNOWN`, `PORT_CARDINALITY` | Declared per-port cardinality |

For example, the original `semantic-only/same-target-cross-unit-edge.json` structurally passes but contains an ordinary edge between two distinct Qt Units. With matching trusted fixture descriptions, its semantic diagnostic includes:

```json
{
  "code": "EDGE_CROSS_UNIT",
  "severity": "error",
  "message": "Ordinary edge crosses Unit boundary",
  "path": "/edges/0",
  "edgeId": "cross-unit"
}
```

## Scope and evidence

Multiple Units, multiple compatible node providers in one Unit and arbitrary domain nodes are legal. There are no Core Start/End, cycle or branch rules. A future generation capability may reject unsupported multi-Unit/channel inputs; that product limitation does not belong in this validator.

The original 59 fixture JSON files and 75 structure tests remain unchanged. Separate semantic tests apply trusted descriptions to the five original `semantic-only` cases and cover additional semantic behaviors. Existing `valid/` means structurally valid, not automatically valid against every trusted context. API examples and diagnostics are implementation evidence, not Qt/Embedded product acceptance.

Package digest checking compares metadata only; no actual package or driver bytes are hashed. The P0.2 API does not check full driver-interface compatibility, plugin property schemas, wire schema payload validity or target business rules. It does not import drivers, allocate/migrate dynamic IDs, save unknown raw bytes, compile IR, generate/build/export, execute plugins or contact AI/network services. Plugin API v1 and the broader platform contracts remain Draft.

## Plugin Descriptor assembly

P0.3 adds [a declarative Descriptor Schema](../../schemas/plugin-descriptor-v1.schema.json) and [plugin-descriptors.ts](plugin-descriptors.ts). The [Draft protocol](../../doc/architecture/plugin-descriptor-contracts.md) defines versions, dependencies, ownership, static ports, properties and Target/Bridge policy.

```typescript
import { assembleTrustedContext, validateBlueprintWithDescriptors } from "./plugin-descriptors.js";

const assembled = assembleTrustedContext(descriptors, independentTrustedLocks);
// assembled.context exists only when assembled.diagnostics is empty.
const diagnostics = validateBlueprintWithDescriptors(blueprint, descriptors, independentTrustedLocks);
```

Arguments are parsed JSON values. The caller must independently vet locks and the source of descriptions; matching metadata is not artifact verification. Capabilities grant no permissions. Successful context assembly feeds the existing P0.2 API; the wrapper additionally enforces properties, static port snapshots/cardinality and Blueprint dependency-lock closure. Inputs stay unchanged. Dynamic port resolution is not implemented.

No installed plugins is a valid initial state: `assembleTrustedContext([], [])` succeeds with an empty context. A structurally valid empty Blueprint (all six record arrays empty) passes the wrapper with empty descriptors and locks. Structure and reference checks still run: locks, Units, nodes or Bridges referring to unavailable plugins produce explicit diagnostics, even if independent trust pins exist. No graph or extension data is removed.

The [offline fixture](../../tests/fixtures/plugin-descriptors/README.md) and plugin-descriptors.test.ts provide complete examples with independent literal pins. Existing npm test discovers the suite without new dependencies or commands. Exact versions have no range resolution or automatic upgrade. All contracts remain Draft.

## Minimal Plugin Registry

[plugin-registry.ts](plugin-registry.ts) adds a synchronous, in-memory registry of caller-vetted declarative metadata. It does not install packages, execute plugins, grant capabilities, or integrate with Qt. It reuses P0.3 assembly/validation; the machine schemas and original P0 APIs keep their behavior.

| API | Behavior |
| --- | --- |
| `new PluginRegistry()` | Empty, valid initial snapshot; no plugins required |
| `register(descriptors: unknown, trustedLocks: unknown): RegistryResult` | Add a whole bundle atomically; pass arrays of new descriptors and their independent locks, not copies of already registered pins |
| `unregister(pluginIds: readonly string[]): RegistryResult` | Explicit batch removal, no cascade; unknown IDs fail; repeated IDs name the same removal |
| `snapshot(): RegistrySnapshot` | Unified discovery: `plugins/nodes/types/targets/bridges/context`, sorted by provider and declaration identity/version |
| `getPlugin(pluginId)` | Descriptor and its independent lock, or `undefined` |
| `getNode(typeId, nodeDefinitionVersion)` | Exact node definition with owner and full static port/property metadata |
| `getType(typeId)`, `getTarget(targetId)` | Exact registered declarations with owner |
| `getBridge(pluginId, adapterVersion)` | Exact Bridge declaration; adapter and package versions are distinct |
| `getDependents(pluginId)` | Sorted direct manifest dependents; prerequisites are in `getPlugin(id).descriptor.manifest.dependencies` |
| `validate(blueprint: unknown): Diagnostic[]` | Full descriptor-aware validation against the current installed metadata; never filters or edits the graph |

`RegistryResult` always contains `diagnostics`; an empty array means the mutation succeeded. Registration copies parsed JSON, validates the complete candidate set and publishes only after preparing a fully frozen snapshot. Failures leave the same previous snapshot and all queries intact. Duplicate providers never replace existing registrations or trust pins. Locks with no corresponding descriptor are not retained or used by future registrations. A valid multi-provider bundle may need to arrive together: a node's Target advertisement is not a reverse manifest dependency, but its reference still must resolve.

Unregistration revalidates the entire remainder, including declaration references, not just manifest dependencies. Even a provider with no declared dependents can be required by another node's Target advertisement. Remove an explicitly chosen complete bundle if necessary; the Registry never deletes dependents or changes documents for the caller.

Snapshots, lookup results, context and nested metadata are recursively frozen and detached from registration inputs; unknown JSON extensions survive. Old snapshots remain immutable point-in-time values, not live views or revocable permissions. Use `registry.validate()` for the current Registry; `snapshot().context` is directly consumable by P0.2 but, as before, does not itself enforce P0.3 properties/static-port/instance-cardinality checks. No raw-byte persistence or GUI placeholder is implemented.

Inputs must be parsed JSON data. Cycles, accessors, sparse arrays (including out-of-range numeric properties), non-finite numbers and non-JSON JavaScript values are rejected with `REGISTRY_INPUT_INVALID`; snapshot preparation failures yield `REGISTRY_PREPARATION_FAILED` without publication. `REGISTRY_NOT_FOUND` identifies unknown removal IDs at `/pluginIds/i`. Other codes come from P0.3; mutation paths refer to the complete candidate set, including existing descriptors. Copying and freezing do not establish provenance, verify package bytes, sandbox hostile schemas/proxies, or authorize execution.

### Offline lifecycle example

Inside this package's TypeScript/Vitest environment, the existing fixtures demonstrate the lifecycle below. The literal pins are test metadata, not verified package digests or production trust establishment. [plugin-registry.test.ts](plugin-registry.test.ts) runs these behaviors against the real implementation.

```typescript
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { PluginRegistry } from "./plugin-registry.js";

const fixture = (name: string): unknown => JSON.parse(readFileSync(new URL(
  `../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url,
), "utf8"));
const descriptors = fixture("descriptors"), blueprint = fixture("blueprint");
const independentPins = [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0",
    sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0",
    sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
];
const registry = new PluginRegistry();
assert(registry.validate(blueprint).some(d => d.code === "PLUGIN_UNTRUSTED"));
assert.deepEqual(registry.register(descriptors, independentPins).diagnostics, []);
assert.equal(registry.getNode("org.example.pipeline/source", "3.0.0")?.declaration.title, "Reading source");
assert.deepEqual(registry.getDependents("org.example.math"), ["org.example.pipeline"]);
assert.deepEqual(registry.validate(blueprint), []);
assert(registry.unregister(["org.example.math"]).diagnostics.some(d => d.code === "DEPENDENCY_MISSING"));
assert.deepEqual(registry.validate(blueprint), []); // failed removal changed nothing
assert.deepEqual(registry.unregister(["org.example.pipeline", "org.example.math"]).diagnostics, []);
assert(registry.validate(blueprint).some(d => d.code === "PLUGIN_UNTRUSTED")); // document retained
```

Run `npm test -- --run plugin-registry.test.ts` for Registry regressions, `npm test` for all suites, and `npm run typecheck`. No new dependency, CMake integration or generated project is required. Executable plugin lifecycle remains out of scope.

## Read-only Blueprint document reader

P1.2A adds [blueprint-document-reader.ts](blueprint-document-reader.ts):

```typescript
readBlueprintDocument(sourceText: string, registry: PluginRegistry): BlueprintDocumentReadResult
```

The caller supplies JSON text already decoded as UTF-8 and an existing, independently populated Registry. This synchronous API does not open files, register plugins, repair input, save documents or authorize generation. It reuses the authoritative Blueprint Schema and the Registry's full P0.2/P0.3 descriptor-aware validation. There is no second set of graph, port, lock or plugin compatibility rules.

| Result | Meaning |
| --- | --- |
| `status: "rejected"` | Invalid JSON, unsupported Schema version or structural error; no usable `document` or node views |
| `status: "read"` | Structurally valid v2 document; complete parsed `document` and ordered `nodes` remain available even when semantic diagnostics exist |
| `sourceText` | Exact original string in either result, including whitespace, escapes and Unicode; never reconstructed with JSON.stringify |
| `diagnostics` | Existing structured codes, severity, paths and related IDs; predictable validation order |
| `semanticValid` | On a read result, whether Registry validation returned no diagnostics; not a generation-capability check |
| `canGenerate: false` | Always false, including valid empty documents and graphs with all plugins available |

### KnownNode and UnknownNode

Each node view contains `sourceIndex`, `nodeId`, `unitId`, `typeId`, `nodeDefinitionVersion` and the complete read-only `data` record. `sourceIndex` is the original zero-based `nodes` array index, associated with `/nodes/{sourceIndex}` diagnostics. Duplicate node IDs remain separate records in source order; the reader does not select a winner, deduplicate nodes or remove edges.

`kind: "known"` requires both an exact `getNode(typeId, nodeDefinitionVersion)` match and the node provider's unique document lock matching its registered trusted lock. It exposes the definition's display `title`. Missing plugins/definitions and absent, ambiguous or mismatched locks instead yield `kind: "unknown"` with `reasonCodes`, while retaining the original node's properties, ports and unknown fields. The reader consumes existing lock diagnostics; it does not independently compare another copy of the five-field lock contract or substitute a nearby definition version.

Recognition is separate from validity: ordinary property, static-port, connection or Unit semantic errors do not by themselves make a trusted exact definition unknown. The view stays KnownNode and the read result reports `semanticValid: false` with the original diagnostics. A KnownNode is neither an executable plugin instance nor evidence that the whole graph is valid.

### Preservation and rejection boundary

Published results, documents, node views and diagnostics are recursively frozen. Nested unknown JSON values and ordinary keys such as `__proto__` remain inert data. The result's top-level generation flag cannot be enabled by a similarly named extension. Reading does not mutate Registry state, and later registration/unregistration does not rewrite an earlier result; call the reader again to obtain current availability.

Malformed JSON returns `JSON_PARSE_ERROR` without echoing the input or engine-specific exception text. Numeric Schema versions other than 2 return `SCHEMA_VERSION_UNSUPPORTED`; missing/incorrectly typed versions and other shape errors use `STRUCTURE_INVALID`. JSON parsing is strict: BOM, comments and trailing commas are rejected rather than removed. Version 1 and future versions are not migrated or downgraded.

The preserved text is a decoded string, not an arbitrary input-byte container. Parsed values follow JSON.parse semantics (including number representation and duplicate-key handling). No save API, opaque-payload byte slicing/roundtrip, layout reader, dynamic port migration, GUI integration, RPC, Core IR or generation is implemented. The Qt/C++ Schema 1 reader and existing generation protocols are unchanged. Matching trusted lock metadata still does not verify package or driver bytes.

### Offline read example

Inside this package's TypeScript/Vitest environment:

```typescript
import assert from "node:assert/strict";
import { readFileSync } from "node:fs";
import { PluginRegistry } from "./plugin-registry.js";
import { readBlueprintDocument } from "./blueprint-document-reader.js";

const sourceText = readFileSync(new URL(
  "../../tests/fixtures/plugin-descriptors/blueprint.json", import.meta.url,
), "utf8");
const registry = new PluginRegistry();
const result = readBlueprintDocument(sourceText, registry);
assert.equal(result.sourceText, sourceText);
assert.equal(result.canGenerate, false);
assert.equal(result.status, "read");
if (result.status === "read") {
  assert.equal(result.semanticValid, false); // no fixture plugins registered
  assert.equal(result.nodes[0].sourceIndex, 0);
  assert.equal(result.nodes[0].kind, "unknown");
  assert.equal(result.nodes[0].nodeId, "source"); // original node is still present
}
```

Registering the independently vetted fixture descriptors and pins as in the Registry example allows exact known definitions to be displayed. It does not enable generation. The empty structural fixture reads successfully with an empty Registry and no semantic diagnostics. Run `npm test --prefix tools/schema-contracts -- blueprint-document-reader.test.ts` from the repository root for reader regressions, then the existing complete test and typecheck commands.

## Preserving Blueprint document store

P1.2B1 adds a separate asynchronous Node store around the Reader. Its scope is an **unchanged source text**, stored as UTF-8 in `blueprint.json` and read back for verification. It never rebuilds JSON with `JSON.stringify(document)`. Whitespace, CRLF, Chinese text, Unicode escape spelling, unknown fields/nodes, locks, ports, edges, channels and driver bindings remain in the original text. Invalid UTF-8 files and text that cannot roundtrip through UTF-8 are refused rather than silently replaced. This does not implement editing or preservation of opaque byte slices after an edit; that belongs to P1.2B2.

The existing Reader remains the only JSON/Schema gate. Structure errors reject a load/save; semantic errors are returned with the complete read result and **allow preserving saves**. Missing plugins, untrusted locks and duplicate IDs do not cause records to be removed. Every operation result keeps `canGenerate: false`.

### Store API

```typescript
loadBlueprintDocument(directory: string, registry: PluginRegistry): Promise<BlueprintDocumentLoadResult>
saveBlueprintDocument(request: BlueprintDocumentSaveRequest, registry: PluginRegistry): Promise<BlueprintDocumentSaveResult>
```

`BlueprintDocumentSaveRequest` contains `directory`, `sourceText` and explicit `mode: "create" | "save-as" | "overwrite"`. Overwrite additionally requires `expectedSha256`; use the exact-byte hash returned by a prior load/save, not a hash of reserialized JSON. Create/save-as never authorize replacement of an existing target.

The store captures these scalar request fields before its first asynchronous operation and uses the validated absolute directory throughout the save. Later caller changes to the request object cannot change the selected destination, source, mode or overwrite baseline; the caller's object is not frozen or modified.

Successful load/save results use `status: "loaded"` / `"saved"`, `readResult` (the Reader's full `status: "read"` result) and `sha256`. A successful save has `writeState: "verified"`. Failures use `status: "failed"`, stable `code`, a safe message and, when available, `readResult` with Reader diagnostics. Saving distinguishes `writeState: "not-written"` from `"committed-unverified"`; the latter requires inspecting/reloading the destination before another save. If safe temp cleanup fails, `cleanupFailed` and `retainedTempPath` report it. Do not retry by deleting files or discarding the expected hash.

Example in this package's TypeScript environment, using caller-selected existing empty directories:

```typescript
import { PluginRegistry } from "./plugin-registry.js";
import { loadBlueprintDocument, saveBlueprintDocument } from "./blueprint-document-store.js";

const registry = new PluginRegistry();
const created = await saveBlueprintDocument({ directory, sourceText, mode: "create" }, registry);
if (created.status === "failed") throw new Error(created.code);
const loaded = await loadBlueprintDocument(directory, registry);
if (loaded.status === "failed") throw new Error(loaded.code);
const unchangedSave = await saveBlueprintDocument({
  directory, sourceText: loaded.readResult.sourceText,
  mode: "overwrite", expectedSha256: loaded.sha256,
}, registry);
if (unchangedSave.status === "failed") throw new Error(unchangedSave.code);
const copy = await saveBlueprintDocument({
  directory: anotherEmptyDirectory, sourceText: loaded.readResult.sourceText, mode: "save-as",
}, registry);
if (copy.status === "failed") throw new Error(copy.code);
// Neither saved result permits generation.
```

This is a text storage boundary, not an edit API. It does not merge documents, modify individual properties or automatically update locks. Source bytes are validated again on saving; callers cannot authorize a save by fabricating a successful Reader result.

### Destination policy

The caller explicitly selects an existing absolute standalone project directory; the target name is always `blueprint.json`. The store does not create directories or derive paths from JSON fields. Creation and explicit save-as require an empty directory. Overwrite requires an existing structurally valid v2 file and its expected SHA-256; unrelated directory entries are refused, not removed. Layout files are not managed in this stage.

Directory ancestors and file types are checked. Generated-workspace manifests, exported scaffold markers and enclosing blueprint projects prevent writes into protected projects. Schema 1 and unsupported/invalid existing files cannot be overwritten. Links, junctions, reparse points, hard-linked targets, filename case aliases, dot traversal, device/UNC/drive-relative paths, alternate data streams and Windows reserved/trailing-dot or trailing-space names are refused. Chinese paths and ordinary spaces are supported.

On Windows, the built-in Windows PowerShell executable performs a fixed, noninteractive native attribute probe with paths passed as data. It checks the [ReparsePoint attribute](https://learn.microsoft.com/en-us/dotnet/api/system.io.fileattributes); an unavailable/failed probe refuses the operation. No npm dependency, native addon, plugin execution or RPC service is added. The PowerShell process is hidden and does not run document-supplied code. On other platforms the Node filesystem checks apply; Windows-specific validation must be reported separately from tests run elsewhere.

### Actual write guarantees

| Situation | Guarantee and limit |
| --- | --- |
| Before publication | Original target is never opened for truncation. An exclusive same-directory temporary file receives complete bytes, is flushed and verified first. A write/sync/pre-commit failure leaves the original target intact. |
| Creation / save-as | Publishes the completed temp through a no-clobber link operation, then removes the owned temp. A target created concurrently is not overwritten. Filesystems without this operation fail; there is no overwrite fallback. |
| Controlled overwrite | Compares actual disk SHA-256 to the caller's baseline initially and immediately before same-directory rename replacement. A detected change/missing file prevents publication. |
| Cleanup | Removes only this operation's temporary file when its identity/path remains safe. Unknown files and stale temps are never automatically deleted. Cleanup failure must identify retained temporary state. |
| After publication | Reopens the file, checks bytes and parses through the Reader. Verification failure is explicitly distinguished from a pre-commit failure; the new file may already be present. No blind rollback may overwrite another writer's data. |
| Concurrency | In-process overlapping loads/saves using the same normalized absolute path are guarded (case-folded on Windows); this is not a universal filesystem-alias lock. Hash/identity checks are not an OS compare-and-swap: another process can change a file or replace a path after the final check. Node path APIs do not eliminate this race. Use trusted local directories without concurrent external writers. |
| Process/system crash | A crash may leave a temp or interrupt post-commit verification. File flush and single-file publication are not a guarantee of directory-entry durability on every filesystem. There is no cross-file/crash transaction, automatic recovery or stale-file deletion. |

The API does not preserve filesystem metadata such as ACLs, ownership or timestamps as a document-format contract. The byte-preservation claim concerns the valid original UTF-8 JSON text. Neither generation workspaces nor the Qt/C++ ProjectStore participate in this store.

## Restricted Blueprint document edits

P1.2B2 adds [blueprint-document-editor.ts](blueprint-document-editor.ts). The pure API accepts original text, explicit operations and the current Registry:

```typescript
prepareBlueprintEdits(sourceText, [
  { kind: "project-name", value: "演示工程" },
  { kind: "node-property", sourceIndex: 0, property: "label", value: "温度" },
  { kind: "port-name", sourceIndex: 0, portIndex: 0, portId: "out", value: "输出" },
], registry);
```

The result is either `status: "prepared"` with candidate `sourceText`, the full post-edit `readResult` and its diagnostics, or `status: "failed"` with a stable `code` and diagnostics and no approved candidate text. All results keep `canGenerate: false`. The whole batch succeeds or fails; repeated/overlapping targets are rejected, even if their replacement values agree. This function performs no file I/O and does not modify the input text, operations or Registry.

| Target | Edit permission |
| --- | --- |
| Top-level `projectName` | String replacement in any structurally valid, unambiguous v2 document, including documents with missing plugins or semantic errors |
| KnownNode direct property | Existing own scalar member, explicitly declared in the exact trusted definition's root `propertiesSchema.properties`; new value must be string, boolean, finite number or null and satisfy its contract |
| KnownNode static port `name` | String replacement; `sourceIndex`, `portIndex` and supplied `portId` must resolve exactly to an existing, uniquely identified static declaration |
| UnknownNode and all other fields | Read-only: identities, locks, versions, nested values, ports other than display names, edges, channels, driver bindings, and unknown extensions cannot be edited |

KnownNode recognition is re-evaluated by the Reader using exact definitions and trusted locks. A duplicate node ID does not collapse array records: `sourceIndex` selects the intended record. Port names never select ports. Pattern-only, conditional-only or reference-only property declarations do not grant edit permission; there must be a direct declaration. The existing P0.3 Ajv compiler/evaluator checks the complete edited property object, including conditional/cross-property rules. Errors on other fields may remain, but they cannot conceal a newly introduced contract failure or an invalid edited field.

Special property names are treated as data. Declared `constructor`, slash and tilde names can be edited using own-member lookup. `__proto__` is an explicit read-only exception: the current Ajv evaluator skips its `properties` constraint, so this API refuses to certify replacements rather than introducing different validation rules. Other edits preserve its original text. This does not change P0.3 or Reader behavior.

### Original-text guarantee

[json-value-spans.ts](json-value-spans.ts) uses bounded iterative token/span traversal, with strict JSON syntax and the existing Reader/Schema as validation authorities. It locates decoded object keys and original array positions; regular expressions and string searches do not choose field locations. Duplicate decoded keys anywhere in the source, including escaped aliases and opaque payloads, refuse editing. Existing B1 unchanged-text saving retains its previous behavior.

Only the authorized scalar value ranges are replaced. Every other text fragment retains identical UTF-8 bytes: whitespace, CRLF, indentation, key order, escape spelling, unrelated number literals, unknown fields/nodes and nested opaque payloads. Replacement scalar spelling may change; the whole document is never rebuilt with `JSON.stringify(document)`. Raw unpaired surrogates and BOM are refused. Negative zero replacements remain `-0`.

Limits are explicit: source and candidate at most 8 Mi UTF-16 code units, at most 250,000 indexed values, 16,384 container depth and 256 operations per batch. Empty batches are rejected. Iterative scanning rejects excess resources before unbounded Reader work and supports deeply nested opaque data without recursive traversal. These edit limits do not redefine the Blueprint machine Schema or the existing Reader/Store acceptance rules.

### Save and edit again

```typescript
const loaded = await loadBlueprintDocument(directory, registry);
if (loaded.status === "loaded") {
  const saved = await saveEditedBlueprintDocument({
    directory,
    sourceText: loaded.readResult.sourceText,
    expectedSha256: loaded.sha256,
    edits: [{ kind: "project-name", value: "新名称" }],
  }, registry);
  if (saved.status === "saved") {
    // Use saved.readResult.sourceText and saved.sha256 for the next edit.
  }
}
```

This entry point re-prepares edits against the current Registry; a previously prepared result is not an authorization token. It checks that the original source's UTF-8 SHA-256 matches the supplied disk baseline before I/O, then delegates controlled overwrite to B1 `saveBlueprintDocument`. Requests and scalar operations are captured before awaiting. There is no second filesystem transaction or path policy.

The Store's optional `expectedRegistrySnapshot` is checked at entry, before publication and after re-read. Changes observed before publication refuse the write; changes observed after publication return `REGISTRY_CHANGED` with `writeState: "committed-unverified"`. Ordinary Store callers without this guard retain their existing behavior. Snapshot identity is an in-process authority guard, not a cross-process filesystem lock.

Edit-validation failures return `writeState: "not-written"` and no approved candidate. Store failures retain the existing `code`, `message` and `writeState`; source/baseline or disk conflicts report `CONFLICT`. Only `status: "saved", writeState: "verified"` confirms the post-write content and Reader result. Do not blindly retry or roll back `committed-unverified`: reload and inspect the actual file first. The concurrency, path-replacement, metadata and crash limits in **Actual write guarantees** still apply unchanged.

This is not a general JSON editor or a save mechanism for layout, node/port insertion, dynamic ports or plugin migration. It does not enable generation or change the legacy Qt/C++ persistence path.

## Qt read-only reader process

P1.2C adds the version1 operation of a one-shot local Node adapter around the existing Reader and Plugin Registry: read supplied UTF-8 JSON text. That compatible operation has no path or write access. P1.2D adds the separate version2 load/preview/save operations described below; neither version exposes generation, build or plugin execution.

Build the compiled runtime after the normal locked dependency installation:

```powershell
npm ci --ignore-scripts --prefix tools/schema-contracts
npm run build:reader --prefix tools/schema-contracts
```

The default output is the repository's `build/v2-reader`. For a different destination, pass an absolute directory after `--`. The output contains `tools/schema-contracts/blueprint-reader-cli.js`, the unchanged machine Schemas and the existing Ajv runtime dependency closure. It can be moved as a whole. It requires the supported Node 24 runtime; it does not execute TypeScript source or depend on a global TS loader. CMake uses the same local build when prerequisites exist, without downloading dependencies. The Qt Schema 1 application remains usable when reader prerequisites are absent, with an actionable error at the v2-open entry.

The caller starts Node directly with the compiled entry, writes one JSON request to stdin and closes stdin. Stdout is reserved for one JSON reply. The pure `handleReaderRequest(input)` is separately testable:

```typescript
const reply = handleReaderRequest({
  protocolVersion: 1,
  requestId: "open-1",
  sourceText,
  registry: { descriptors, trustedLocks },
});
```

The reply echoes `protocolVersion: 1` and `requestId`, always includes `canGenerate: false`, and has `status: "read" | "rejected" | "error"`. Read/rejected results carry the existing Reader fields and diagnostics. Errors identify invalid transport/configuration rather than turning a failed Registry registration into an empty Registry. An empty Registry must be explicit (`descriptors: [], trustedLocks: []`).

Declarations and pins are independent host inputs. Qt may load a user-selected `{descriptors,trustedLocks}` JSON bundle, but does not discover or trust it from the Blueprint's pluginLocks. These are trusted metadata, not proof of package byte integrity, installation or execution rights.

Blueprint source is limited to 8 MiB UTF-8; stdin/reply messages to 32 MiB. Qt bounds stderr and enforces a 15-second process deadline, rejects malformed/version-mismatched/uncorrelated replies and ignores obsolete results. Missing Node, timeout, crash or invalid reply cannot replace the active project. Only the supported display data is rendered; structural and semantic decisions stay in TS. This process is not an OS sandbox or a general Plugin Host.

Successful version1 reading does not authorize saving. The v2 canvas remains structurally read-only; the restricted form and saving introduced in P1.2D use the version2 operations rather than turning `semanticValid` into permission. See [the viewing guide](../../doc/project-usage-guide.md#510-只读查看-blueprint-v2) and [restricted GUI editing](../../doc/project-usage-guide.md#511-blueprint-v2-受限编辑与保存).

## Qt restricted document operations (Draft transport version 2)

P1.2D adds `handleBlueprintDocumentRequest(input)` in `blueprint-document-protocol.ts`. The same compiled CLI awaits one versioned request and emits one correlated reply; it is not a persistent Host or network RPC service. Transport version2 is independent of Blueprint schemaVersion2 and generation manifest versions. Existing version1 read requests and `handleReaderRequest()` remain compatible.

All requests carry `protocolVersion: 2`, `requestId`, `operation` and the independent `{descriptors,trustedLocks}` Registry input. Only these document operations are available:

| Operation | Payload | Authority |
| --- | --- | --- |
| `load` | Absolute standalone `directory` | Existing Store load supplies the exact source/hash. Policy-rejected but structurally readable sources can remain displayed read-only, with no writable baseline. |
| `preview` | Original baseline `sourceText`, complete canonical `edits` batch | Existing B2 preparation; updates Reader data and diagnostics without file writes. |
| `save` | `directory`, original baseline `sourceText`, `expectedSha256`, complete canonical `edits` | Existing B2 save, then B1 controlled overwrite and re-read; no alternate transaction or path policy. |

The envelope repeats version/requestId/operation and `canGenerate:false`. `result` carries the existing load/preparation/save result, including failure codes and `writeState`; `readResult` and `editTargets` provide display data and form hints when available. `writable` and `readOnlyReason` distinguish file-policy eligibility from document structure or semantic validity. Only a Store `loaded` result establishes a save baseline, and only `saved` with `writeState: "verified"` advances it.

Project-name and port-name edits keep B2's ordinary string values. Node-property edits use `valueLiteral`, a JSON scalar literal string at the transport boundary, so Qt number serialization cannot silently convert `-0` to `0`. The adapter decodes that single literal and passes the resulting finite scalar to B2; it never reconstructs the document. For example:

```typescript
await handleBlueprintDocumentRequest({
  protocolVersion: 2, requestId: "preview-1", operation: "preview",
  registry: { descriptors, trustedLocks }, sourceText: originalSource,
  edits: [
    { kind: "project-name", value: "新工程" },
    { kind: "node-property", sourceIndex: 0, property: "label", valueLiteral: '"温度"' },
    { kind: "port-name", sourceIndex: 0, portIndex: 0, portId: "out", value: "输出" },
  ],
});
```

Shared B2 target eligibility supplies the form's available fields; submitting them is not an authorization token. Each preview/save reconstructs the explicit Registry and revalidates the edits. KnownNode with an invalid original property can still be offered a repair; UnknownNode internals, undeclared/nested properties and identity/connection fields remain unavailable. Multiple changes to one target replace its pending edit; save still uses the original disk source/hash, not an intermediate preview's source.

Qt serializes saving within the active document. While a save runs, another save, preview, configuration change, project switch or close is refused. A lost/crashed/timed-out writer or malformed reply has an uncertain commit outcome: retain the draft and old baseline, do not auto-retry, and require explicit reload/inspection. A returned `committed-unverified` likewise does not mean the old file is intact. Store `not-written` failures retain the existing ordinary failure guarantees; no rollback may overwrite another writer. Cross-process/path-replacement/crash limits in **Actual write guarantees** remain unchanged.

No v2 GUI SaveAs, new-project creation, layout persistence, dynamic ports, plugin execution or generation is added. Runtime bundling continues to stage the existing production TS dependency closure and schemas without new packages or build-time downloads.
