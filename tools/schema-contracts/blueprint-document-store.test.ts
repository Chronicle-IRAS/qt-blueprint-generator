import fs from "node:fs/promises";
import { readFileSync } from "node:fs";
import { createHash } from "node:crypto";
import os from "node:os";
import path from "node:path";
import childProcess from "node:child_process";
import { afterEach, expect, it, vi } from "vitest";
import { PluginRegistry } from "./plugin-registry.js";
import { loadBlueprintDocument, saveBlueprintDocument } from "./blueprint-document-store.js";

const roots: string[] = [];
vi.setConfig({ testTimeout: 30000 });
const registry = new PluginRegistry();
const fixture = readFileSync(new URL("../../tests/fixtures/plugin-descriptors/blueprint.json", import.meta.url), "utf8");
const sourceText = " \r\n\t" + fixture.replace("温度", "温\\u5ea6").replace(/\r?\n/g, "\r\n") + "\r\n ";
const hash = (bytes: string | Buffer) => createHash("sha256").update(bytes).digest("hex");
const target = (directory: string) => path.join(directory, "blueprint.json");
async function directory() {
  const root = await fs.mkdtemp(path.join(os.tmpdir(), "blueprint-store-"));
  roots.push(root);
  return root;
}
afterEach(async () => {
  vi.restoreAllMocks();
  for (const root of roots.splice(0)) await fs.rm(root, { recursive: true, force: true });
});

it.each(["create", "save-as"] as const)("%s publishes exact UTF-8 bytes and reloads opaque invalid-semantic records", async mode => {
  const dir = await directory();
  const saved = await saveBlueprintDocument({ directory: dir, sourceText, mode }, registry);
  expect(saved.status).toBe("saved");
  expect(await fs.readFile(target(dir))).toEqual(Buffer.from(sourceText));
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
  const loaded = await loadBlueprintDocument(dir, registry);
  expect(loaded).toMatchObject({ status: "loaded", sha256: hash(sourceText), canGenerate: false,
    readResult: { status: "read", sourceText, semanticValid: false, canGenerate: false,
      nodes: [{ kind: "unknown", nodeId: "source" }, { nodeId: "consumer" }, { nodeId: "archive-consumer" }] } });
});

