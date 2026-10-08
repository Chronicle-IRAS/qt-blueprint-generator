#include "blueprint/blueprint_project_store.h"
#include "blueprint/blueprint_serializer.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>

#include <cmath>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
constexpr qint64 MaximumBlueprintBytes = 16 * 1024 * 1024;
constexpr qint64 MaximumLayoutBytes = 8 * 1024 * 1024;

bool fail(QString *error, const QString &message)
{
    if (error) *error = message;
    return false;
}

bool isLink(const QFileInfo &info)
{
    if (info.isSymbolicLink()) return true;
#ifdef Q_OS_WIN
    const QString path = QDir::toNativeSeparators(info.absoluteFilePath());
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(path.utf16()));
    if (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT))
        return true;
#endif
    return false;
}

bool checkDirectory(const QString &directory, bool saving, QString *error)
{
    if (directory.isEmpty() || !QDir::isAbsolutePath(directory) || !QFileInfo(directory).isDir())
        return fail(error, QStringLiteral("Blueprint project directory must be an existing absolute directory"));
    QString cursor = QDir::cleanPath(QFileInfo(directory).absoluteFilePath());
    for (;;) {
        const QFileInfo info(cursor);
        if (isLink(info))
            return fail(error, QStringLiteral("Blueprint project paths cannot contain links or reparse points: %1").arg(cursor));
        if (saving) {
            const QDir ancestor(cursor);
            const bool workspace = QFileInfo::exists(ancestor.filePath("generation-manifest.json"));
            const bool scaffold = QFileInfo::exists(ancestor.filePath("src/contracts/blueprint.json"))
                                  && QFileInfo::exists(ancestor.filePath("src/contracts/source-blueprint.json"));
            if (workspace || scaffold)
                return fail(error, QStringLiteral("This directory belongs to a generated workspace or exported project; use Save As in a separate blueprint project directory: %1").arg(cursor));
        }
        const QString parent = info.absolutePath();
        if (parent == cursor) break;
        cursor = parent;
    }
    const QDir root(directory);
    const QStringList entries = root.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    for (const QString &name : {QStringLiteral("blueprint.json"), QStringLiteral("layout.json")}) {
        for (const QString &entry : entries) {
            if (entry.compare(name, Qt::CaseInsensitive) == 0 && entry != name)
                return fail(error, QStringLiteral("Blueprint project filename has a case alias: %1").arg(entry));
        }
        const QFileInfo info(root.filePath(name));
        if (isLink(info) || (info.exists() && !info.isFile()))
            return fail(error, QStringLiteral("Blueprint project target must be a regular non-link file: %1").arg(info.absoluteFilePath()));
    }
    return true;
}

bool readOptional(const QString &path, qint64 maximumBytes, std::optional<QByteArray> &result,
                  QString *error)
{
    result.reset();
    const QFileInfo info(path);
    if (!info.exists()) return true;
    if (info.size() > maximumBytes)
        return fail(error, QStringLiteral("Blueprint project file exceeds the size limit: %1").arg(path));
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, QStringLiteral("Cannot read %1: %2").arg(path, file.errorString()));
    const QByteArray bytes = file.read(maximumBytes + 1);
    if (file.error() != QFileDevice::NoError || bytes.size() > maximumBytes || !file.atEnd())
        return fail(error, QStringLiteral("Cannot read complete file within the size limit: %1").arg(path));
    result = bytes;
    return true;
}

QString semanticHash(const BlueprintDocument &document)
{
    return QString::fromLatin1(QCryptographicHash::hash(BlueprintSerializer::toJson(document),
                                                       QCryptographicHash::Sha256).toHex());
}

