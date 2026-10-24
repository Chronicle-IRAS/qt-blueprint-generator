import { readBlueprintDocument, type BlueprintDocumentReadResult } from "./blueprint-document-reader.js";
import { PluginRegistry } from "./plugin-registry.js";
import type { Diagnostic } from "./semantic-validator.js";

export const SOURCE_LIMIT = 8 * 1024 * 1024;
export const MESSAGE_LIMIT = 32 * 1024 * 1024;
export interface ReaderRequest {
  protocolVersion: 1; requestId: string; sourceText: string;
  registry: { descriptors: unknown; trustedLocks: unknown };
}
export type ReaderReply = { protocolVersion: 1; requestId: string; canGenerate: false } &
  (BlueprintDocumentReadResult | { status: "error"; diagnostics: readonly Diagnostic[] });
export function handleReaderRequest(input: unknown): ReaderReply {
  const object = (v: unknown): v is Record<string, unknown> => v !== null && typeof v === "object" && !Array.isArray(v);
  const requestId = object(input) && typeof input.requestId === "string" ? input.requestId.slice(0, 256) : "";
  const error = (code: string, message: string): ReaderReply => ({ protocolVersion: 1, requestId, canGenerate: false, status: "error", diagnostics: [{ code, message, severity: "error", path: "/request" }] });
  if (!object(input) || input.protocolVersion !== 1 || typeof input.requestId !== "string" ||
      !input.requestId || input.requestId.length > 256 || typeof input.sourceText !== "string" ||
      !object(input.registry) || !("descriptors" in input.registry) || !("trustedLocks" in input.registry))
    return error("PROTOCOL_INVALID", "Expected protocol version 1, request ID, source text and explicit registry declarations");
  const request = input as unknown as ReaderRequest;
  if (Buffer.byteLength(request.sourceText, "utf8") > SOURCE_LIMIT) return error("SOURCE_TOO_LARGE", "Blueprint source exceeds 8 MiB");
  const registry = new PluginRegistry();
  const registration = registry.register(request.registry.descriptors, request.registry.trustedLocks);
  if (registration.diagnostics.length) return { protocolVersion: 1, requestId, canGenerate: false, status: "error", diagnostics: registration.diagnostics };
  return { protocolVersion: 1, requestId: request.requestId, ...readBlueprintDocument(request.sourceText, registry) };
}
