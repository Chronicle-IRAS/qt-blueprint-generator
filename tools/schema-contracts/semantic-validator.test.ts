import { readFileSync } from "node:fs";
import { describe, expect, it } from "vitest";
import { createContracts } from "./validator.js";
import { validateBlueprintSemantics } from "./semantic-validator.js";
import { trustedContext } from "./semantic-context.fixture.js";

function fixture(name: string): any {
  return JSON.parse(readFileSync(new URL(`../../tests/fixtures/blueprint-v2/${name}.json`, import.meta.url), "utf8"));
}
const cases = [
  ["duplicate-node-id", "DUPLICATE_NODE_ID"],
  ["dangling-endpoint", "NODE_NOT_FOUND"],
  ["same-target-cross-unit-edge", "EDGE_CROSS_UNIT"],
  ["required-driver-missing", "BRIDGE_DRIVER_REQUIRED"],
  ["driver-wrong-endpoint-unit", "DRIVER_WRONG_UNIT"],
] as const;
describe("P0.1 semantic-only regressions", () => {
  const { validateBlueprint } = createContracts();
  it.each(cases)("structure accepts %s", (name) => {
    expect(validateBlueprint(fixture(`semantic-only/${name}`))).toBe(true);
  });
  it.each(cases)("semantics rejects %s with %s", (name, code) => {
    expect(validateBlueprintSemantics(fixture(`semantic-only/${name}`), trustedContext).map(d => d.code)).toContain(code);
  });
});

