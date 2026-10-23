import { readFileSync } from "node:fs";
import { afterEach, expect, it, vi } from "vitest";
import { PluginRegistry } from "./plugin-registry.js";
import { prepareBlueprintEdits, type BlueprintEditPreparationResult } from "./blueprint-document-editor.js";

const load = (name: string): any => JSON.parse(readFileSync(new URL(`../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url), "utf8"));
const pins = [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0", sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
];
const populated = (descriptors = load("descriptors")) => {
  const registry = new PluginRegistry();
  expect(registry.register(descriptors, pins).diagnostics).toEqual([]);
  return registry;
};
const source = () => JSON.stringify(load("blueprint"), null, 2);
afterEach(() => vi.restoreAllMocks());
function prepared(result: BlueprintEditPreparationResult) {
  expect(result.status).toBe("prepared");
  if (result.status !== "prepared") throw new Error(JSON.stringify(result));
  return result;
}
function failed(result: BlueprintEditPreparationResult, code: string) {
  expect(result).toMatchObject({ status: "failed", code, canGenerate: false });
  expect(result).not.toHaveProperty("sourceText");
  expect(result).not.toHaveProperty("readResult");
}

it("replaces only the project-name literal and retains current Reader diagnostics", () => {
  const text = source(), registry = new PluginRegistry();
  const result = prepared(prepareBlueprintEdits(text, [{ kind: "project-name", value: "新项目 🚀" }], registry));
  expect(result.status).toBe("prepared");
  expect(result.sourceText).toBe(text.replace('"Reading pipeline"', '"新项目 🚀"'));
  expect(result.readResult.semanticValid).toBe(false);
  expect(result.diagnostics).toEqual(result.readResult.diagnostics);
  expect(result.canGenerate).toBe(false);
});

it("edits KnownNode property and static port display name as one batch", () => {
  const text = source();
  const result = prepared(prepareBlueprintEdits(text, [
    { kind: "node-property", sourceIndex: 0, property: "label", value: "气温" },
    { kind: "port-name", sourceIndex: 0, portIndex: 0, portId: "out", value: "输出" },
  ], populated()));
  expect(result.status).toBe("prepared");
  expect(result.sourceText).toBe(text.replace('"温度"', '"气温"').replace('"可编辑显示名"', '"输出"'));
  expect(result.readResult.semanticValid).toBe(true);
});

it.each([17, null, false])("rejects a value violating the declared property contract: %s", value => {
  failed(prepareBlueprintEdits(source(), [{ kind: "node-property", sourceIndex: 0, property: "label", value }], populated()), "PROPERTY_CONTRACT");
});

it("does not let an invalid sibling hide new errors or forbid a valid field edit", () => {
  const descriptors = load("descriptors"), doc = load("blueprint");
  descriptors[0].nodeDefinitions[0].propertiesSchema.properties.sibling = { type: "integer" };
  doc.nodes[0].properties.sibling = "already invalid";
  const registry = populated(descriptors), text = JSON.stringify(doc);
  const good = prepared(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "label", value: "new label" }], registry));
  expect(good.diagnostics).toContainEqual(expect.objectContaining({ code: "NODE_PROPERTIES" }));
  failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "label", value: 42 }], registry), "PROPERTY_CONTRACT");
  failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "sibling", value: "still invalid" }], registry), "PROPERTY_CONTRACT");
  expect(prepared(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "sibling", value: 7 }], registry)).readResult.semanticValid).toBe(true);
});

it("rejects new conditional errors elsewhere even when NODE_PROPERTIES already exists", () => {
  const descriptors = load("descriptors"), doc = load("blueprint");
  descriptors[0].nodeDefinitions[0].propertiesSchema = {
    type: "object", properties: { label: { type: "string" }, sibling: { type: "integer" }, mode: { type: "string" } },
    if: { required: ["mode"], properties: { mode: { const: "strict" } } },
    then: { properties: { label: { const: "required label" } } },
  };
  doc.nodes[0].properties = { label: "ordinary", sibling: "invalid", mode: "normal" };
  const registry = populated(descriptors), text = JSON.stringify(doc);
  failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "mode", value: "strict" }], registry), "PROPERTY_CONTRACT");
  const result = prepared(prepareBlueprintEdits(text, [
    { kind: "node-property", sourceIndex: 0, property: "mode", value: "strict" },
    { kind: "node-property", sourceIndex: 0, property: "label", value: "required label" },
  ], registry));
  expect(result.readResult.document.nodes[0].properties).toEqual({ label: "required label", sibling: "invalid", mode: "strict" });
});

it.each(["constructor", "a/b~"])("handles a declared special property as own data: %s", property => {
  const descriptors = load("descriptors"), doc = load("blueprint");
  Object.defineProperty(descriptors[0].nodeDefinitions[0].propertiesSchema.properties, property, { value: { type: "string" }, enumerable: true });
  Object.defineProperty(doc.nodes[0].properties, property, { value: "special", enumerable: true });
  const registry = populated(descriptors), text = JSON.stringify(doc);
  const good = prepared(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property, value: "changed" }], registry));
  expect(Object.getOwnPropertyDescriptor(good.readResult.document.nodes[0].properties, property)?.value).toBe("changed");
  failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property, value: 17 }], registry), "PROPERTY_CONTRACT");
});

it("refuses __proto__ evaluation uncertainty and preserves it while editing surrounding fields", () => {
  const descriptors = load("descriptors"), doc = load("blueprint");
  Object.defineProperty(descriptors[0].nodeDefinitions[0].propertiesSchema.properties, "__proto__", { value: { type: "string" }, enumerable: true });
  Object.defineProperty(doc.nodes[0].properties, "__proto__", { value: "special", enumerable: true });
  const registry = populated(descriptors), text = JSON.stringify(doc).replace('"__proto__":"special"', '"__proto__" : "special\\u0020data"');
  for (const value of ["looks valid", 17]) failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "__proto__", value }], registry), "PROPERTY_CONTRACT");
  const result = prepared(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "label", value: "changed" }], registry));
  expect(result.sourceText).toBe(text.replace('"label":"温度"', '"label":"changed"'));
});

it.each([
  '{"x":1,"x":2}', '{"x":1,"\\u0078":2}', '{"arr":[{"__proto__":null,"__proto__":false}]}',
])("rejects duplicates in otherwise opaque extension data: %s", opaque => {
  const text = source().slice(0, -1) + ',"opaque":' + opaque + "}";
  failed(prepareBlueprintEdits(text, [{ kind: "project-name", value: "changed" }], populated()), "DUPLICATE_KEY");
});

it("rejects the entire repeated-target batch without exposing a candidate", () => {
  const edit = { kind: "project-name" as const, value: "same" };
  failed(prepareBlueprintEdits(source(), [edit, edit], populated()), "OVERLAPPING_EDITS");
});

it.each([NaN, Infinity, -Infinity, {}, []])("rejects unsupported replacement scalars: %s", value => {
  failed(prepareBlueprintEdits(source(), [{ kind: "node-property", sourceIndex: 0, property: "label", value } as any], populated()), "INVALID_EDIT");
});

it.each([
  { kind: "node-property", sourceIndex: 0, property: "missing", value: "x" },
  { kind: "node-property", sourceIndex: 1, property: "constructor", value: "x" },
  { kind: "port-name", sourceIndex: 0, portIndex: 0, portId: "wrong", value: "x" },
  { kind: "port-name", sourceIndex: 0, portIndex: 1, portId: "out", value: "x" },
])("refuses unauthorized or mismatched targets: %j", edit => {
  failed(prepareBlueprintEdits(source(), [edit as any], populated()), "TARGET_UNAVAILABLE");
});

it.each(["missing", "untrusted", "version"])("refuses node edits with %s authority", change => {
  const doc = load("blueprint");
  if (change === "untrusted") doc.pluginLocks[0].sha256 = "c".repeat(64);
  if (change === "version") doc.nodes[0].nodeDefinitionVersion = "3.0.1";
  failed(prepareBlueprintEdits(JSON.stringify(doc), [{ kind: "node-property", sourceIndex: 0, property: "label", value: "x" }], change === "missing" ? new PluginRegistry() : populated()), "TARGET_UNAVAILABLE");
});

it("addresses duplicate node IDs by source position, but refuses duplicate instance port IDs", () => {
  const doc = load("blueprint");
  doc.nodes.push(structuredClone(doc.nodes[0]));
  const text = JSON.stringify(doc);
  const result = prepared(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 3, property: "label", value: "last" }], populated()));
  expect(result.readResult.document.nodes[0].properties).toEqual({ label: "温度" });
  expect(result.readResult.document.nodes[3].properties).toEqual({ label: "last" });
  doc.nodes[0].ports.push(structuredClone(doc.nodes[0].ports[0]));
  failed(prepareBlueprintEdits(JSON.stringify(doc), [{ kind: "port-name", sourceIndex: 0, portIndex: 0, portId: "out", value: "x" }], populated()), "TARGET_UNAVAILABLE");
});

it("preserves every untouched fragment with escaped keys, CRLF, Chinese, astral and unusual numeric spelling", () => {
  const text = source().replace('"label": "温度"', '"la\\u0062el": "温\\u5ea6", "opaque": [-0.00e+00, 1E+02, "😀标签", {"label":"诱饵"}]')
    .replaceAll("\n", "\r\n");
  const edits = Object.freeze([Object.freeze({ kind: "node-property" as const, sourceIndex: 0, property: "label", value: '引号"\\\n😀' })]);
  const result = prepared(prepareBlueprintEdits(text, edits, populated()));
  expect(result.sourceText).toBe(text.replace('"温\\u5ea6"', JSON.stringify('引号"\\\n😀')));
  expect(Object.isFrozen(result)).toBe(true);
  expect(Object.isFrozen(result.diagnostics)).toBe(true);
  expect(Object.isFrozen(result.readResult.document.nodes[0].properties)).toBe(true);
  expect(edits[0].value).toBe('引号"\\\n😀');
});

it("retains a 10,000-level opaque subtree without recursive traversal", () => {
  const text = source().slice(0, -1) + ',"opaque":' + "[".repeat(10_000) + '"深😀"' + "]".repeat(10_000) + "}";
  const result = prepared(prepareBlueprintEdits(text, [{ kind: "project-name", value: "deep" }], new PluginRegistry()));
  expect(result.sourceText).toBe(text.replace('"Reading pipeline"', '"deep"'));
});

it.each(["{", '{"schemaVersion":1}', '{"schemaVersion":2}', source() + ","])("refuses source rejected by the current Reader: %s", text => {
  failed(prepareBlueprintEdits(text, [{ kind: "project-name", value: "x" }], new PluginRegistry()), "SOURCE_REJECTED");
});

it.each(["\ufeff" + source(), source().replace("温度", "raw\ud800")])("requires lossless original UTF-8", text => {
  failed(prepareBlueprintEdits(text, [{ kind: "project-name", value: "x" }], populated()), "ENCODING_INVALID");
});

it("rejects empty and oversized edit batches", () => {
  failed(prepareBlueprintEdits(source(), [], populated()), "INVALID_EDIT");
  failed(prepareBlueprintEdits(source(), Array(257).fill({ kind: "project-name", value: "x" }), populated()), "RESOURCE_LIMIT");
});

it.each([null, true, false, 3.25, -0, "😀", "\ud800"])("supports every finite scalar kind with explicit negative zero: %s", value => {
  const descriptors = load("descriptors");
  descriptors[0].nodeDefinitions[0].propertiesSchema.properties.label = true;
  const text = source(), result = prepared(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "label", value }], populated(descriptors)));
  expect(Object.is((result.readResult.document.nodes[0].properties as any).label, value)).toBe(true);
  expect(result.sourceText).toBe(text.replace('"温度"', Object.is(value, -0) ? "-0" : JSON.stringify(value)));
});

it.each([
  { type: "object", properties: {}, patternProperties: { "^label$": { type: "string" } } },
  { type: "object", if: { properties: { label: { type: "string" } } }, then: { properties: { label: { type: "string" } } } },
  { $ref: "#/$defs/props", $defs: { props: { type: "object", properties: { label: { type: "string" } } } } },
])("does not grant edit authority from non-root declarations", schema => {
  const descriptors = load("descriptors"); descriptors[0].nodeDefinitions[0].propertiesSchema = schema;
  failed(prepareBlueprintEdits(source(), [{ kind: "node-property", sourceIndex: 0, property: "label", value: "new" }], populated(descriptors)), "TARGET_UNAVAILABLE");
});

it("refuses to edit nested data or create an absent explicitly declared property", () => {
  const descriptors = load("descriptors"), doc = load("blueprint");
  descriptors[0].nodeDefinitions[0].propertiesSchema.properties.label = true;
  descriptors[0].nodeDefinitions[0].propertiesSchema.properties.absent = true;
  doc.nodes[0].properties.label = { nested: "old" };
  const registry = populated(descriptors), text = JSON.stringify(doc);
  for (const property of ["label", "absent"]) failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property, value: "new" }], registry), "TARGET_UNAVAILABLE");
});

it("rejects residual whole-object contract failures when field validity cannot be established", () => {
  const descriptors = load("descriptors"), doc = load("blueprint");
  descriptors[0].nodeDefinitions[0].propertiesSchema = { type: "object", properties: { label: { type: "string" } }, const: { label: "only allowed" } };
  doc.nodes[0].properties.label = "already invalid";
  const registry = populated(descriptors), text = JSON.stringify(doc);
  failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "label", value: "still invalid" }], registry), "PROPERTY_CONTRACT");
  expect(prepared(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "label", value: "only allowed" }], registry)).readResult.semanticValid).toBe(true);
});

it("rejects an oversized replacement before serializing that scalar", () => {
  const text = source(), value = "x".repeat(8 * 1024 * 1024 + 1), registry = populated();
  const stringify = JSON.stringify.bind(JSON);
  vi.spyOn(JSON, "stringify").mockImplementation(((data: any, ...args: any[]) => {
    if (data === value) throw new Error("oversized replacement was serialized");
    return stringify(data, ...args);
  }) as typeof JSON.stringify);
  failed(prepareBlueprintEdits(text, [{ kind: "project-name", value }], registry), "RESOURCE_LIMIT");
});

it("bounds escaped replacement expansion and the final combined candidate", () => {
  const text = source(), registry = populated();
  failed(prepareBlueprintEdits(text, [{ kind: "project-name", value: "\u0000".repeat(1_500_000) }], registry), "RESOURCE_LIMIT");
  failed(prepareBlueprintEdits(text, [{ kind: "project-name", value: "x".repeat(8 * 1024 * 1024 - 2) }], registry), "RESOURCE_LIMIT");
});

it.each([
  () => source() + " ".repeat(8 * 1024 * 1024),
  () => source().slice(0, -1) + ',"opaque":' + "[".repeat(16_384) + "0" + "]".repeat(16_384) + "}",
  () => source().slice(0, -1) + ',"opaque":[' + Array(250_000).fill("0").join(",") + "]}",
])("enforces resource limits before Reader semantic work", oversized => {
  const registry = new PluginRegistry(), validate = vi.spyOn(registry, "validate");
  failed(prepareBlueprintEdits(oversized(), [{ kind: "project-name", value: "bounded" }], registry), "RESOURCE_LIMIT");
  expect(validate).not.toHaveBeenCalled();
});

it("rejects edits under an explicitly false property schema", () => {
  const descriptors = load("descriptors"); descriptors[0].nodeDefinitions[0].propertiesSchema.properties.label = false;
  failed(prepareBlueprintEdits(source(), [{ kind: "node-property", sourceIndex: 0, property: "label", value: "x" }], populated(descriptors)), "PROPERTY_CONTRACT");
});

it("keeps an atomic batch unapproved when only one targeted field violates its contract", () => {
  failed(prepareBlueprintEdits(source(), [{ kind: "project-name", value: "valid change" }, { kind: "node-property", sourceIndex: 0, property: "label", value: 42 }], populated()), "PROPERTY_CONTRACT");
});

it("refuses property evaluation uncertainty from a deeply recursive declared schema", () => {
  const descriptors = load("descriptors");
  descriptors[0].nodeDefinitions[0].propertiesSchema = { type: "object", properties: { label: { type: "string" }, opaque: { $ref: "#/$defs/nested" } },
    $defs: { nested: { type: "object", properties: { next: { $ref: "#/$defs/nested" } } } } };
  const text = source().replace('"label": "温度"', '"label": "温度", "opaque": ' + '{"next":'.repeat(10_000) + "{}" + "}".repeat(10_000));
  failed(prepareBlueprintEdits(text, [{ kind: "node-property", sourceIndex: 0, property: "label", value: "new" }], populated(descriptors)), "PROPERTY_CONTRACT");
});

it("retains current Reader diagnostics on failed authorization without exposing its candidate", () => {
  const result = prepareBlueprintEdits(source(), [{ kind: "node-property", sourceIndex: 0, property: "label", value: 17 }], populated());
  failed(result, "PROPERTY_CONTRACT");
  expect(result.diagnostics).toContainEqual(expect.objectContaining({ code: "NODE_PROPERTIES", path: "/nodes/0/properties" }));
  expect(result.diagnostics).toContainEqual(expect.objectContaining({ code: "PROPERTY_CONTRACT" }));
  expect(Object.isFrozen(result.diagnostics)).toBe(true);
});
