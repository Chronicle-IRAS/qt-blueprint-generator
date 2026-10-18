import { readFileSync } from "node:fs";
import { Ajv2020, type AnySchema, type AnySchemaObject, type ErrorObject, type ValidateFunction } from "ajv/dist/2020.js";
import { schema as blueprintSchema, createContracts } from "./validator.js";
import { validateBlueprintSemantics, type Diagnostic, type PluginLock, type TrustedSemanticContext } from "./semantic-validator.js";

export interface AssemblyResult {
  readonly diagnostics: readonly Diagnostic[];
  readonly context?: TrustedSemanticContext;
}

// Read-only views after the JSON Schema gate; there is no second structural validator.
type StaticPort = Readonly<{ id: string; direction: string; kind: string; typeId: string; minConnections?: number; maxConnections?: number }>;
type Definition = Readonly<{ typeId: string; nodeDefinitionVersion: string; supportedTargets: readonly string[]; propertiesSchema: AnySchema; ports: readonly StaticPort[] }>;
type DefinitionRef = Readonly<{ typeId: string; nodeDefinitionVersion: string }>;
type Target = Readonly<{ targetId: string; supportedNodeDefinitions: readonly DefinitionRef[] }>;
type TypeDefinition = Readonly<{ typeId: string; compatibleTo: readonly string[] }>;
type Bridge = Omit<TrustedSemanticContext["bridges"][number], "pluginId">;
type Descriptor = Readonly<{
  manifest: Readonly<Omit<PluginLock, "sha256"> & {
    supportedBlueprintSchemas: readonly number[]; nodeTypes: readonly string[]; targets: readonly string[];
    dependencies: readonly Readonly<{ pluginId: string; version: string }>[];
  }>;
  nodeDefinitions: readonly Definition[]; types: readonly TypeDefinition[]; targets: readonly Target[]; bridges: readonly Bridge[];
}>;
type Entry<T> = Readonly<{ value: T; owner: Descriptor; path: string }>;
type Prepared = AssemblyResult & { definitions?: Map<string, Entry<Definition>>; properties?: Map<string, ValidateFunction>; descriptors?: readonly Descriptor[] };
const tuple = (...parts: string[]) => JSON.stringify(parts);
const compare = (a: string, b: string) => a < b ? -1 : a > b ? 1 : 0;
const definitionKey = (d: DefinitionRef) => tuple(d.typeId, d.nodeDefinitionVersion);
const options = { strict: true, allErrors: true, coerceTypes: false, removeAdditional: false, useDefaults: false } as const;
const descriptorSchema: AnySchemaObject = JSON.parse(readFileSync(new URL("../../schemas/plugin-descriptor-v1.schema.json", import.meta.url), "utf8"));
const ajv = new Ajv2020(options);
ajv.addSchema(blueprintSchema, "blueprint-v2.schema.json");
ajv.addSchema(descriptorSchema, "plugin-descriptor-v1.schema.json");
const validateDescriptors = ajv.compile<readonly Descriptor[]>({ type: "array", items: { $ref: "plugin-descriptor-v1.schema.json" } });
const validateLocks = ajv.compile<readonly PluginLock[]>({ type: "array", items: { $ref: "blueprint-v2.schema.json#/$defs/PluginLock" } });
const { validateBlueprint } = createContracts();
const diagnostic = (code: string, message: string, path: string, location: Partial<Diagnostic> = {}): Diagnostic => ({ code, severity: "error", message, path, ...location });
const schemaErrors = (errors: ErrorObject[] | null | undefined, code: string, prefix: string) =>
  (errors ?? []).map(e => diagnostic(code, `${e.keyword}: ${e.message ?? "Invalid structure"}`, prefix + e.instancePath));