const check = (doc: unknown, context = trustedContext) => validateBlueprintSemantics(doc, context);
const codes = (doc: unknown, context = trustedContext) => check(doc, context).map(d => d.code);
const single = () => fixture("valid/single-qt");
const serial = () => fixture("valid/two-independent-driver-bindings");
const ipc = () => fixture("valid/driver-free-bridge");
function freeze<T>(value: T): T {
  if (value && typeof value === "object") { Object.values(value).forEach(freeze); Object.freeze(value); }
  return value;
}
describe("read-only generic semantics", () => {
  it.each(["single-qt", "multi-plugin-multi-unit", "driver-free-bridge", "two-independent-driver-bindings", "empty-structural-document", "opaque-json-values"])("accepts %s with independent descriptions", name => {
    expect(check(fixture(`valid/${name}`))).toEqual([]);
  });
  it.each([null, [], {}, { nodes: [null] }, { schemaVersion: 1 }])("gates malformed structure %j", doc => {
    expect(check(doc).every(d => d.code === "STRUCTURE_INVALID")).toBe(true);
    expect(check(doc).length).toBeGreaterThan(0);
  });
  it.each([
    ["units", "DUPLICATE_UNIT_ID"], ["nodes", "DUPLICATE_NODE_ID"], ["edges", "DUPLICATE_EDGE_ID"],
    ["channels", "DUPLICATE_CHANNEL_ID"], ["driverBindings", "DUPLICATE_DRIVER_BINDING_ID"], ["pluginLocks", "DUPLICATE_PLUGIN_LOCK"],
  ])("rejects duplicate %s", (collection, code) => {
    const doc = collection === "edges" ? single() : serial();
    doc[collection].push(structuredClone(doc[collection][0]));
    expect(codes(doc)).toContain(code);
    if (collection === "nodes") expect(codes(doc)).toContain("NODE_NOT_FOUND");
    if (collection === "driverBindings") expect(codes(doc)).toContain("BRIDGE_DRIVER_REQUIRED");
    if (collection === "pluginLocks") expect(codes(doc)).toContain("PLUGIN_LOCK_MISSING");
  });
  it("reports exact duplicate path and rejects ambiguous ports", () => {
    expect(check(fixture("semantic-only/duplicate-node-id"))).toContainEqual(expect.objectContaining({ code: "DUPLICATE_NODE_ID", nodeId: "start", path: "/nodes/1/id" }));
    const doc = single(); doc.nodes[0].ports.push(structuredClone(doc.nodes[0].ports[0]));
    expect(codes(doc)).toEqual(expect.arrayContaining(["DUPLICATE_PORT_ID", "PORT_NOT_FOUND"]));
  });
  it.each([
    ["port", "PORT_NOT_FOUND", (d: any) => { d.edges[0].target.portId = "missing"; }],
    ["direction", "PORT_DIRECTION", (d: any) => { d.nodes[0].ports[0].direction = "in"; }],
    ["kind", "EDGE_KIND", (d: any) => { d.edges[0].kind = "data"; }],
    ["type", "EDGE_TYPE", (d: any) => { d.nodes[0].ports[0].typeId = "org.blueprint.qt/QString@1"; }],
    ["unit", "UNIT_NOT_FOUND", (d: any) => { d.nodes[0].unitId = "missing"; }],
    ["definition", "NODE_DEFINITION_UNKNOWN", (d: any) => { d.nodes[0].typeId = "unknown/node"; }],
    ["definition version", "NODE_DEFINITION_UNKNOWN", (d: any) => { d.nodes[0].nodeDefinitionVersion = "99"; }],
    ["port registration", "PORT_TYPE_UNKNOWN", (d: any) => { d.nodes[0].ports[0].typeId = "unknown/type"; }],
    ["provider", "PLUGIN_LOCK_MISSING", (d: any) => { d.pluginLocks = []; }],
    ["target", "TARGET_UNKNOWN", (d: any) => { d.units[0].targetId = "unknown/target"; }],
    ["target provider", "TARGET_PROVIDER_MISMATCH", (d: any) => { d.units[0].pluginId = "org.example.audit"; }],
    ["support", "NODE_TARGET_UNSUPPORTED", (d: any) => { d.units[0].targetId = "org.example.compute/worker"; d.units[0].pluginId = "org.example.compute"; }],
  ] as const)("rejects %s", (_name, code, mutate) => {
    const doc = single(); mutate(doc); expect(codes(doc)).toContain(code);
  });
  it.each(["package", "version", "sha256", "pluginApiVersion"])("matches complete plugin metadata: %s", field => {
    const doc = single(); doc.pluginLocks[0][field] = field === "sha256" ? "b".repeat(64) : "other";
    expect(codes(doc)).toContain("PLUGIN_LOCK_MISMATCH");
  });
  it("requires trusted package metadata independently of document locks", () => {
    const ctx = structuredClone(trustedContext); const doc = single();
    expect(codes(doc, { ...ctx, pluginLocks: ctx.pluginLocks.slice(1), nodeDefinitions: [], targets: [], bridges: [] })).toContain("PLUGIN_UNTRUSTED");
  });
  it("uses only direct directional compatibility", () => {
    const doc = single(); doc.nodes[0].ports[0].typeId = "org.example.audit/Entry@3";
    const sourceTypeId = "org.example.audit/Entry@3", targetTypeId = "org.blueprint.core/control-flow@1";
    expect(check(doc, { ...trustedContext, compatibility: [{ sourceTypeId, targetTypeId }] })).toEqual([]);
    expect(codes(doc, { ...trustedContext, compatibility: [{ sourceTypeId: targetTypeId, targetTypeId: sourceTypeId }] })).toContain("EDGE_TYPE");
    expect(codes(doc, { ...trustedContext, compatibility: [{ sourceTypeId, targetTypeId: "org.blueprint.qt/QString@1" }, { sourceTypeId: "org.blueprint.qt/QString@1", targetTypeId }] })).toContain("EDGE_TYPE");
  });
  it("permits branch and cycle without topology hardcoding", () => {
    const doc = single();
    doc.edges.push({ id: "branch", kind: "control", source: { nodeId: "start", portId: "flow-out" }, target: { nodeId: "page", portId: "flow-in" } });
    doc.edges.push({ id: "cycle", kind: "control", source: { nodeId: "page", portId: "flow-out" }, target: { nodeId: "logic", portId: "flow-in" } });
    expect(check(doc)).toEqual([]);
  });
  it("accepts prototype names and collision-safe tuple identities", () => {
    const doc = single(); doc.nodes[0].id = "__proto__"; doc.edges[0].source.nodeId = "__proto__";
    doc.nodes[0].ports[0].id = "constructor"; doc.edges[0].source.portId = "constructor";
    expect(check(doc)).toEqual([]);
    const ctx = { ...trustedContext, nodeDefinitions: [...trustedContext.nodeDefinitions,
      { typeId: "org.blueprint.qt/a:b", version: "c", pluginId: "org.blueprint.qt", supportedTargets: [] },
      { typeId: "org.blueprint.qt/a", version: "b:c", pluginId: "org.blueprint.qt", supportedTargets: [] }] };
    expect(check(doc, ctx)).toEqual([]);
  });
  it("freezes both inputs, preserves opaque data and returns stable diagnostics", () => {
    const doc = freeze(fixture("semantic-only/duplicate-node-id")); const ctx = freeze(structuredClone(trustedContext));
    const before = JSON.stringify([doc, ctx]);
    const first = check(doc, ctx); expect(check(doc, ctx)).toEqual(first);
    expect(check(doc, { ...ctx, pluginLocks: [...ctx.pluginLocks].reverse(), nodeDefinitions: [...ctx.nodeDefinitions].reverse(), targets: [...ctx.targets].reverse(), bridges: [...ctx.bridges].reverse() })).toEqual(first);
    expect(JSON.stringify([doc, ctx])).toBe(before);
    expect(check(freeze(fixture("valid/opaque-json-values")), ctx)).toEqual([]);
  });
});

