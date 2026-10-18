# Blueprint and Plugin Descriptor contracts — P0.3 Draft

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
