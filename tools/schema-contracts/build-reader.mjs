import { cpSync, mkdirSync, writeFileSync } from "node:fs";
import { fileURLToPath } from "node:url";
import { resolve } from "node:path";
import { spawnSync } from "node:child_process";
const directory = fileURLToPath(new URL(".", import.meta.url));
const output = resolve(process.argv[2] ?? fileURLToPath(new URL("../../build/v2-reader", import.meta.url)));
const compiled = spawnSync(process.execPath, [resolve(directory, "node_modules/typescript/bin/tsc"), "-p", resolve(directory, "tsconfig.reader.json"), "--outDir", resolve(output, "tools/schema-contracts")], { stdio: "inherit" });
if (compiled.status !== 0) process.exit(compiled.status ?? 1);
mkdirSync(output, { recursive: true });
writeFileSync(resolve(output, "package.json"), '{"type":"module"}\n');
cpSync(resolve(directory, "../../schemas"), resolve(output, "schemas"), { recursive: true });
for (const dependency of ["ajv", "fast-deep-equal", "fast-uri", "json-schema-traverse", "require-from-string"])
  cpSync(resolve(directory, "node_modules", dependency), resolve(output, "node_modules", dependency), { recursive: true });
