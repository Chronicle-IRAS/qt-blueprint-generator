import { createContracts } from "./validator.js";

export interface Diagnostic {
  readonly code: string; readonly severity: "error"; readonly message: string; readonly path: string;
  readonly nodeId?: string; readonly portId?: string; readonly edgeId?: string;
  readonly channelId?: string; readonly unitId?: string; readonly driverBindingId?: string; readonly pluginId?: string;
}
export interface PluginLock {
  readonly pluginId: string; readonly package: string; readonly version: string;
  readonly sha256: string; readonly pluginApiVersion: string;
}
export interface TrustedSemanticContext {
  readonly pluginLocks: readonly PluginLock[];
  readonly nodeDefinitions: readonly { readonly typeId: string; readonly version: string;
    readonly pluginId: string; readonly supportedTargets: readonly string[] }[];
  readonly targets: readonly { readonly targetId: string; readonly pluginId: string }[];
  readonly portTypes: readonly string[];
  readonly compatibility: readonly { readonly sourceTypeId: string; readonly targetTypeId: string }[];
  readonly bridges: readonly { readonly pluginId: string; readonly version: string;
    readonly mappings: readonly { readonly sourceTargetId: string; readonly targetTargetId: string;
      readonly sourceTypeId: string; readonly targetTypeId: string; readonly wireTypeId: string }[];
    readonly requiredDriverCount: { readonly source: number; readonly target: number } }[];
  /** Instance IDs resolved by caller. Counts edge AND channel endpoint occurrences. */
  readonly portCardinality: readonly { readonly nodeId: string; readonly portId: string;
    readonly min?: number; readonly max?: number }[];
}
// Read-only views AFTER the authoritative JSON Schema gate, not another runtime schema.
type Port = Readonly<{ id: string; direction: "in" | "out"; kind: string; typeId: string }>;
type Node = Readonly<{ id: string; unitId: string; typeId: string; nodeDefinitionVersion: string; ports: readonly Port[] }>;
type Endpoint = Readonly<{ nodeId: string; portId: string }>;
type ChannelEndpoint = Endpoint & Readonly<{ unitId: string }>;
type Blueprint = Readonly<{
  pluginLocks: readonly PluginLock[];
  units: readonly Readonly<{ id: string; targetId: string; pluginId: string }>[];
  nodes: readonly Node[];
  edges: readonly Readonly<{ id: string; kind: string; source: Endpoint; target: Endpoint }>[];
  channels: readonly Readonly<{ id: string; source: ChannelEndpoint; target: ChannelEndpoint;
    wireSchema: Readonly<{ typeId: string }>; bridge: Readonly<{ pluginId: string; version: string;
      endpointDriverBindingIds?: Readonly<{ source?: readonly string[]; target?: readonly string[] }> }> }>[];
  driverBindings: readonly Readonly<{ id: string; unitId: string }>[];
}>;
type Location = Partial<Omit<Diagnostic, "code" | "severity" | "message">>;
const { validateBlueprint } = createContracts();
const key = (...parts: string[]) => JSON.stringify(parts);
const lockFields = ["pluginId", "package", "version", "sha256", "pluginApiVersion"] as const;
const compare = (a: string, b: string) => a < b ? -1 : a > b ? 1 : 0;

