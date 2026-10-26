import type { PluginRegistry } from "./plugin-registry.js";
import { readBlueprintDocument, type BlueprintDocumentReadResult } from "./blueprint-document-reader.js";
import type { Diagnostic } from "./semantic-validator.js";
import { indexJsonValueSpans, JsonSpanError, JSON_SPAN_LIMITS, type JsonValueSpan } from "./json-value-spans.js";
import { compileNodePropertiesSchema, evaluateNodeProperties } from "./plugin-descriptors.js";
import type { ErrorObject } from "ajv";
import { createHash } from "node:crypto";
import { isDeepStrictEqual } from "node:util";
import { saveBlueprintDocument, type BlueprintDocumentSaveResult } from "./blueprint-document-store.js";
export type Scalar = string | boolean | number | null;
export type BlueprintEdit = Readonly<
  { kind: "project-name"; value: string } |
  { kind: "node-property"; sourceIndex: number; property: string; value: Scalar } |
  { kind: "port-name"; sourceIndex: number; portIndex: number; portId: string; value: string }
>;
export type BlueprintEditedDocumentSaveRequest = Readonly<{ directory: string; sourceText: string; expectedSha256: string; edits: readonly BlueprintEdit[] }>;
type ReadDocument = Extract<BlueprintDocumentReadResult, { status: "read" }>;
export type BlueprintEditFailureCode = "SOURCE_REJECTED" | "ENCODING_INVALID" | "RESOURCE_LIMIT" | "DUPLICATE_KEY" | "INVALID_EDIT" | "TARGET_UNAVAILABLE" | "OVERLAPPING_EDITS" | "CANDIDATE_REJECTED" | "PROPERTY_CONTRACT" | "REGISTRY_CHANGED";
type EditFailure = Readonly<{ status: "failed"; code: BlueprintEditFailureCode; diagnostics: readonly Diagnostic[]; canGenerate: false }>;
export type BlueprintEditPreparationResult = Readonly<{
  status: "prepared"; sourceText: string; readResult: ReadDocument; diagnostics: readonly Diagnostic[]; canGenerate: false;
}> | EditFailure;
export type BlueprintEditedDocumentSaveResult = BlueprintDocumentSaveResult | (EditFailure & Readonly<{ writeState: "not-written" }>);
const failure = (code: BlueprintEditFailureCode, message: string, diagnostics?: readonly Diagnostic[]): EditFailure => Object.freeze({
  status: "failed", code, diagnostics: diagnostics ?? Object.freeze([Object.freeze({ code, severity: "error" as const, message, path: "" })]), canGenerate: false,
});
const own = (object: unknown, key: string): unknown => object && typeof object === "object" ? Object.getOwnPropertyDescriptor(object, key)?.value : undefined;
const scalar = (value: unknown): value is Scalar => value === null || typeof value === "string" || typeof value === "boolean" || (typeof value === "number" && Number.isFinite(value));
function propertyEligible(node: ReadDocument["nodes"][number], definition: {propertiesSchema: unknown}, property: string): boolean {
  return property !== "__proto__" && own(own(definition.propertiesSchema, "properties"), property) !== undefined &&
    !!node.data.properties && typeof node.data.properties === "object" && !Array.isArray(node.data.properties) && scalar(own(node.data.properties, property));
}
function portEligible(node: ReadDocument["nodes"][number], definition: {portMode: string; ports: readonly {id: string}[]}, portIndex: number, portId: string): boolean {
  return definition.portMode === "static" && node.data.ports[portIndex]?.id === portId &&
    node.data.ports.filter(p => p.id === portId).length === 1 && definition.ports.filter(p => p.id === portId).length === 1;
}
export interface BlueprintEditTargets {
  projectName: string;
  nodes: {sourceIndex: number; properties: {property: string; valueLiteral: string}[]; ports: {portIndex: number; portId: string; value: string}[]}[];
}
/** Declarative hints only; preparation reauthorizes every target and contract. */
export function blueprintEditTargets(read: ReadDocument, registry: PluginRegistry): BlueprintEditTargets {
  return {projectName: String(read.document.projectName), nodes: read.nodes.flatMap(node => {
    if (node.kind !== "known") return [];
    const definition = registry.getNode(node.typeId, node.nodeDefinitionVersion)!.declaration;
    const properties = Object.keys(node.data.properties && typeof node.data.properties === "object" ? node.data.properties : {}).filter(property => propertyEligible(node, definition, property)).map(property => {
      const value = own(node.data.properties, property) as Scalar;
      return {property, valueLiteral: Object.is(value, -0) ? "-0" : JSON.stringify(value)};
    });
    const ports = node.data.ports.flatMap((port, portIndex) => typeof port.id === "string" && portEligible(node, definition, portIndex, port.id) ? [{portIndex, portId: port.id, value: String(port.name)}] : []);
    return [{sourceIndex: node.sourceIndex, properties, ports}];
  })};
}
const at = (span: JsonValueSpan | undefined, key: string | number) => typeof key === "number" ? span?.items?.[key] : span?.members?.get(key);
const pointer = (key: string) => "/" + key.replaceAll("~", "~0").replaceAll("/", "~1");
const errorLocation = (error: ErrorObject) => [error.instancePath, error.schemaPath, error.keyword].map(part => `${part.length}:${part}`).join("");
function literalLength(value: Scalar): number {
  if (typeof value !== "string") return Object.is(value, -0) ? 2 : JSON.stringify(value).length;
  if (value.length > JSON_SPAN_LIMITS.sourceLength) return Infinity;
  let length = 2;
  for (let i = 0; i < value.length; i++) {
    const code = value.charCodeAt(i);
    if (code === 34 || code === 92 || [8, 9, 10, 12, 13].includes(code)) length += 2;
    else if (code < 32) length += 6;
    else if (code >= 0xd800 && code <= 0xdbff && value.charCodeAt(i + 1) >= 0xdc00 && value.charCodeAt(i + 1) <= 0xdfff) { length += 2; i++; }
    else length += code >= 0xd800 && code <= 0xdfff ? 6 : 1;
    if (length > JSON_SPAN_LIMITS.sourceLength) return Infinity;
  }
  return length;
}

