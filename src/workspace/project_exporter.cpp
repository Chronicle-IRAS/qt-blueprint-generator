#include "workspace/project_exporter.h"
#include "workspace/project_exporter_recovery_p.h"

#include "blueprint/blueprint_serializer.h"
#include "blueprint/blueprint_validator.h"
#include "generation/workspace_io.h"
#include "workspace/external_code_importer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonObject>
#include <QMap>
#include <QSet>
#include <QUuid>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
struct ExportFile
{
    QString source;
    QString destination;
    QByteArray hash;
};

struct ExportPlan
{
    QVector<ExportFile> files;
    QSet<QString> directories;
    QMap<QString, QString> spellings;
    QSet<QString> fileKeys;
};

bool fail(QString *error, const QString &message)
{
    if (error)
        *error = message;
    return false;
}

QString folded(QString value)
{
    return value.toCaseFolded();
}

bool inspectAbsolutePath(const QString &path, bool requireDirectory, QString *error)
{
    if (!QDir::isAbsolutePath(path))
        return fail(error, QStringLiteral("Workspace and export target paths must be absolute"));
    const QString requested = QDir::cleanPath(path);
    QString cursor = requested;
    while (true) {
        const QFileInfo info(cursor);
        bool link = info.isSymbolicLink();
#ifdef Q_OS_WIN
        const QString native = QDir::toNativeSeparators(cursor);
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        link = link || (attributes != INVALID_FILE_ATTRIBUTES
                        && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (link)
            return fail(error, QStringLiteral("Links and reparse points are forbidden: ") + cursor);
        if (info.exists()) {
            if (!info.isDir())
                return fail(error, cursor == requested
                    ? QStringLiteral("Workspace and export target must be directories")
                    : QStringLiteral("A path ancestor is not a directory: ") + cursor);
            const QDir parent(info.absolutePath());
            for (const QString &entry : parent.entryList(QDir::AllEntries | QDir::Hidden
                                                          | QDir::System | QDir::NoDotAndDotDot)) {
                if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0
                    && entry != info.fileName())
                    return fail(error, QStringLiteral("Path case alias is forbidden: ") + cursor);
            }
        }
        const QString parent = info.absolutePath();
        if (parent == cursor)
            break;
        cursor = parent;
    }
    if (requireDirectory && !QFileInfo(requested).isDir())
        return fail(error, QStringLiteral("Workspace and export target must be existing directories"));
    return true;
}

bool overlap(const QString &left, const QString &right)
{
    QString a = QDir::cleanPath(left);
    QString b = QDir::cleanPath(right);
#ifdef Q_OS_WIN
    a = a.toCaseFolded();
    b = b.toCaseFolded();
#endif
    return a == b || a.startsWith(b + QLatin1Char('/')) || b.startsWith(a + QLatin1Char('/'));
}

bool emptyDirectory(const QString &path)
{
    return QDir(path).entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                | QDir::NoDotAndDotDot).isEmpty();
}

bool addDirectory(ExportPlan &plan, const QString &destination, QString *error)
{
    if (!WorkspaceIo::safeRelative(destination))
        return fail(error, QStringLiteral("Export contains a non-portable path: ") + destination);
    QString prefix;
    for (const QString &segment : destination.split('/')) {
        prefix += prefix.isEmpty() ? segment : QStringLiteral("/") + segment;
        const QString key = folded(prefix);
        if (plan.spellings.contains(key) && plan.spellings.value(key) != prefix)
            return fail(error, QStringLiteral("Export path case alias is forbidden: ") + destination);
        if (plan.fileKeys.contains(key))
            return fail(error, QStringLiteral("Export file and directory paths collide: ") + destination);
        plan.spellings.insert(key, prefix);
        plan.directories.insert(prefix);
    }
    return true;
}

