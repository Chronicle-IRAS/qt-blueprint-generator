import fs from "node:fs/promises";
import type { BigIntStats } from "node:fs";
import path from "node:path";
import childProcess from "node:child_process";

export class BlueprintPathError extends Error {
  constructor(readonly code: "PATH_UNSAFE" | "DIRECTORY_UNSUITABLE" | "CONFLICT", message: string) { super(message); }
}
export const fileIdentity = (stat: BigIntStats): string => `${stat.dev}:${stat.ino}:${stat.birthtimeNs}`;
export interface BlueprintPathSnapshot {
  readonly directory: string;
  readonly destination: string;
  readonly ancestors: readonly string[];
  readonly targetIdentity?: string;
}

export function blueprintDirectory(directory: string): string {
  if (typeof directory !== "string" || !path.isAbsolute(directory) || /^[\\/]{2}/.test(directory)) {
    throw new BlueprintPathError("PATH_UNSAFE", "An absolute local project directory is required");
  }
  const root = path.parse(directory).root;
  const parts = directory.slice(root.length).split(/[\\/]/);
  if (!parts.some(Boolean) || parts.some(part => part === "." || part === ".." ||
    /[\x00-\x1f<>:"|?*]/.test(part) || /[. ]$/.test(part) ||
    /^(con|prn|aux|nul|com[1-9¹²³]|lpt[1-9¹²³])(?:\.|$)/i.test(part))) {
    throw new BlueprintPathError("PATH_UNSAFE", "The directory contains an unsafe or ambiguous path component");
  }
  // Reject drive-relative/root-relative Windows paths even when interpreted on another host.
  if (process.platform === "win32" && !/^[a-z]:[\\/]/i.test(directory)) {
    throw new BlueprintPathError("PATH_UNSAFE", "A local drive absolute directory is required");
  }
  return path.resolve(directory);
}

// No path is ever interpolated into executable text. JSON travels over stdin as UTF-8 data.
const attributeCommand = "$ErrorActionPreference='Stop'; [Console]::InputEncoding=[Text.UTF8Encoding]::new($false); " +
  "try { $paths=ConvertFrom-Json ([Console]::In.ReadToEnd()); $values=@(foreach($p in $paths) { " +
  "[int][IO.File]::GetAttributes([string]$p) }); ConvertTo-Json -Compress -InputObject $values } catch { exit 1 }";
async function rejectNativeReparsePoints(paths: readonly string[]): Promise<void> {
  if (process.platform !== "win32") return;
  let output: string;
  try {
    output = await new Promise<string>((resolve, reject) => {
      const child = childProcess.execFile(path.join(process.env.SystemRoot ?? "C:\\Windows", "System32", "WindowsPowerShell", "v1.0", "powershell.exe"),
        ["-NoLogo", "-NoProfile", "-NonInteractive", "-Command", attributeCommand],
        { windowsHide: true, timeout: 10000, maxBuffer: 1024 * 1024, encoding: "utf8" },
        (error, stdout) => error ? reject(error) : resolve(stdout));
      child.stdin?.on("error", reject);
      child.stdin?.end(JSON.stringify(paths), "utf8");
    });
    const attributes: unknown = JSON.parse(output);
    if (!Array.isArray(attributes) || attributes.length !== paths.length ||
      attributes.some(value => !Number.isInteger(value) || value < 0 || (value & 0x400) !== 0)) throw new Error();
  } catch { throw new BlueprintPathError("PATH_UNSAFE", "Windows reparse-point checks failed or found an unsafe path"); }
}

async function maybeStat(filename: string): Promise<BigIntStats | undefined> {
  try { return await fs.lstat(filename, { bigint: true }); }
  catch (error) { if ((error as NodeJS.ErrnoException).code === "ENOENT") return undefined; throw error; }
}

/** All checks are repeated per phase. Node path APIs cannot bind paths race-free against hostile writers. */
export async function inspectBlueprintDirectory(directory: string, mode: "empty" | "existing", ownedTemp?: string): Promise<BlueprintPathSnapshot> {
  directory = blueprintDirectory(directory);
  const ancestorPaths: string[] = [];
  for (let current = directory; ; current = path.dirname(current)) {
    ancestorPaths.unshift(current);
    if (path.dirname(current) === current) break;
  }
  const ancestors: string[] = [];
  for (const current of ancestorPaths) {
    const stat = await fs.lstat(current, { bigint: true });
    if (stat.isSymbolicLink() || !stat.isDirectory()) throw new BlueprintPathError("PATH_UNSAFE", "Project ancestors must be ordinary directories");
    ancestors.push(fileIdentity(stat));
  }
  const destination = path.join(directory, "blueprint.json");
  const target = await maybeStat(destination);
  if (target && (!target.isFile() || target.isSymbolicLink() || target.nlink !== 1n)) {
    throw new BlueprintPathError("PATH_UNSAFE", "Blueprint files must be regular files with a single link");
  }
  await rejectNativeReparsePoints([...ancestorPaths, ...(target ? [destination] : [])]);
  for (const current of ancestorPaths) {
    const entries = await fs.readdir(current);
    const lower = entries.map(name => name.toLowerCase());
    if (lower.includes("generation-manifest.json") ||
      (await maybeStat(path.join(current, "src", "contracts", "source-blueprint.json")) &&
        await maybeStat(path.join(current, "src", "contracts", "blueprint.json"))) ||
      (current !== directory && lower.includes("blueprint.json"))) {
      throw new BlueprintPathError("DIRECTORY_UNSUITABLE", "Generated or nested blueprint project directories are protected");
    }
    if (current === directory) {
      if (entries.some(name => name !== "blueprint.json" && name !== ownedTemp)) {
        throw new BlueprintPathError("DIRECTORY_UNSUITABLE", "The project directory contains unexpected entries");
      }
      if ((mode === "empty" && target) || (mode === "existing" && !target)) {
        throw new BlueprintPathError("CONFLICT", "The destination no longer matches the requested save mode");
      }
    }
  }
  return { directory, destination, ancestors, ...(target ? { targetIdentity: fileIdentity(target) } : {}) };
}

export function sameBlueprintLocation(a: BlueprintPathSnapshot, b: BlueprintPathSnapshot): boolean {
  return a.directory === b.directory && a.ancestors.length === b.ancestors.length && a.ancestors.every((id, i) => id === b.ancestors[i]);
}

/** Cleanup deliberately ignores directory contents, but never ignores ownership or ancestor safety. */
export async function inspectOwnedTemp(snapshot: BlueprintPathSnapshot, temp: string, identity: string, links: bigint): Promise<void> {
  const paths: string[] = [];
  for (let current = snapshot.directory; ; current = path.dirname(current)) {
    paths.unshift(current);
    if (path.dirname(current) === current) break;
  }
  for (const [index, current] of paths.entries()) {
    const stat = await fs.lstat(current, { bigint: true });
    if (!stat.isDirectory() || stat.isSymbolicLink() || fileIdentity(stat) !== snapshot.ancestors[index]) {
      throw new BlueprintPathError("CONFLICT", "The staging directory identity changed");
    }
  }
  const stat = await fs.lstat(temp, { bigint: true });
  if (!stat.isFile() || stat.isSymbolicLink() || fileIdentity(stat) !== identity || stat.nlink !== links) {
    throw new BlueprintPathError("CONFLICT", "The staging file is no longer exclusively owned");
  }
  await rejectNativeReparsePoints([...paths, temp]);
}
