import type { PluginRegistry } from "./plugin-registry.js";
import type { Diagnostic } from "./semantic-validator.js";
import { createContracts } from "./validator.js";

export type JsonValue = null | boolean | number | string | readonly JsonValue[] | JsonObject;
export interface JsonObject { readonly [key: string]: JsonValue }
export type BlueprintNodeData = JsonObject & Readonly<{
  id: string; unitId: string; typeId: string; nodeDefinitionVersion: string;
  properties: JsonValue; ports: readonly JsonObject[];
}>;
export type BlueprintDocument = JsonObject & Readonly<{ nodes: readonly BlueprintNodeData[] }>;
interface NodeView {
  readonly sourceIndex: number;
  readonly nodeId: string;
  readonly unitId: string;
  readonly typeId: string;
  readonly nodeDefinitionVersion: string;
  readonly data: BlueprintNodeData;
}
export interface KnownNode extends NodeView { readonly kind: "known"; readonly title: string }
export interface UnknownNode extends NodeView { readonly kind: "unknown"; readonly reasonCodes: readonly string[] }
interface ReadResultBase {
  readonly sourceText: string;
  readonly diagnostics: readonly Diagnostic[];
  readonly canGenerate: false;
}
export type BlueprintDocumentReadResult = ReadResultBase & (
  | { readonly status: "rejected" }
  | { readonly status: "read"; readonly document: BlueprintDocument; readonly semanticValid: boolean; readonly nodes: readonly (KnownNode | UnknownNode)[] }
);

const { validateBlueprint } = createContracts();
const diagnostic = (code: string, message: string, path = ""): Diagnostic => ({ code, severity: "error", message, path });
const lockTrustCodes = new Set(["PLUGIN_LOCK_MISSING", "PLUGIN_UNTRUSTED", "PLUGIN_LOCK_MISMATCH", "DUPLICATE_PLUGIN_LOCK"]);

function freeze<T>(value: T): T {
  if (value && typeof value === "object" && !Object.isFrozen(value)) {
    Object.values(value).forEach(freeze);
    Object.freeze(value);
  }
  return value;
}

/** Reads already-decoded JSON text. This API neither saves nor authorizes generation. */
export function readBlueprintDocument(sourceText: string, registry: PluginRegistry): BlueprintDocumentReadResult {
  const rejected = (diagnostics: readonly Diagnostic[]): BlueprintDocumentReadResult => freeze({ status: "rejected", sourceText, diagnostics, canGenerate: false });
  let parsed: unknown;
  try { parsed = JSON.parse(sourceText); }
  catch { return rejected([diagnostic("JSON_PARSE_ERROR", "Invalid JSON text")]); }
  if (parsed !== null && typeof parsed === "object" && !Array.isArray(parsed) &&
    "schemaVersion" in parsed && typeof parsed.schemaVersion === "number" && parsed.schemaVersion !== 2) {
    return rejected([diagnostic("SCHEMA_VERSION_UNSUPPORTED", "Only Blueprint schema version 2 is supported", "/schemaVersion")]);
  }
  if (!validateBlueprint(parsed)) {
    return rejected((validateBlueprint.errors ?? []).map(error => diagnostic("STRUCTURE_INVALID", `${error.keyword}: ${error.message ?? "Invalid structure"}`, error.instancePath)));
  }
  const document = parsed as BlueprintDocument;
  const diagnostics = registry.validate(document);
  const nodes = document.nodes.map((data, sourceIndex): KnownNode | UnknownNode => {
    const definition = registry.getNode(data.typeId, data.nodeDefinitionVersion);
    // Namespace associates existing trust diagnostics only; it never substitutes a definition.
    const providerId = definition?.pluginId ?? data.typeId.split("/")[0];
    const reasonCodes = [...new Set(diagnostics.filter(d =>
      (lockTrustCodes.has(d.code) && d.pluginId === providerId) ||
      (d.code === "NODE_DEFINITION_UNKNOWN" && d.path === `/nodes/${sourceIndex}`),
    ).map(d => d.code))];
    const view = { sourceIndex, nodeId: data.id, unitId: data.unitId, typeId: data.typeId, nodeDefinitionVersion: data.nodeDefinitionVersion, data };
    if (definition && registry.getPlugin(definition.pluginId) && reasonCodes.length === 0) {
      return { ...view, kind: "known", title: definition.declaration.title };
    }
    return { ...view, kind: "unknown", reasonCodes };
  });
  return freeze({ status: "read", sourceText, document, semanticValid: diagnostics.length === 0, nodes, diagnostics, canGenerate: false });
}
