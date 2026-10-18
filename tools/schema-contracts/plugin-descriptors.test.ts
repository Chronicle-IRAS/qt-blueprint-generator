import { readFileSync } from "node:fs";
import { expect, it } from "vitest";
import { assembleTrustedContext, validateBlueprintWithDescriptors } from "./plugin-descriptors.js";
import { validateBlueprintSemantics } from "./semantic-validator.js";

const load = (name: string): any => JSON.parse(readFileSync(new URL(`../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url), "utf8"));
const trustedLocks = [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0", sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
];

it("rejects a descriptor with a missing manifest instead of yielding trusted context", () => {
  const descriptors = load("descriptors");
  delete descriptors[0].manifest;
  const result = assembleTrustedContext(descriptors, trustedLocks);
  expect(result.diagnostics.map(d => d.code)).toContain("DESCRIPTOR_STRUCTURE");
  expect(result.context).toBeUndefined();
});

const assembleCodes = (descriptors: unknown, locks: unknown = trustedLocks) => assembleTrustedContext(descriptors, locks).diagnostics.map(d => d.code);
const validate = (doc = load("blueprint"), descriptors = load("descriptors"), locks: unknown = trustedLocks) => validateBlueprintWithDescriptors(doc, descriptors, locks);

it("assembles two independent providers into the unchanged P0.2 validator", () => {
  const result = assembleTrustedContext(load("descriptors"), trustedLocks);
  expect(result.diagnostics).toEqual([]);
  expect(result.context).toBeDefined();
  expect(result.context && validateBlueprintSemantics(load("blueprint"), result.context)).toEqual([]);
  expect(result.context?.compatibility).toEqual([{ sourceTypeId: "org.example.pipeline/Reading@1", targetTypeId: "org.example.math/Value@1" }]);
  expect(validate()).toEqual([]);
});

it.each([
  ["missing dependency", "DEPENDENCY_MISSING", (d: any) => { d[0].manifest.dependencies[0].pluginId = "org.missing"; }],
  ["dependency version", "DEPENDENCY_VERSION", (d: any) => { d[0].manifest.dependencies[0].version = "^2.0"; }],
  ["dependency cycle", "DEPENDENCY_CYCLE", (d: any) => { d[1].manifest.dependencies.push({ pluginId: "org.example.pipeline", version: "1.2.0" }); }],
  ["duplicate dependency", "DESCRIPTOR_DUPLICATE", (d: any) => { d[0].manifest.dependencies.push({ pluginId: "org.example.math", version: "other" }); }],
  ["duplicate plugin", "DESCRIPTOR_DUPLICATE", (d: any) => { d.push(structuredClone(d[0])); }],
  ["duplicate definition", "DESCRIPTOR_DUPLICATE", (d: any) => { d[0].nodeDefinitions.push(structuredClone(d[0].nodeDefinitions[0])); }],
  ["duplicate type", "DESCRIPTOR_DUPLICATE", (d: any) => { d[0].types.push(structuredClone(d[0].types[0])); }],
  ["duplicate target", "DESCRIPTOR_DUPLICATE", (d: any) => { d[0].targets.push(structuredClone(d[0].targets[0])); }],
  ["duplicate Bridge adapter", "DESCRIPTOR_DUPLICATE", (d: any) => { d[0].bridges.push(structuredClone(d[0].bridges[0])); }],
  ["duplicate static port", "DESCRIPTOR_DUPLICATE", (d: any) => { d[0].nodeDefinitions[0].ports.push(structuredClone(d[0].nodeDefinitions[0].ports[0])); }],
  ["foreign definition ownership", "DESCRIPTOR_OWNER", (d: any) => { d[0].nodeDefinitions[0].typeId = "org.other/source"; }],
  ["foreign type ownership", "DESCRIPTOR_OWNER", (d: any) => { d[0].types[0].typeId = "org.other/Value@1"; }],
  ["foreign target ownership", "DESCRIPTOR_OWNER", (d: any) => { d[0].targets[0].targetId = "org.other/target"; }],
  ["node declarations", "MANIFEST_DECLARATIONS", (d: any) => { d[0].manifest.nodeTypes = []; }],
  ["target declarations", "MANIFEST_DECLARATIONS", (d: any) => { d[0].manifest.targets = []; }],
  ["undeclared foreign consumption", "DEPENDENCY_UNDECLARED", (d: any) => { d[0].manifest.dependencies = []; }],
  ["unknown port type", "DESCRIPTOR_REFERENCE", (d: any) => { d[0].nodeDefinitions[0].ports[0].typeId = "org.example.pipeline/Absent@1"; }],
  ["unknown compatibility type", "DESCRIPTOR_REFERENCE", (d: any) => { d[0].types[0].compatibleTo = ["org.example.math/Absent@1"]; }],
  ["unknown Target", "DESCRIPTOR_REFERENCE", (d: any) => { d[1].nodeDefinitions[0].supportedTargets = ["org.absent/target"]; }],
  ["unknown Target definition version", "DESCRIPTOR_REFERENCE", (d: any) => { d[0].targets[0].supportedNodeDefinitions[0].nodeDefinitionVersion = "99"; }],
  ["one-sided Target support", "TARGET_SUPPORT_MISMATCH", (d: any) => { d[0].targets[0].supportedNodeDefinitions.pop(); }],
  ["one-sided node support", "TARGET_SUPPORT_MISMATCH", (d: any) => { d[1].nodeDefinitions[0].supportedTargets = []; }],
  ["unknown Bridge wire type", "DESCRIPTOR_REFERENCE", (d: any) => { d[0].bridges[0].mappings[0].wireTypeId = "org.example.math/Absent@1"; }],
  ["cardinality contradiction", "PORT_LIMITS", (d: any) => { d[0].nodeDefinitions[0].ports[0].minConnections = 3; }],
  ["invalid properties schema", "PROPERTY_SCHEMA_INVALID", (d: any) => { d[0].nodeDefinitions[0].propertiesSchema = { type: "made-up-type" }; }],
  ["unresolvable properties reference", "PROPERTY_SCHEMA_INVALID", (d: any) => { d[0].nodeDefinitions[0].propertiesSchema = { $ref: "https://offline.invalid/schema" }; }],
  ["async properties schema", "PROPERTY_SCHEMA_INVALID", (d: any) => { d[0].nodeDefinitions[0].propertiesSchema = { $async: true, type: "object" }; }],
  ["unknown descriptor version", "DESCRIPTOR_STRUCTURE", (d: any) => { d[0].descriptorVersion = "99"; }],
  ["dynamic ports unsupported", "DESCRIPTOR_STRUCTURE", (d: any) => { d[0].nodeDefinitions[0].portMode = "dynamic"; }],
  ["negative policy count", "DESCRIPTOR_STRUCTURE", (d: any) => { d[0].bridges[0].requiredDriverCount.source = -1; }],
  ["unsafe cardinality integer", "DESCRIPTOR_STRUCTURE", (d: any) => { d[0].nodeDefinitions[0].ports[0].maxConnections = 1e100; }],
] as const)("rejects %s without publishing a partial context", (_name, code, mutate) => {
  const descriptors = load("descriptors"); mutate(descriptors);
  const result = assembleTrustedContext(descriptors, trustedLocks);
  expect(result.diagnostics.map(d => d.code)).toContain(code);
  expect(result.context).toBeUndefined();
});

it.each([null, {}, [null], []])("rejects malformed or empty descriptor collection %j", input => {
  expect(assembleCodes(input)).toContain("DESCRIPTOR_STRUCTURE");
});
it.each([null, [{}], [{ ...trustedLocks[0], sha256: "bad" }]])("validates the independent trusted lock container %j", locks => {
  expect(assembleCodes(load("descriptors"), locks)).toContain("TRUSTED_LOCK_STRUCTURE");
});
it("rejects absent, duplicate or mismatched trust pins", () => {
  expect(assembleCodes(load("descriptors"), [])).toContain("DESCRIPTOR_UNTRUSTED");
  expect(assembleCodes(load("descriptors"), [...trustedLocks, trustedLocks[0]])).toContain("TRUSTED_LOCK_DUPLICATE");
  for (const field of ["package", "version", "pluginApiVersion"] as const) {
    const locks = structuredClone(trustedLocks); locks[0][field] = "other";
    expect(assembleCodes(load("descriptors"), locks)).toContain("DESCRIPTOR_LOCK_MISMATCH");
  }
});
it("requires an explicitly supported API and Blueprint version", () => {
  const descriptors = load("descriptors"); descriptors[0].manifest.pluginApiVersion = "future";
  const locks = structuredClone(trustedLocks); locks[0].pluginApiVersion = "future";
  expect(assembleCodes(descriptors, locks)).toContain("DESCRIPTOR_API_UNSUPPORTED");
  descriptors[0].manifest.supportedBlueprintSchemas = [1];
  expect(assembleCodes(descriptors, locks)).toContain("DESCRIPTOR_BLUEPRINT_UNSUPPORTED");
});

it.each([
  ["missing static port", "NODE_PORT_MISSING", (d: any) => { d.nodes[0].ports = []; }],
  ["unknown static port", "NODE_PORT_UNKNOWN", (d: any) => { d.nodes[0].ports.push({ ...d.nodes[0].ports[0], id: "extra" }); }],
  ["changed port type", "NODE_PORT_CONTRACT", (d: any) => { d.nodes[0].ports[0].typeId = "org.example.math/Value@1"; }],
  ["changed port kind", "NODE_PORT_CONTRACT", (d: any) => { d.nodes[0].ports[0].kind = "event"; }],
  ["changed port direction", "NODE_PORT_CONTRACT", (d: any) => { d.nodes[0].ports[0].direction = "in"; }],
  ["invalid properties", "NODE_PROPERTIES", (d: any) => { d.nodes[0].properties = { label: 7 }; }],
  ["missing dependency lock", "DEPENDENCY_LOCK_MISSING", (d: any) => { d.pluginLocks.pop(); }],
  ["unfulfilled minimum cardinality", "PORT_CARDINALITY", (d: any) => { d.edges = []; }],
  ["unfulfilled Bridge driver", "BRIDGE_DRIVER_REQUIRED", (d: any) => { d.channels[0].bridge.endpointDriverBindingIds = {}; }],
  ["unknown adapter version", "BRIDGE_UNKNOWN", (d: any) => { d.channels[0].bridge.version = "1.2.0"; }],
] as const)("checks blueprint %s against declarations", (_name, code, mutate) => {
  const doc = load("blueprint"); mutate(doc);
  expect(validate(doc).map(d => d.code)).toContain(code);
});
it("projects declared maximum cardinality, preserves port display-name edits and extensions", () => {
  const descriptors = load("descriptors"); descriptors[0].nodeDefinitions[0].ports[0].maxConnections = 1;
  expect(validate(load("blueprint"), descriptors).map(d => d.code)).toContain("PORT_CARDINALITY");
  expect(validate()).toEqual([]);
});
it("gates malformed blueprints before reading node snapshots", () => {
  expect(validate(null).map(d => d.code)).toEqual(expect.arrayContaining(["STRUCTURE_INVALID"]));
});
function freeze<T>(value: T): T {
  if (value && typeof value === "object") { Object.values(value).forEach(freeze); Object.freeze(value); }
  return value;
}
it("does not mutate inputs, returns independent snapshots and stable context regardless of descriptor order", () => {
  const descriptors = freeze(load("descriptors")), locks = freeze(structuredClone(trustedLocks)), doc = freeze(load("blueprint"));
  const before = JSON.stringify([descriptors, locks, doc]);
  const result = assembleTrustedContext(descriptors, locks);
  expect(result.context).toBeDefined();
  expect(assembleTrustedContext([...descriptors].reverse(), [...locks].reverse()).context).toEqual(result.context);
  expect(validate(doc, descriptors, locks)).toEqual([]);
  expect(JSON.stringify([descriptors, locks, doc])).toBe(before);
  (result.context!.nodeDefinitions[0].supportedTargets as string[]).push("unrelated/target");
  expect(JSON.stringify([descriptors, locks, doc])).toBe(before);
});

it("accepts an entirely different domain through data alone", () => {
  const rename = (value: unknown) => JSON.parse(JSON.stringify(value).replaceAll("org.example.pipeline", "com.acme.sensor").replaceAll("org.example.math", "com.acme.metrics"));
  expect(validate(rename(load("blueprint")), rename(load("descriptors")), rename(trustedLocks))).toEqual([]);
});
it("uses only independent pins for hashes and gives capabilities no runtime authority", () => {
  const descriptors = load("descriptors");
  descriptors[0].manifest.sha256 = "c".repeat(64);
  descriptors[0].manifest.capabilities.push("filesystem.unrestricted", "build.execute");
  const result = assembleTrustedContext(descriptors, trustedLocks);
  expect(result.context?.pluginLocks.find(p => p.pluginId === "org.example.pipeline")?.sha256).toBe("a".repeat(64));
  expect(validate(load("blueprint"), descriptors)).toEqual([]);
  const doc = load("blueprint"); doc.pluginLocks[0].sha256 = "c".repeat(64);
  expect(validate(doc, descriptors).map(d => d.code)).toContain("PLUGIN_LOCK_MISMATCH");
});
it("checks the complete declared dependency closure, including transitive packages", () => {
  const descriptors = load("descriptors"), third = structuredClone(descriptors[1]);
  third.manifest = { ...third.manifest, pluginId: "org.example.foundation", package: "foundation-plugin", nodeTypes: [], dependencies: [] };
  third.nodeDefinitions = []; third.types = [];
  descriptors.push(third);
  descriptors[1].manifest.dependencies = [{ pluginId: "org.example.foundation", version: "2.0.0" }];
  const locks = [...trustedLocks, { ...trustedLocks[1], pluginId: "org.example.foundation", package: "foundation-plugin" }];
  expect(assembleTrustedContext(descriptors, locks).diagnostics).toEqual([]);
  expect(validate(load("blueprint"), descriptors, locks).map(d => d.code)).toContain("DEPENDENCY_LOCK_MISSING");
  const doc = load("blueprint"); doc.pluginLocks.push(locks[2]);
  expect(validate(doc, descriptors, locks)).toEqual([]);
});
it("returns repeatable diagnostics and safely handles duplicate graph identities", () => {
  const doc = load("blueprint"); doc.nodes.push(structuredClone(doc.nodes[0]));
  const first = validate(freeze(doc));
  expect(first.map(d => d.code)).toContain("DUPLICATE_NODE_ID");
  expect(first.map(d => d.code)).not.toContain("CONTEXT_INVALID");
  expect(validate(doc)).toEqual(first);
});
it("keeps versions separate and permits multiple declared node-definition versions", () => {
  const descriptors = load("descriptors"), newer = structuredClone(descriptors[0].nodeDefinitions[0]);
  newer.nodeDefinitionVersion = "3.1.0";
  descriptors[0].nodeDefinitions.push(newer);
  descriptors[0].targets[0].supportedNodeDefinitions.push({ typeId: newer.typeId, nodeDefinitionVersion: newer.nodeDefinitionVersion });
  expect(validate(load("blueprint"), descriptors)).toEqual([]);
  const doc = load("blueprint"); doc.nodes[0].nodeDefinitionVersion = "3.1.0";
  expect(validate(doc, descriptors)).toEqual([]);
});

it.each([{ $ref: "#" }, { $id: "urn:example:recursive-properties", $ref: "urn:example:recursive-properties" }])(
  "returns diagnostics instead of throwing when a compiled properties schema cannot evaluate %j", propertiesSchema => {
    const descriptors = load("descriptors"); descriptors[0].nodeDefinitions[0].propertiesSchema = propertiesSchema;
    const result = validate(load("blueprint"), descriptors);
    expect(result).toContainEqual(expect.objectContaining({ code: "NODE_PROPERTIES_EVALUATION", severity: "error", nodeId: "source", path: "/nodes/0/properties" }));
  },
);
it("continues to support well-founded local recursive properties schemas", () => {
  const descriptors = load("descriptors");
  descriptors[0].nodeDefinitions[0].propertiesSchema = {
    $defs: { chain: { type: "object", properties: { next: { $ref: "#/$defs/chain" } }, additionalProperties: false } },
    $ref: "#/$defs/chain",
  };
  const doc = load("blueprint"); doc.nodes[0].properties = { next: { next: {} } };
  expect(validate(doc, descriptors)).toEqual([]);
});
