import fs from "node:fs/promises";
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import os from "node:os";
import path from "node:path";
import { afterEach, expect, it, vi } from "vitest";
import { PluginRegistry } from "./plugin-registry.js";
import { prepareBlueprintEdits, saveEditedBlueprintDocument, type BlueprintEdit } from "./blueprint-document-editor.js";
import { loadBlueprintDocument, saveBlueprintDocument } from "./blueprint-document-store.js";

vi.setConfig({ testTimeout: 30_000 });
const load = (name: string): any => JSON.parse(readFileSync(new URL(`../../tests/fixtures/plugin-descriptors/${name}.json`, import.meta.url), "utf8"));
const pins = [
  { pluginId: "org.example.pipeline", package: "pipeline-plugin", version: "1.2.0", sha256: "a".repeat(64), pluginApiVersion: "1.0-draft" },
  { pluginId: "org.example.math", package: "math-plugin", version: "2.0.0", sha256: "b".repeat(64), pluginApiVersion: "1.0-draft" },
];
const populated = () => {
  const registry = new PluginRegistry();
  expect(registry.register(load("descriptors"), pins).diagnostics).toEqual([]);
  return registry;
};
const source = readFileSync(new URL("../../tests/fixtures/plugin-descriptors/blueprint.json", import.meta.url), "utf8");
const hash = (text: string) => createHash("sha256").update(text, "utf8").digest("hex");
const target = (dir: string) => path.join(dir, "blueprint.json");
const roots: string[] = [];
async function directory() {
  const dir = await fs.mkdtemp(path.join(os.tmpdir(), "blueprint-editor-"));
  roots.push(dir); await fs.writeFile(target(dir), source); return dir;
}
afterEach(async () => { vi.restoreAllMocks(); for (const dir of roots.splice(0)) await fs.rm(dir, { recursive: true, force: true }); });
const nameEdits: readonly BlueprintEdit[] = [{ kind: "project-name", value: "保存新名😀" }];
const request = (dir: string) => ({ directory: dir, sourceText: source, expectedSha256: hash(source), edits: nameEdits });
function duringStage(action: () => Promise<void> | void) {
  const readFile = fs.readFile.bind(fs);
  let fired = false;
  vi.spyOn(fs, "readFile").mockImplementation((async (...args: Parameters<typeof fs.readFile>) => {
    const bytes = await readFile(...args);
    if (!fired && String(args[0]).endsWith(".tmp")) { fired = true; await action(); }
    return bytes;
  }) as typeof fs.readFile);
}

it("saves, reloads, and re-edits exact source bytes through the existing Store", async () => {
  const dir = await directory(), registry = populated();
  const next = source.replace('"Reading pipeline"', '"保存新名😀"');
  expect(await saveEditedBlueprintDocument(request(dir), registry)).toMatchObject({ status: "saved", sha256: hash(next), canGenerate: false });
  expect(await fs.readFile(target(dir))).toEqual(Buffer.from(next));
  const loaded = await loadBlueprintDocument(dir, registry);
  if (loaded.status !== "loaded") throw new Error("Expected loaded document");
  const second = next.replace('"温度"', '"new label"');
  expect(await saveEditedBlueprintDocument({ directory: dir, sourceText: loaded.readResult.sourceText, expectedSha256: loaded.sha256,
    edits: [{ kind: "node-property", sourceIndex: 0, property: "label", value: "new label" }] }, registry)).toMatchObject({ status: "saved", sha256: hash(second) });
  expect(await fs.readFile(target(dir), "utf8")).toBe(second);
});

it("rejects an original source/hash mismatch before any filesystem I/O", async () => {
  const inspect = vi.spyOn(fs, "lstat");
  expect(await saveEditedBlueprintDocument({ ...request("does-not-exist"), expectedSha256: "0".repeat(64) }, new PluginRegistry()))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(inspect).not.toHaveBeenCalled();
});

it("Store rejects a stale optional Registry snapshot before filesystem I/O", async () => {
  const registry = populated(), expectedRegistrySnapshot = registry.snapshot();
  registry.unregister(["org.example.pipeline", "org.example.math"]);
  const inspect = vi.spyOn(fs, "lstat");
  expect(await saveBlueprintDocument({ directory: "does-not-exist", sourceText: source, mode: "overwrite", expectedSha256: hash(source), expectedRegistrySnapshot } as any, registry))
    .toMatchObject({ status: "failed", code: "REGISTRY_CHANGED", writeState: "not-written" });
  expect(inspect).not.toHaveBeenCalled();
});

