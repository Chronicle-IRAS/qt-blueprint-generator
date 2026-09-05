#include "generation/workspace_io.h"
#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSet>
#include <memory>
#include <vector>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace WorkspaceIo {
bool fail(QString *error, const QString &message) { if (error) *error = message; return false; }
QString sha256(const QByteArray &bytes) { return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex()); }
QByteArray json(const QJsonObject &object) { return QJsonDocument(object).toJson(QJsonDocument::Indented); }
bool safeId(const QString &id)
{
    static const QRegularExpression pattern("\\A[A-Za-z0-9][A-Za-z0-9_-]{0,79}\\z");
    return pattern.match(id).hasMatch() && safeRelative(id);
}
bool safeRelative(const QString &path)
{
    if (path.isEmpty() || QDir::isAbsolutePath(path) || path.contains('\\')) return false;
    static const QRegularExpression segmentPattern("\\A[A-Za-z0-9_.-]+\\z");
    static const QRegularExpression reserved("\\A(CON|PRN|AUX|NUL|COM[1-9]|LPT[1-9])\\z", QRegularExpression::CaseInsensitiveOption);
    for (const QString &segment : path.split('/')) {
        if (!segmentPattern.match(segment).hasMatch() || segment == "." || segment == ".."
            || segment.endsWith('.') || reserved.match(segment.section('.', 0, 0)).hasMatch()) return false;
    }
    return true;
}
bool checkPath(const QString &workspace, const QString &relative, QString *error)
{
    if (!QDir::isAbsolutePath(workspace) || !QFileInfo(workspace).isDir() || !safeRelative(relative))
        return fail(error, "Workspace must exist and paths must be portable relative paths");
    const QString absolute = QDir(workspace).absoluteFilePath(relative);
    QString cursor = QDir::cleanPath(absolute);
    while (true) {
        const QFileInfo info(cursor);
        bool link = info.isSymbolicLink();
#ifdef Q_OS_WIN
        const QString native = QDir::toNativeSeparators(cursor);
        const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
        link = link || (attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (link) return fail(error, "Directory links and reparse points are forbidden: " + cursor);
        if (info.exists()) {
            if (cursor != absolute && !info.isDir()) return fail(error, "A path ancestor is a file: " + cursor);
            const QDir parent(info.absolutePath());
            for (const QString &entry : parent.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
                if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0 && entry != info.fileName())
                    return fail(error, "Path case alias is forbidden: " + cursor);
        } else {
            const QDir parent(info.absolutePath());
            for (const QString &entry : parent.entryList(QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot))
                if (entry.compare(info.fileName(), Qt::CaseInsensitive) == 0)
                    return fail(error, "Path case alias is forbidden: " + cursor);
        }
        const QString parent = info.absolutePath();
        if (parent == cursor) break;
        cursor = parent;
    }
    return true;
}
bool read(const QString &workspace, const QString &relative, std::optional<QByteArray> &bytes, QString *error)
{
    bytes.reset();
    if (!checkPath(workspace, relative, error)) return false;
    QFile f(QDir(workspace).filePath(relative));
    if (!f.exists()) return true;
    if (!QFileInfo(f).isFile() || !f.open(QIODevice::ReadOnly)) return fail(error, "Cannot read file: " + relative);
    const QByteArray data = f.readAll();
    if (f.error() != QFileDevice::NoError) return fail(error, "Cannot read complete file: " + relative);
    bytes = data;
    return true;
}
bool transaction(const QString &workspace, const QMap<QString, QByteArray> &writes, QString *error)
{
    struct Entry { QString path; std::optional<QByteArray> before; std::unique_ptr<QSaveFile> file; };
    std::vector<Entry> entries;
    QSet<QString> keys;
    for (auto it = writes.begin(); it != writes.end(); ++it) {
        const QString key = it.key().toCaseFolded();
        for (const QString &other : keys)
            if (other == key || other.startsWith(key + '/') || key.startsWith(other + '/'))
                return fail(error, "Transaction contains colliding paths");
        keys.insert(key);
        std::optional<QByteArray> before;
        if (!read(workspace, it.key(), before, error)) return false;
        entries.push_back({it.key(), before, nullptr});
    }
    for (auto &entry : entries) {
        if (!checkPath(workspace, entry.path, error)) return false;
        const QString path = QDir(workspace).filePath(entry.path);
        if (!QDir().mkpath(QFileInfo(path).absolutePath()) || !checkPath(workspace, entry.path, error))
            return fail(error, "Cannot prepare transaction directory: " + entry.path);
        entry.file = std::make_unique<QSaveFile>(path);
        entry.file->setDirectWriteFallback(false);
        if (!entry.file->open(QIODevice::WriteOnly) || entry.file->write(writes.value(entry.path)) != writes.value(entry.path).size())
            return fail(error, "Cannot stage transaction file: " + entry.path);
    }
    for (size_t index = 0; index < entries.size(); ++index) {
        auto &entry = entries[index];
        if (checkPath(workspace, entry.path, error) && entry.file->commit()) continue;
        bool restored = true;
        for (size_t restore = 0; restore < index; ++restore) {
            auto &previous = entries[restore];
            if (!checkPath(workspace, previous.path, nullptr)) { restored = false; continue; }
            const QString path = QDir(workspace).filePath(previous.path);
            if (previous.before) {
                QSaveFile backup(path); backup.setDirectWriteFallback(false);
                if (!backup.open(QIODevice::WriteOnly) || backup.write(*previous.before) != previous.before->size() || !backup.commit()) restored = false;
            } else if (!QFile::remove(path)) restored = false;
        }
        return fail(error, restored ? "Transaction failed; all committed files restored" : "Transaction failed and rollback FAILED; inspect workspace before any further operation");
    }
    return true;
}
bool ownedPath(const QString &nodeId, const QString &path)
{
    if (!safeId(nodeId) || !safeRelative(path)) return false;
    const QString suffix = QFileInfo(path).suffix();
    if (suffix != "h" && suffix != "hpp" && suffix != "cpp" && suffix != "cc") return false;
    return path.startsWith("src/modules/" + nodeId + "/implementation/") || path.startsWith("tests/" + nodeId + "/");
}
bool loadManifest(const QString &workspace, QJsonObject &manifest, QString *error)
{
    std::optional<QByteArray> bytes;
    if (!read(workspace, "generation-manifest.json", bytes, error)) return false;
    if (!bytes) return fail(error, "Scaffold manifest is missing");
    QJsonParseError parse;
    const QJsonDocument document = QJsonDocument::fromJson(*bytes, &parse);
    if (parse.error != QJsonParseError::NoError || !document.isObject()) return fail(error, "Malformed generation manifest");
    manifest = document.object();
    static const QRegularExpression hash("\\A[a-f0-9]{64}\\z");
    if (manifest.size() != 6 || manifest.value("version") != 1 || !hash.match(manifest.value("blueprintSha256").toString()).hasMatch()
        || !manifest.value("modules").isArray() || !manifest.value("protectedFiles").isObject()
        || !manifest.value("files").isObject() || !manifest.value("batches").isObject())
        return fail(error, "Malformed generation manifest schema");
    QSet<QString> nodes;
    for (const QJsonValue &value : manifest.value("modules").toArray()) {
        if (!value.isString() || !safeId(value.toString()) || nodes.contains(value.toString().toCaseFolded())) return fail(error, "Malformed manifest module");
        nodes.insert(value.toString().toCaseFolded());
    }
    const QJsonObject protectedFiles = manifest.value("protectedFiles").toObject();
    if (protectedFiles.isEmpty()) return fail(error, "Manifest contains no scaffold files");
    for (auto it = protectedFiles.begin(); it != protectedFiles.end(); ++it)
        if (!safeRelative(it.key()) || !hash.match(it.value().toString()).hasMatch()) return fail(error, "Malformed protected file record");
    const QJsonObject files = manifest.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        const QJsonObject file = it.value().toObject();
        if (file.size() != 3 || !nodes.contains(file.value("nodeId").toString().toCaseFolded())
            || !ownedPath(file.value("nodeId").toString(), it.key()) || !hash.match(file.value("sha256").toString()).hasMatch()
            || !safeId(file.value("generationId").toString())) return fail(error, "Malformed accepted file record");
    }
    const QJsonObject batches = manifest.value("batches").toObject();
    QSet<QString> generationIds;
    for (auto it = batches.begin(); it != batches.end(); ++it) {
        const QJsonObject batch = it.value().toObject();
        const QString node = batch.value("nodeId").toString();
        if (!safeId(it.key()) || generationIds.contains(it.key().toCaseFolded()) || batch.size() != 6
            || !manifest.value("modules").toArray().contains(node) || !batch.value("model").isString()
            || !batch.value("summary").isString() || !hash.match(batch.value("promptSha256").toString()).hasMatch()
            || (batch.value("state") != "active" && batch.value("state") != "cancelled")
            || !batch.value("files").isObject() || batch.value("files").toObject().isEmpty()) return fail(error, "Malformed candidate batch record");
        generationIds.insert(it.key().toCaseFolded());
        QSet<QString> paths;
        const auto candidateFiles = batch.value("files").toObject();
        for (auto f = candidateFiles.begin(); f != candidateFiles.end(); ++f) {
            const auto record = f.value().toObject();
            const QString baseline = record.value("baselineSha256").toString();
            if (!ownedPath(node, f.key()) || protectedFiles.contains(f.key()) || record.size() != 3
                || !hash.match(record.value("sha256").toString()).hasMatch() || !record.value("baselineSha256").isString()
                || (!baseline.isEmpty() && !hash.match(baseline).hasMatch())
                || (record.value("state") != "pending" && record.value("state") != "accepted" && record.value("state") != "rejected"))
                return fail(error, "Malformed candidate file record");
            const QString key = f.key().toCaseFolded();
            for (const QString &path : paths)
                if (path == key || path.startsWith(key + '/') || key.startsWith(path + '/')) return fail(error, "Manifest candidate paths collide");
            paths.insert(key);
        }
    }
    for (auto it = files.begin(); it != files.end(); ++it) {
        const auto record = it.value().toObject();
        const auto batch = batches.value(record.value("generationId").toString()).toObject();
        if (batch.value("nodeId") != record.value("nodeId") || batch.value("files").toObject().value(it.key()).toObject().value("state") != "accepted")
            return fail(error, "Accepted file references a missing generation");
    }
    return true;
}
}
