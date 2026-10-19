import { assembleTrustedContext, validateBlueprintWithDescriptors, type PluginDescriptor, type NodeDefinitionDescriptor, type PortTypeDescriptor,
  type TargetPolicyDescriptor, type BridgePolicyDescriptor } from "./plugin-descriptors.js";
import type { Diagnostic, PluginLock, TrustedSemanticContext } from "./semantic-validator.js";

export interface RegistryResult { readonly diagnostics: readonly Diagnostic[] }
export interface RegisteredPlugin { readonly descriptor: PluginDescriptor; readonly lock: PluginLock }
export interface Declaration<T> { readonly pluginId: string; readonly declaration: T }
export interface RegistrySnapshot {
  readonly plugins: readonly RegisteredPlugin[];
  readonly nodes: readonly Declaration<NodeDefinitionDescriptor>[];
  readonly types: readonly Declaration<PortTypeDescriptor>[];
  readonly targets: readonly Declaration<TargetPolicyDescriptor>[];
  readonly bridges: readonly Declaration<BridgePolicyDescriptor>[];
  readonly context: TrustedSemanticContext;
}

const compare = (a: string, b: string) => a < b ? -1 : a > b ? 1 : 0;
const failure = (code: string, message: string, path = "/registry", pluginId?: string): RegistryResult => ({
  diagnostics: [{ code, severity: "error", message, path, ...(pluginId === undefined ? {} : { pluginId }) }],
});

// Copy parsed JSON without invoking accessors or silently discarding extensions.
// This is a data boundary, not a sandbox for arbitrary JS objects/proxies or hostile schemas.
function copyJson(value: unknown, active = new Set<object>()): unknown {
  if (value === null || typeof value === "string" || typeof value === "boolean") return value;
  if (typeof value === "number" && Number.isFinite(value)) return value;
  if (typeof value !== "object" || active.has(value)) throw new Error("Expected acyclic JSON");
  const array = Array.isArray(value);
  if (!array && ![Object.prototype, null].includes(Object.getPrototypeOf(value))) throw new Error("Expected JSON object");
  active.add(value);
  const output = array ? [] : {};
  let count = 0;
  for (const key of Reflect.ownKeys(value)) {
    if (array && key === "length") continue;
    const property = Object.getOwnPropertyDescriptor(value, key)!;
    if (typeof key !== "string" || !property.enumerable || !("value" in property) ||
      (array && (!/^(0|[1-9][0-9]*)$/.test(key) || Number(key) >= value.length))) throw new Error("Expected JSON data property");
    Object.defineProperty(output, key, { value: copyJson(property.value, active), enumerable: true, writable: true, configurable: true });
    count++;
  }
  if (array && count !== value.length) throw new Error("Sparse arrays are not parsed JSON");
  active.delete(value);
  return output;
}

function freeze<T>(value: T): T {
  if (value && typeof value === "object" && !Object.isFrozen(value)) { Object.values(value).forEach(freeze); Object.freeze(value); }
  return value;
}

function snapshot(descriptors: readonly PluginDescriptor[], context: TrustedSemanticContext): RegistrySnapshot {
  const locks = new Map(context.pluginLocks.map(lock => [lock.pluginId, lock]));
  const plugins = [...descriptors].sort((a, b) => compare(a.manifest.pluginId, b.manifest.pluginId))
    .map(descriptor => ({ descriptor, lock: locks.get(descriptor.manifest.pluginId)! }));
  function declarations<T>(select: (d: PluginDescriptor) => readonly T[], identity: (d: T) => string): Declaration<T>[] {
    return plugins.flatMap(({ descriptor }) => select(descriptor).map(declaration => ({ pluginId: descriptor.manifest.pluginId, declaration })))
      .sort((a, b) => compare(JSON.stringify([a.pluginId, identity(a.declaration)]), JSON.stringify([b.pluginId, identity(b.declaration)])));
  }
  return freeze({ plugins, context,
    nodes: declarations(d => d.nodeDefinitions, n => JSON.stringify([n.typeId, n.nodeDefinitionVersion])),
    types: declarations(d => d.types, t => t.typeId), targets: declarations(d => d.targets, t => t.targetId),
    bridges: declarations(d => d.bridges, b => b.version),
  });
}

