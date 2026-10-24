import { readFileSync } from "node:fs";
import { expect, it } from "vitest";
import { handleReaderRequest } from "./blueprint-reader-protocol.js";
const fixture = (name: string) => readFileSync(new URL(`../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url), "utf8");
const pins = [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0", sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
];
const request = (sourceText = fixture("blueprint"), descriptors: unknown = JSON.parse(fixture("descriptors")), trustedLocks: unknown = pins) => ({ protocolVersion: 1, requestId: "读取-1", sourceText, registry: { descriptors, trustedLocks } });
it("reads all fixture nodes with independent host declarations and keeps generation forbidden", () => {
  const reply = handleReaderRequest(request());
  expect(reply.status).toBe("read");
  expect(reply).toMatchObject({ protocolVersion: 1, requestId: "读取-1", canGenerate: false });
  if (reply.status !== "read") throw new Error("Expected read");
  expect(reply.nodes.map(n => n.kind)).toEqual(["known", "known", "known"]);
  expect(reply.document.nodes[0].properties).toEqual({ label: "温度" });
});
it.each([null, {}, { ...request(), protocolVersion: 2 }, { ...request(), requestId: "" }, { ...request(), sourceText: 9 }, { ...request(), registry: {} }])("fails closed for malformed protocol requests", input => {
  expect(handleReaderRequest(input).status).toBe("error");
});
it("reports invalid host declarations instead of silently reading with zero plugins", () => {
  expect(handleReaderRequest(request(undefined, {}, [])).status).toBe("error");
});
it("does not derive installed pins from the document", () => {
  const reply = handleReaderRequest(request(undefined, JSON.parse(fixture("descriptors")), []));
  expect(reply.status).toBe("error");
});
it("preserves unknown nodes without plugins and reads a valid empty graph", () => {
  const reply = handleReaderRequest(request(undefined, [], []));
  expect(reply.status).toBe("read");
  if (reply.status !== "read") throw new Error("Expected read");
  expect(reply.nodes.map(n => n.kind)).toEqual(["unknown", "unknown", "unknown"]);
  const empty = handleReaderRequest(request(JSON.stringify({ schemaVersion: 2, projectId: "empty", projectName: "空", pluginLocks: [], units: [], nodes: [], edges: [], channels: [], driverBindings: [] }), [], []));
  expect(empty).toMatchObject({ status: "read", nodes: [], semanticValid: true, canGenerate: false });
});
it("rejects wrong blueprint version and invalid JSON without authorizing generation", () => {
  expect(handleReaderRequest(request('{"schemaVersion":1}'))).toMatchObject({ status: "rejected", canGenerate: false });
  expect(handleReaderRequest(request('{'))).toMatchObject({ status: "rejected", canGenerate: false });
});
it("bounds decoded source bytes", () => {
  expect(handleReaderRequest(request("温".repeat(3 * 1024 * 1024), [], [])).status).toBe("error");
});
