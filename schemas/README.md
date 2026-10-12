# Blueprint v2 structural contract (P0.1 Draft)

`blueprint-v2.schema.json` is the sole language-neutral structural definition. Its dialect is JSON Schema Draft 2020-12 and every `$ref` in the file resolves to local `$defs`. The current C++/Qt application still reads Blueprint Schema 1 only. This contract is not an application reader, a frozen plugin protocol, or a generation manifest schema.

From the repository root, using Node 24.18.1 and npm 11.16.0:

```powershell
npm ci --prefix tools/schema-contracts --ignore-scripts --no-audit --no-fund --registry=https://registry.npmjs.org
npm test --prefix tools/schema-contracts
npm run typecheck --prefix tools/schema-contracts
```

The isolated private tool package pins Ajv 8.20.0, Vitest 5.0.4, TypeScript 5.9.3 and `@types/node` 24.19.2 in its lockfile. It is not a root npm workspace or part of CMake. Tests invoke the installed local Vitest; no `npx` download is used. Dependency setup can require registry access. With the lockfile's packages already in the npm cache, replace the setup command with `npm ci --prefix tools/schema-contracts --offline --ignore-scripts --no-audit --no-fund`; a cache miss must fail rather than fetch. Tests and type checking then run locally without remote schema retrieval or AI requests.

Ajv2020 compiles synchronously with `strict` and `allErrors`; coercion, removal of extra fields and defaults are disabled. `loadSchema` is not configured. A remote-looking `$ref` inside opaque data is data, not a schema fetch. TypeScript supplies testing utilities, not a parallel Blueprint model or a semantic checker.

The schema checks required fields, known field types, direction/kind/mode enums, namespaced type/target IDs, lowercase SHA-256 syntax, endpoint shape and duplicate IDs within each Bridge side's binding-ID array. Arrays may be empty. Every record accepts unknown extra fields; `properties`, `settings`, `configuration`, `interfaceContract` and `wireSchema.schema` accept any JSON value, including null and non-objects. `extensions`, when present, is an object. Passing structure does not mean a graph is executable or that `wireSchema.schema` is a valid plugin schema.

Future semantic checks must enforce record ID uniqueness, reference existence, Unit membership, same-Unit ordinary edges even for equal target IDs, direction/kind/type/cardinality compatibility, registered types and targetability, lock/version compatibility and actual package/file hashes. Locked Bridge policy must check wire mapping and each endpoint's required driver bindings, Unit and interface; an optional field cannot permit a missing required driver. Dynamic ID persistence/migration, unknown opaque byte preservation on save, IR normalization/hashes and generation context invalidation require future implementations. Validation only verifies that the parsed test input was not mutated; it does not implement raw-byte round trips, source-root access or portable driver export.

`DriverBinding.files` is a Draft array of `{path, sha256}` records, unlike the existing importer's file map. `Reference` requires no invented mandatory locator field: source location, controlled roots and export policy remain undecided. The strict six-field legacy `generation-manifest.json` version 1/2 remains separate and unchanged.

See [fixture meanings](../tests/fixtures/blueprint-v2/README.md) and [the design Draft](../doc/architecture/blueprint-schema-v2.md).