bool addFile(ExportPlan &plan, const QString &source, const QString &destination, QString *error)
{
    const QString parent = destination.section('/', 0, -2);
    if (destination.contains('/') && !addDirectory(plan, parent, error))
        return false;
    if (!WorkspaceIo::safeRelative(destination))
        return fail(error, QStringLiteral("Export contains a non-portable path: ") + destination);
    const QString key = folded(destination);
    if (plan.spellings.contains(key) || plan.fileKeys.contains(key))
        return fail(error, QStringLiteral("Export paths collide: ") + destination);
    for (const QString &other : plan.fileKeys) {
        if (other.startsWith(key + QLatin1Char('/')) || key.startsWith(other + QLatin1Char('/')))
            return fail(error, QStringLiteral("Export file paths collide: ") + destination);
    }
    QFile file(source);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot read export source: ") + source);
    const QByteArray bytes = file.readAll();
    if (file.error() != QFileDevice::NoError)
        return fail(error, QStringLiteral("Cannot completely read export source: ") + source);
    plan.spellings.insert(key, destination);
    plan.fileKeys.insert(key);
    plan.files.append({source, destination,
                       QCryptographicHash::hash(bytes, QCryptographicHash::Sha256)});
    return true;
}

bool scanTree(const QString &workspace, const QString &sourceRelative, const QString &destination,
              ExportPlan &plan, QString *error)
{
    if (!WorkspaceIo::checkPath(workspace, sourceRelative, error))
        return false;
    const QFileInfo root(QDir(workspace).filePath(sourceRelative));
    if (!root.isDir())
        return fail(error, QStringLiteral("Required export source directory is missing: ") + sourceRelative);
    if (!destination.isEmpty() && !addDirectory(plan, destination, error))
        return false;
    QStringList pending{sourceRelative};
    while (!pending.isEmpty()) {
        const QString directory = pending.takeLast();
        const QDir dir(QDir(workspace).filePath(directory));
        if (!dir.exists() || !dir.isReadable())
            return fail(error, QStringLiteral("Cannot inspect export source directory: ") + directory);
        const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                               | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &entry : entries) {
            const QString relative = directory + QLatin1Char('/') + entry.fileName();
            if (!WorkspaceIo::checkPath(workspace, relative, error))
                return false;
            const QString suffix = relative.mid(sourceRelative.size() + 1);
            const QString mapped = destination.isEmpty() ? suffix : destination + QLatin1Char('/') + suffix;
            if (entry.isDir()) {
                if (!addDirectory(plan, mapped, error))
                    return false;
                pending.append(relative);
            } else if (entry.isFile()) {
                if (!addFile(plan, entry.absoluteFilePath(), mapped, error))
                    return false;
            } else {
                return fail(error, QStringLiteral("Export source contains an unsupported entry: ") + relative);
            }
        }
    }
    return true;
}

bool verifyExternalSources(const QString &workspace, const BlueprintDocument &document,
                           QStringList &nodeIds, QString *error)
{
    QMap<QString, QString> ids;
    for (const BlueprintNode &node : document.nodes) {
        if (node.type != NodeType::ExternalCode)
            continue;
        const QString key = folded(node.id);
        if (ids.contains(key))
            return fail(error, QStringLiteral("External node paths collide"));
        if (!ExternalCodeImporter::verifyImport(node, workspace, error))
            return false;
        ids.insert(key, node.id);
        nodeIds.append(node.id);
    }
    const QFileInfo externalInfo(QDir(workspace).filePath(QStringLiteral("external")));
    if (!externalInfo.exists())
        return nodeIds.isEmpty() || fail(error, QStringLiteral("External source root is missing"));
    if (!WorkspaceIo::checkPath(workspace, QStringLiteral("external"), error) || !externalInfo.isDir())
        return false;
    const QDir external(externalInfo.absoluteFilePath());
    const auto entries = external.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System
                                                | QDir::NoDotAndDotDot);
    if (entries.size() != nodeIds.size())
        return fail(error, QStringLiteral("External source root contains untracked entries"));
    for (const QFileInfo &entry : entries) {
        if (!entry.isDir() || !ids.contains(folded(entry.fileName()))
            || ids.value(folded(entry.fileName())) != entry.fileName())
            return fail(error, QStringLiteral("External source root contains an invalid or aliased node path"));
    }
    return true;
}