/** In-memory declarative metadata only. Caller verifies provenance and pins before registration. */
export class PluginRegistry {
  #state = snapshot([], assembleTrustedContext([], []).context!);

  register(descriptors: unknown, trustedLocks: unknown): RegistryResult {
    let copiedDescriptors: unknown, copiedLocks: unknown;
    try { copiedDescriptors = copyJson(descriptors); copiedLocks = copyJson(trustedLocks); }
    catch { return failure("REGISTRY_INPUT_INVALID", "Registry input must be acyclic parsed JSON data"); }
    if (!Array.isArray(copiedDescriptors) || !Array.isArray(copiedLocks)) {
      return { diagnostics: assembleTrustedContext(copiedDescriptors, copiedLocks).diagnostics };
    }
    return this.#publish([...this.#state.plugins.map(p => p.descriptor), ...copiedDescriptors], [...this.#state.context.pluginLocks, ...copiedLocks]);
  }

  #publish(descriptors: readonly unknown[], locks: readonly unknown[]): RegistryResult {
    try {
      const result = assembleTrustedContext(descriptors, locks);
      if (!result.context) return { diagnostics: result.diagnostics };
      const candidate = snapshot(descriptors as readonly PluginDescriptor[], result.context);
      this.#state = candidate; // The only state write: after every check and snapshot preparation succeeds.
      return { diagnostics: [] };
    } catch { return failure("REGISTRY_PREPARATION_FAILED", "Could not prepare Registry snapshot; installed state is unchanged"); }
  }

  snapshot(): RegistrySnapshot { return this.#state; }
  getPlugin(id: string): RegisteredPlugin | undefined { return this.#state.plugins.find(p => p.descriptor.manifest.pluginId === id); }
  getNode(id: string, version: string): Declaration<NodeDefinitionDescriptor> | undefined {
    return this.#state.nodes.find(n => n.declaration.typeId === id && n.declaration.nodeDefinitionVersion === version);
  }
  getType(id: string): Declaration<PortTypeDescriptor> | undefined { return this.#state.types.find(t => t.declaration.typeId === id); }
  getTarget(id: string): Declaration<TargetPolicyDescriptor> | undefined { return this.#state.targets.find(t => t.declaration.targetId === id); }
  getBridge(id: string, version: string): Declaration<BridgePolicyDescriptor> | undefined { return this.#state.bridges.find(b => b.pluginId === id && b.declaration.version === version); }
  unregister(pluginIds: readonly string[]): RegistryResult {
    let ids: unknown;
    try { ids = copyJson(pluginIds); }
    catch { return failure("REGISTRY_INPUT_INVALID", "Removal IDs must be a JSON array of strings"); }
    if (!Array.isArray(ids) || !ids.every(id => typeof id === "string")) return failure("REGISTRY_INPUT_INVALID", "Removal IDs must be a JSON array of strings");
    const missing = ids.flatMap((id, i) => this.getPlugin(id) ? [] : failure("REGISTRY_NOT_FOUND", "Plugin is not registered", `/pluginIds/${i}`, id).diagnostics);
    if (missing.length) return { diagnostics: missing };
    const removed = new Set(ids), remaining = this.#state.plugins.filter(p => !removed.has(p.descriptor.manifest.pluginId));
    return this.#publish(remaining.map(p => p.descriptor), remaining.map(p => p.lock));
  }
  getDependents(pluginId: string): readonly string[] {
    return freeze(this.#state.plugins.filter(p => p.descriptor.manifest.dependencies.some(d => d.pluginId === pluginId))
      .map(p => p.descriptor.manifest.pluginId));
  }
  validate(blueprint: unknown): Diagnostic[] {
    return validateBlueprintWithDescriptors(blueprint, this.#state.plugins.map(p => p.descriptor), this.#state.context.pluginLocks);
  }
}
