#include "generation/generation_service.h"
#include "generation/workspace_io.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSet>

namespace {
using namespace WorkspaceIo;
QString root(const QString &generation, const QString &node) { return "candidates/" + generation + '/' + node + '/'; }
bool knownNode(const QJsonObject &manifest, const QString &node)
{
    return manifest.value("modules").toArray().contains(node);
}
bool protectedPath(const QJsonObject &manifest, const QString &path)
{
    const QJsonObject protectedFiles = manifest.value("protectedFiles").toObject();
    for (auto it = protectedFiles.begin(); it != protectedFiles.end(); ++it)
        if (it.key().compare(path, Qt::CaseInsensitive) == 0) return true;
    return false;
}
bool pendingBatch(const QJsonObject &manifest, const QString &generation, const QString &node,
                  QJsonObject &batch, QString *error)
{
    if (!safeId(generation) || !safeId(node) || !knownNode(manifest, node)) return fail(error, "Invalid generation or node ID");
    batch = manifest.value("batches").toObject().value(generation).toObject();
    if (batch.isEmpty() || batch.value("nodeId") != node || batch.value("state") != "active") return fail(error, "Candidate batch is missing or cancelled");
    return true;
}
bool pendingFile(const QJsonObject &manifest, const QJsonObject &batch, const QString &node,
                 const QString &path, QJsonObject &file, QString *error)
{
    if (!ownedPath(node, path) || protectedPath(manifest, path)) return fail(error, "Candidate path is outside node ownership or is protected");
    file = batch.value("files").toObject().value(path).toObject();
    if (file.isEmpty() || file.value("state") != "pending") return fail(error, "Candidate file is missing or already resolved");
    return true;
}
void updateBatch(QJsonObject &manifest, const QString &generation, const QJsonObject &batch)
{
    auto batches = manifest.value("batches").toObject(); batches[generation] = batch; manifest["batches"] = batches;
}
void updateFile(QJsonObject &batch, const QString &path, QJsonObject file, const QString &state)
{
    file["state"] = state;
    auto files = batch.value("files").toObject(); files[path] = file; batch["files"] = files;
}
}

bool GenerationService::persistCandidate(const QString &workspace, const QString &generation,
                                         const GenerationResult &result, const QString &model,
                                         const QString &prompt, QString *error)
{
    if (error) error->clear();
    QJsonObject manifest;
    if (!loadManifest(workspace, manifest, error)) return false;
    if (!safeId(generation) || !safeId(result.nodeId) || !knownNode(manifest, result.nodeId)) return fail(error, "Invalid generation or node ID");
    const QJsonObject batches = manifest.value("batches").toObject();
    for (auto it = batches.begin(); it != batches.end(); ++it)
        if (it.key().compare(generation, Qt::CaseInsensitive) == 0) return fail(error, "Generation ID already exists");
    const QString candidateRoot = root(generation, result.nodeId);
    if (!checkPath(workspace, candidateRoot + "sentinel.h", error)) return false;
    if (QFileInfo::exists(QDir(workspace).filePath("candidates/" + generation))) return fail(error, "Untracked or stale candidate directory already exists");

    // Re-run response validation at persistence time; never trust absoluteCandidatePath
    // or assume that a caller-created GenerationResult went through AI validation.
    QJsonArray responseFiles;
    for (const GeneratedFile &file : result.files) responseFiles.append(QJsonObject{{"path", file.relativePath}, {"content", file.content}});
    const QByteArray response = QJsonDocument(QJsonObject{{"nodeId", result.nodeId}, {"summary", result.summary}, {"files", responseFiles}}).toJson(QJsonDocument::Compact);
    const auto validated = parseAndValidate(response, result.nodeId, QDir(workspace).filePath(candidateRoot), {}, error);
    if (!validated) return false;
    QMap<QString, QByteArray> writes;
    QJsonObject records;
    QSet<QString> keys;
    const QJsonObject accepted = manifest.value("files").toObject();
    for (const GeneratedFile &file : validated->files) {
        const QString path = file.relativePath;
        if (!ownedPath(result.nodeId, path) || protectedPath(manifest, path)) return fail(error, "Candidate path is outside node ownership or is protected: " + path);
        const QString key = path.toCaseFolded();
        for (const QString &previous : keys)
            if (previous == key || previous.startsWith(key + '/') || key.startsWith(previous + '/')) return fail(error, "Candidate paths collide");
        keys.insert(key);
        for (auto it = accepted.begin(); it != accepted.end(); ++it)
            if (it.key().compare(path, Qt::CaseInsensitive) == 0 && (it.key() != path || it.value().toObject().value("nodeId") != result.nodeId)) return fail(error, "Candidate conflicts with another owned path");
        std::optional<QByteArray> current;
        if (!read(workspace, "generated-project/" + path, current, error)) return false;
        writes[candidateRoot + path] = file.content.toUtf8();
        records[path] = QJsonObject{{"sha256", sha256(file.content.toUtf8())},
                                    {"baselineSha256", current ? sha256(*current) : QString()}, {"state", "pending"}};
    }
    const QJsonObject batch{{"nodeId", result.nodeId}, {"model", model}, {"promptSha256", sha256(prompt.toUtf8())},
                             {"summary", result.summary}, {"state", "active"}, {"files", records}};
    updateBatch(manifest, generation, batch);
    writes["generation-manifest.json"] = json(manifest);
    return transaction(workspace, writes, error);
}