bool verifyManifestFilesExist(const QString &workspace, const QJsonObject &manifest, QString *error)
{
    QStringList paths = manifest.value(QStringLiteral("protectedFiles")).toObject().keys();
    paths.append(manifest.value(QStringLiteral("files")).toObject().keys());
    for (const QString &path : paths) {
        std::optional<QByteArray> bytes;
        if (!WorkspaceIo::read(workspace, QStringLiteral("generated-project/") + path, bytes, error))
            return false;
        if (!bytes)
            return fail(error, QStringLiteral("Manifest project file is missing: ") + path);
    }
    return true;
}

bool removeTree(const QString &path)
{
    return !QFileInfo::exists(path) || QDir(path).removeRecursively();
}
}

ProjectExporterRecovery::Decision ProjectExporterRecovery::decideBackupRemovalFailure(
    bool moved, bool restored, const QString &targetPath,
    const QString &backupPath, const QString &rollbackPath)
{
    if (restored)
        return {true, QStringLiteral("Could not remove export backup; target restored")};
    const QString completedExportPath = moved ? rollbackPath : targetPath;
    return {false,
            QStringLiteral("Could not remove export backup and target restoration failed; "
                           "original empty target retained at %1; completed export retained at %2")
                .arg(backupPath, completedExportPath)};
}

