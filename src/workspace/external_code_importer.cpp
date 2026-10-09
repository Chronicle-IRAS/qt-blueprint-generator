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
            || (suffix != "h" && suffix != "hpp" && suffix != "cpp" && suffix != "cc" && suffix != "c" && suffix != "cxx"))
            return WorkspaceIo::fail(error, "External files must use portable relative paths and .h, .hpp, .c, .cpp, .cc or .cxx extensions");
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
                QJsonObject &manifest, QString *error, bool matchCurrentContract = true)
{
    std::optional<QByteArray> bytes;
    if (!WorkspaceIo::read(workspace, nodeRoot(node) + '/' + manifestName, bytes, error)) return false;
    if (!bytes) return WorkspaceIo::fail(error, "External import manifest is missing; revalidation is required");
    QJsonParseError parse;
    const auto document = QJsonDocument::fromJson(*bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject())
        return WorkspaceIo::fail(error, "Malformed external import manifest");
    manifest = document.object();
    const auto contract = manifest.value("contract").toObject();
    if (manifest.size() != 5 || manifest.value("version") != 1
        || manifest.value("generationPolicy") != "readOnly"
        || !manifest.value("contract").isObject()
        || manifest.value("contractSha256") != WorkspaceIo::sha256(WorkspaceIo::json(contract))
        || !manifest.value("files").isObject())
        return WorkspaceIo::fail(error, "External import contract or manifest changed; revalidation is required");
    // Parse with the existing blueprint schema, then round-trip to reject unknown
    // fields as well as malformed contracts before trusting stored ownership.
    BlueprintDocument envelope;
    auto serialized = QJsonDocument::fromJson(BlueprintSerializer::toJson(envelope)).object();
    serialized["nodes"] = QJsonArray{contract};
    const auto stored = BlueprintSerializer::fromJson(WorkspaceIo::json(serialized), error);
    if (!stored || stored->nodes.size() != 1 || !validContract(stored->nodes.first(), error)) return false;
    if (stored->nodes.first().id != node.id || contractFor(stored->nodes.first()) != contract
        || (matchCurrentContract && contract != contractFor(node)))
        return WorkspaceIo::fail(error, "External import contract ownership changed; revalidation is required");
    const auto files = manifest.value("files").toObject();
    if (!validSelection(files.keys(), error)) return false;
    static const QRegularExpression hash("\\A[a-f0-9]{64}\\z");
    for (auto file = files.begin(); file != files.end(); ++file)
        if (!file.value().isString() || !hash.match(file.value().toString()).hasMatch())
            return WorkspaceIo::fail(error, "Malformed external file hash");
    return true;
}

bool verifyFiles(const BlueprintNode &node, const QString &workspace,
                 const QJsonObject &manifest, QString *error, bool verifyBytes = true)
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
        if (!WorkspaceIo::checkPath(workspace, relative, error)) return false;
        if (verifyBytes) {
            std::optional<QByteArray> bytes;
            if (!WorkspaceIo::read(workspace, relative, bytes, error)) return false;
            if (!bytes || WorkspaceIo::sha256(*bytes) != file.value().toString())
                return WorkspaceIo::fail(error, "External source changed or was deleted; revalidation is required: " + file.key());
        }
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

bool selectedWrites(const BlueprintNode &node, const QString &sourceRoot,
                    const QStringList &paths, QMap<QString, QByteArray> &writes,
                    QJsonObject &hashes, QString *error)
{
    for (const auto &path : paths) {
        std::optional<QByteArray> bytes;
        if (!WorkspaceIo::read(sourceRoot, path, bytes, error)) return false;
        if (!bytes) return WorkspaceIo::fail(error, "Selected external file does not exist: " + path);
        hashes.insert(path, WorkspaceIo::sha256(*bytes));
        writes.insert(nodeRoot(node) + '/' + path, *bytes);
    }
    return true;
}

QJsonObject manifestFor(const BlueprintNode &node, const QJsonObject &hashes)
{
    const auto contract = contractFor(node);
    return {{"version", 1}, {"generationPolicy", "readOnly"},
            {"contract", contract}, {"contractSha256", WorkspaceIo::sha256(WorkspaceIo::json(contract))},
            {"files", hashes}};
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
    if (!selectedWrites(node, sourceRoot, relativeFiles, writes, hashes, error)) return false;
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
    writes.insert(manifestPath, WorkspaceIo::json(manifestFor(node, hashes)));
    return WorkspaceIo::transaction(workspace, writes, error);
}

bool ExternalCodeImporter::verifyImport(const BlueprintNode &node, const QString &workspace, QString *error)
{
    QJsonObject manifest;
    return importManifest(node, workspace, manifest, error);
}