export function prepareBlueprintEdits(sourceText: string, edits: readonly BlueprintEdit[], registry: PluginRegistry): BlueprintEditPreparationResult {
  const snapshot = registry.snapshot();
  if (typeof sourceText !== "string") return failure("SOURCE_REJECTED", "Source text must be a string");
  if (sourceText.length > JSON_SPAN_LIMITS.sourceLength) return failure("RESOURCE_LIMIT", "Source text exceeds the length limit");
  if (sourceText.startsWith("\ufeff") || Buffer.from(sourceText, "utf8").toString("utf8") !== sourceText) return failure("ENCODING_INVALID", "Source text must be lossless UTF-8 without a BOM");
  if (!Array.isArray(edits) || edits.length === 0) return failure("INVALID_EDIT", "At least one explicit edit is required");
  if (edits.length > 256) return failure("RESOURCE_LIMIT", "At most 256 edits are supported");
  let index: JsonValueSpan;
  try { index = indexJsonValueSpans(sourceText); }
  catch (error) { return failure(error instanceof JsonSpanError && error.code !== "JSON_PARSE_ERROR" ? error.code : "SOURCE_REJECTED", "Source cannot be indexed safely"); }
  const before = readBlueprintDocument(sourceText, registry);
  if (before.status !== "read") return failure("SOURCE_REJECTED", "Source does not satisfy Blueprint v2", before.diagnostics);
  const replacements: { span: JsonValueSpan; value: Scalar; length: number }[] = [];
  const editedProperties = new Map<number, Set<string>>();
  for (const edit of edits) {
    if (!edit || typeof edit !== "object" || !scalar(edit.value)) return failure("INVALID_EDIT", "Edits require finite JSON scalar values");
    let target: JsonValueSpan | undefined;
    if (edit.kind === "project-name") {
      if (typeof edit.value !== "string") return failure("INVALID_EDIT", "Project name must be a string");
      target = at(index, "projectName");
    } else if (edit.kind === "node-property" || edit.kind === "port-name") {
      if (!Number.isSafeInteger(edit.sourceIndex) || edit.sourceIndex < 0) return failure("INVALID_EDIT", "A source array index is required");
      const node = before.nodes[edit.sourceIndex];
      if (node?.kind !== "known") return failure("TARGET_UNAVAILABLE", "Only a current KnownNode can be edited");
      const definition = registry.getNode(node.typeId, node.nodeDefinitionVersion)!.declaration;
      const nodeSpan = at(at(index, "nodes"), edit.sourceIndex);
      if (edit.kind === "node-property") {
        // Ajv's existing P0.3 compiler deliberately omits this key from properties.
        // It cannot certify even a valid-looking replacement; preserve it as opaque data.
        if (edit.property === "__proto__") return failure("PROPERTY_CONTRACT", "The existing property evaluator cannot certify __proto__ edits");
        if (typeof edit.property !== "string" || !propertyEligible(node, definition, edit.property)) return failure("TARGET_UNAVAILABLE", "Property must already be a directly declared scalar member");
        target = at(at(nodeSpan, "properties"), edit.property);
        const names = editedProperties.get(edit.sourceIndex) ?? new Set<string>();
        names.add(edit.property); editedProperties.set(edit.sourceIndex, names);
      } else {
        if (typeof edit.value !== "string" || typeof edit.portId !== "string" || !Number.isSafeInteger(edit.portIndex) || edit.portIndex < 0) return failure("INVALID_EDIT", "Port name requires an exact index and ID");
        if (!portEligible(node, definition, edit.portIndex, edit.portId)) return failure("TARGET_UNAVAILABLE", "Port requires a unique static declaration at the supplied position");
        target = at(at(at(nodeSpan, "ports"), edit.portIndex), "name");
      }
    } else return failure("INVALID_EDIT", "Unsupported edit kind");
    if (!target || target.kind !== "scalar") return failure("TARGET_UNAVAILABLE", "Target scalar was not found");
    const length = literalLength(edit.value);
    if (length > JSON_SPAN_LIMITS.sourceLength) return failure("RESOURCE_LIMIT", "Replacement exceeds the source length limit");
    replacements.push({ span: target, value: edit.value, length });
  }
  replacements.sort((a, b) => b.span.start - a.span.start);
  let boundary = sourceText.length, candidateLength = sourceText.length;
  for (const { span, length } of replacements) {
    if (span.end > boundary) return failure("OVERLAPPING_EDITS", "Repeated or overlapping edits are not supported");
    candidateLength += length - (span.end - span.start);
    boundary = span.start;
  }
  if (candidateLength > JSON_SPAN_LIMITS.sourceLength) return failure("RESOURCE_LIMIT", "Candidate exceeds the length limit");
  const pieces: string[] = [];
  boundary = sourceText.length;
  for (const { span, value } of replacements) {
    pieces.push(sourceText.slice(span.end, boundary), Object.is(value, -0) ? "-0" : JSON.stringify(value));
    boundary = span.start;
  }
  pieces.push(sourceText.slice(0, boundary));
  const candidate = pieces.reverse().join("");
  const after = readBlueprintDocument(candidate, registry);
  if (after.status !== "read") return failure("CANDIDATE_REJECTED", "Edited document does not satisfy Blueprint v2", after.diagnostics);
  const contractFailure = (message: string): EditFailure => failure("PROPERTY_CONTRACT", message,
    Object.freeze([...after.diagnostics, Object.freeze({ code: "PROPERTY_CONTRACT", severity: "error" as const, message, path: "" })]));
  for (const [sourceIndex, names] of editedProperties) {
    const node = before.nodes[sourceIndex];
    try {
      const schema = registry.getNode(node.typeId, node.nodeDefinitionVersion)!.declaration.propertiesSchema;
      const validate = compileNodePropertiesSchema(schema);
      const original = evaluateNodeProperties(validate, node.data.properties);
      const changed = evaluateNodeProperties(validate, after.nodes[sourceIndex].data.properties);
      if (original.status !== "evaluated" || changed.status !== "evaluated") return contractFailure("Property contract could not safely evaluate");
      const previous = new Map<string, ErrorObject[]>();
      for (const error of original.errors) {
        const key = errorLocation(error), group = previous.get(key) ?? [];
        group.push(error); previous.set(key, group);
      }
      if (changed.errors.some(error => !previous.get(errorLocation(error))?.some(old => isDeepStrictEqual(old.params, error.params)) ||
        (error.instancePath === "" && ["const", "enum", "not", "oneOf", "false schema"].includes(error.keyword)) || [...names].some(name => {
        const path = pointer(name);
        return error.instancePath === path || error.instancePath.startsWith(path + "/") ||
          (error.instancePath === "" && [error.params.missingProperty, error.params.additionalProperty, error.params.unevaluatedProperty, error.params.propertyName, error.params.property].includes(name));
      }))) return contractFailure("Edit introduces or retains a targeted property contract failure");
    } catch { return contractFailure("Property contract could not safely evaluate"); }
  }
  if (registry.snapshot() !== snapshot) return failure("REGISTRY_CHANGED", "Registry authority changed during preparation");
  return Object.freeze({ status: "prepared", sourceText: candidate, readResult: after, diagnostics: after.diagnostics, canGenerate: false });
}