it.each(["{", '{"schemaVersion":1}', '{"schemaVersion":2}'])("rejects structurally invalid input without writing: %s", async invalid => {
  const dir = await directory();
  expect(await saveBlueprintDocument({ directory: dir, sourceText: invalid, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "SOURCE_REJECTED", writeState: "not-written", readResult: { status: "rejected" } });
  expect(await fs.readdir(dir)).toEqual([]);
});

it.each(["\ufeff" + sourceText, sourceText.replace("Reading pipeline", "bad\ud800")])("rejects BOM and raw unpaired surrogates before writing", async invalid => {
  const dir = await directory();
  expect(await saveBlueprintDocument({ directory: dir, sourceText: invalid, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "ENCODING_INVALID", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual([]);
});

it.each([Buffer.concat([Buffer.from([0xef, 0xbb, 0xbf]), Buffer.from(sourceText)]), Buffer.from([0xff, 0xfe, 0x7b])])("refuses invalid disk encoding without changing bytes", async bytes => {
  const dir = await directory();
  await fs.writeFile(target(dir), bytes);
  expect(await loadBlueprintDocument(dir, registry)).toMatchObject({ status: "failed", code: "ENCODING_INVALID" });
  expect(await fs.readFile(target(dir))).toEqual(bytes);
});

it("overwrites only a structurally valid target with the caller's exact-byte baseline", async () => {
  const dir = await directory();
  await fs.writeFile(target(dir), sourceText);
  const next = sourceText + "\n";
  expect(await saveBlueprintDocument({ directory: dir, sourceText: next, mode: "overwrite", expectedSha256: hash(sourceText) }, registry))
    .toMatchObject({ status: "saved", writeState: "verified", sha256: hash(next) });
  expect(await fs.readFile(target(dir), "utf8")).toBe(next);
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it.each(["create", "save-as"] as const)("%s never clobbers an existing target", async mode => {
  const dir = await directory();
  await fs.writeFile(target(dir), "unrelated bytes");
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode }, registry))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe("unrelated bytes");
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it.each(["stale", "malformed", "missing"] as const)("refuses %s overwrite baseline", async kind => {
  const dir = await directory();
  if (kind !== "missing") await fs.writeFile(target(dir), sourceText);
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "overwrite",
    expectedSha256: kind === "malformed" ? "bad" : "0".repeat(64) }, registry))
    .toMatchObject({ status: "failed", code: kind === "malformed" ? "INVALID_REQUEST" : "CONFLICT", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual(kind === "missing" ? [] : ["blueprint.json"]);
  if (kind !== "missing") expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
});

it.each(['{"schemaVersion":1}', '{"schemaVersion":3}', '{"schemaVersion":2}', "invalid"])("preserves rejected existing format %s even when hash matches", async existing => {
  const dir = await directory();
  await fs.writeFile(target(dir), existing);
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "overwrite", expectedSha256: hash(existing) }, registry))
    .toMatchObject({ status: "failed", code: "EXISTING_REJECTED", readResult: { status: "rejected" }, writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(existing);
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it.each(["layout.json", "generation-manifest.json", ".blueprint-stale.tmp", "Blueprint.json", "user.txt"])("refuses directory with unrelated/protected entry %s", async name => {
  const dir = await directory();
  await fs.writeFile(path.join(dir, name), "keep");
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry)).toMatchObject({ status: "failed", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual([name]);
  expect(await fs.readFile(path.join(dir, name), "utf8")).toBe("keep");
});

it.each(["blueprint.json", "generation-manifest.json", "scaffold"])("refuses projects nested below protected %s ancestor", async marker => {
  const root = await directory();
  const dir = path.join(root, "nested");
  await fs.mkdir(dir);
  if (marker === "scaffold") {
    await fs.mkdir(path.join(root, "src", "contracts"), { recursive: true });
    await fs.writeFile(path.join(root, "src", "contracts", "blueprint.json"), "generated");
    await fs.writeFile(path.join(root, "src", "contracts", "source-blueprint.json"), "generated");
  } else await fs.writeFile(path.join(root, marker), "protected");
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "DIRECTORY_UNSUITABLE" });
  expect(await fs.readdir(dir)).toEqual([]);
});

it("rejects hardlinked target on both load and overwrite", async () => {
  const root = await directory();
  const dir = path.join(root, "project");
  await fs.mkdir(dir);
  const other = path.join(root, "other.json");
  await fs.writeFile(other, sourceText);
  await fs.link(other, target(dir));
  expect(await loadBlueprintDocument(dir, registry)).toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect(await saveBlueprintDocument({ directory: dir, sourceText: sourceText + " ", mode: "overwrite", expectedSha256: hash(sourceText) }, registry))
    .toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect(await fs.readFile(other, "utf8")).toBe(sourceText);
});

it("rejects nonregular blueprint target without deleting it", async () => {
  const dir = await directory();
  await fs.mkdir(target(dir));
  expect(await loadBlueprintDocument(dir, registry)).toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "overwrite", expectedSha256: hash(sourceText) }, registry))
    .toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect((await fs.stat(target(dir))).isDirectory()).toBe(true);
});