ExternalCodeImporter::ImportState ExternalCodeImporter::inspectImport(
    const BlueprintNode &node, const QString &workspace, QJsonObject &verifiedManifest, QString *error)
{
    verifiedManifest = {};
    if (error) error->clear();
    if (node.type != NodeType::ExternalCode || !WorkspaceIo::safeId(node.id)) {
        WorkspaceIo::fail(error, "Import inspection requires an ExternalCode node with a safe id");
        return ImportState::Invalid;
    }
    const QString root = nodeRoot(node);
    if (!WorkspaceIo::checkPath(workspace, root, error)) return ImportState::Invalid;
    if (QFileInfo::exists(QDir(workspace).filePath(root)))
        return importManifest(node, workspace, verifiedManifest, error) ? ImportState::Verified : ImportState::Invalid;

    // A surviving scaffold can prove association, never a valid import. Only
    // consult it when the entire destination is absent; normal verification stays strict.
    std::optional<QByteArray> generationBytes, sourceBytes;
    const QString sourcePath = QStringLiteral("src/contracts/source-blueprint.json");
    if (!WorkspaceIo::read(workspace, "generation-manifest.json", generationBytes, error)
        || !WorkspaceIo::read(workspace, "generated-project/" + sourcePath, sourceBytes, error))
        return ImportState::Invalid;
    if (generationBytes) {
        QJsonObject generation;
        if (!WorkspaceIo::loadManifest(workspace, generation, error)) return ImportState::Invalid;
        const auto expected = generation.value("protectedFiles").toObject().value(sourcePath).toString();
        if (!sourceBytes || expected.isEmpty() || WorkspaceIo::sha256(*sourceBytes) != expected) {
            WorkspaceIo::fail(error, "Scaffold source blueprint changed or is missing; import association cannot be verified");
            return ImportState::Invalid;
        }
    }
    if (sourceBytes) {
        const auto source = BlueprintSerializer::fromJson(*sourceBytes, error);
        if (!source) return ImportState::Invalid;
        for (const auto &sourceNode : source->nodes)
            if (sourceNode.type == NodeType::ExternalCode && sourceNode.id == node.id) {
                WorkspaceIo::fail(error, "External import is missing from an associated scaffold; revalidation is required");
                return ImportState::Invalid;
            }
    }
    return ImportState::NotImported;
}

bool ExternalCodeImporter::importManifest(const BlueprintNode &node, const QString &workspace,
                                        QJsonObject &manifest, QString *error)
{
    manifest = {};
    if (error) error->clear();
    if (!validContract(node, error)) return false;
    QJsonObject verified;
    if (!loadImport(node, workspace, verified, error) || !verifyFiles(node, workspace, verified, error)) return false;
    manifest = verified;
    return true;
}

bool ExternalCodeImporter::readImportedFile(const BlueprintNode &node, const QString &workspace,
                                          const QString &relativePath, QByteArray &bytes, QString *error)
{
    bytes.clear();
    QJsonObject manifest;
    if (!importManifest(node, workspace, manifest, error)) return false;
    const auto files = manifest.value("files").toObject();
    if (!WorkspaceIo::safeRelative(relativePath) || !files.contains(relativePath))
        return WorkspaceIo::fail(error, "File is not a tracked external source: " + relativePath);
    std::optional<QByteArray> verified;
    if (!WorkspaceIo::read(workspace, nodeRoot(node) + '/' + relativePath, verified, error)) return false;
    if (!verified || WorkspaceIo::sha256(*verified) != files.value(relativePath).toString())
        return WorkspaceIo::fail(error, "External source changed while reading: " + relativePath);
    bytes = *verified;
    return true;
}

bool ExternalCodeImporter::reimportFiles(const BlueprintNode &node, const QString &sourceRoot,
                                       const QStringList &relativeFiles, const QString &workspace, QString *error)
{
    if (error) error->clear();
    if (!validContract(node, error) || !validSelection(relativeFiles, error)) return false;
    QJsonObject manifest;
    if (!loadImport(node, workspace, manifest, error, false)
        || !verifyFiles(node, workspace, manifest, error, false)) return false;
    QStringList selected = relativeFiles;
    selected.sort();
    if (selected != manifest.value("files").toObject().keys())
        return WorkspaceIo::fail(error, "External reimport requires the same tracked file paths");
    QMap<QString, QByteArray> writes;
    QJsonObject hashes;
    if (!selectedWrites(node, sourceRoot, relativeFiles, writes, hashes, error)) return false;
    writes.insert(nodeRoot(node) + '/' + manifestName, WorkspaceIo::json(manifestFor(node, hashes)));
    return WorkspaceIo::transaction(workspace, writes, error);
}