/** Re-authorizes against current metadata; the Store alone owns the filesystem transaction. */
export async function saveEditedBlueprintDocument(request: BlueprintEditedDocumentSaveRequest, registry: PluginRegistry): Promise<BlueprintEditedDocumentSaveResult> {
  const { directory, sourceText, expectedSha256, edits: requestedEdits } = request;
  // All edit data are scalar. Copy before handing control to any asynchronous operation.
  if (Array.isArray(requestedEdits) && requestedEdits.length > 256) {
    return Object.freeze({ ...failure("RESOURCE_LIMIT", "At most 256 edits are supported"), writeState: "not-written" });
  }
  const edits = Array.isArray(requestedEdits) ? requestedEdits.map(edit => ({ ...edit })) : requestedEdits;
  const prepared = prepareBlueprintEdits(sourceText, edits, registry);
  if (prepared.status === "failed") return Object.freeze({ ...prepared, writeState: "not-written" });
  if (typeof expectedSha256 !== "string" || !/^[a-f0-9]{64}$/i.test(expectedSha256)) {
    return Object.freeze({ status: "failed", code: "INVALID_REQUEST", message: "An original SHA-256 baseline is required", writeState: "not-written", canGenerate: false });
  }
  if (createHash("sha256").update(sourceText, "utf8").digest("hex") !== expectedSha256.toLowerCase()) {
    return Object.freeze({ status: "failed", code: "CONFLICT", message: "Original source text does not match the supplied baseline", writeState: "not-written", canGenerate: false });
  }
  return saveBlueprintDocument({ directory, sourceText: prepared.sourceText, mode: "overwrite", expectedSha256, expectedRegistrySnapshot: registry.snapshot() }, registry);
}