function prepare(input: unknown, trustedLocks: unknown): Prepared {
  if (!validateDescriptors(input)) return { diagnostics: schemaErrors(validateDescriptors.errors, "DESCRIPTOR_STRUCTURE", "/descriptors") };
  if (!validateLocks(trustedLocks)) return { diagnostics: schemaErrors(validateLocks.errors, "TRUSTED_LOCK_STRUCTURE", "/trustedLocks") };
  const diagnostics: Diagnostic[] = [];
  const add = (code: string, message: string, path: string, pluginId?: string) => diagnostics.push(diagnostic(code, message, path, pluginId ? { pluginId } : {}));
  function index<T>(items: readonly T[], identity: (item: T) => string, code: string, path: string): Map<string, T> {
    const result = new Map<string, T>();
    items.forEach((item, i) => {
      const id = identity(item);
      if (result.has(id)) add(code, "Duplicate declaration identity", `${path}/${i}`);
      else result.set(id, item);
    });
    return result;
  }
  const pins = index(trustedLocks, p => p.pluginId, "TRUSTED_LOCK_DUPLICATE", "/trustedLocks");
  const packages = index(input, d => d.manifest.pluginId, "DESCRIPTOR_DUPLICATE", "/descriptors");
  if (diagnostics.length) return { diagnostics };
  const entries = input.map((value, i) => ({ value, path: `/descriptors/${i}` })).sort((a, b) => compare(a.value.manifest.pluginId, b.value.manifest.pluginId));
  const definitions = new Map<string, Entry<Definition>>(), types = new Map<string, Entry<TypeDefinition>>(), targets = new Map<string, Entry<Target>>();
  const properties = new Map<string, ValidateFunction>();
  function register<T>(map: Map<string, Entry<T>>, id: string, value: T, owner: Descriptor, path: string, ownedId: string) {
    if (map.has(id)) add("DESCRIPTOR_DUPLICATE", "Duplicate declaration identity", path, owner.manifest.pluginId);
    else map.set(id, { value, owner, path });
    if (ownedId.split("/")[0] !== owner.manifest.pluginId) add("DESCRIPTOR_OWNER", "Declaration is outside provider namespace", path, owner.manifest.pluginId);
  }
  const equalSets = (a: readonly string[], b: readonly string[]) => a.length === new Set(b).size && a.every(x => b.includes(x));
  for (const { value: d, path } of entries) {
    const m = d.manifest, pin = pins.get(m.pluginId);
    if (!pin) add("DESCRIPTOR_UNTRUSTED", "Provider has no independent trusted lock", path, m.pluginId);
    else if ((["package", "version", "pluginApiVersion"] as const).some(f => pin[f] !== m[f])) add("DESCRIPTOR_LOCK_MISMATCH", "Manifest differs from independent lock metadata", path, m.pluginId);
    if (m.pluginApiVersion !== "1.0-draft") add("DESCRIPTOR_API_UNSUPPORTED", "Unsupported Plugin API version", `${path}/manifest/pluginApiVersion`, m.pluginId);
    if (!m.supportedBlueprintSchemas.includes(2)) add("DESCRIPTOR_BLUEPRINT_UNSUPPORTED", "Descriptor does not support Blueprint Schema 2", `${path}/manifest/supportedBlueprintSchemas`, m.pluginId);
    if (!equalSets(m.nodeTypes, d.nodeDefinitions.map(n => n.typeId)) || !equalSets(m.targets, d.targets.map(t => t.targetId))) add("MANIFEST_DECLARATIONS", "Manifest and supplied declarations differ", `${path}/manifest`, m.pluginId);
    index(m.dependencies, dep => dep.pluginId, "DESCRIPTOR_DUPLICATE", `${path}/manifest/dependencies`);
    m.dependencies.forEach((dep, i) => {
      const provider = packages.get(dep.pluginId);
      if (!provider) add("DEPENDENCY_MISSING", "Required provider descriptor is absent", `${path}/manifest/dependencies/${i}`, m.pluginId);
      else if (provider.manifest.version !== dep.version) add("DEPENDENCY_VERSION", "Dependency requires a different exact package version", `${path}/manifest/dependencies/${i}`, m.pluginId);
    });
    d.types.forEach((t, i) => register(types, t.typeId, t, d, `${path}/types/${i}`, t.typeId));
    d.targets.forEach((t, i) => {
      register(targets, t.targetId, t, d, `${path}/targets/${i}`, t.targetId);
      index(t.supportedNodeDefinitions, definitionKey, "DESCRIPTOR_DUPLICATE", `${path}/targets/${i}/supportedNodeDefinitions`);
    });
    d.nodeDefinitions.forEach((n, i) => {
      const nodePath = `${path}/nodeDefinitions/${i}`;
      register(definitions, definitionKey(n), n, d, nodePath, n.typeId);
      index(n.ports, p => p.id, "DESCRIPTOR_DUPLICATE", `${nodePath}/ports`);
      n.ports.forEach((p, j) => {
        if (p.minConnections !== undefined && p.maxConnections !== undefined && p.minConnections > p.maxConnections) add("PORT_LIMITS", "Minimum connections exceeds maximum", `${nodePath}/ports/${j}`, m.pluginId);
      });
      try {
        // Isolated compiler: only this schema's self/local references are available.
        const validate = new Ajv2020(options).compile(n.propertiesSchema);
        if ((validate as { $async?: boolean }).$async) throw new Error("Async properties schemas are unsupported");
        properties.set(definitionKey(n), validate);
      } catch {
        add("PROPERTY_SCHEMA_INVALID", "Invalid or unsupported strict Draft 2020-12 properties schema", `${nodePath}/propertiesSchema`, m.pluginId);
      }
    });
    index(d.bridges, b => b.version, "DESCRIPTOR_DUPLICATE", `${path}/bridges`);
    d.bridges.forEach((b, i) => index(b.mappings, x => tuple(x.sourceTargetId, x.targetTargetId, x.sourceTypeId, x.targetTypeId, x.wireTypeId), "DESCRIPTOR_DUPLICATE", `${path}/bridges/${i}/mappings`));
  }
  // Kahn's algorithm checks dependencies only; no plugin activation takes place.
  const remaining = new Map<string, number>(), dependents = new Map<string, string[]>();
  for (const d of packages.values()) {
    remaining.set(d.manifest.pluginId, d.manifest.dependencies.filter(dep => packages.has(dep.pluginId)).length);
    for (const dep of d.manifest.dependencies) {
      const list = dependents.get(dep.pluginId) ?? []; list.push(d.manifest.pluginId); dependents.set(dep.pluginId, list);
    }
  }
  const ready = [...remaining.keys()].filter(id => remaining.get(id) === 0).sort(compare);
  for (let i = 0; i < ready.length; i++) for (const dependent of dependents.get(ready[i]) ?? []) {
    remaining.set(dependent, remaining.get(dependent)! - 1);
    if (remaining.get(dependent) === 0) ready.push(dependent);
  }
  if (ready.length !== packages.size) add("DEPENDENCY_CYCLE", "Plugin dependencies contain a cycle", "/descriptors");
  if (diagnostics.length) return { diagnostics };

  function reference<T>(map: Map<string, Entry<T>>, id: string, owner: Descriptor, path: string, consumed = true): Entry<T> | undefined {
    const resolved = map.get(id);
    if (!resolved) add("DESCRIPTOR_REFERENCE", "Declaration reference is unresolved", path, owner.manifest.pluginId);
    else if (consumed && resolved.owner !== owner && !owner.manifest.dependencies.some(dep => dep.pluginId === resolved.owner.manifest.pluginId)) add("DEPENDENCY_UNDECLARED", "Foreign declaration requires a direct dependency", path, owner.manifest.pluginId);
    return resolved;
  }
  for (const { value: n, owner, path } of definitions.values()) {
    for (const p of n.ports) reference(types, p.typeId, owner, `${path}/ports`);
    for (const targetId of n.supportedTargets) {
      // Advertising compatibility does not consume a Target or imply a reverse dependency.
      const target = reference(targets, targetId, owner, `${path}/supportedTargets`, false);
      if (target && !target.value.supportedNodeDefinitions.some(r => definitionKey(r) === definitionKey(n))) add("TARGET_SUPPORT_MISMATCH", "Target does not confirm node support", path, owner.manifest.pluginId);
    }
  }
  for (const { value: t, owner, path } of types.values()) for (const id of t.compatibleTo) reference(types, id, owner, `${path}/compatibleTo`);
  for (const { value: target, owner, path } of targets.values()) for (const ref of target.supportedNodeDefinitions) {
    const node = reference(definitions, definitionKey(ref), owner, `${path}/supportedNodeDefinitions`);
    if (node && !node.value.supportedTargets.includes(target.targetId)) add("TARGET_SUPPORT_MISMATCH", "Node does not advertise Target support", path, owner.manifest.pluginId);
  }
  for (const { value: d, path } of entries) for (const b of d.bridges) for (const m of b.mappings) {
    reference(targets, m.sourceTargetId, d, `${path}/bridges`); reference(targets, m.targetTargetId, d, `${path}/bridges`);
    for (const id of [m.sourceTypeId, m.targetTypeId, m.wireTypeId]) reference(types, id, d, `${path}/bridges`);
  }
  if (diagnostics.length) return { diagnostics };
  const sorted = <T>(map: Map<string, Entry<T>>) => [...map.entries()].sort(([a], [b]) => compare(a, b)).map(([, entry]) => entry);
  const context: TrustedSemanticContext = {
    pluginLocks: entries.map(({ value: d }) => {
      const { pluginId, package: packageName, version, sha256, pluginApiVersion } = pins.get(d.manifest.pluginId)!;
      return { pluginId, package: packageName, version, sha256, pluginApiVersion };
    }),
    nodeDefinitions: sorted(definitions).map(({ value: n, owner }) => ({ typeId: n.typeId, version: n.nodeDefinitionVersion, pluginId: owner.manifest.pluginId, supportedTargets: [...n.supportedTargets].sort(compare) })),
    targets: sorted(targets).map(({ value: t, owner }) => ({ targetId: t.targetId, pluginId: owner.manifest.pluginId })),
    portTypes: [...types.keys()].sort(compare),
    compatibility: sorted(types).flatMap(({ value: t }) => [...t.compatibleTo].sort(compare).map(targetTypeId => ({ sourceTypeId: t.typeId, targetTypeId }))),
    bridges: entries.flatMap(({ value: d }) => [...d.bridges].sort((a, b) => compare(a.version, b.version)).map(b => ({
      pluginId: d.manifest.pluginId, version: b.version, requiredDriverCount: { source: b.requiredDriverCount.source, target: b.requiredDriverCount.target },
      mappings: b.mappings.map(m => ({ sourceTargetId: m.sourceTargetId, targetTargetId: m.targetTargetId, sourceTypeId: m.sourceTypeId, targetTypeId: m.targetTypeId, wireTypeId: m.wireTypeId })).sort((a, b) => compare(tuple(a.sourceTargetId, a.targetTargetId, a.sourceTypeId, a.targetTypeId, a.wireTypeId), tuple(b.sourceTargetId, b.targetTargetId, b.sourceTypeId, b.targetTypeId, b.wireTypeId))),
    }))),
    portCardinality: [],
  };
  return { diagnostics: [], context, definitions, properties, descriptors: entries.map(e => e.value) };
}