bool ProjectExporter::exportProject(const QString &workspacePath, const QString &targetPath,
                                    QString *error)
{
    if (error)
        error->clear();
    const QString workspace = QDir::cleanPath(workspacePath);
    const QString target = QDir::cleanPath(targetPath);
    if (!inspectAbsolutePath(workspace, true, error) || !inspectAbsolutePath(target, true, error))
        return false;
    if (overlap(workspace, target))
        return fail(error, QStringLiteral("Workspace and export target must not overlap"));
    if (!emptyDirectory(target))
        return fail(error, QStringLiteral("Export target must be empty"));

    QJsonObject manifest;
    if (!WorkspaceIo::loadManifest(workspace, manifest, error))
        return false;
    std::optional<QByteArray> blueprintBytes;
    if (!WorkspaceIo::read(workspace, QStringLiteral("generated-project/src/contracts/source-blueprint.json"),
                           blueprintBytes, error))
        return false;
    if (!blueprintBytes)
        return fail(error, QStringLiteral("Source blueprint is missing"));
    const QString blueprintRelative = QStringLiteral("src/contracts/source-blueprint.json");
    const QString sourceBlueprintHash = WorkspaceIo::sha256(*blueprintBytes);
    if (manifest.value(QStringLiteral("blueprintSha256")).toString() != sourceBlueprintHash
        || manifest.value(QStringLiteral("protectedFiles")).toObject().value(blueprintRelative).toString()
               != sourceBlueprintHash)
        return fail(error, QStringLiteral("Source blueprint does not match the generation manifest"));
    if (!verifyManifestFilesExist(workspace, manifest, error))
        return false;
    const auto document = BlueprintSerializer::fromJson(*blueprintBytes, error);
    if (!document)
        return false;
    const auto diagnostics = BlueprintValidator::validate(*document, {workspace});
    if (!diagnostics.isEmpty())
        return fail(error, QStringLiteral("Source blueprint validation failed: ")
                               + diagnostics.first().code + QStringLiteral(": ")
                               + diagnostics.first().message);

    QStringList externalNodeIds;
    if (!verifyExternalSources(workspace, *document, externalNodeIds, error))
        return false;
    if (!externalNodeIds.isEmpty()) {
        const QString generatedExternal = QStringLiteral("generated-project/src/external");
        if (!WorkspaceIo::checkPath(workspace, generatedExternal, error))
            return false;
        if (QFileInfo::exists(QDir(workspace).filePath(generatedExternal)))
            return fail(error, QStringLiteral("Generated project src/external collides with verified external imports"));
    }

    ExportPlan plan;
    if (!addFile(plan, QDir(workspace).filePath(QStringLiteral("generation-manifest.json")),
                 QStringLiteral("generation-manifest.json"), error)
        || !addFile(plan, QDir(workspace).filePath(QStringLiteral("generated-project/src/contracts/source-blueprint.json")),
                    QStringLiteral("blueprint.json"), error)
        || !scanTree(workspace, QStringLiteral("generated-project"), {}, plan, error))
        return false;
    for (const QString &nodeId : externalNodeIds) {
        if (!scanTree(workspace, QStringLiteral("external/") + nodeId,
                      QStringLiteral("src/external/") + nodeId, plan, error))
            return false;
    }

    const QFileInfo targetInfo(target);
    QDir parent(targetInfo.absolutePath());
    const QString stageName = QStringLiteral(".%1.task9-export-stage-%2")
                                  .arg(targetInfo.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString backupName = QStringLiteral(".%1.task9-export-backup-%2")
                                   .arg(targetInfo.fileName(), QUuid::createUuid().toString(QUuid::WithoutBraces));
    const QString stagePath = parent.filePath(stageName);
    if (!parent.mkdir(stageName))
        return fail(error, QStringLiteral("Cannot create export staging directory"));

    const auto abandonStage = [&](const QString &message) {
        if (error && error->isEmpty())
            *error = message;
        if (!removeTree(stagePath) && error)
            *error += QStringLiteral("; staging cleanup failed: ") + stagePath;
        return false;
    };
    for (const QString &directory : plan.directories) {
        if (!QDir().mkpath(QDir(stagePath).filePath(directory)))
            return abandonStage(QStringLiteral("Cannot create staged export directory: ") + directory);
    }
    for (const ExportFile &copy : plan.files) {
        QByteArray bytes;
        {
            QFile source(copy.source);
            if (!source.open(QIODevice::ReadOnly))
                return abandonStage(QStringLiteral("Cannot reopen export source: ") + copy.source);
            bytes = source.readAll();
            if (source.error() != QFileDevice::NoError
                || QCryptographicHash::hash(bytes, QCryptographicHash::Sha256) != copy.hash)
                return abandonStage(QStringLiteral("Export source changed during staging: ") + copy.source);
        }
        const QString destinationPath = QDir(stagePath).filePath(copy.destination);
        bool wrote = false;
        {
            QFile destination(destinationPath);
            wrote = destination.open(QIODevice::WriteOnly | QIODevice::NewOnly)
                && destination.write(bytes) == bytes.size() && destination.flush();
        }
        if (!wrote)
            return abandonStage(QStringLiteral("Cannot write staged export file: ") + copy.destination);
        bool verified = false;
        {
            QFile verify(destinationPath);
            verified = verify.open(QIODevice::ReadOnly)
                && QCryptographicHash::hash(verify.readAll(), QCryptographicHash::Sha256) == copy.hash
                && verify.error() == QFileDevice::NoError;
        }
        if (!verified)
            return abandonStage(QStringLiteral("Staged export verification failed: ") + copy.destination);
    }

    if (!inspectAbsolutePath(target, true, error) || !emptyDirectory(target))
        return abandonStage(QStringLiteral("Export target changed before commit"));
    if (!parent.rename(targetInfo.fileName(), backupName))
        return abandonStage(QStringLiteral("Cannot prepare export target for commit"));
    if (!parent.rename(stageName, targetInfo.fileName())) {
        const bool restored = parent.rename(backupName, targetInfo.fileName());
        removeTree(stagePath);
        return fail(error, restored ? QStringLiteral("Could not commit export; target restored")
                                    : QStringLiteral("Could not commit export and target restoration failed"));
    }
    if (!parent.rmdir(backupName)) {
        const QString rollbackName = stageName + QStringLiteral("-rollback");
        const QString rollbackPath = parent.filePath(rollbackName);
        const QString backupPath = parent.filePath(backupName);
        const bool moved = parent.rename(targetInfo.fileName(), rollbackName);
        const bool restored = moved && parent.rename(backupName, targetInfo.fileName());
        const auto decision = ProjectExporterRecovery::decideBackupRemovalFailure(
            moved, restored, target, backupPath, rollbackPath);
        if (decision.removeRollback)
            removeTree(rollbackPath);
        return fail(error, decision.error);
    }
    return true;
}
