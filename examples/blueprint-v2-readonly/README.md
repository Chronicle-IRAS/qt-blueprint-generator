# Blueprint v2 read-only example

Open this directory with **File → Open Blueprint v2 (read-only)**. With no bundle selected all three nodes remain visible as UnknownNode. Explicitly select `trusted-declarations.json` to see KnownNode recognition from the existing Registry and Reader.

The blueprint and declaration metadata come from the repository's plugin-descriptors fixture. The trusted pins are independently specified demonstration declarations. They are not inferred from the document's locks and are not verified package hashes. No plugin package is installed or executed.

The graph contains three nodes across two units, one ordinary edge and one declared channel. A channel is a metadata relationship; this view does not establish communication. Properties, port snapshots and diagnostics are selectable/copyable. Saving, editing, generation, candidate acceptance, Build and Export are unavailable in this view. **Return to Schema 1 project** restores the retained legacy project.
