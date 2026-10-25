import { spawn, spawnSync } from "node:child_process";
import { readFileSync, mkdtempSync, rmSync } from "node:fs";
import { tmpdir } from "node:os";
import { resolve } from "node:path";
import { fileURLToPath } from "node:url";
import { afterAll, beforeAll, expect, it } from "vitest";
const cwd = fileURLToPath(new URL(".", import.meta.url));
const temporary = mkdtempSync(resolve(tmpdir(), "读取-reader-"));
const runtime = resolve(temporary, "部署");
const entry = resolve(runtime, "tools/schema-contracts/blueprint-reader-cli.js");
beforeAll(() => {
  const result = spawnSync(process.execPath, [resolve(cwd, "build-reader.mjs"), runtime], { encoding: "utf8" });
  expect(result.status, result.stderr + result.stdout).toBe(0);
}, 30000);
afterAll(() => rmSync(temporary, { recursive: true, force: true }));
const request = () => ({ protocolVersion: 1, requestId: "温度-1", sourceText: readFileSync(resolve(cwd, "../../tests/fixtures/plugin-descriptors/blueprint.json"), "utf8"), registry: { descriptors: [], trustedLocks: [] } });
function invoke(input: string | Buffer) {
  return spawnSync(process.execPath, [entry], { input, encoding: "utf8", maxBuffer: 40 * 1024 * 1024, timeout: 15000, cwd: temporary });
}
it("runs compiled and relocated without source TS, reads Unicode and waits for stdin EOF", async () => {
  const child = spawn(process.execPath, [entry], { cwd: temporary });
  const exit = new Promise(resolve => child.on("close", resolve));
  let output = ""; child.stdout.on("data", data => { output += data; });
  child.stdin.write(JSON.stringify(request()));
  await new Promise(resolve => setTimeout(resolve, 100));
  expect(output).toBe("");
  child.stdin.end(); await exit;
  expect(output).not.toBe("");
  expect(JSON.parse(output)).toMatchObject({ status: "read", requestId: "温度-1", canGenerate: false });
});
it.each(["{", "{}{}", "", Buffer.from([0xff, 0xc0, 0x80])])("returns exactly one bounded error for malformed bytes or multiple requests", input => {
  const result = invoke(input); expect(result.status, result.stderr).toBe(0);
  expect(JSON.parse(result.stdout)).toMatchObject({ status: "error", canGenerate: false });
});
it("rejects oversize stdin and source before parsing a blueprint", () => {
  const input = invoke(" ".repeat(32 * 1024 * 1024 + 1));
  expect(JSON.parse(input.stdout)).toMatchObject({ status: "error", diagnostics: [{ code: "REQUEST_TOO_LARGE" }] });
  const source = invoke(JSON.stringify({ ...request(), sourceText: "x".repeat(8 * 1024 * 1024 + 1) }));
  expect(JSON.parse(source.stdout)).toMatchObject({ status: "error", diagnostics: [{ code: "SOURCE_TOO_LARGE" }] });
}, 20000);
it("keeps request correlation when diagnostics make the reply exceed its output limit", () => {
  const value = request();
  const document = JSON.parse(value.sourceText);
  document.nodes[0].ports = Array.from({ length: 60000 }, () => ({ ...document.nodes[0].ports[0] }));
  const result = invoke(JSON.stringify({ ...value, sourceText: JSON.stringify(document) }));
  expect(result.status, result.stderr).toBe(0);
  expect(JSON.parse(result.stdout)).toMatchObject({ requestId: "温度-1", status: "error", diagnostics: [{ code: "REPLY_TOO_LARGE" }] });
}, 20000);