QByteArray serializeLayout(const BlueprintDocument &document, const QHash<QString, QPointF> &layout)
{
    QJsonArray positions;
    // Document order gives stable output regardless of QHash iteration order.
    for (const BlueprintNode &node : document.nodes) {
        const auto entry = layout.constFind(node.id);
        if (entry == layout.cend()) continue;
        positions.append(QJsonObject{{"nodeId", node.id}, {"x", entry->x()}, {"y", entry->y()}});
    }
    return QJsonDocument(QJsonObject{{"schemaVersion", 1}, {"blueprintSha256", semanticHash(document)},
                                    {"positions", positions}}).toJson(QJsonDocument::Indented);
}

bool parseLayout(const QByteArray &bytes, const BlueprintDocument &document,
                 QHash<QString, QPointF> &layout, QString *error)
{
    QJsonParseError parse;
    const QJsonDocument json = QJsonDocument::fromJson(bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !json.isObject())
        return fail(error, QStringLiteral("layout.json must contain a valid JSON object"));
    const QJsonObject root = json.object();
    const QJsonValue version = root.value("schemaVersion");
    if (!version.isDouble() || version.toDouble() != 1.0)
        return fail(error, QStringLiteral("layout.json.schemaVersion must be supported version 1"));
    if (!root.value("blueprintSha256").isString()
        || root.value("blueprintSha256").toString() != semanticHash(document))
        return fail(error, QStringLiteral("layout.json does not match blueprint.json; the project may contain an interrupted save"));
    if (!root.value("positions").isArray())
        return fail(error, QStringLiteral("layout.json.positions must be an array"));
    QHash<QString, QPointF> parsed;
    for (const QJsonValue &value : root.value("positions").toArray()) {
        if (!value.isObject())
            return fail(error, QStringLiteral("Each layout position must be an object"));
        const QJsonObject position = value.toObject();
        if (!position.value("nodeId").isString() || !position.value("x").isDouble()
            || !position.value("y").isDouble())
            return fail(error, QStringLiteral("Layout position requires a string nodeId and numeric x/y"));
        const QString id = position.value("nodeId").toString();
        if (parsed.contains(id))
            return fail(error, QStringLiteral("Duplicate layout node ID: %1").arg(id));
        parsed.insert(id, QPointF(position.value("x").toDouble(), position.value("y").toDouble()));
    }
    if (!BlueprintProjectStore::validate(document, parsed, error)) return false;
    layout = std::move(parsed);
    return true;
}

bool stage(QSaveFile &file, const QByteArray &bytes, QString *error)
{
    file.setDirectWriteFallback(false);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size())
        return fail(error, QStringLiteral("Cannot stage %1: %2").arg(file.fileName(), file.errorString()));
    return true;
}

bool restoreLayout(const QString &directory, const std::optional<QByteArray> &before)
{
    if (!checkDirectory(directory, false, nullptr)) return false;
    const QString path = QDir(directory).filePath("layout.json");
    if (!before) return QFile::remove(path);
    QSaveFile restore(path);
    return stage(restore, *before, nullptr) && restore.commit();
}
}

bool BlueprintProjectStore::validate(const BlueprintDocument &document,
                                    const QHash<QString, QPointF> &layout, QString *error)
{
    if (error) error->clear();
    if (document.schemaVersion != BlueprintDocument::CurrentSchemaVersion)
        return fail(error, QStringLiteral("Unsupported blueprint schema version"));
    QSet<QString> nodeIds;
    for (const BlueprintNode &node : document.nodes) {
        if (node.id.isEmpty() || node.id != node.id.trimmed() || nodeIds.contains(node.id))
            return fail(error, QStringLiteral("Blueprint node IDs must be nonempty, unpadded and unique: %1").arg(node.id));
        switch (node.type) {
        case NodeType::Start: case NodeType::End: case NodeType::UiPage:
        case NodeType::LogicModule: case NodeType::Decision: case NodeType::ExternalCode: break;
        default: return fail(error, QStringLiteral("Unknown blueprint node type: %1").arg(node.id));
        }
        nodeIds.insert(node.id);
    }
    QSet<QString> edgeIds;
    for (const BlueprintEdge &edge : document.edges) {
        if (edge.id.isEmpty() || edge.id != edge.id.trimmed() || edgeIds.contains(edge.id)
            || !nodeIds.contains(edge.source) || !nodeIds.contains(edge.target))
            return fail(error, QStringLiteral("Blueprint edges require unique unpadded IDs and existing endpoints: %1").arg(edge.id));
        edgeIds.insert(edge.id);
    }
    for (auto entry = layout.cbegin(); entry != layout.cend(); ++entry) {
        if (!nodeIds.contains(entry.key()) || !std::isfinite(entry->x()) || !std::isfinite(entry->y()))
            return fail(error, QStringLiteral("Layout positions require known node IDs and finite coordinates: %1").arg(entry.key()));
    }
    return true;
}

