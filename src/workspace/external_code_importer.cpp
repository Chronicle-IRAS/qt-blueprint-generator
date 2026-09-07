#include "workspace/external_code_importer.h"
#include "blueprint/blueprint_serializer.h"
#include "generation/workspace_io.h"

#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSet>

namespace {
const QString manifestName = QStringLiteral("import-manifest.json");

QString nodeRoot(const BlueprintNode &node) { return "external/" + node.id; }

bool validContract(const BlueprintNode &node, QString *error)
{
    if (node.type != NodeType::ExternalCode || !WorkspaceIo::safeId(node.id)
        || node.name.trimmed().isEmpty() || node.description.trimmed().isEmpty())
        return WorkspaceIo::fail(error, "Import requires an ExternalCode node with a safe id and a manually supplied name and description");
    for (const auto &ports : {node.inputs, node.outputs}) {
        QSet<QString> names;
        for (const auto &port : ports) {
            if (port.name.trimmed().isEmpty() || port.type.trimmed().isEmpty() || names.contains(port.name))
                return WorkspaceIo::fail(error, "Interface ports require distinct names and explicit types");
            names.insert(port.name);
        }
    }
    return true;
}

QJsonObject contractFor(const BlueprintNode &node)
{
    BlueprintDocument document;
    document.nodes = {node};
    return QJsonDocument::fromJson(BlueprintSerializer::toJson(document))
        .object().value("nodes").toArray().first().toObject();
}

// Check the entire selection before any copying, including directory case aliases
// on case-sensitive filesystems and file/directory prefix collisions.
bool validSelection(const QStringList &paths, QString *error)
{
    if (paths.isEmpty()) return WorkspaceIo::fail(error, "Select at least one external source file");
    QSet<QString> files;
    QMap<QString, QString> spellings;
    for (const auto &path : paths) {
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (!WorkspaceIo::safeRelative(path)
            || (suffix != "h" && suffix != "hpp" && suffix != "cpp" && suffix != "cc"))
            return WorkspaceIo::fail(error, "External files must use portable relative paths and .h, .hpp, .cpp or .cc extensions");
        const QString key = path.toCaseFolded();
        for (const auto &other : files)
            if (key == other || key.startsWith(other + '/') || other.startsWith(key + '/'))
                return WorkspaceIo::fail(error, "External file paths collide");
        files.insert(key);
        QString prefix;
        for (const auto &segment : path.split('/')) {
            prefix += (prefix.isEmpty() ? QString() : QStringLiteral("/")) + segment;
            const QString folded = prefix.toCaseFolded();
            if (spellings.contains(folded) && spellings.value(folded) != prefix)
                return WorkspaceIo::fail(error, "External directory case aliases are forbidden");
            spellings.insert(folded, prefix);
        }
    }
    return true;
}

bool loadImport(const BlueprintNode &node, const QString &workspace,
                QJsonObject &manifest, QString *error)
{
    std::optional<QByteArray> bytes;
    if (!WorkspaceIo::read(workspace, nodeRoot(node) + '/' + manifestName, bytes, error)) return false;
    if (!bytes) return WorkspaceIo::fail(error, "External import manifest is missing; revalidation is required");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(*bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return WorkspaceIo::fail(error, "Malformed external import manifest");
    manifest = document.object();
    const auto contract = contractFor(node);
    if (manifest.size() != 5 || manifest.value("version") != 1
        || manifest.value("generationPolicy") != "readOnly"
        || !manifest.value("contract").isObject() || manifest.value("contract").toObject() != contract
        || manifest.value("contractSha256") != WorkspaceIo::sha256(WorkspaceIo::json(contract))
        || !manifest.value("files").isObject())
        return WorkspaceIo::fail(error, "External import contract or manifest changed; revalidation is required");
    const auto files = manifest.value("files").toObject();
    if (!validSelection(files.keys(), error)) return false;
    static const QRegularExpression hash("\\A[a-f0-9]{64}\\z");
    for (auto file = files.begin(); file != files.end(); ++file)
        if (!file.value().isString() || !hash.match(file.value().toString()).hasMatch())
            return WorkspaceIo::fail(error, "Malformed external file hash");
    return true;
}

bool verifyFiles(const BlueprintNode &node, const QString &workspace,
                 const QJsonObject &manifest, QString *error)
{
    const QString root = nodeRoot(node);
    const auto files = manifest.value("files").toObject();
    QSet<QString> expectedFiles{root + '/' + manifestName};
    QSet<QString> expectedDirectories{root};
    for (auto file = files.begin(); file != files.end(); ++file) {
        const QString relative = root + '/' + file.key();
        expectedFiles.insert(relative);
        QString parent = relative.section('/', 0, -2);
        while (parent != root) {
            expectedDirectories.insert(parent);
            parent = parent.section('/', 0, -2);
        }
        std::optional<QByteArray> bytes;
        if (!WorkspaceIo::read(workspace, relative, bytes, error)) return false;
        if (!bytes || WorkspaceIo::sha256(*bytes) != file.value().toString())
            return WorkspaceIo::fail(error, "External source changed or was deleted; revalidation is required: " + file.key());
    }
    // Inventory every entry, including hidden files and unlisted directory links.
    // Inspect before descending, so links and cycles are never followed.
    QStringList pending{root};
    while (!pending.isEmpty()) {
        const QString directory = pending.takeLast();
        if (!WorkspaceIo::checkPath(workspace, directory, error)) return false;
        const QDir dir(QDir(workspace).filePath(directory));
        if (!dir.exists() || !dir.isReadable()) return WorkspaceIo::fail(error, "Cannot inspect external directory");
        const auto entries = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
        for (const auto &entry : entries) {
            const QString relative = directory + '/' + entry.fileName();
            if (!WorkspaceIo::checkPath(workspace, relative, error)) return false;
            if (entry.isDir() && expectedDirectories.contains(relative)) pending.append(relative);
            else if (!entry.isFile() || !expectedFiles.contains(relative))
                return WorkspaceIo::fail(error, "Unexpected external import entry; revalidation is required: " + relative);
        }
    }
    return true;
}
}

bool ExternalCodeImporter::importFiles(const BlueprintNode &node, const QString &sourceRoot,
                                     const QStringList &relativeFiles, const QString &workspace, QString *error)
{
    if (error) error->clear();
    if (!validContract(node, error) || !validSelection(relativeFiles, error)) return false;
    const QString root = nodeRoot(node);
    const QString manifestPath = root + '/' + manifestName;
    std::optional<QByteArray> existing;
    if (!WorkspaceIo::read(workspace, manifestPath, existing, error)) return false;

    QMap<QString, QByteArray> writes;
    QJsonObject hashes;
    for (const auto &path : relativeFiles) {
        std::optional<QByteArray> bytes;
        if (!WorkspaceIo::read(sourceRoot, path, bytes, error)) return false;
        if (!bytes) return WorkspaceIo::fail(error, "Selected external file does not exist: " + path);
        hashes.insert(path, WorkspaceIo::sha256(*bytes));
        writes.insert(root + '/' + path, *bytes);
    }
    if (existing) {
        QJsonObject manifest;
        if (!loadImport(node, workspace, manifest, error) || !verifyFiles(node, workspace, manifest, error)) return false;
        if (manifest.value("files").toObject() != hashes)
            return WorkspaceIo::fail(error, "External import replacement is forbidden; revalidation is required");
        return true; // Exact verified import: no writes, including manifest timestamps.
    }
    const QDir directory(QDir(workspace).filePath(root));
    if (directory.exists() && !directory.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot).isEmpty())
        return WorkspaceIo::fail(error, "External destination contains untracked entries");
    const auto contract = contractFor(node);
    const QJsonObject manifest{{"version", 1}, {"generationPolicy", "readOnly"},
                               {"contract", contract}, {"contractSha256", WorkspaceIo::sha256(WorkspaceIo::json(contract))},
                               {"files", hashes}};
    writes.insert(manifestPath, WorkspaceIo::json(manifest));
    return WorkspaceIo::transaction(workspace, writes, error);
}

bool ExternalCodeImporter::verifyImport(const BlueprintNode &node, const QString &workspace, QString *error)
{
    if (error) error->clear();
    if (!validContract(node, error)) return false;
    QJsonObject manifest;
    return loadImport(node, workspace, manifest, error) && verifyFiles(node, workspace, manifest, error);
}