/** Offline descriptions only: no plugin execution, permissions or byte-hash verification. */
export function validateBlueprintSemantics(input: unknown, context: TrustedSemanticContext): Diagnostic[] {
  if (!validateBlueprint(input)) return (validateBlueprint.errors ?? []).map(error => ({
    code: "STRUCTURE_INVALID", severity: "error", message: `${error.keyword}: ${error.message ?? "Invalid structure"}`, path: error.instancePath,
  }));
  const doc = input as Blueprint;
  const diagnostics: Diagnostic[] = [];
  const add = (code: string, message: string, location: Location = {}) => {
    diagnostics.push({ code, severity: "error", message, path: "", ...location });
  };
  // Undefined marks ambiguity: no first/last winner; Map supports every legal string ID.
  function index<T>(items: readonly T[], identity: (item: T) => string, code: string,
    location: (item: T, i: number) => Location): Map<string, T | undefined> {
    const result = new Map<string, T | undefined>();
    items.forEach((item, i) => {
      const id = identity(item);
      if (result.has(id)) { result.set(id, undefined); add(code, `Duplicate identity ${id}`, location(item, i)); }
      else result.set(id, item);
    });
    return result;
  }
  const ci = <T>(items: readonly T[], identity: (item: T) => string) => index(items, identity, "CONTEXT_INVALID", () => ({}));
  const trustedLocks = ci(context.pluginLocks, x => x.pluginId);
  const definitions = ci(context.nodeDefinitions, x => key(x.typeId, x.version));
  const targets = ci(context.targets, x => x.targetId);
  const types = ci(context.portTypes, x => x);
  const compatibility = ci(context.compatibility, x => key(x.sourceTypeId, x.targetTypeId));
  const bridges = ci(context.bridges, x => key(x.pluginId, x.version));
  ci(context.portCardinality, x => key(x.nodeId, x.portId));
  const inconsistent = (condition: boolean) => { if (condition) add("CONTEXT_INVALID", "Inconsistent trusted description"); };
  for (const t of context.targets) inconsistent(!trustedLocks.get(t.pluginId));
  for (const d of context.nodeDefinitions) inconsistent(!trustedLocks.get(d.pluginId) ||
    d.typeId.split("/")[0] !== d.pluginId || d.supportedTargets.some(id => !targets.get(id)));
  for (const p of context.compatibility) inconsistent(!types.has(p.sourceTypeId) || !types.has(p.targetTypeId));
  for (const b of context.bridges) {
    inconsistent(!trustedLocks.get(b.pluginId) || Object.values(b.requiredDriverCount).some(n => !Number.isSafeInteger(n) || n < 0));
    ci(b.mappings, m => key(m.sourceTargetId, m.targetTargetId, m.sourceTypeId, m.targetTypeId, m.wireTypeId));
    for (const m of b.mappings) inconsistent(!targets.get(m.sourceTargetId) || !targets.get(m.targetTargetId) || !types.has(m.sourceTypeId) || !types.has(m.targetTypeId));
  }
  for (const r of context.portCardinality) inconsistent([r.min, r.max].some(n => n !== undefined && (!Number.isSafeInteger(n) || n < 0)) ||
    (r.min !== undefined && r.max !== undefined && r.min > r.max));
  // One deterministic failure independent of context registration order.
  if (diagnostics.length) return [{ code: "CONTEXT_INVALID", severity: "error", message: "Trusted descriptions contain duplicate or inconsistent records", path: "/context" }];

  const units = index(doc.units, x => x.id, "DUPLICATE_UNIT_ID", (x, i) => ({ unitId: x.id, path: `/units/${i}/id` }));
  const nodes = index(doc.nodes, x => x.id, "DUPLICATE_NODE_ID", (x, i) => ({ nodeId: x.id, path: `/nodes/${i}/id` }));
  index(doc.edges, x => x.id, "DUPLICATE_EDGE_ID", (x, i) => ({ edgeId: x.id, path: `/edges/${i}/id` }));
  index(doc.channels, x => x.id, "DUPLICATE_CHANNEL_ID", (x, i) => ({ channelId: x.id, path: `/channels/${i}/id` }));
  const drivers = index(doc.driverBindings, x => x.id, "DUPLICATE_DRIVER_BINDING_ID", (x, i) => ({ driverBindingId: x.id, path: `/driverBindings/${i}/id` }));
  const locks = index(doc.pluginLocks, x => x.pluginId, "DUPLICATE_PLUGIN_LOCK", (x, i) => ({ pluginId: x.pluginId, path: `/pluginLocks/${i}/pluginId` }));
  const ports = new Map<Node, Map<string, Port | undefined>>();
  doc.nodes.forEach((n, i) => ports.set(n, index(n.ports, x => x.id, "DUPLICATE_PORT_ID", (p, j) => ({ nodeId: n.id, portId: p.id, path: `/nodes/${i}/ports/${j}/id` }))));
  const requirePlugin = (pluginId: string, location: Location) => {
    if (!locks.get(pluginId)) add("PLUGIN_LOCK_MISSING", "Provider lock missing or ambiguous", { ...location, pluginId });
    if (!trustedLocks.get(pluginId)) add("PLUGIN_UNTRUSTED", "Provider has no trusted description", { ...location, pluginId });
  };
  doc.pluginLocks.forEach((lock, i) => {
    const trusted = trustedLocks.get(lock.pluginId);
    if (!trusted) add("PLUGIN_UNTRUSTED", "No trusted package description", { pluginId: lock.pluginId, path: `/pluginLocks/${i}` });
    else if (lockFields.some(f => lock[f] !== trusted[f])) add("PLUGIN_LOCK_MISMATCH", "Declared metadata differs from trusted metadata (not a byte hash check)", { pluginId: lock.pluginId, path: `/pluginLocks/${i}` });
  });
  doc.units.forEach((unit, i) => {
    const loc = { unitId: unit.id, path: `/units/${i}` };
    requirePlugin(unit.pluginId, loc);
    const target = targets.get(unit.targetId);
    if (!target) add("TARGET_UNKNOWN", "Target has no trusted description", loc);
    else if (target.pluginId !== unit.pluginId) add("TARGET_PROVIDER_MISMATCH", "Target provider differs from Unit provider", loc);
  });
  doc.nodes.forEach((node, i) => {
    const loc = { nodeId: node.id, unitId: node.unitId, path: `/nodes/${i}` };
    const unit = units.get(node.unitId);
    if (!unit) add("UNIT_NOT_FOUND", "Node Unit missing or ambiguous", loc);
    const definition = definitions.get(key(node.typeId, node.nodeDefinitionVersion));
    if (!definition) add("NODE_DEFINITION_UNKNOWN", "Node type/version has no trusted definition", loc);
    else {
      requirePlugin(definition.pluginId, loc);
      if (unit && !definition.supportedTargets.includes(unit.targetId)) add("NODE_TARGET_UNSUPPORTED", "Node definition does not support target", loc);
    }
    node.ports.forEach((port, j) => {
      if (!types.has(port.typeId)) add("PORT_TYPE_UNKNOWN", "Port type is not registered", { ...loc, portId: port.id, path: `/nodes/${i}/ports/${j}/typeId` });
    });
  });
  doc.driverBindings.forEach((d, i) => {
    if (!units.get(d.unitId)) add("UNIT_NOT_FOUND", "Driver Unit missing or ambiguous", { driverBindingId: d.id, unitId: d.unitId, path: `/driverBindings/${i}/unitId` });
  });
  const counts = new Map<string, number>();
  function endpoint(ep: Endpoint, side: "source" | "target", location: Location) {
    const loc = { ...location, nodeId: ep.nodeId, portId: ep.portId };
    const node = nodes.get(ep.nodeId);
    if (!node) { add("NODE_NOT_FOUND", "Endpoint node missing or ambiguous", loc); return; }
    const port = ports.get(node)?.get(ep.portId);
    if (!port) { add("PORT_NOT_FOUND", "Endpoint port missing or ambiguous", loc); return; }
    const id = key(ep.nodeId, ep.portId);
    counts.set(id, (counts.get(id) ?? 0) + 1);
    if (port.direction !== (side === "source" ? "out" : "in")) add("PORT_DIRECTION", "Connection must be output to input", loc);
    return { node, port };
  }
  doc.edges.forEach((edge, i) => {
    const loc = { edgeId: edge.id, path: `/edges/${i}` };
    const source = endpoint(edge.source, "source", { ...loc, path: `${loc.path}/source` });
    const target = endpoint(edge.target, "target", { ...loc, path: `${loc.path}/target` });
    for (const end of [source, target]) if (end && end.port.kind !== edge.kind) add("EDGE_KIND", "Edge kind differs from endpoint", { ...loc, nodeId: end.node.id, portId: end.port.id });
    if (!source || !target) return;
    if (source.node.unitId !== target.node.unitId) add("EDGE_CROSS_UNIT", "Ordinary edge crosses Unit boundary", loc);
    if (source.port.typeId !== target.port.typeId && !compatibility.has(key(source.port.typeId, target.port.typeId))) add("EDGE_TYPE", "No exact or explicit directional compatibility", loc);
  });
  doc.channels.forEach((channel, i) => {
    const loc = { channelId: channel.id, path: `/channels/${i}` };
    requirePlugin(channel.bridge.pluginId, loc);
    const policy = bridges.get(key(channel.bridge.pluginId, channel.bridge.version));
    if (!policy) add("BRIDGE_UNKNOWN", "No trusted Bridge adapter at this version", { ...loc, pluginId: channel.bridge.pluginId });
    const resolved: Partial<Record<"source" | "target", ReturnType<typeof endpoint>>> = {};
    for (const side of ["source", "target"] as const) {
      const ep = channel[side];
      const sideLoc = { ...loc, unitId: ep.unitId, nodeId: ep.nodeId, portId: ep.portId, path: `${loc.path}/${side}` };
      resolved[side] = endpoint(ep, side, sideLoc);
      const unit = units.get(ep.unitId);
      if (!unit) add("UNIT_NOT_FOUND", "Channel Unit missing or ambiguous", sideLoc);
      if (resolved[side] && resolved[side]!.node.unitId !== ep.unitId) add("CHANNEL_UNIT_MISMATCH", "Endpoint node belongs to different Unit", sideLoc);
      let validDrivers = 0;
      for (const id of channel.bridge.endpointDriverBindingIds?.[side] ?? []) {
        const driver = drivers.get(id);
        const driverLoc = { ...sideLoc, driverBindingId: id };
        if (!driver) add("DRIVER_NOT_FOUND", "Binding missing or ambiguous", driverLoc);
        else if (driver.unitId !== ep.unitId) add("DRIVER_WRONG_UNIT", "Binding belongs to different endpoint Unit", driverLoc);
        else if (unit && resolved[side]?.node.unitId === ep.unitId) validDrivers++;
      }
      if (policy && validDrivers < policy.requiredDriverCount[side]) add("BRIDGE_DRIVER_REQUIRED", "Required endpoint driver count is not satisfied", sideLoc);
    }
    const { source, target } = resolved;
    if (source && target) {
      if (source.port.kind !== target.port.kind) add("CHANNEL_KIND", "Channel endpoint kinds differ", loc);
      if (policy && !policy.mappings.some(m => m.sourceTargetId === units.get(channel.source.unitId)?.targetId &&
        m.targetTargetId === units.get(channel.target.unitId)?.targetId && m.sourceTypeId === source.port.typeId &&
        m.targetTypeId === target.port.typeId && m.wireTypeId === channel.wireSchema.typeId)) add("BRIDGE_MAPPING_UNSUPPORTED", "No explicit mapping for targets, native types and wire type", loc);
    }
  });
  for (const r of [...context.portCardinality].sort((a, b) => compare(key(a.nodeId, a.portId), key(b.nodeId, b.portId)))) {
    const node = nodes.get(r.nodeId);
    const loc = { nodeId: r.nodeId, portId: r.portId, path: "/context/portCardinality" };
    if (!node || !ports.get(node)?.get(r.portId)) { add("CARDINALITY_ENDPOINT_UNKNOWN", "Rule endpoint missing or ambiguous", loc); continue; }
    const count = counts.get(key(r.nodeId, r.portId)) ?? 0;
    if (count < (r.min ?? 0) || (r.max !== undefined && count > r.max)) add("PORT_CARDINALITY", `Declared connections (${count}) violate cardinality`, loc);
  }
  return diagnostics;
}