it("refuses Registry changes while staging and preserves the original disk bytes", async () => {
  const dir = await directory(), registry = populated();
  duringStage(() => { registry.unregister(["org.example.pipeline", "org.example.math"]); });
  expect(await saveEditedBlueprintDocument(request(dir), registry)).toMatchObject({ status: "failed", code: "REGISTRY_CHANGED", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(source);
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it("reports committed-unverified when Registry authority changes after publication", async () => {
  const dir = await directory(), registry = populated(), rename = fs.rename.bind(fs);
  vi.spyOn(fs, "rename").mockImplementationOnce(async (from, to) => {
    await rename(from, to); registry.unregister(["org.example.pipeline", "org.example.math"]);
  });
  expect(await saveEditedBlueprintDocument(request(dir), registry)).toMatchObject({ status: "failed", code: "REGISTRY_CHANGED", writeState: "committed-unverified" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(source.replace('"Reading pipeline"', '"保存新名😀"'));
});

it("re-prepares current node authority instead of trusting an earlier prepared result", async () => {
  const dir = await directory(), registry = populated();
  const edits: readonly BlueprintEdit[] = [{ kind: "node-property", sourceIndex: 0, property: "label", value: "new" }];
  expect(prepareBlueprintEdits(source, edits, registry).status).toBe("prepared");
  registry.unregister(["org.example.pipeline", "org.example.math"]);
  expect(await saveEditedBlueprintDocument({ ...request(dir), edits }, registry)).toMatchObject({ status: "failed", code: "TARGET_UNAVAILABLE", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(source);
});

it("captures request fields and edit scalars before the first await", async () => {
  const dir = await directory(), other = await directory(), registry = populated();
  const edits = [{ kind: "project-name" as const, value: "captured" }];
  const mutable = { ...request(dir), edits };
  const saving = saveEditedBlueprintDocument(mutable, registry);
  mutable.directory = other; mutable.sourceText = source + " "; mutable.expectedSha256 = hash(source + " ");
  edits[0].value = "changed later"; edits.push({ kind: "project-name", value: "extra" });
  expect(await saving).toMatchObject({ status: "saved" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(source.replace('"Reading pipeline"', '"captured"'));
  expect(await fs.readFile(target(other), "utf8")).toBe(source);
});

it("still saves project-name edits with unavailable plugins", async () => {
  const dir = await directory();
  expect(await saveEditedBlueprintDocument(request(dir), new PluginRegistry())).toMatchObject({ status: "saved", readResult: { semanticValid: false } });
  expect(await fs.readFile(target(dir), "utf8")).toBe(source.replace('"Reading pipeline"', '"保存新名😀"'));
});

it("rejects a replacement Registry snapshot even when its declarations have the same contents", async () => {
  const dir = await directory(), registry = populated(), original = registry.snapshot();
  duringStage(() => {
    registry.unregister(["org.example.pipeline", "org.example.math"]);
    expect(registry.register(load("descriptors"), pins).diagnostics).toEqual([]);
  });
  expect(await saveEditedBlueprintDocument(request(dir), registry)).toMatchObject({ status: "failed", code: "REGISTRY_CHANGED", writeState: "not-written" });
  expect(registry.snapshot()).not.toBe(original);
  expect(await fs.readFile(target(dir), "utf8")).toBe(source);
});

it("refuses a stale source baseline when the disk changed before saving", async () => {
  const dir = await directory();
  await fs.writeFile(target(dir), source + "\n");
  expect(await saveEditedBlueprintDocument(request(dir), populated())).toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(source + "\n");
});

it("rejects an oversized batch before copying edit data", async () => {
  const edits = Array(257).fill({ kind: "project-name", value: "bounded" });
  const copy = vi.spyOn(edits, "map").mockImplementation(() => { throw new Error("oversized batch was copied"); });
  await expect(saveEditedBlueprintDocument({ ...request("not-used"), edits }, new PluginRegistry())).resolves.toMatchObject({ status: "failed", code: "RESOURCE_LIMIT", writeState: "not-written" });
  expect(copy).not.toHaveBeenCalled();
});

it.each(["disk-conflict", "write", "rename", "postcommit"])("inherits Store protection for %s", async fault => {
  const dir = await directory(), registry = populated();
  if (fault === "disk-conflict") duringStage(async () => { await fs.writeFile(target(dir), "external writer"); });
  if (fault === "rename") vi.spyOn(fs, "rename").mockRejectedValueOnce(new Error("rename denied"));
  if (fault === "write") {
    const open = fs.open.bind(fs);
    vi.spyOn(fs, "open").mockImplementation((async (...args: Parameters<typeof fs.open>) => {
      const handle = await open(...args);
      if (args[1] === "wx") vi.spyOn(handle, "writeFile").mockRejectedValueOnce(new Error("disk full"));
      return handle;
    }) as typeof fs.open);
  }
  if (fault === "postcommit") {
    const rename = fs.rename.bind(fs);
    vi.spyOn(fs, "rename").mockImplementationOnce(async (from, to) => { await rename(from, to); await fs.writeFile(to, "external after publish"); });
  }
  expect(await saveEditedBlueprintDocument(request(dir), registry)).toMatchObject({ status: "failed",
    code: fault === "disk-conflict" ? "CONFLICT" : fault === "postcommit" ? "VERIFY_FAILED" : "IO_ERROR",
    writeState: fault === "postcommit" ? "committed-unverified" : "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(fault === "disk-conflict" ? "external writer" : fault === "postcommit" ? "external after publish" : source);
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});