/** Caller-vetted metadata is mandatory. This does not establish artifact trust or activate plugins. */
export function assembleTrustedContext(input: unknown, trustedLocks: unknown): AssemblyResult {
  const { diagnostics, context } = prepare(input, trustedLocks);
  return context ? { diagnostics, context } : { diagnostics };
}

/** Adds static ports/properties/dependency closure while preserving the original P0.2 API. */
export function validateBlueprintWithDescriptors(input: unknown, descriptors: unknown, trustedLocks: unknown): Diagnostic[] {
  const prepared = prepare(descriptors, trustedLocks);
  if (!prepared.context) return [...prepared.diagnostics];
  if (!validateBlueprint(input)) return validateBlueprintSemantics(input, prepared.context);
  const doc = input as Readonly<{
    pluginLocks: readonly PluginLock[];
    nodes: readonly Readonly<{ id: string; typeId: string; nodeDefinitionVersion: string; properties: unknown; ports: readonly StaticPort[] }>[];
  }>;
  const extra: Diagnostic[] = [];
  const limits: TrustedSemanticContext["portCardinality"][number][] = [];
  const limited = new Set<string>();
  const locked = new Set(doc.pluginLocks.map(l => l.pluginId));
  for (const d of prepared.descriptors!) if (locked.has(d.manifest.pluginId)) for (const dep of d.manifest.dependencies) {
    if (!locked.has(dep.pluginId)) extra.push(diagnostic("DEPENDENCY_LOCK_MISSING", "Blueprint must lock the provider dependency", "/pluginLocks", { pluginId: dep.pluginId }));
  }
  doc.nodes.forEach((node, i) => {
    const defKey = definitionKey(node), definition = prepared.definitions!.get(defKey)?.value;
    if (!definition) return; // Original validator diagnoses unknown definitions; do not guess a version.
    const location = { nodeId: node.id }, path = `/nodes/${i}`;
    const validateProperties = prepared.properties!.get(defKey)!;
    try {
      if (!validateProperties(node.properties)) extra.push(diagnostic("NODE_PROPERTIES", "Properties violate the declared node schema", `${path}/properties`, location));
    } catch {
      extra.push(diagnostic("NODE_PROPERTIES_EVALUATION", "Declared node schema could not safely evaluate properties", `${path}/properties`, location));
    }
    for (const p of definition.ports) {
      const observed = node.ports.find(actual => actual.id === p.id);
      if (!observed) extra.push(diagnostic("NODE_PORT_MISSING", "Declared static port is missing", `${path}/ports`, { ...location, portId: p.id }));
      else {
        if (observed.direction !== p.direction || observed.kind !== p.kind || observed.typeId !== p.typeId) extra.push(diagnostic("NODE_PORT_CONTRACT", "Static port direction, kind or type differs from declaration", `${path}/ports`, { ...location, portId: p.id }));
        const key = tuple(node.id, p.id);
        if (!limited.has(key) && (p.minConnections !== undefined || p.maxConnections !== undefined)) {
          limits.push({ nodeId: node.id, portId: p.id, min: p.minConnections, max: p.maxConnections }); limited.add(key);
        }
      }
    }
    node.ports.forEach((p, j) => {
      if (!definition.ports.some(expected => expected.id === p.id)) extra.push(diagnostic("NODE_PORT_UNKNOWN", "Static port has no declaration", `${path}/ports/${j}`, { ...location, portId: p.id }));
    });
  });
  return [...validateBlueprintSemantics(input, { ...prepared.context, portCardinality: limits }), ...extra];
}
