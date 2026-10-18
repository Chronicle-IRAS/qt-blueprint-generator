# P0.3 offline descriptor fixtures

`descriptors.json` is a closed pair of generic-domain Plugin Descriptors. `blueprint.json` uses their static nodes in two Build Units, an ordinary edge, a Bridge with an independent adapter version, and a target-side driver binding. These are declarations only; no plugin is loaded and no project is generated.

Tests provide independent literal trusted locks, rather than deriving pins from these JSON documents. Placeholder hashes are not artifact verification. The pipeline provider depends on math; the math node advertises pipeline Target compatibility without a reverse dependency.

Negative cases use explicit table-driven mutations in [plugin-descriptors.test.ts](../../../tools/schema-contracts/plugin-descriptors.test.ts) and hand-written diagnostic expectations. They cover structure, trust, versions, dependencies/cycles, ownership, ports/types/targets/Bridge, property schemas, cardinality and Blueprint lock closure. Original P0.1/P0.2 fixture JSON files are unchanged.

See [the Draft protocol](../../../doc/architecture/plugin-descriptor-contracts.md) and [tool usage](../../../tools/schema-contracts/README.md). Run `npm test --prefix tools/schema-contracts` from repository root; dependency and typecheck commands are unchanged.
