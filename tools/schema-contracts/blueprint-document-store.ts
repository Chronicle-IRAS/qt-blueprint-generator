import fs from "node:fs/promises";
import path from "node:path";
import { createHash, randomUUID } from "node:crypto";
import type { PluginRegistry } from "./plugin-registry.js";
import { readBlueprintDocument, type BlueprintDocumentReadResult } from "./blueprint-document-reader.js";
import { BlueprintPathError, blueprintDirectory, inspectBlueprintDirectory, inspectOwnedTemp, fileIdentity, sameBlueprintLocation, type BlueprintPathSnapshot } from "./blueprint-store-paths.js";

export type BlueprintDocumentSaveRequest = Readonly<{ directory: string; sourceText: string } & (
  | { mode: "create" | "save-as" }
  | { mode: "overwrite"; expectedSha256: string }
)>;
export type BlueprintStoreFailureCode = "SOURCE_REJECTED" | "EXISTING_REJECTED" | "ENCODING_INVALID" |
  "PATH_UNSAFE" | "DIRECTORY_UNSUITABLE" | "CONFLICT" | "BUSY" | "INVALID_REQUEST" | "IO_ERROR" | "VERIFY_FAILED" | "CLEANUP_FAILED";
type ReadDocument = Extract<BlueprintDocumentReadResult, { status: "read" }>;
interface StoreFailure {
  readonly status: "failed";
  readonly code: BlueprintStoreFailureCode;
  readonly message: string;
  readonly canGenerate: false;
  readonly writeState: "not-written" | "committed-unverified";
  readonly readResult?: BlueprintDocumentReadResult;
  readonly cleanupFailed?: true;
  readonly retainedTempPath?: string;
}
export type BlueprintDocumentLoadResult = Readonly<{
  status: "loaded"; readResult: ReadDocument; sha256: string; canGenerate: false;
}> | StoreFailure;
export type BlueprintDocumentSaveResult = Readonly<{
  status: "saved"; writeState: "verified"; readResult: ReadDocument; sha256: string; canGenerate: false;
}> | StoreFailure;

class StoreError extends Error {
  constructor(readonly code: BlueprintStoreFailureCode, message: string, readonly readResult?: BlueprintDocumentReadResult) { super(message); }
}
const digest = (bytes: Buffer) => createHash("sha256").update(bytes).digest("hex");
function decode(bytes: Buffer): string {
  try {
    const text = new TextDecoder("utf-8", { fatal: true, ignoreBOM: true }).decode(bytes);
    if (text.startsWith("\ufeff")) throw new Error();
    return text;
  } catch { throw new StoreError("ENCODING_INVALID", "Only lossless UTF-8 without a BOM is supported"); }
}
function read(bytes: Buffer, registry: PluginRegistry, code: "SOURCE_REJECTED" | "EXISTING_REJECTED"): ReadDocument {
  const result = readBlueprintDocument(decode(bytes), registry);
  if (result.status === "rejected") throw new StoreError(code, "The document does not satisfy the Blueprint v2 structure", result);
  return result;
}
function failure(error: unknown, committed = false): StoreFailure {
  return Object.freeze({ status: "failed", code: error instanceof StoreError || error instanceof BlueprintPathError ? error.code : "IO_ERROR",
    message: error instanceof StoreError || error instanceof BlueprintPathError ? error.message : "The filesystem operation failed",
    canGenerate: false, writeState: committed ? "committed-unverified" : "not-written",
    ...(error instanceof StoreError && error.readResult ? { readResult: error.readResult } : {}) });
}

async function readTarget(snapshot: BlueprintPathSnapshot): Promise<Buffer> {
  let handle;
  try { handle = await fs.open(snapshot.destination, "r"); }
  catch (error) {
    if ((error as NodeJS.ErrnoException).code === "ENOENT") throw new StoreError("CONFLICT", "The destination disappeared");
    throw error;
  }
  try {
    const before = await handle.stat({ bigint: true });
    if (!before.isFile() || before.nlink !== 1n || fileIdentity(before) !== snapshot.targetIdentity) {
      throw new StoreError("CONFLICT", "The destination identity changed");
    }
    const bytes = await handle.readFile();
    const after = await handle.stat({ bigint: true });
    const linked = await fs.lstat(snapshot.destination, { bigint: true });
    if (before.mtimeNs !== after.mtimeNs || before.ctimeNs !== after.ctimeNs || fileIdentity(linked) !== snapshot.targetIdentity || linked.nlink !== 1n) {
      throw new StoreError("CONFLICT", "The destination changed while reading");
    }
    return bytes;
  } finally { await handle.close(); }
}

const activeDirectories = new Set<string>();
function acquire(directory: string): string {
  const resolved = blueprintDirectory(directory);
  const key = process.platform === "win32" ? resolved.toLowerCase() : resolved;
  if (activeDirectories.has(key)) throw new StoreError("BUSY", "An operation is already active for this directory");
  activeDirectories.add(key);
  return key;
}

async function loadDocument(directory: string, registry: PluginRegistry): Promise<BlueprintDocumentLoadResult> {
  try {
    const snapshot = await inspectBlueprintDirectory(directory, "existing");
    const bytes = await readTarget(snapshot);
    const current = await inspectBlueprintDirectory(directory, "existing");
    if (!sameBlueprintLocation(snapshot, current) || snapshot.targetIdentity !== current.targetIdentity) {
      throw new StoreError("CONFLICT", "The project location changed while reading");
    }
    return Object.freeze({ status: "loaded", readResult: read(bytes, registry, "EXISTING_REJECTED"), sha256: digest(bytes), canGenerate: false });
  } catch (error) { return failure(error); }
}