std::optional<CandidatePreview> GenerationService::previewCandidate(
    const QString &workspace, const QString &generation, const QString &node,
    const QString &path, QString *error)
{
    if (error) error->clear();
    QJsonObject manifest, batch, file;
    if (!loadManifest(workspace, manifest, error) || !pendingBatch(manifest, generation, node, batch, error)
        || !pendingFile(manifest, batch, node, path, file, error)) return std::nullopt;
    std::optional<QByteArray> candidate, current;
    if (!read(workspace, root(generation, node) + path, candidate, error)
        || !read(workspace, "generated-project/" + path, current, error)) return std::nullopt;
    if (!candidate || sha256(*candidate) != file.value("sha256").toString()) {
        fail(error, "Candidate file was changed or removed"); return std::nullopt;
    }
    const QString currentHash = current ? sha256(*current) : QString();
    const QString lastGenerated = manifest.value("files").toObject().value(path).toObject().value("sha256").toString();
    const QString baseline = file.value("baselineSha256").toString();
    return CandidatePreview{generation, node, path, current, *candidate, currentHash, sha256(*candidate),
                             lastGenerated, baseline, currentHash != lastGenerated || currentHash != baseline};
}

bool GenerationService::acceptCandidate(const QString &workspace, const CandidatePreview &preview,
                                        bool confirmOverwrite, const std::optional<QByteArray> &editedContent,
                                        QString *error)
{
    if (error) error->clear();
    const auto current = previewCandidate(workspace, preview.generationId, preview.nodeId, preview.relativePath, error);
    if (!current) return false;
    if (current->currentSha256 != preview.currentSha256 || current->candidateSha256 != preview.candidateSha256
        || current->baselineSha256 != preview.baselineSha256 || current->lastGeneratedSha256 != preview.lastGeneratedSha256)
        return fail(error, "Preview is stale; inspect the current file again before acceptance");
    if (current->conflict && !confirmOverwrite) return fail(error, "Current file has manual or newer changes; explicit confirmation is required");
    const QByteArray bytes = editedContent ? *editedContent : current->candidateContent;
    if (bytes.size() > GenerationLimits{}.maxFileBytes) return fail(error, "Edited content exceeds maximum file size");
    QJsonObject manifest, batch, file;
    if (!loadManifest(workspace, manifest, error) || !pendingBatch(manifest, preview.generationId, preview.nodeId, batch, error)
        || !pendingFile(manifest, batch, preview.nodeId, preview.relativePath, file, error)) return false;
    updateFile(batch, preview.relativePath, file, "accepted");
    updateBatch(manifest, preview.generationId, batch);
    auto files = manifest.value("files").toObject();
    files[preview.relativePath] = QJsonObject{{"nodeId", preview.nodeId}, {"generationId", preview.generationId}, {"sha256", sha256(bytes)}};
    manifest["files"] = files;
    return transaction(workspace, {{"generated-project/" + preview.relativePath, bytes}, {"generation-manifest.json", json(manifest)}}, error);
}

bool GenerationService::rejectCandidate(const QString &workspace, const QString &generation, const QString &node,
                                        const QString &path, QString *error)
{
    if (error) error->clear();
    QJsonObject manifest, batch, file;
    if (!loadManifest(workspace, manifest, error) || !pendingBatch(manifest, generation, node, batch, error)
        || !pendingFile(manifest, batch, node, path, file, error)) return false;
    updateFile(batch, path, file, "rejected"); updateBatch(manifest, generation, batch);
    return transaction(workspace, {{"generation-manifest.json", json(manifest)}}, error);
}

bool GenerationService::cancelCandidate(const QString &workspace, const QString &generation, const QString &node, QString *error)
{
    if (error) error->clear();
    QJsonObject manifest, batch;
    if (!loadManifest(workspace, manifest, error) || !pendingBatch(manifest, generation, node, batch, error)) return false;
    batch["state"] = "cancelled";
    auto files = batch.value("files").toObject();
    for (auto it = files.begin(); it != files.end(); ++it) {
        auto file = it.value().toObject();
        if (file.value("state") == "pending") { file["state"] = "rejected"; it.value() = file; }
    }
    batch["files"] = files; updateBatch(manifest, generation, batch);
    return transaction(workspace, {{"generation-manifest.json", json(manifest)}}, error);
}
