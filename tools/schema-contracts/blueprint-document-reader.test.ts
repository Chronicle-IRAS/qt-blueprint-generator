import { readFileSync } from "node:fs";
import { expect, it } from "vitest";
import { PluginRegistry } from "./plugin-registry.js";
import { readBlueprintDocument } from "./blueprint-document-reader.js";

const load = (name: string): any => JSON.parse(readFileSync(new URL(`../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url), "utf8"));
const pins = [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0", sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
];
const populated = () => {
  const registry = new PluginRegistry();
  expect(registry.register(load("descriptors"), pins).diagnostics).toEqual([]);
  return registry;
};
const empty = () => ({ schemaVersion: 2, projectId: "empty", projectName: "空蓝图", pluginLocks: [], units: [], nodes: [], edges: [], channels: [], driverBindings: [] });
const read = (document: unknown, registry = populated()) => {
  const result = readBlueprintDocument(JSON.stringify(document), registry);
  expect(result.status).toBe("read");
  if (result.status !== "read") throw new Error("Expected structurally valid document");
  return result;
};

it("reads a valid v2 document", () => {
  const sourceText = readFileSync(new URL("../../tests/fixtures/plugin-descriptors/blueprint.json", import.meta.url), "utf8");
  const result = readBlueprintDocument(sourceText, populated());
  expect(result.status).toBe("read");
  expect(result.sourceText).toBe(sourceText);
  expect(result.canGenerate).toBe(false);
  if (result.status !== "read") throw new Error("Expected readable fixture");
  expect(result.document).toMatchObject({ schemaVersion: 2, projectId: "descriptor-demo", projectName: "Reading pipeline" });
});

it("retains nodes when their plugin is missing", () => {
  const result = read(load("blueprint"), new PluginRegistry());
  expect(result.semanticValid).toBe(false);
  expect(result.canGenerate).toBe(false);
  expect(result.nodes).toHaveLength(3);
  expect(result.nodes[0]).toMatchObject({ kind: "unknown", sourceIndex: 0, nodeId: "source", unitId: "main", typeId: "org.example.pipeline/source", nodeDefinitionVersion: "3.0.0",
    data: { properties: { label: "温度" }, ports: [{ id: "out", name: "可编辑显示名", direction: "out", kind: "data", typeId: "org.example.pipeline/Reading@1" }] } });
  expect(result.nodes[0].kind === "unknown" && result.nodes[0].reasonCodes).toContain("NODE_DEFINITION_UNKNOWN");
  expect(result.nodes[0].kind === "unknown" && result.nodes[0].reasonCodes).toContain("PLUGIN_UNTRUSTED");
  expect(result.diagnostics).toContainEqual(expect.objectContaining({ code: "NODE_DEFINITION_UNKNOWN", path: "/nodes/0", nodeId: "source" }));
});

it("does not substitute a different definition version", () => {
  const document = load("blueprint"); document.nodes[0].nodeDefinitionVersion = "3.0.1";
  const result = read(document);
  expect(result.nodes[0]).toMatchObject({ kind: "unknown", sourceIndex: 0, nodeDefinitionVersion: "3.0.1", reasonCodes: ["NODE_DEFINITION_UNKNOWN"] });
  expect(result.nodes[0]).not.toHaveProperty("title");
  expect(result.nodes[1]).toMatchObject({ kind: "known", title: "Value consumer" });
  expect(result.semanticValid).toBe(false);
});

it.each([
  ["version", "1.2.1"], ["package", "another-package"], ["sha256", "c".repeat(64)], ["pluginApiVersion", "future-api"],
] as const)("requires trusted document lock %s even when the exact definition is installed", (field, value) => {
  const document = load("blueprint"), registry = populated(); document.pluginLocks[0][field] = value;
  expect(registry.getNode("org.example.pipeline/source", "3.0.0")).toBeDefined();
  const result = read(document, registry);
  expect(result.nodes[0]).toMatchObject({ kind: "unknown", reasonCodes: ["PLUGIN_LOCK_MISMATCH"] });
  expect(result.nodes[1]).toMatchObject({ kind: "known", title: "Value consumer" });
  expect(result.diagnostics).toContainEqual(expect.objectContaining({ code: "PLUGIN_LOCK_MISMATCH", path: "/pluginLocks/0", pluginId: "org.example.pipeline" }));
  expect(result.semanticValid).toBe(false);
});

it("requires a provider lock instead of accepting an exact definition alone", () => {
  const document = load("blueprint"); document.pluginLocks.shift();
  const result = read(document);
  expect(result.nodes[0]).toMatchObject({ kind: "unknown", reasonCodes: ["PLUGIN_LOCK_MISSING"] });
  expect(result.nodes[1]).toMatchObject({ kind: "known", title: "Value consumer" });
  expect(result.diagnostics).toContainEqual(expect.objectContaining({ code: "PLUGIN_LOCK_MISSING", path: "/nodes/0", pluginId: "org.example.pipeline" }));
});

it("rejects ambiguous duplicate provider locks even when both equal the trusted lock", () => {
  const document = load("blueprint"); document.pluginLocks.push(structuredClone(document.pluginLocks[0]));
  const result = read(document);
  expect(result.nodes[0]).toMatchObject({ kind: "unknown", reasonCodes: ["DUPLICATE_PLUGIN_LOCK", "PLUGIN_LOCK_MISSING"] });
  expect(result.nodes[1]).toMatchObject({ kind: "known" });
  expect(result.diagnostics).toContainEqual(expect.objectContaining({ code: "DUPLICATE_PLUGIN_LOCK", path: "/pluginLocks/2/pluginId", pluginId: "org.example.pipeline" }));
});

it("does not treat a renamed provider lock as trust for the original provider", () => {
  const document = load("blueprint"); document.pluginLocks[0].pluginId = "org.absent";
  const result = read(document);
  expect(result.nodes[0]).toMatchObject({ kind: "unknown", reasonCodes: ["PLUGIN_LOCK_MISSING"] });
  expect(result.diagnostics).toContainEqual(expect.objectContaining({ code: "PLUGIN_UNTRUSTED", pluginId: "org.absent" }));
});

it("forwards descriptor property and static-port diagnostics while retaining KnownNode", () => {
  const document = load("blueprint");
  document.nodes[0].properties.label = 17;
  document.nodes[0].ports[0].direction = "in";
  document.nodes[1].ports = [];
  document.nodes[2].ports.push({ id: "extra", name: "extra", direction: "in", kind: "data", typeId: "org.example.math/Value@1" });
  const result = read(document);
  expect(result.semanticValid).toBe(false);
  expect(result.nodes.map(n => n.kind)).toEqual(["known", "known", "known"]);
  expect(result.nodes[0]).toMatchObject({ title: "Reading source", data: { properties: { label: 17 }, ports: [{ direction: "in" }] } });
  expect(result.diagnostics).toEqual(expect.arrayContaining([
    expect.objectContaining({ code: "NODE_PROPERTIES", path: "/nodes/0/properties" }),
    expect.objectContaining({ code: "NODE_PORT_CONTRACT", path: "/nodes/0/ports" }),
    expect.objectContaining({ code: "NODE_PORT_MISSING", path: "/nodes/1/ports" }),
    expect.objectContaining({ code: "NODE_PORT_UNKNOWN", path: "/nodes/2/ports/1" }),
  ]));
});

it("reads a valid multi Unit graph with channels and driver records without granting generation", () => {
  const result = read(load("blueprint"));
  expect(result.semanticValid).toBe(true);
  expect(result.diagnostics).toEqual([]);
  expect(result.canGenerate).toBe(false);
  expect(result.nodes.map(n => ({ kind: n.kind, sourceIndex: n.sourceIndex, nodeId: n.nodeId, unitId: n.unitId, title: n.kind === "known" ? n.title : undefined }))).toEqual([
    { kind: "known", sourceIndex: 0, nodeId: "source", unitId: "main", title: "Reading source" },
    { kind: "known", sourceIndex: 1, nodeId: "consumer", unitId: "main", title: "Value consumer" },
    { kind: "known", sourceIndex: 2, nodeId: "archive-consumer", unitId: "archive", title: "Value consumer" },
  ]);
  expect(result.document).toMatchObject({ channels: [{ id: "archive-value", bridge: { version: "adapter-7", endpointDriverBindingIds: { target: ["archive-input"] } } }], driverBindings: [{ id: "archive-input", mode: "Snapshot" }] });
});

it("preserves original text and nested extension values", () => {
  const document = load("blueprint");
  document.nodes[0].nodeDefinitionVersion = "unavailable";
  document.nodes[0].properties.custom = { list: [null, false, 7, { text: "嵌套值" }] };
  document.nodes[0].ports[0].futurePort = { values: ["保留", true] };
  document.nodes[0].futureNode = [1, { deeper: { value: "unknown" } }];
  document.units[0].futureUnit = { setting: null };
  document.channels[0].wireSchema.schema = { futureWire: [{ value: "opaque" }] };
  document.driverBindings[0].files.push({ path: "未读取.cpp", sha256: "c".repeat(64), futureFile: [3] });
  document.extensions = { futureRoot: { nested: [null, { value: "原始" }] } };
  const sourceText = " \r\n\t" + JSON.stringify(document, null, 2).replace('"温度"', '"温\\u5ea6\\n\\\"测量\\\""') + "\r\n ";
  const result = readBlueprintDocument(sourceText, populated());
  expect(result.status).toBe("read");
  if (result.status !== "read") throw new Error("Expected preserved document");
  expect(result.sourceText).toBe(sourceText);
  expect(result.nodes[0]).toMatchObject({ kind: "unknown", data: {
    properties: { label: "温度\n\"测量\"", custom: { list: [null, false, 7, { text: "嵌套值" }] } },
    ports: [{ futurePort: { values: ["保留", true] } }], futureNode: [1, { deeper: { value: "unknown" } }],
  } });
  expect(result.document).toMatchObject({ units: [{ futureUnit: { setting: null } }, { id: "archive" }],
    channels: [{ wireSchema: { schema: { futureWire: [{ value: "opaque" }] } } }],
    driverBindings: [{ files: [{ path: "未读取.cpp", futureFile: [3] }] }], extensions: { futureRoot: { nested: [null, { value: "原始" }] } } });
});

it("keeps extension keys as inert data", () => {
  const document = load("blueprint");
  const extension = JSON.parse('{"__proto__":{"polluted":"kept"},"constructor":{"prototype":{"polluted":"also kept"}},"canGenerate":true}');
  Object.defineProperty(document, "__proto__", { value: extension.__proto__, enumerable: true });
  document.constructor = extension.constructor;
  document.canGenerate = true;
  document.extensions = extension;
  document.nodes[0].canGenerate = true;
  document.nodes[0].properties.extensions = extension;
  const result = read(document);
  expect(result.canGenerate).toBe(false);
  expect(result.document.canGenerate).toBe(true);
  expect(result.document["__proto__"]).toEqual({ polluted: "kept" });
  expect(Object.hasOwn(result.document, "__proto__")).toBe(true);
  expect(result.document.constructor).toEqual({ prototype: { polluted: "also kept" } });
  expect(result.nodes[0].data.properties).toMatchObject({ extensions: { canGenerate: true, constructor: { prototype: { polluted: "also kept" } } } });
  expect(({} as any).polluted).toBeUndefined();
  expect((result as any).polluted).toBeUndefined();
});

it("retains semantically invalid records with diagnostics", () => {
  const fixture = load("blueprint"), first = fixture.nodes[0], second = structuredClone(first);
  first.properties.label = 17;
  second.properties.label = 18;
  second.unitId = "missing-unit";
  const document = { ...fixture, nodes: [first, second], channels: [], driverBindings: [],
    edges: [{ id: "dangling", kind: "data", source: { nodeId: "absent", portId: "out" }, target: { nodeId: "also-absent", portId: "in" } }] };
  const result = read(document);
  expect(result.semanticValid).toBe(false);
  expect(result.canGenerate).toBe(false);
  expect(result.nodes.map(n => ({ sourceIndex: n.sourceIndex, nodeId: n.nodeId, unitId: n.unitId, kind: n.kind, properties: n.data.properties }))).toEqual([
    { sourceIndex: 0, nodeId: "source", unitId: "main", kind: "known", properties: { label: 17 } },
    { sourceIndex: 1, nodeId: "source", unitId: "missing-unit", kind: "known", properties: { label: 18 } },
  ]);
  expect(result.nodes[0].data).not.toBe(result.nodes[1].data);
  expect(result.document).toMatchObject({ nodes: [{ properties: { label: 17 } }, { properties: { label: 18 } }], edges: [{ id: "dangling", source: { nodeId: "absent" }, target: { nodeId: "also-absent" } }] });
  expect(result.diagnostics.map(d => ({ code: d.code, path: d.path }))).toEqual([
    { code: "DUPLICATE_NODE_ID", path: "/nodes/1/id" },
    { code: "UNIT_NOT_FOUND", path: "/nodes/1" },
    { code: "NODE_NOT_FOUND", path: "/edges/0/source" },
    { code: "NODE_NOT_FOUND", path: "/edges/0/target" },
    { code: "CARDINALITY_ENDPOINT_UNKNOWN", path: "/context/portCardinality" },
    { code: "NODE_PROPERTIES", path: "/nodes/0/properties" },
    { code: "NODE_PROPERTIES", path: "/nodes/1/properties" },
  ]);
});

it("returns a detached read-time result", () => {
  const registry = populated(), snapshot = registry.snapshot();
  const sourceText = JSON.stringify(load("blueprint"));
  const first = readBlueprintDocument(sourceText, registry);
  const before = JSON.stringify(first);
  expect(readBlueprintDocument(sourceText, registry)).toEqual(first);
  expect(registry.snapshot()).toBe(snapshot);
  expect(registry.unregister(["org.example.pipeline", "org.example.math"]).diagnostics).toEqual([]);
  expect(JSON.stringify(first)).toBe(before);
  expect(first.status === "read" && first.nodes[0]).toMatchObject({ kind: "known", title: "Reading source" });
  const next = readBlueprintDocument(sourceText, registry);
  expect(next.status === "read" && next.nodes.map(n => n.kind)).toEqual(["unknown", "unknown", "unknown"]);
  expect(next.status === "read" && next.semanticValid).toBe(false);
});

it("freezes results, diagnostics and every nested parsed value before publication", () => {
  const document = load("blueprint"); document.nodes[0].properties.label = 17;
  document.extensions = JSON.parse('{"__proto__":{"list":[{"value":"original"}]}}');
  const result = read(document);
  const assertFrozen = (value: unknown): void => {
    if (value && typeof value === "object") {
      expect(Object.isFrozen(value)).toBe(true);
      Object.values(value).forEach(assertFrozen);
    }
  };
  assertFrozen(result);
  expect(() => { (result.document.extensions as any).__proto__.list[0].value = "changed"; }).toThrow(TypeError);
  expect(() => { (result.nodes[0].data.properties as any).label = "changed"; }).toThrow(TypeError);
  expect(() => { (result.nodes[0].data.ports as any).pop(); }).toThrow(TypeError);
  expect(() => { (result.diagnostics[0] as any).code = "changed"; }).toThrow(TypeError);
  expect(() => { (result as any).canGenerate = true; }).toThrow(TypeError);
  const unknown = read(load("blueprint"), new PluginRegistry());
  assertFrozen(unknown);
  expect(() => { (unknown.nodes[0] as any).reasonCodes.push("changed"); }).toThrow(TypeError);
  const rejected = readBlueprintDocument("{", new PluginRegistry());
  assertFrozen(rejected);
  expect(() => { (rejected.diagnostics as any).pop(); }).toThrow(TypeError);
});

it("preserves source spelling separately from parsed numeric values and duplicate keys", () => {
  const sourceText = JSON.stringify(empty()).replace(/}$/, ',"numeric":1e0,"duplicate":1,"duplicate":2}');
  const result = readBlueprintDocument(sourceText, new PluginRegistry());
  expect(result.status).toBe("read");
  expect(result.sourceText).toBe(sourceText);
  expect(result.status === "read" && result.document).toMatchObject({ numeric: 1, duplicate: 2 });
});

it("reads an empty blueprint without plugins", () => {
  const result = readBlueprintDocument(JSON.stringify(empty()), new PluginRegistry());
  expect(result.status).toBe("read");
  if (result.status !== "read") throw new Error("Expected readable empty graph");
  expect(result.document).toEqual({ schemaVersion: 2, projectId: "empty", projectName: "空蓝图", pluginLocks: [], units: [], nodes: [], edges: [], channels: [], driverBindings: [] });
  expect(result.nodes).toEqual([]);
  expect(result.diagnostics).toEqual([]);
  expect(result.semanticValid).toBe(true);
  expect(result.canGenerate).toBe(false);
});

it.each([
  { name: "empty text", text: "", code: "JSON_PARSE_ERROR" },
  { name: "whitespace", text: " \r\n\t", code: "JSON_PARSE_ERROR" },
  { name: "malformed JSON", text: '{"secret":"private payload",', code: "JSON_PARSE_ERROR" },
  { name: "BOM", text: "\uFEFF" + JSON.stringify(empty()), code: "JSON_PARSE_ERROR" },
  { name: "comments", text: "/* comment */" + JSON.stringify(empty()), code: "JSON_PARSE_ERROR" },
  { name: "trailing comma", text: JSON.stringify(empty()).replace(/}$/, ",}"), code: "JSON_PARSE_ERROR" },
  { name: "null root", text: "null", code: "STRUCTURE_INVALID" },
  { name: "array root", text: "[]", code: "STRUCTURE_INVALID" },
  { name: "scalar root", text: "2", code: "STRUCTURE_INVALID" },
  { name: "missing fields", text: '{"schemaVersion":2}', code: "STRUCTURE_INVALID" },
  { name: "missing version", text: JSON.stringify({ ...empty(), schemaVersion: undefined }), code: "STRUCTURE_INVALID" },
  { name: "string version", text: JSON.stringify({ ...empty(), schemaVersion: "2" }), code: "STRUCTURE_INVALID" },
  { name: "null version", text: JSON.stringify({ ...empty(), schemaVersion: null }), code: "STRUCTURE_INVALID" },
  { name: "old version", text: '{"schemaVersion":1}', code: "SCHEMA_VERSION_UNSUPPORTED" },
  { name: "future version", text: '{"schemaVersion":3}', code: "SCHEMA_VERSION_UNSUPPORTED" },
  { name: "fractional version", text: '{"schemaVersion":2.5}', code: "SCHEMA_VERSION_UNSUPPORTED" },
  { name: "wrong array type", text: JSON.stringify({ ...empty(), nodes: {} }), code: "STRUCTURE_INVALID" },
  { name: "malformed node", text: JSON.stringify({ ...empty(), nodes: [{ id: "n" }] }), code: "STRUCTURE_INVALID" },
])("rejects unsupported or invalid input without repair: $name", ({ text, code }) => {
  const result = readBlueprintDocument(text, populated());
  expect(result.status).toBe("rejected");
  expect(result.sourceText).toBe(text);
  expect(result.canGenerate).toBe(false);
  expect(result.diagnostics.map(d => d.code)).toContain(code);
  expect(result).not.toHaveProperty("document");
  expect(result).not.toHaveProperty("nodes");
  if (code === "JSON_PARSE_ERROR") {
    expect(result.diagnostics).toEqual([{ code: "JSON_PARSE_ERROR", severity: "error", message: "Invalid JSON text", path: "" }]);
  }
});