std::optional<BlueprintProject> BlueprintProjectStore::load(const QString &directory, QString *error)
{
    if (error) error->clear();
    if (!checkDirectory(directory, false, error)) return std::nullopt;
    std::optional<QByteArray> semantic;
    std::optional<QByteArray> sidecar;
    if (!readOptional(QDir(directory).filePath("blueprint.json"), MaximumBlueprintBytes, semantic, error))
        return std::nullopt;
    if (!semantic) {
        fail(error, QStringLiteral("blueprint.json is missing from the selected directory"));
        return std::nullopt;
    }
    auto document = BlueprintSerializer::fromJson(*semantic, error);
    if (!document || !validate(*document, {}, error)) return std::nullopt;
    if (!readOptional(QDir(directory).filePath("layout.json"), MaximumLayoutBytes, sidecar, error))
        return std::nullopt;
    BlueprintProject project{std::move(*document), {}};
    if (sidecar && !parseLayout(*sidecar, project.document, project.layout, error)) return std::nullopt;
    return project;
}

bool BlueprintProjectStore::save(const QString &directory, const BlueprintDocument &document,
                                const QHash<QString, QPointF> &layout, QString *error)
{
    if (error) error->clear();
    if (!validate(document, layout, error) || !checkDirectory(directory, true, error)) return false;
    const QByteArray semantic = BlueprintSerializer::toJson(document);
    const QByteArray sidecar = serializeLayout(document, layout);
    if (semantic.size() > MaximumBlueprintBytes || sidecar.size() > MaximumLayoutBytes)
        return fail(error, QStringLiteral("Blueprint project exceeds the file size limit"));
    const QDir root(directory);
    std::optional<QByteArray> previousSemantic;
    std::optional<QByteArray> previousLayout;
    if (!readOptional(root.filePath("blueprint.json"), MaximumBlueprintBytes, previousSemantic, error)
        || !readOptional(root.filePath("layout.json"), MaximumLayoutBytes, previousLayout, error)) return false;
    QSaveFile semanticFile(root.filePath("blueprint.json"));
    QSaveFile layoutFile(root.filePath("layout.json"));
    if (!stage(semanticFile, semantic, error) || !stage(layoutFile, sidecar, error)) return false;
    if (!checkDirectory(directory, true, error)) return false;
    if (!layoutFile.commit())
        return fail(error, QStringLiteral("Cannot commit layout.json: %1").arg(layoutFile.errorString()));
    // QSaveFile commits one file atomically. The pair is not crash-atomic; the hash detects
    // interrupted mixed revisions, and ordinary second-commit failures restore the sidecar.
    if (checkDirectory(directory, true, error) && semanticFile.commit()) return true;
    const QString commitError = error && !error->isEmpty() ? *error : semanticFile.errorString();
    const bool restored = restoreLayout(directory, previousLayout);
    return fail(error, restored
                       ? QStringLiteral("Cannot commit blueprint.json: %1. Previous layout restored.").arg(commitError)
                       : QStringLiteral("Cannot commit blueprint.json: %1. Layout rollback FAILED; inspect the project files before saving again.").arg(commitError));
}
