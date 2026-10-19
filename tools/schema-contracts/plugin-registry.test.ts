import { readFileSync } from "node:fs";
import { expect, it } from "vitest";
import { PluginRegistry } from "./plugin-registry.js";
import { validateBlueprintWithDescriptors } from "./plugin-descriptors.js";
import { validateBlueprintSemantics } from "./semantic-validator.js";

const load = (name: string): any => JSON.parse(readFileSync(new URL(`../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url), "utf8"));
const pins = [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0", sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
];

it("starts with a complete empty snapshot and accepts zero-plugin registration", () => {
  const registry = new PluginRegistry();
  expect(registry.register([], [])).toEqual({ diagnostics: [] });
  expect(registry.snapshot()).toEqual({ plugins: [], nodes: [], types: [], targets: [], bridges: [], context: {
    pluginLocks: [], nodeDefinitions: [], targets: [], portTypes: [], compatibility: [], bridges: [], portCardinality: [],
  } });
});
it("publishes a valid closed bundle with provider-owned discovery records", () => {
  const registry = new PluginRegistry();
  expect(registry.register(load("descriptors"), pins).diagnostics).toEqual([]);
  expect(registry.snapshot()).toMatchObject({
    plugins: [{ lock: { pluginId: "org.example.math" } }, { lock: { pluginId: "org.example.pipeline" } }],
    nodes: [{ pluginId: "org.example.math", declaration: { typeId: "org.example.math/consumer", title: "Value consumer" } },
      { pluginId: "org.example.pipeline", declaration: { typeId: "org.example.pipeline/source" } }],
    bridges: [{ pluginId: "org.example.pipeline", declaration: { version: "adapter-7" } }],
  });
});

function freeze<T>(value: T): T {
  if (value && typeof value === "object") { Object.values(value).forEach(freeze); Object.freeze(value); }
  return value;
}
const codes = (result: { diagnostics: readonly { code: string }[] }) => result.diagnostics.map(d => d.code);
const populated = () => { const r = new PluginRegistry(); expect(r.register(load("descriptors"), pins).diagnostics).toEqual([]); return r; };
const independent = (id = "org.extra") => ({
  descriptorVersion: "1.0-draft", manifest: { manifestVersion: "1.0-draft", pluginId: id, package: "extra", version: "1",
    pluginApiVersion: "1.0-draft", supportedBlueprintSchemas: [2], nodeTypes: [], targets: [], capabilities: [], dependencies: [] },
  nodeDefinitions: [], types: [], targets: [], bridges: [], extensions: { note: "unknown metadata" },
});
const independentPin = (id = "org.extra") => ({ pluginId: id, package: "extra", version: "1", sha256: "c".repeat(64), pluginApiVersion: "1.0-draft" });

it.each([
  ["missing dependency", "DEPENDENCY_MISSING", (d: any) => { d[0].manifest.dependencies[0].pluginId = "org.absent"; }],
  ["version mismatch", "DEPENDENCY_VERSION", (d: any) => { d[0].manifest.dependencies[0].version = "other"; }],
  ["cycle", "DEPENDENCY_CYCLE", (d: any) => { d[1].manifest.dependencies.push({ pluginId: "org.example.pipeline", version: "1.2.0" }); }],
  ["foreign ownership", "DESCRIPTOR_OWNER", (d: any) => { d[0].types[0].typeId = "org.other/Type@1"; }],
  ["unknown type", "DESCRIPTOR_REFERENCE", (d: any) => { d[0].nodeDefinitions[0].ports[0].typeId = "org.absent/Type@1"; }],
  ["target mismatch", "TARGET_SUPPORT_MISMATCH", (d: any) => { d[0].targets[0].supportedNodeDefinitions.pop(); }],
  ["invalid property schema", "PROPERTY_SCHEMA_INVALID", (d: any) => { d[0].nodeDefinitions[0].propertiesSchema = { type: "unknown" }; }],
  ["duplicate node version", "DESCRIPTOR_DUPLICATE", (d: any) => { d[0].nodeDefinitions.push(structuredClone(d[0].nodeDefinitions[0])); }],
] as const)("keeps every installed record intact after batch %s", (_name, code, mutate) => {
  const r = new PluginRegistry();
  expect(r.register([independent()], [independentPin()]).diagnostics).toEqual([]);
  const before = r.snapshot(), descriptors = load("descriptors"); mutate(descriptors);
  expect(codes(r.register(descriptors, pins))).toContain(code);
  expect(r.snapshot()).toBe(before);
  expect(r.getPlugin("org.example.pipeline")).toBeUndefined();
});
it.each([{ descriptors: null }, { descriptors: {} }, { descriptors: [null] }])("rejects malformed batch $descriptors without changing state", ({ descriptors }) => {
  const r = populated(), before = r.snapshot();
  expect(codes(r.register(descriptors, []))).toContain("DESCRIPTOR_STRUCTURE");
  expect(r.snapshot()).toBe(before);
});
it("does not leak a valid member of a failed batch into a later registration", () => {
  const r = new PluginRegistry(), invalid = independent("org.invalid"); invalid.manifest.package = "mismatch";
  expect(codes(r.register([independent(), invalid], [independentPin(), independentPin("org.invalid")]))).toContain("DESCRIPTOR_LOCK_MISMATCH");
  expect(r.getPlugin("org.extra")).toBeUndefined();
  const dependent = independent("org.dependent") as any;
  dependent.manifest.dependencies = [{ pluginId: "org.extra", version: "1" }];
  expect(codes(r.register([dependent], [independentPin("org.dependent")]))).toContain("DEPENDENCY_MISSING");
  expect(r.register([independent()], [independentPin()]).diagnostics).toEqual([]);
});
it("retains no orphan trust pins and refuses absent or mismatched pins", () => {
  const r = new PluginRegistry();
  expect(r.register([], pins).diagnostics).toEqual([]);
  expect(r.snapshot()).toMatchObject({ plugins: [], context: { pluginLocks: [] } });
  expect(codes(r.register(load("descriptors"), []))).toContain("DESCRIPTOR_UNTRUSTED");
  expect(codes(r.register(load("descriptors"), [{ ...pins[0], sha256: "invalid" }, pins[1]]))).toContain("TRUSTED_LOCK_STRUCTURE");
  expect(codes(r.register(load("descriptors"), [{ ...pins[0], version: "other" }, pins[1]]))).toContain("DESCRIPTOR_LOCK_MISMATCH");
});
it("rejects duplicate provider IDs rather than replacing any package or trust metadata", () => {
  const r = populated(), before = r.snapshot();
  expect(codes(r.register(load("descriptors"), pins))).toContain("DESCRIPTOR_DUPLICATE");
  expect(codes(r.register([], [{ ...pins[0], sha256: "d".repeat(64) }]))).toContain("TRUSTED_LOCK_DUPLICATE");
  expect(r.snapshot()).toBe(before);
  expect(r.getPlugin("org.example.pipeline")?.lock.sha256).toBe("a".repeat(64));
});
it("discovers and queries exact definition and adapter versions without latest-version guesses", () => {
  const r = new PluginRegistry(), descriptors = load("descriptors");
  const node = structuredClone(descriptors[0].nodeDefinitions[0]); node.nodeDefinitionVersion = "3.1.0";
  descriptors[0].nodeDefinitions.push(node);
  descriptors[0].targets[0].supportedNodeDefinitions.push({ typeId: node.typeId, nodeDefinitionVersion: node.nodeDefinitionVersion });
  const bridge = structuredClone(descriptors[0].bridges[0]); bridge.version = "adapter-8"; descriptors[0].bridges.push(bridge);
  expect(r.register(descriptors, pins).diagnostics).toEqual([]);
  expect(r.getNode(node.typeId, "3.0.0")).toMatchObject({ declaration: { nodeDefinitionVersion: "3.0.0" } });
  expect(r.getNode(node.typeId, "3.1.0")).toMatchObject({ declaration: { nodeDefinitionVersion: "3.1.0" } });
  expect(r.getNode(node.typeId, "latest")).toBeUndefined();
  expect(r.getBridge("org.example.pipeline", "adapter-8")).toMatchObject({ declaration: { version: "adapter-8" } });
  expect(r.getBridge("org.example.pipeline", "1.2.0")).toBeUndefined();
  expect(r.getType("org.example.math/Value@1")).toMatchObject({ pluginId: "org.example.math", declaration: { compatibleTo: [] } });
  expect(r.getTarget("org.example.pipeline/application")).toMatchObject({ pluginId: "org.example.pipeline" });
  expect(r.getPlugin("missing")).toBeUndefined(); expect(r.getType("missing/type")).toBeUndefined(); expect(r.getTarget("missing/target")).toBeUndefined();
});
it("returns deeply frozen isolated metadata and retains unknown JSON including __proto__ keys", () => {
  const r = new PluginRegistry(), descriptors = load("descriptors"), locks = structuredClone(pins);
  descriptors[0].extensions.opaque = JSON.parse('{"__proto__":{"kept":true},"nested":[{"value":"original"}]}');
  expect(r.register(descriptors, locks).diagnostics).toEqual([]);
  const before = JSON.stringify(r.snapshot()), queried = r.getPlugin("org.example.pipeline")!;
  expect(queried).toBeDefined();
  descriptors[0].nodeDefinitions[0].ports[0].id = "changed"; locks[0].sha256 = "e".repeat(64);
  expect(JSON.stringify(r.snapshot())).toBe(before);
  expect(() => { (queried as any).descriptor.extensions.opaque.nested[0].value = "changed"; }).toThrow();
  expect(() => { (r.getNode("org.example.pipeline/source", "3.0.0") as any).declaration.ports.pop(); }).toThrow();
  expect(() => { (r.snapshot().context.pluginLocks as any).pop(); }).toThrow();
  expect((queried as any).descriptor.extensions.opaque["__proto__"]).toEqual({ kept: true });
  expect(JSON.stringify(r.snapshot())).toBe(before);
});
it("has stable discovery order and supports a wholly different domain without Core rules", () => {
  const a = populated(), b = new PluginRegistry();
  const descriptors = freeze(load("descriptors")), locks = freeze(structuredClone(pins)), before = JSON.stringify([descriptors, locks]);
  expect(b.register([...descriptors].reverse(), [...locks].reverse()).diagnostics).toEqual([]);
  expect(b.snapshot()).toEqual(a.snapshot()); expect(JSON.stringify([descriptors, locks])).toBe(before);
  const rename = (x: unknown) => JSON.parse(JSON.stringify(x).replaceAll("org.example.pipeline", "com.domain.ingest").replaceAll("org.example.math", "com.domain.storage"));
  const c = new PluginRegistry(); expect(c.register(rename(descriptors), rename(locks)).diagnostics).toEqual([]);
  expect(c.getNode("com.domain.storage/consumer", "4.0.0")).toMatchObject({ pluginId: "com.domain.storage" });
});
it.each([
  { name: "function", value: () => undefined }, { name: "undefined", value: undefined },
  { name: "non-finite", value: Infinity }, { name: "Date", value: new Date(0) },
])("rejects non-JSON $name without partial registration", ({ value }) => {
  const r = populated(), before = r.snapshot(), descriptor: any = independent(); descriptor.extensions.bad = value;
  expect(codes(r.register([descriptor], [independentPin()]))).toContain("REGISTRY_INPUT_INVALID");
  expect(r.snapshot()).toBe(before);
});
it("rejects cyclic or accessor metadata without evaluating callbacks or changing state", () => {
  const r = populated(), before = r.snapshot(), descriptor: any = independent(); descriptor.extensions.loop = descriptor;
  expect(codes(r.register([descriptor], [independentPin()]))).toContain("REGISTRY_INPUT_INVALID");
  delete descriptor.extensions.loop;
  let called = false;
  Object.defineProperty(descriptor.extensions, "getter", { enumerable: true, get() { called = true; return "value"; } });
  expect(codes(r.register([descriptor], [independentPin()]))).toContain("REGISTRY_INPUT_INVALID");
  expect(called).toBe(false); expect(r.snapshot()).toBe(before);
});
it.each(["descriptors", "pins", "unregister"] as const)("rejects an out-of-range numeric array key masquerading as a dense %s batch", operation => {
  const r = populated(), before = r.snapshot(), sparse = new Array(1) as any;
  sparse["4294967295"] = operation === "descriptors" ? independent() : operation === "pins" ? independentPin() : "org.example.pipeline";
  const result = operation === "descriptors" ? r.register(sparse, [independentPin()]) :
    operation === "pins" ? r.register([independent()], sparse) : r.unregister(sparse);
  expect(codes(result)).toContain("REGISTRY_INPUT_INVALID");
  expect(r.snapshot()).toBe(before); expect(r.getPlugin("org.extra")).toBeUndefined();
});

it("queries exact declared dependencies without inventing reverse Target dependencies", () => {
  const r = populated();
  expect(r.getDependents("org.example.math")).toEqual(["org.example.pipeline"]);
  expect(r.getDependents("org.example.pipeline")).toEqual([]);
  expect(r.getDependents("missing")).toEqual([]);
  expect(r.getPlugin("org.example.pipeline")?.descriptor.manifest.dependencies).toEqual([{ pluginId: "org.example.math", version: "2.0.0" }]);
  expect(() => { (r.getDependents("org.example.math") as string[]).push("changed"); }).toThrow();
});
it.each([
  { id: "org.example.math", code: "DEPENDENCY_MISSING" },
  { id: "org.example.pipeline", code: "DESCRIPTOR_REFERENCE" },
])("refuses removal of $id when remaining dependencies or advertisements would dangle", ({ id, code }) => {
  const r = populated(), before = r.snapshot();
  expect(codes(r.unregister([id]))).toContain(code);
  expect(r.snapshot()).toBe(before);
  expect(r.getNode("org.example.pipeline/source", "3.0.0")).toBe(before.nodes[1]);
});
it("unregisters a complete bundle while keeping prior snapshots intact", () => {
  const r = populated(), old = r.snapshot(), oldText = JSON.stringify(old);
  expect(r.unregister(["org.example.pipeline", "org.example.math"]).diagnostics).toEqual([]);
  expect(r.snapshot().plugins).toEqual([]); expect(r.snapshot().nodes).toEqual([]);
  expect(r.snapshot().types).toEqual([]); expect(r.snapshot().targets).toEqual([]); expect(r.snapshot().bridges).toEqual([]);
  expect(r.snapshot().context.pluginLocks).toEqual([]); expect(r.getPlugin("org.example.math")).toBeUndefined();
  expect(r.getNode("org.example.pipeline/source", "3.0.0")).toBeUndefined();
  expect(JSON.stringify(old)).toBe(oldText);
});
it("removes an independent leaf but does not partially apply an unknown-ID batch", () => {
  const r = populated(); expect(r.register([independent()], [independentPin()]).diagnostics).toEqual([]);
  const before = r.snapshot();
  expect(r.unregister(["org.extra", "org.absent"]).diagnostics).toContainEqual(expect.objectContaining({ code: "REGISTRY_NOT_FOUND", pluginId: "org.absent", path: "/pluginIds/1" }));
  expect(r.snapshot()).toBe(before);
  expect(r.unregister(["org.extra"]).diagnostics).toEqual([]);
  expect(r.getPlugin("org.extra")).toBeUndefined(); expect(r.snapshot().plugins).toHaveLength(2);
  expect(r.unregister([]).diagnostics).toEqual([]);
});
it("preserves transitive dependency closure while allowing explicit reverse-order removal", () => {
  const r = new PluginRegistry(), a: any = independent("org.a"), b: any = independent("org.b"), c: any = independent("org.c");
  b.manifest.dependencies = [{ pluginId: "org.a", version: "1" }]; c.manifest.dependencies = [{ pluginId: "org.b", version: "1" }];
  expect(r.register([c, a, b], [independentPin("org.a"), independentPin("org.b"), independentPin("org.c")]).diagnostics).toEqual([]);
  const before = r.snapshot();
  expect(codes(r.unregister(["org.a"])) ).toContain("DEPENDENCY_MISSING");
  expect(codes(r.unregister(["org.a", "org.b"])) ).toContain("DEPENDENCY_MISSING"); expect(r.snapshot()).toBe(before);
  expect(r.unregister(["org.c"]).diagnostics).toEqual([]);
  expect(r.unregister(["org.b"]).diagnostics).toEqual([]);
  expect(r.unregister(["org.a"]).diagnostics).toEqual([]); expect(r.snapshot().plugins).toEqual([]);
});
it.each([{ value: null }, { value: "org.extra" }, { value: [7] }])("rejects malformed unregister input $value without changing state", ({ value }) => {
  const r = populated(), before = r.snapshot();
  expect(codes(r.unregister(value as any))).toContain("REGISTRY_INPUT_INVALID"); expect(r.snapshot()).toBe(before);
});

it("allows an empty Blueprint with zero plugins without changing input or registry", () => {
  const r = new PluginRegistry(), doc = freeze({ schemaVersion: 2, projectId: "empty", projectName: "",
    pluginLocks: [], units: [], nodes: [], edges: [], channels: [], driverBindings: [], extensions: { future: ["保留", null] } });
  const before = JSON.stringify(doc), state = r.snapshot();
  expect(r.validate(doc)).toEqual([]); expect(JSON.stringify(doc)).toBe(before); expect(r.snapshot()).toBe(state);
});
it("diagnoses missing plugins, validates registration, and restores the unchanged document after re-registration", () => {
  const r = new PluginRegistry(), doc = load("blueprint");
  doc.nodes[0].opaque = { future: ["unknown", null, { kept: true }] }; freeze(doc);
  const before = JSON.stringify(doc);
  const expected = [
    expect.objectContaining({ code: "PLUGIN_UNTRUSTED", pluginId: "org.example.pipeline", path: "/pluginLocks/0" }),
    expect.objectContaining({ code: "NODE_DEFINITION_UNKNOWN", nodeId: "source" }),
    expect.objectContaining({ code: "TARGET_UNKNOWN", unitId: "main" }),
    expect.objectContaining({ code: "BRIDGE_UNKNOWN", channelId: "archive-value" }),
  ];
  expect(r.validate(doc)).toEqual(expect.arrayContaining(expected));
  expect(r.register(load("descriptors"), pins).diagnostics).toEqual([]); expect(r.validate(doc)).toEqual([]);
  expect(r.unregister(["org.example.pipeline", "org.example.math"]).diagnostics).toEqual([]);
  expect(r.validate(doc)).toEqual(expect.arrayContaining(expected)); expect(JSON.stringify(doc)).toBe(before);
  expect(r.register(load("descriptors"), pins).diagnostics).toEqual([]); expect(r.validate(doc)).toEqual([]);
  expect(JSON.stringify(doc)).toBe(before);
});
it("exports context directly consumable by the original P0.2 API", () => {
  const r = populated(), doc = freeze(load("blueprint")), state = r.snapshot();
  expect(validateBlueprintSemantics(doc, state.context)).toEqual([]);
  expect(r.validate(doc)).toEqual(validateBlueprintWithDescriptors(doc, load("descriptors"), pins));
  expect(r.snapshot()).toBe(state);
});
it.each([
  ["static ports", "NODE_PORT_CONTRACT", { nodeId: "source", portId: "out" }, (d: any) => { d.nodes[0].ports[0].typeId = "org.example.math/Value@1"; }],
  ["properties", "NODE_PROPERTIES", { nodeId: "source" }, (d: any) => { d.nodes[0].properties = { label: 7 }; }],
  ["cardinality", "PORT_CARDINALITY", { nodeId: "consumer", portId: "in" }, (d: any) => { d.edges = []; }],
  ["dependency closure", "DEPENDENCY_LOCK_MISSING", { pluginId: "org.example.math" }, (d: any) => { d.pluginLocks.pop(); }],
  ["lock hash", "PLUGIN_LOCK_MISMATCH", { pluginId: "org.example.pipeline" }, (d: any) => { d.pluginLocks[0].sha256 = "d".repeat(64); }],
  ["Unit boundaries", "EDGE_CROSS_UNIT", { edgeId: "value" }, (d: any) => { d.edges[0].target.nodeId = "archive-consumer"; }],
] as const)("preserves P0.3 %s checks through the Registry entry point", (_name, code, location, mutate) => {
  const r = populated(), doc = load("blueprint"); mutate(doc); freeze(doc);
  const before = JSON.stringify(doc), state = r.snapshot(), diagnostics = r.validate(doc);
  expect(diagnostics).toEqual(expect.arrayContaining([expect.objectContaining({ code, severity: "error", ...location })]));
  expect(diagnostics).toEqual(validateBlueprintWithDescriptors(doc, load("descriptors"), pins));
  expect(JSON.stringify(doc)).toBe(before); expect(r.snapshot()).toBe(state);
});
it("does not bypass structure validation or borrow trust pins from a failed registration", () => {
  const r = new PluginRegistry();
  expect(codes(r.register(load("descriptors"), []))).toContain("DESCRIPTOR_UNTRUSTED");
  expect(r.validate(null)).toContainEqual(expect.objectContaining({ code: "STRUCTURE_INVALID" }));
  expect(r.validate(load("blueprint"))).toContainEqual(expect.objectContaining({ code: "PLUGIN_UNTRUSTED", pluginId: "org.example.pipeline" }));
});
