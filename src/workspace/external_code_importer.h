#pragma once

#include "blueprint/blueprint_document.h"
#include <QJsonObject>

class ExternalCodeImporter final
{
public:
    enum class ImportState { NotImported, Verified, Invalid };
    // Neutral only for a safe, absent destination without surviving association
    // evidence. Existing destinations and associated metadata require verification.
    static ImportState inspectImport(const BlueprintNode &node, const QString &workspace,
                                     QJsonObject &verifiedManifest, QString *error = nullptr);
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
    // Metadata and bytes are returned only after the complete import verifies.
    // Output parameters are cleared on every failure.
    static bool importManifest(const BlueprintNode &node, const QString &workspace,
                               QJsonObject &manifest, QString *error = nullptr);
    static bool readImportedFile(const BlueprintNode &node, const QString &workspace,
                                 const QString &relativePath, QByteArray &bytes,
                                 QString *error = nullptr);
    // Explicitly authorized replacement of the same tracked path set only.
    // The previous manifest must prove ownership even when bytes need repair.
    static bool reimportFiles(const BlueprintNode &node, const QString &sourceRoot,
                              const QStringList &relativeFiles, const QString &workspace,
                              QString *error = nullptr);

    ExternalCodeImporter() = delete;
};