it("rejects a directory junction/symlink ancestor", async () => {
  const root = await directory();
  const real = path.join(root, "real"), link = path.join(root, "alias");
  await fs.mkdir(real);
  await fs.mkdir(path.join(real, "child"));
  await fs.symlink(real, link, process.platform === "win32" ? "junction" : "dir");
  expect(await saveBlueprintDocument({ directory: path.join(link, "child"), sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect(await fs.readdir(path.join(real, "child"))).toEqual([]);
});

it.each(["relative", "C:relative", "\\\\server\\share\\project", "\\\\?\\C:\\project", "CON", "name.", "name ", "part:stream", ".", ".."])("rejects unsafe path syntax %s", async name => {
  const root = await directory();
  await fs.mkdir(path.join(root, "child"));
  const dir = ["relative", "C:relative"].includes(name) || name.startsWith("\\\\") ? name : root + path.sep + "child" + path.sep + name;
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect(await fs.readdir(root)).toEqual(["child"]);
});

it("accepts Chinese, spaces and shell metacharacters as literal directory data", async () => {
  const root = await directory(), dir = path.join(root, "中文 空格 ' ; $() & 项目");
  await fs.mkdir(dir);
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry)).toMatchObject({ status: "saved" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
});

// Intercept only the staged read to place an external disk mutation at a deterministic boundary.
function duringStage(action: (temp: string) => Promise<void>) {
  const readFile = fs.readFile.bind(fs);
  let triggered = false;
  vi.spyOn(fs, "readFile").mockImplementation((async (...args: Parameters<typeof fs.readFile>) => {
    const bytes = await readFile(...args);
    if (!triggered && typeof args[0] === "string" && path.basename(args[0]).endsWith(".tmp")) {
      triggered = true;
      await action(args[0]);
    }
    return bytes;
  }) as typeof fs.readFile);
}

it.each(["changed", "same-hash-replaced", "missing"] as const)("rechecks overwrite %s immediately before publication", async change => {
  const root = await directory(), dir = path.join(root, "project");
  await fs.mkdir(dir);
  await fs.writeFile(target(dir), sourceText);
  duringStage(async () => {
    if (change === "changed") await fs.writeFile(target(dir), "external writer");
    else {
      await fs.rename(target(dir), path.join(root, "original.json"));
      if (change === "same-hash-replaced") await fs.writeFile(target(dir), sourceText);
    }
  });
  expect(await saveBlueprintDocument({ directory: dir, sourceText: sourceText + " ", mode: "overwrite", expectedSha256: hash(sourceText) }, registry))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual(change === "missing" ? [] : ["blueprint.json"]);
  if (change !== "missing") expect(await fs.readFile(target(dir), "utf8")).toBe(change === "changed" ? "external writer" : sourceText);
});

it.each(["create", "save-as"] as const)("%s preserves a target created during staging", async mode => {
  const dir = await directory();
  duringStage(async () => { await fs.writeFile(target(dir), "concurrent creation"); });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode }, registry))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe("concurrent creation");
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it("uses no-clobber publication even after the final directory check", async () => {
  const dir = await directory(), link = fs.link.bind(fs);
  vi.spyOn(fs, "link").mockImplementationOnce(async (from, to) => {
    await fs.writeFile(to, "last moment writer");
    return link(from, to);
  });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe("last moment writer");
});

it("rechecks unrelated entries introduced during staging", async () => {
  const dir = await directory();
  duringStage(async () => { await fs.writeFile(path.join(dir, "layout.json"), "keep"); });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "DIRECTORY_UNSUITABLE", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual(["layout.json"]);
  expect(await fs.readFile(path.join(dir, "layout.json"), "utf8")).toBe("keep");
});

it.each(["write", "sync", "close"] as const)("preserves the old file and removes only its temp after staged %s failure", async failure => {
  const dir = await directory(), open = fs.open.bind(fs);
  await fs.writeFile(target(dir), sourceText);
  vi.spyOn(fs, "open").mockImplementation((async (...args: Parameters<typeof fs.open>) => {
    const handle = await open(...args);
    if (args[1] === "wx") {
      if (failure === "write") {
        const writeFile = handle.writeFile.bind(handle);
        vi.spyOn(handle, "writeFile").mockImplementationOnce(async () => { await writeFile("partial"); throw new Error("disk full"); });
      } else if (failure === "sync") vi.spyOn(handle, "sync").mockRejectedValueOnce(new Error("sync failed"));
      else {
        const close = handle.close.bind(handle);
        vi.spyOn(handle, "close").mockImplementationOnce(async () => { await close(); throw new Error("close failed"); });
      }
    }
    return handle;
  }) as typeof fs.open);
  expect(await saveBlueprintDocument({ directory: dir, sourceText: sourceText + " ", mode: "overwrite", expectedSha256: hash(sourceText) }, registry))
    .toMatchObject({ status: "failed", code: "IO_ERROR", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it.each(["rename", "link"] as const)("does not fall back to truncation after %s publication failure", async operation => {
  const dir = await directory();
  if (operation === "rename") await fs.writeFile(target(dir), sourceText);
  vi.spyOn(fs, operation).mockRejectedValueOnce(new Error("filesystem does not support publication"));
  const request = operation === "rename" ? { directory: dir, sourceText: sourceText + " ", mode: "overwrite" as const, expectedSha256: hash(sourceText) } :
    { directory: dir, sourceText, mode: "create" as const };
  expect(await saveBlueprintDocument(request, registry)).toMatchObject({ status: "failed", code: "IO_ERROR", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual(operation === "rename" ? ["blueprint.json"] : []);
  if (operation === "rename") expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
});

it("reports retained staging file when precommit cleanup fails", async () => {
  const dir = await directory();
  await fs.writeFile(target(dir), sourceText);
  vi.spyOn(fs, "rename").mockRejectedValueOnce(new Error("rename denied"));
  vi.spyOn(fs, "unlink").mockRejectedValue(new Error("cleanup denied"));
  const result = await saveBlueprintDocument({ directory: dir, sourceText: sourceText + " ", mode: "overwrite", expectedSha256: hash(sourceText) }, registry);
  expect(result).toMatchObject({ status: "failed", writeState: "not-written", cleanupFailed: true, retainedTempPath: expect.any(String) });
  if (result.status !== "failed" || !result.retainedTempPath) throw new Error("Expected retained temp");
  expect(await fs.readFile(result.retainedTempPath, "utf8")).toBe(sourceText + " ");
  expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
});

it("reports committed cleanup failure without implying the published file was rolled back", async () => {
  const dir = await directory();
  vi.spyOn(fs, "unlink").mockRejectedValue(new Error("cleanup denied"));
  const result = await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry);
  expect(result).toMatchObject({ status: "failed", code: "CLEANUP_FAILED", writeState: "committed-unverified", cleanupFailed: true, retainedTempPath: expect.any(String) });
  expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
  expect(await fs.readdir(dir)).toHaveLength(2);
});

it("does not delete an unrelated replacement at the owned temp name", async () => {
  const root = await directory(), dir = path.join(root, "project");
  await fs.mkdir(dir);
  let replaced = "";
  duringStage(async temp => {
    replaced = temp;
    await fs.rename(temp, path.join(root, "owned-original.tmp"));
    await fs.writeFile(temp, "foreign temp");
  });
  const result = await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry);
  expect(result).toMatchObject({ status: "failed", writeState: "not-written", cleanupFailed: true, retainedTempPath: replaced });
  expect(await fs.readFile(replaced, "utf8")).toBe("foreign temp");
  expect(await fs.readdir(dir)).toEqual([path.basename(replaced)]);
});

it("does not clean through a replaced parent directory", async () => {
  const root = await directory(), dir = path.join(root, "project"), moved = path.join(root, "moved");
  await fs.mkdir(dir);
  let replaced = "";
  duringStage(async temp => {
    replaced = temp;
    await fs.rename(dir, moved);
    await fs.mkdir(dir);
    await fs.writeFile(temp, "foreign directory file");
  });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written", cleanupFailed: true });
  expect(await fs.readFile(replaced, "utf8")).toBe("foreign directory file");
  expect(await fs.readFile(path.join(moved, path.basename(replaced)), "utf8")).toBe(sourceText);
});

it("rejects changed staged bytes before publication", async () => {
  const dir = await directory(), open = fs.open.bind(fs);
  vi.spyOn(fs, "open").mockImplementation((async (...args: Parameters<typeof fs.open>) => {
    const handle = await open(...args);
    if (args[1] === "wx") {
      const sync = handle.sync.bind(handle);
      vi.spyOn(handle, "sync").mockImplementationOnce(async () => { await sync(); await fs.writeFile(args[0], "corrupt staging"); });
    }
    return handle;
  }) as typeof fs.open);
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "VERIFY_FAILED", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual([]);
});

it("rejects same-inode staged mutation after the first verification read", async () => {
  const dir = await directory();
  duringStage(async temp => { await fs.writeFile(temp, "changed after read"); });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "VERIFY_FAILED", writeState: "not-written" });
  expect(await fs.readdir(dir)).toEqual([]);
});

it("refuses a temp path replaced while its final verification handle is open", async () => {
  const root = await directory(), dir = path.join(root, "project"), open = fs.open.bind(fs);
  await fs.mkdir(dir);
  let replaced = "";
  vi.spyOn(fs, "open").mockImplementation((async (...args: Parameters<typeof fs.open>) => {
    const handle = await open(...args);
    if (args[1] === "r" && typeof args[0] === "string" && args[0].endsWith(".tmp")) {
      const readFile = handle.readFile.bind(handle);
      vi.spyOn(handle, "readFile").mockImplementationOnce(async () => {
        const bytes = await readFile();
        replaced = args[0] as string;
        await fs.rename(replaced, path.join(root, "original.tmp"));
        await fs.writeFile(replaced, "foreign final temp");
        return bytes;
      });
    }
    return handle;
  }) as typeof fs.open);
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", writeState: "not-written", cleanupFailed: true });
  expect(await fs.readFile(replaced, "utf8")).toBe("foreign final temp");
  expect(await fs.readdir(dir)).toEqual([path.basename(replaced)]);
});

it("rechecks the overwrite baseline after final staged-byte verification", async () => {
  const dir = await directory(), open = fs.open.bind(fs);
  await fs.writeFile(target(dir), sourceText);
  vi.spyOn(fs, "open").mockImplementation((async (...args: Parameters<typeof fs.open>) => {
    const handle = await open(...args);
    if (args[1] === "r" && typeof args[0] === "string" && args[0].endsWith(".tmp")) {
      const readFile = handle.readFile.bind(handle);
      vi.spyOn(handle, "readFile").mockImplementationOnce(async () => {
        const bytes = await readFile();
        await fs.writeFile(target(dir), "writer during final verification");
        return bytes;
      });
    }
    return handle;
  }) as typeof fs.open);
  expect(await saveBlueprintDocument({ directory: dir, sourceText: sourceText + " ", mode: "overwrite", expectedSha256: hash(sourceText) }, registry))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe("writer during final verification");
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it("preserves duplicate IDs and arbitrary extension payloads through save and reload", async () => {
  const dir = await directory();
  const document = JSON.parse(fixture);
  document.nodes.push({ ...document.nodes[0], opaqueExtension: { list: [null, "未知", 17], nested: { flag: true } } });
  document.extension = { arbitrary: ["keep", { value: "\\u4e2d" }] };
  const text = "\r\n" + JSON.stringify(document, null, "\t") + "\r\n";
  expect(await saveBlueprintDocument({ directory: dir, sourceText: text, mode: "create" }, registry))
    .toMatchObject({ status: "saved", readResult: { semanticValid: false, sourceText: text } });
  expect(await fs.readFile(target(dir))).toEqual(Buffer.from(text));
  expect(await loadBlueprintDocument(dir, registry))
    .toMatchObject({ status: "loaded", readResult: { sourceText: text, semanticValid: false } });
});

it.each(["replace", "read-failure"] as const)("reports committed-unverified after postcommit %s without unsafe rollback", async action => {
  const dir = await directory(), rename = fs.rename.bind(fs), open = fs.open.bind(fs);
  await fs.writeFile(target(dir), sourceText);
  let published = false;
  vi.spyOn(fs, "rename").mockImplementationOnce(async (from, to) => {
    await rename(from, to); published = true;
    if (action === "replace") await fs.writeFile(to, "external after publication");
  });
  vi.spyOn(fs, "open").mockImplementation((async (...args: Parameters<typeof fs.open>) => {
    if (published && action === "read-failure" && args[0] === target(dir)) throw new Error("postcommit read denied");
    return open(...args);
  }) as typeof fs.open);
  expect(await saveBlueprintDocument({ directory: dir, sourceText: sourceText + " ", mode: "overwrite", expectedSha256: hash(sourceText) }, registry))
    .toMatchObject({ status: "failed", code: "VERIFY_FAILED", writeState: "committed-unverified" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(action === "replace" ? "external after publication" : sourceText + " ");
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it("rejects overlapping operations for the same directory", async () => {
  const dir = await directory();
  const first = saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry);
  const second = await saveBlueprintDocument({ directory: dir + path.sep, sourceText, mode: "save-as" }, registry);
  const concurrentLoad = await loadBlueprintDocument(dir, registry);
  expect(second).toMatchObject({ status: "failed", code: "BUSY", writeState: "not-written" });
  expect(concurrentLoad).toMatchObject({ status: "failed", code: "BUSY" });
  expect(await first).toMatchObject({ status: "saved" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
});

it("keeps the original overwrite baseline when the caller mutates its request", async () => {
  const dir = await directory();
  const external = sourceText + "\n";
  await fs.writeFile(target(dir), sourceText);
  const request = { directory: dir, sourceText: sourceText + " ", mode: "overwrite" as const, expectedSha256: hash(sourceText) };
  duringStage(async () => {
    await fs.writeFile(target(dir), external);
    request.expectedSha256 = hash(external);
  });
  expect(await saveBlueprintDocument(request, registry))
    .toMatchObject({ status: "failed", code: "CONFLICT", writeState: "not-written" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(external);
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
});

it("keeps the selected directory and source when the caller mutates its request", async () => {
  const dir = await directory(), other = await directory();
  const request = { directory: dir, sourceText, mode: "create" as const };
  const saving = saveBlueprintDocument(request, registry);
  request.directory = other + path.sep;
  request.sourceText = sourceText + " ";
  expect(await saving).toMatchObject({ status: "saved", writeState: "verified" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText);
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
  expect(await fs.readdir(other)).toEqual([]);
  expect(request.directory).toBe(other + path.sep);
});

it("keeps the overwrite mode when the caller mutates its request", async () => {
  const dir = await directory();
  await fs.writeFile(target(dir), sourceText);
  const request: { directory: string; sourceText: string; mode: "overwrite" | "create"; expectedSha256: string } =
    { directory: dir, sourceText: sourceText + " ", mode: "overwrite", expectedSha256: hash(sourceText) };
  duringStage(async () => { request.mode = "create"; });
  expect(await saveBlueprintDocument(request, registry)).toMatchObject({ status: "saved", writeState: "verified" });
  expect(await fs.readFile(target(dir), "utf8")).toBe(sourceText + " ");
  expect(await fs.readdir(dir)).toEqual(["blueprint.json"]);
  expect(request.mode).toBe("create");
});

it.runIf(process.platform === "win32")("fails closed when native Windows attribute inspection is unavailable", async () => {
  const dir = await directory();
  vi.spyOn(childProcess, "execFile").mockImplementationOnce((...args: any[]) => {
    queueMicrotask(() => args.at(-1)(new Error("PowerShell unavailable"), "", ""));
    return { stdin: { on() { return this; }, end() {} } } as any;
  });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect(await fs.readdir(dir)).toEqual([]);
});

it.runIf(process.platform === "win32")("rejects native reparse attributes even when lstat calls the directory ordinary", async () => {
  const dir = await directory();
  vi.spyOn(childProcess, "execFile").mockImplementationOnce((...args: any[]) => {
    return { stdin: { on() { return this; }, end(data: string) {
      args.at(-1)(null, JSON.stringify(JSON.parse(data).map(() => 0x400)), "");
    } } } as any;
  });
  expect(await saveBlueprintDocument({ directory: dir, sourceText, mode: "create" }, registry))
    .toMatchObject({ status: "failed", code: "PATH_UNSAFE" });
  expect(await fs.readdir(dir)).toEqual([]);
});