describe("Bridge descriptions and driver dependencies", () => {
  it.each([undefined, {}, { source: [], target: [] }])("accepts driver-free endpoints %j", bindings => {
    const doc = ipc(); doc.channels[0].bridge.endpointDriverBindingIds = bindings; expect(check(doc)).toEqual([]);
  });
  it("keeps adapter and package versions independent", () => {
    const doc = ipc(); doc.channels[0].bridge.version = "adapter-7";
    const ctx = { ...trustedContext, bridges: trustedContext.bridges.map(b => b.pluginId.endsWith(".ipc") ? { ...b, version: "adapter-7" } : b) };
    expect(check(doc, ctx)).toEqual([]); expect(codes(doc)).toContain("BRIDGE_UNKNOWN");
  });
  it("never guesses driver policy from plugin names", () => {
    const doc = fixture("semantic-only/required-driver-missing");
    expect(check(doc, { ...trustedContext, bridges: trustedContext.bridges.map(b => ({ ...b, requiredDriverCount: { source: 0, target: 0 } })) })).toEqual([]);
    expect(codes(doc, { ...trustedContext, bridges: [] })).toContain("BRIDGE_UNKNOWN");
  });
  it.each([
    ["dangling driver", "DRIVER_NOT_FOUND", (d: any) => { d.channels[0].bridge.endpointDriverBindingIds.source = ["missing"]; }],
    ["wrong driver unit", "DRIVER_WRONG_UNIT", (d: any) => { d.channels[0].bridge.endpointDriverBindingIds.source = ["peer-serial"]; }],
    ["invalid driver unit", "UNIT_NOT_FOUND", (d: any) => { d.driverBindings[0].unitId = "missing"; }],
    ["channel unit mismatch", "CHANNEL_UNIT_MISMATCH", (d: any) => { d.channels[0].source.unitId = "desktop-peer"; }],
    ["missing channel unit", "UNIT_NOT_FOUND", (d: any) => { d.channels[0].source.unitId = "missing"; }],
    ["channel port", "PORT_NOT_FOUND", (d: any) => { d.channels[0].source.portId = "missing"; }],
    ["channel direction", "PORT_DIRECTION", (d: any) => { d.nodes[0].ports[0].direction = "in"; }],
    ["channel kind", "CHANNEL_KIND", (d: any) => { d.nodes[0].ports[0].kind = "event"; }],
    ["wire mapping", "BRIDGE_MAPPING_UNSUPPORTED", (d: any) => { d.channels[0].wireSchema.typeId = "other/wire"; }],
    ["missing bridge lock", "PLUGIN_LOCK_MISSING", (d: any) => { d.pluginLocks.pop(); }],
  ] as const)("rejects %s", (_name, code, mutate) => {
    const doc = serial(); mutate(doc); expect(codes(doc)).toContain(code);
    if (code === "DRIVER_NOT_FOUND" || code === "DRIVER_WRONG_UNIT") expect(codes(doc)).toContain("BRIDGE_DRIVER_REQUIRED");
  });
  it("allows different native types only with explicit bridge mapping", () => {
    const doc = ipc(); doc.nodes[1].ports[0].typeId = "org.example.audit/Entry@3";
    expect(codes(doc)).toContain("BRIDGE_MAPPING_UNSUPPORTED");
    const ctx = { ...trustedContext, bridges: trustedContext.bridges.map(b => ({ ...b, mappings: b.mappings.map(m => ({ ...m, targetTypeId: "org.example.audit/Entry@3" })) })) };
    expect(check(doc, ctx)).toEqual([]);
  });
});

describe("trusted context consistency and explicit cardinality", () => {
  it.each(["pluginLocks", "nodeDefinitions", "targets", "portTypes", "bridges"] as const)("rejects duplicate context %s", field => {
    const ctx = { ...trustedContext, [field]: [...trustedContext[field], trustedContext[field][0]] };
    expect(codes(single(), ctx)).toEqual(["CONTEXT_INVALID"]);
  });
  it("rejects conflicting providers and compatibility referencing unregistered types", () => {
    expect(codes(single(), { ...trustedContext, nodeDefinitions: [{ ...trustedContext.nodeDefinitions[0], pluginId: "org.example.audit" }] })).toEqual(["CONTEXT_INVALID"]);
    expect(codes(single(), { ...trustedContext, compatibility: [{ sourceTypeId: "missing/type", targetTypeId: "org.blueprint.qt/QString@1" }] })).toEqual(["CONTEXT_INVALID"]);
  });
  it.each([{ min: -1 }, { max: 0.5 }, { min: 2, max: 1 }])("rejects invalid cardinality %j", limits => {
    expect(codes(single(), { ...trustedContext, portCardinality: [{ nodeId: "start", portId: "flow-out", ...limits }] })).toEqual(["CONTEXT_INVALID"]);
  });
  it("counts channel plus edge occupancy and enforces min/max", () => {
    const doc = fixture("semantic-only/same-target-cross-unit-edge");
    const ctx = { ...trustedContext, portCardinality: [{ nodeId: "sender", portId: "out", max: 1 }] };
    expect(codes(doc, ctx)).toEqual(expect.arrayContaining(["EDGE_CROSS_UNIT", "PORT_CARDINALITY"]));
    expect(codes(single(), { ...trustedContext, portCardinality: [{ nodeId: "start", portId: "flow-out", min: 2 }] })).toContain("PORT_CARDINALITY");
    expect(codes(single(), { ...trustedContext, portCardinality: [{ nodeId: "missing", portId: "port", max: 1 }] })).toContain("CARDINALITY_ENDPOINT_UNKNOWN");
  });
});
