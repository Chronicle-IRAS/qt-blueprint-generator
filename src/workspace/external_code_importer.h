#pragma once

#include "blueprint/blueprint_document.h"

class ExternalCodeImporter final
{
public:
    // Explicit selection only. The workspace and source root must already exist.
    // Nested portable relative paths are preserved; no overwrite/delete operation.
    // The node's manually supplied contract is bound to the imported byte hashes.
    // Read-only is a generation policy, not a change to source filesystem permissions.
    // Like WorkspaceIo, assumes a trusted local filesystem with a single writer.
    static bool importFiles(const BlueprintNode &node, const QString &sourceRoot,
                            const QStringList &relativeFiles, const QString &workspace,
                            QString *error = nullptr);
    static bool verifyImport(const BlueprintNode &node, const QString &workspace,
                             QString *error = nullptr);

    ExternalCodeImporter() = delete;
};