export async function loadBlueprintDocument(directory: string, registry: PluginRegistry): Promise<BlueprintDocumentLoadResult> {
  let key: string | undefined;
  try { key = acquire(directory); return await loadDocument(directory, registry); }
  catch (error) { return failure(error); }
  finally { if (key !== undefined) activeDirectories.delete(key); }
}

export async function saveBlueprintDocument(request: BlueprintDocumentSaveRequest, registry: PluginRegistry): Promise<BlueprintDocumentSaveResult> {
  let temp: string | undefined;
  let committed = false;
  let key: string | undefined;
  let snapshot: BlueprintPathSnapshot | undefined;
  let tempIdentity: string | undefined;
  const cleanup = async () => {
    if (!temp || !snapshot || !tempIdentity) throw new StoreError("CLEANUP_FAILED", "Staging ownership cannot be verified");
    await inspectOwnedTemp(snapshot, temp, tempIdentity, committed ? 2n : 1n);
    await fs.unlink(temp);
    temp = undefined;
  };
  try {
    const { directory: requestedDirectory, sourceText, mode: saveMode } = request;
    const expectedSha256 = request.mode === "overwrite" ? request.expectedSha256 : undefined;
    const directory = blueprintDirectory(requestedDirectory);
    key = acquire(directory);
    const bytes = Buffer.from(sourceText, "utf8");
    if (decode(bytes) !== sourceText) throw new StoreError("ENCODING_INVALID", "Source text cannot be encoded losslessly");
    read(bytes, registry, "SOURCE_REJECTED");
    if (!["create", "save-as", "overwrite"].includes(saveMode) ||
      (saveMode === "overwrite" && !/^[a-f0-9]{64}$/i.test(expectedSha256 ?? ""))) {
      throw new StoreError("INVALID_REQUEST", "A supported mode and overwrite SHA-256 baseline are required");
    }
    const baseline = expectedSha256?.toLowerCase();
    const mode = saveMode === "overwrite" ? "existing" : "empty";
    snapshot = await inspectBlueprintDirectory(directory, mode);
    const { destination } = snapshot;
    if (saveMode === "overwrite") {
      const existing = await readTarget(snapshot);
      if (digest(existing) !== baseline) throw new StoreError("CONFLICT", "The destination content changed");
      read(existing, registry, "EXISTING_REJECTED");
    }
    const candidate = path.join(directory, `.blueprint-${randomUUID()}.tmp`);
    const handle = await fs.open(candidate, "wx", 0o600);
    temp = candidate;
    try {
      tempIdentity = fileIdentity(await handle.stat({ bigint: true }));
      await handle.writeFile(bytes); await handle.sync();
    }
    finally { await handle.close(); }
    if (!(await fs.readFile(temp)).equals(bytes)) throw new StoreError("VERIFY_FAILED", "Staged bytes do not match source text");
    const current = await inspectBlueprintDirectory(directory, mode, path.basename(temp));
    if (!sameBlueprintLocation(snapshot, current) || snapshot.targetIdentity !== current.targetIdentity) {
      throw new StoreError("CONFLICT", "The project or destination identity changed");
    }
    await inspectOwnedTemp(snapshot, temp, tempIdentity, 1n);
    // Reopen and verify after eligibility checks, so mutation after the first read cannot publish.
    const staged = await fs.open(temp, "r");
    try {
      const before = await staged.stat({ bigint: true });
      const stagedBytes = await staged.readFile();
      const after = await staged.stat({ bigint: true });
      if (fileIdentity(before) !== tempIdentity || before.nlink !== 1n ||
        before.mtimeNs !== after.mtimeNs || before.ctimeNs !== after.ctimeNs || !stagedBytes.equals(bytes)) {
        throw new StoreError("VERIFY_FAILED", "Staged bytes or identity changed before publication");
      }
    } finally { await staged.close(); }
    if (saveMode === "overwrite") {
      const existing = await readTarget(current);
      if (digest(existing) !== baseline) throw new StoreError("CONFLICT", "The destination content changed");
      read(existing, registry, "EXISTING_REJECTED");
      await fs.rename(temp, destination);
      temp = undefined;
    } else {
      try { await fs.link(temp, destination); }
      catch (error) {
        if ((error as NodeJS.ErrnoException).code === "EEXIST") throw new StoreError("CONFLICT", "The destination was created concurrently");
        throw error;
      }
    }
    committed = true;
    if (temp) {
      try { await cleanup(); }
      catch { throw new StoreError("CLEANUP_FAILED", "Published file exists but staging cleanup failed"); }
    }
    const loaded = await loadDocument(directory, registry);
    if (loaded.status !== "loaded" || loaded.sha256 !== digest(bytes)) throw new StoreError("VERIFY_FAILED", "Published bytes could not be verified");
    return Object.freeze({ status: "saved", writeState: "verified", readResult: loaded.readResult, sha256: loaded.sha256, canGenerate: false });
  } catch (error) {
    const result = failure(error, committed);
    if (temp) {
      try { await cleanup(); }
      catch { return Object.freeze({ ...result, cleanupFailed: true, retainedTempPath: temp }); }
    }
    return result;
  } finally { if (key !== undefined) activeDirectories.delete(key); }
}
