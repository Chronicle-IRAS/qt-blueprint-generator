#include "workspace/external_code_importer.h"
#include "blueprint/blueprint_validator.h"
#include "generation/generation_service.h"
#include "generation/ir_compiler.h"
#include "generation/project_scaffolder.h"
#include "generation/prompt_compiler.h"
#include "generation/workspace_io.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>

namespace {
BlueprintNode externalNode()
{
    BlueprintNode node;
    node.id = "vendor";
    node.type = NodeType::ExternalCode;
    node.name = "VendorService";
    node.description = "User supplied interface for a text lookup";
    node.inputs = {{"query", "QString", "Lookup key"}};
    node.outputs = {{"value", "QString", "Lookup result"}};
    node.constraints = {"Opaque implementation"};
    node.acceptanceCriteria = {"Returns requested value"};
    return node;
}

BlueprintDocument graph(const BlueprintNode &external)
{
    BlueprintDocument document;
    document.projectId = "external-demo";
    document.projectName = "External Demo";
    document.target = "qt6-widgets-cpp17-cmake";
    BlueprintNode logic;
    logic.id = "logic";
    logic.type = NodeType::LogicModule;
    logic.name = "LookupClient";
    logic.description = "Calls the vendor interface";
    BlueprintNode start; start.id = "start";
    BlueprintNode end; end.id = "end"; end.type = NodeType::End;
    document.nodes = {start, external, logic, end};
    document.edges = {{"a", "start", external.id, {}}, {"b", external.id, "logic", {}},
                      {"c", "logic", "end", {}}};
    return document;
}

bool write(const QString &root, const QString &path, const QByteArray &bytes)
{
    const QString absolute = QDir(root).filePath(path);
    if (!QDir().mkpath(QFileInfo(absolute).absolutePath())) return false;
    QFile file(absolute);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray read(const QString &root, const QString &path)
{
    QFile file(QDir(root).filePath(path));
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
const QString manifestPath = "external/vendor/import-manifest.json";
}

class ExternalCodeImporterTest final : public QObject
{
    Q_OBJECT
private slots:
    void preservesSelectedBytesAndRecordsContract();
    void acceptsCAndCxxSelections();
    void exposesOnlyVerifiedMetadataAndBytes();
    void failedReadsClearOutputs_data();
    void failedReadsClearOutputs();
    void authorizedReplacementRebindsContractAndRepairsFiles_data();
    void authorizedReplacementRebindsContractAndRepairsFiles();
    void authorizedReplacementRefusesUnsafeOwnership_data();
    void authorizedReplacementRefusesUnsafeOwnership();
    void authorizedReplacementRejectsLinks_data();
    void authorizedReplacementRejectsLinks();
    void rejectsUnsafeSelections_data();
    void rejectsUnsafeSelections();
    void requiresExplicitExternalContract();
    void refusesUntrackedDestinationsAndCaseAliases();
    void exactReimportIsIdempotentButChangesAreRejected();
    void detectsTampering_data();
    void detectsTampering();
    void externalContentsNeverEnterGeneration();
    void rejectsDirectoryLinks_data();
    void rejectsDirectoryLinks();
    void deletedManifestCannotBypassValidation_data();
    void deletedManifestCannotBypassValidation();
};

void ExternalCodeImporterTest::acceptsCAndCxxSelections()
{
    QTemporaryDir source, workspace;
    const QStringList paths{"src/api.c", "src/detail.cxx"};
    for (const auto &path : paths) QVERIFY(write(source.path(), path, path.toUtf8()));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(externalNode(), source.path(), paths, workspace.path(), &error), qPrintable(error));
    QVERIFY2(ExternalCodeImporter::verifyImport(externalNode(), workspace.path(), &error), qPrintable(error));
    for (const auto &path : paths) QCOMPARE(read(workspace.path(), "external/vendor/" + path), path.toUtf8());
}

void ExternalCodeImporterTest::exposesOnlyVerifiedMetadataAndBytes()
{
    QTemporaryDir source, workspace;
    const auto node = externalNode();
    const QByteArray original = QByteArray::fromHex("000d0aff80");
    QVERIFY(write(source.path(), "include/api.h", original));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"include/api.h"}, workspace.path(), &error), qPrintable(error));
    QJsonObject manifest{{"stale", true}};
    QVERIFY2(ExternalCodeImporter::importManifest(node, workspace.path(), manifest, &error), qPrintable(error));
    QCOMPARE(manifest, QJsonDocument::fromJson(read(workspace.path(), manifestPath)).object());
    QByteArray bytes = "stale";
    QVERIFY2(ExternalCodeImporter::readImportedFile(node, workspace.path(), "include/api.h", bytes, &error), qPrintable(error));
    QCOMPARE(bytes, original);
    QVERIFY(error.isEmpty());
}

void ExternalCodeImporterTest::failedReadsClearOutputs_data()
{
    QTest::addColumn<QString>("change");
    for (const char *change : {"unknown-path", "unsafe-path", "case-alias", "bytes", "missing-file", "contract", "manifest", "inventory", "wrong-node"})
        QTest::newRow(change) << QString::fromLatin1(change);
}

void ExternalCodeImporterTest::failedReadsClearOutputs()
{
    QFETCH(QString, change);
    QTemporaryDir source, workspace;
    auto node = externalNode();
    QVERIFY(write(source.path(), "api.h", "original"));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error), qPrintable(error));
    QString path = "api.h";
    if (change == "unknown-path") path = "other.h";
    else if (change == "unsafe-path") path = "../api.h";
    else if (change == "case-alias") path = "API.h";
    else if (change == "bytes") QVERIFY(write(workspace.path(), "external/vendor/api.h", "tampered"));
    else if (change == "missing-file") QVERIFY(QFile::remove(workspace.filePath("external/vendor/api.h")));
    else if (change == "contract") node.description += " changed";
    else if (change == "manifest") QVERIFY(write(workspace.path(), manifestPath, "bad json"));
    else if (change == "inventory") QVERIFY(write(workspace.path(), "external/vendor/extra.h", "extra"));
    else node.id = "other";
    QByteArray bytes = "stale private bytes";
    QVERIFY(!ExternalCodeImporter::readImportedFile(node, workspace.path(), path, bytes, &error));
    QVERIFY(bytes.isEmpty());
    QVERIFY(!error.isEmpty());
    if (change == "unknown-path" || change == "unsafe-path" || change == "case-alias") return;
    QJsonObject manifest{{"stale", "private metadata"}};
    QVERIFY(!ExternalCodeImporter::importManifest(node, workspace.path(), manifest, &error));
    QVERIFY(manifest.isEmpty());
    QVERIFY(!error.isEmpty());
}

void ExternalCodeImporterTest::authorizedReplacementRebindsContractAndRepairsFiles_data()
{
    QTest::addColumn<QString>("change");
    for (const char *change : {"source", "contract", "tampered", "missing-file", "missing-directory"})
        QTest::newRow(change) << QString::fromLatin1(change);
}

void ExternalCodeImporterTest::authorizedReplacementRebindsContractAndRepairsFiles()
{
    QFETCH(QString, change);
    QTemporaryDir source, workspace;
    auto node = externalNode();
    const QStringList paths{"api.h", "src/api.cpp"};
    for (const auto &path : paths) QVERIFY(write(source.path(), path, "original " + path.toUtf8()));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), paths, workspace.path(), &error), qPrintable(error));
    if (change == "source") QVERIFY(write(source.path(), "api.h", "replacement"));
    else if (change == "contract") node.outputs.first().description += " changed";
    else if (change == "tampered") QVERIFY(write(workspace.path(), "external/vendor/api.h", "tampered"));
    else if (change == "missing-file") QVERIFY(QFile::remove(workspace.filePath("external/vendor/api.h")));
    else {
        QVERIFY(QFile::remove(workspace.filePath("external/vendor/src/api.cpp")));
        QVERIFY(QDir().rmdir(workspace.filePath("external/vendor/src")));
    }
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), paths, workspace.path(), &error));
    QVERIFY2(ExternalCodeImporter::reimportFiles(node, source.path(), paths, workspace.path(), &error), qPrintable(error));
    QVERIFY2(ExternalCodeImporter::verifyImport(node, workspace.path(), &error), qPrintable(error));
    for (const auto &path : paths) QCOMPARE(read(workspace.path(), "external/vendor/" + path), read(source.path(), path));
    QJsonObject manifest;
    QVERIFY2(ExternalCodeImporter::importManifest(node, workspace.path(), manifest, &error), qPrintable(error));
    QCOMPARE(manifest.value("contract").toObject().value("outputs").toArray().first().toObject().value("description").toString(), node.outputs.first().description);
}

void ExternalCodeImporterTest::authorizedReplacementRefusesUnsafeOwnership_data()
{
    QTest::addColumn<QString>("change");
    for (const char *change : {"added-selection", "removed-selection", "missing-source", "untracked-file", "untracked-directory", "manifest-missing", "manifest-json", "manifest-extra-field", "manifest-hash", "manifest-version", "manifest-policy", "wrong-id", "wrong-type", "invalid-contract", "invalid-port", "unknown-contract-field", "invalid-file-hash", "unsafe-record", "case-record", "directory-in-place", "invalid-current-contract"})
        QTest::newRow(change) << QString::fromLatin1(change);
}

void ExternalCodeImporterTest::authorizedReplacementRefusesUnsafeOwnership()
{
    QFETCH(QString, change);
    QTemporaryDir source, workspace;
    auto node = externalNode();
    QStringList paths{"api.h", "src/api.cpp"};
    for (const auto &path : paths) QVERIFY(write(source.path(), path, "original " + path.toUtf8()));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), paths, workspace.path(), &error), qPrintable(error));
    QVERIFY(write(source.path(), "api.h", "replacement"));
    if (change == "added-selection") { QVERIFY(write(source.path(), "extra.cpp", "extra")); paths.append("extra.cpp"); }
    else if (change == "removed-selection") paths.removeLast();
    else if (change == "missing-source") QVERIFY(QFile::remove(source.filePath("src/api.cpp")));
    else if (change == "untracked-file") QVERIFY(write(workspace.path(), "external/vendor/extra.h", "keep"));
    else if (change == "untracked-directory") QVERIFY(QDir(workspace.path()).mkpath("external/vendor/empty"));
    else if (change == "manifest-missing") QVERIFY(QFile::remove(workspace.filePath(manifestPath)));
    else if (change == "manifest-json") QVERIFY(write(workspace.path(), manifestPath, "not json"));
    else if (change == "directory-in-place") {
        QVERIFY(QFile::remove(workspace.filePath("external/vendor/api.h")));
        QVERIFY(QDir(workspace.path()).mkpath("external/vendor/api.h"));
    } else if (change == "invalid-current-contract") node.inputs.first().type.clear();
    else {
        auto manifest = QJsonDocument::fromJson(read(workspace.path(), manifestPath)).object();
        auto contract = manifest.value("contract").toObject();
        if (change == "manifest-extra-field") manifest["unknown"] = true;
        else if (change == "manifest-hash") manifest["contractSha256"] = QString(64, '0');
        else if (change == "manifest-version") manifest["version"] = 2;
        else if (change == "manifest-policy") manifest["generationPolicy"] = "writable";
        else if (change == "invalid-file-hash") manifest["files"] = QJsonObject{{"api.h", "bad hash"}, {"src/api.cpp", WorkspaceIo::sha256("original src/api.cpp")}};
        else if (change == "unsafe-record") manifest["files"] = QJsonObject{{"../api.h", WorkspaceIo::sha256("original")}};
        else if (change == "case-record") manifest["files"] = QJsonObject{{"api.h", WorkspaceIo::sha256("original")}, {"API.h", WorkspaceIo::sha256("original")}};
        else {
            if (change == "wrong-id") contract["id"] = "other";
            else if (change == "wrong-type") contract["type"] = "LogicModule";
            else if (change == "invalid-contract") contract["description"] = "";
            else if (change == "unknown-contract-field") contract["unknown"] = "field";
            else {
                auto inputs = contract.value("inputs").toArray();
                auto port = inputs.first().toObject(); port["type"] = 42; inputs[0] = port; contract["inputs"] = inputs;
            }
            manifest["contract"] = contract;
            manifest["contractSha256"] = WorkspaceIo::sha256(WorkspaceIo::json(contract));
        }
        QVERIFY(write(workspace.path(), manifestPath, WorkspaceIo::json(manifest)));
    }
    const auto beforeManifest = read(workspace.path(), manifestPath);
    const auto beforeFile = read(workspace.path(), "external/vendor/api.h");
    const auto beforeOther = read(workspace.path(), "external/vendor/src/api.cpp");
    QVERIFY(!ExternalCodeImporter::reimportFiles(node, source.path(), paths, workspace.path(), &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(read(workspace.path(), manifestPath), beforeManifest);
    QCOMPARE(read(workspace.path(), "external/vendor/api.h"), beforeFile);
    QCOMPARE(read(workspace.path(), "external/vendor/src/api.cpp"), beforeOther);
    QVERIFY(!QFileInfo::exists(workspace.filePath("external/vendor/extra.cpp")));
}

void ExternalCodeImporterTest::authorizedReplacementRejectsLinks_data()
{
    QTest::addColumn<QString>("location");
    for (const char *location : {"source-root", "source-child", "workspace-root", "destination-child", "unlisted-cycle"})
        QTest::newRow(location) << QString::fromLatin1(location);
}

void ExternalCodeImporterTest::authorizedReplacementRejectsLinks()
{
    QFETCH(QString, location);
    QTemporaryDir source, workspace, outside;
    const QStringList paths{"api.h", "nested/api.h"};
    for (const auto &path : paths) {
        QVERIFY(write(source.path(), path, "original"));
        QVERIFY(write(outside.path(), path, "outside sentinel"));
    }
    QString error;
    const auto node = externalNode();
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), paths, workspace.path(), &error), qPrintable(error));
    const auto beforeManifest = read(workspace.path(), manifestPath);
    QVERIFY(write(source.path(), "api.h", "replacement"));
    QString selectedRoot = source.path(), selectedWorkspace = workspace.path();
    QString link, target = outside.path();
    if (location == "source-root") { link = source.filePath("linked-root"); selectedRoot = link; }
    else if (location == "source-child") {
        QVERIFY(QFile::remove(source.filePath("nested/api.h")));
        QVERIFY(QDir().rmdir(source.filePath("nested")));
        link = source.filePath("nested");
    } else if (location == "workspace-root") { link = workspace.filePath("linked-root"); target = workspace.path(); selectedWorkspace = link; }
    else if (location == "destination-child") {
        QVERIFY(QFile::remove(workspace.filePath("external/vendor/nested/api.h")));
        QVERIFY(QDir().rmdir(workspace.filePath("external/vendor/nested")));
        link = workspace.filePath("external/vendor/nested");
    } else { link = workspace.filePath("external/vendor/cycle"); target = workspace.filePath("external/vendor"); }
#ifdef Q_OS_WIN
    if (QProcess::execute("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}) != 0)
        QSKIP("Directory junction creation unavailable");
    const auto cleanup = qScopeGuard([&] { QDir().rmdir(link); });
#else
    if (!QFile::link(target, link)) QSKIP("Directory symlink creation unavailable");
    const auto cleanup = qScopeGuard([&] { QFile::remove(link); });
#endif
    QVERIFY(!ExternalCodeImporter::reimportFiles(node, selectedRoot, paths, selectedWorkspace, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(read(workspace.path(), manifestPath), beforeManifest);
    QCOMPARE(read(workspace.path(), "external/vendor/api.h"), "original");
    for (const auto &path : paths) QCOMPARE(read(outside.path(), path), "outside sentinel");
    if (location == "destination-child" || location == "unlisted-cycle" || location == "workspace-root") {
        QJsonObject manifest{{"stale", true}};
        QByteArray bytes = "stale";
        QVERIFY(!ExternalCodeImporter::importManifest(node, selectedWorkspace, manifest, &error));
        QVERIFY(manifest.isEmpty());
        QVERIFY(!ExternalCodeImporter::readImportedFile(node, selectedWorkspace, "api.h", bytes, &error));
        QVERIFY(bytes.isEmpty());
    }
}

void ExternalCodeImporterTest::preservesSelectedBytesAndRecordsContract()
{
    QTemporaryDir source, workspace;
    const auto node = externalNode();
    const QStringList paths = {"api.h", "include/api.hpp", "src/api.cpp", "src/detail/api.cc"};
    const QByteArray bytes = QByteArray::fromHex("efbbbf2f2f20707269766174650d0a00ff80");
    for (const auto &path : paths) QVERIFY(write(source.path(), path, bytes + path.toUtf8()));
    QVERIFY(write(source.path(), "unselected.cpp", "private unselected"));
    const auto permissions = QFileInfo(QDir(source.path()).filePath(paths.first())).permissions();
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), paths, workspace.path(), &error), qPrintable(error));
    QVERIFY2(ExternalCodeImporter::verifyImport(node, workspace.path(), &error), qPrintable(error));
    const auto manifest = QJsonDocument::fromJson(read(workspace.path(), manifestPath)).object();
    QCOMPARE(manifest.value("version").toInt(), 1);
    QCOMPARE(manifest.value("generationPolicy").toString(), "readOnly");
    const auto contract = manifest.value("contract").toObject();
    QCOMPARE(contract.value("description").toString(), node.description);
    QCOMPARE(contract.value("inputs").toArray().first().toObject().value("name").toString(), "query");
    QCOMPARE(contract.value("outputs").toArray().first().toObject().value("name").toString(), "value");
    QCOMPARE(manifest.value("contractSha256").toString(), WorkspaceIo::sha256(WorkspaceIo::json(contract)));
    QCOMPARE(manifest.value("files").toObject().size(), paths.size());
    for (const auto &path : paths) {
        QCOMPARE(read(workspace.path(), "external/vendor/" + path), bytes + path.toUtf8());
        QCOMPARE(manifest.value("files").toObject().value(path).toString(), WorkspaceIo::sha256(bytes + path.toUtf8()));
        QCOMPARE(read(source.path(), path), bytes + path.toUtf8());
        QVERIFY(!WorkspaceIo::ownedPath("vendor", "external/vendor/" + path));
    }
    QCOMPARE(QFileInfo(QDir(source.path()).filePath(paths.first())).permissions(), permissions);
    QVERIFY(!QFileInfo::exists(QDir(workspace.path()).filePath("external/vendor/unselected.cpp")));
    QVERIFY(!QFileInfo::exists(QDir(workspace.path()).filePath("generation-manifest.json")));
    QVERIFY(BlueprintValidator::validate(graph(node), {workspace.path()}).isEmpty());
}

void ExternalCodeImporterTest::rejectsUnsafeSelections_data()
{
    QTest::addColumn<QStringList>("paths");
    QTest::newRow("traversal") << QStringList{"../escape.cpp"};
    QTest::newRow("nested-traversal") << QStringList{"dir/../../escape.cpp"};
    QTest::newRow("absolute") << QStringList{"C:/escape.cpp"};
    QTest::newRow("backslash") << QStringList{"dir\\escape.cpp"};
    QTest::newRow("empty") << QStringList{};
    QTest::newRow("extension") << QStringList{"secret.txt"};
    QTest::newRow("duplicate") << QStringList{"ok.cpp", "ok.cpp"};
    QTest::newRow("case-alias") << QStringList{"ok.cpp", "OK.cpp"};
    QTest::newRow("prefix") << QStringList{"ok.cpp", "ok.cpp/child.h"};
    QTest::newRow("case-directory") << QStringList{"inc/a.h", "INC/b.h"};
    QTest::newRow("reserved-name") << QStringList{"NUL.cpp"};
    QTest::newRow("missing-file") << QStringList{"missing.cpp"};
    QTest::newRow("directory") << QStringList{"dir.cpp"};
}

void ExternalCodeImporterTest::rejectsUnsafeSelections()
{
    QFETCH(QStringList, paths);
    QTemporaryDir source, workspace;
    QVERIFY(write(source.path(), "ok.cpp", "ok"));
    QVERIFY(write(source.path(), "secret.txt", "secret"));
    QVERIFY(QDir(source.path()).mkpath("dir.cpp"));
    QString error;
    QVERIFY(!ExternalCodeImporter::importFiles(externalNode(), source.path(), paths, workspace.path(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFileInfo::exists(QDir(workspace.path()).filePath(manifestPath)));
    QVERIFY(!QFileInfo::exists(QDir(workspace.path()).filePath("external/vendor/ok.cpp")));
}

void ExternalCodeImporterTest::requiresExplicitExternalContract()
{
    QTemporaryDir source, workspace;
    QVERIFY(write(source.path(), "api.h", "int inferredName();"));
    auto node = externalNode();
    node.description.clear();
    QString error;
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error));
    QVERIFY(!error.isEmpty());
    node = externalNode(); node.type = NodeType::LogicModule;
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error));
    node = externalNode(); node.id = "../vendor";
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error));
    node = externalNode(); node.inputs.first().type.clear();
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error));
    QVERIFY(!QFileInfo::exists(QDir(workspace.path()).filePath(manifestPath)));
    node = externalNode(); node.inputs.clear(); node.outputs.clear(); // Explicit no-argument/void interface.
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error), qPrintable(error));
}

void ExternalCodeImporterTest::refusesUntrackedDestinationsAndCaseAliases()
{
    QTemporaryDir source, workspace;
    QVERIFY(write(source.path(), "api.h", "new"));
    QVERIFY(write(workspace.path(), "external/vendor/api.h", "existing"));
    QString error;
    QVERIFY(!ExternalCodeImporter::importFiles(externalNode(), source.path(), {"api.h"}, workspace.path(), &error));
    QCOMPARE(read(workspace.path(), "external/vendor/api.h"), "existing");
    QVERIFY(QFile::remove(QDir(workspace.path()).filePath("external/vendor/api.h")));
    QVERIFY(write(workspace.path(), "external/vendor/untracked.txt", "keep"));
    QVERIFY(!ExternalCodeImporter::importFiles(externalNode(), source.path(), {"api.h"}, workspace.path(), &error));
    QTemporaryDir aliasWorkspace;
    QVERIFY(QDir(aliasWorkspace.path()).mkpath("external/VENDOR"));
    QVERIFY(!ExternalCodeImporter::importFiles(externalNode(), source.path(), {"api.h"}, aliasWorkspace.path(), &error));
    QVERIFY(!error.isEmpty());
}

void ExternalCodeImporterTest::exactReimportIsIdempotentButChangesAreRejected()
{
    QTemporaryDir source, workspace;
    const auto node = externalNode();
    QVERIFY(write(source.path(), "api.h", "original"));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error), qPrintable(error));
    const auto originalManifest = read(workspace.path(), manifestPath);
    const auto modified = QFileInfo(QDir(workspace.path()).filePath(manifestPath)).lastModified();
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error), qPrintable(error));
    QCOMPARE(QFileInfo(QDir(workspace.path()).filePath(manifestPath)).lastModified(), modified);
    QVERIFY(write(source.path(), "api.h", "changed"));
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error));
    QVERIFY(write(source.path(), "extra.cpp", "extra"));
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), {"api.h", "extra.cpp"}, workspace.path(), &error));
    auto changedContract = node; changedContract.outputs.first().description = "Changed meaning";
    QVERIFY(!ExternalCodeImporter::verifyImport(changedContract, workspace.path(), &error));
    QVERIFY(!BlueprintValidator::validate(graph(changedContract), {workspace.path()}).isEmpty());
    QCOMPARE(read(workspace.path(), "external/vendor/api.h"), "original");
    QCOMPARE(read(workspace.path(), manifestPath), originalManifest);
}

void ExternalCodeImporterTest::detectsTampering_data()
{
    QTest::addColumn<QString>("change");
    for (const char *change : {"bytes", "deleted", "extra-file", "manifest-json", "manifest-hash", "manifest-policy", "manifest-path"})
        QTest::newRow(change) << QString::fromLatin1(change);
}

void ExternalCodeImporterTest::detectsTampering()
{
    QFETCH(QString, change);
    QTemporaryDir source, workspace;
    const auto node = externalNode();
    QVERIFY(write(source.path(), "api.h", "original"));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error), qPrintable(error));
    if (change == "bytes") QVERIFY(write(workspace.path(), "external/vendor/api.h", "tampered"));
    else if (change == "deleted") QVERIFY(QFile::remove(QDir(workspace.path()).filePath("external/vendor/api.h")));
    else if (change == "extra-file") QVERIFY(write(workspace.path(), "external/vendor/nested/extra.cpp", "extra"));
    else if (change == "manifest-json") QVERIFY(write(workspace.path(), manifestPath, "not json"));
    else {
        auto manifest = QJsonDocument::fromJson(read(workspace.path(), manifestPath)).object();
        if (change == "manifest-hash") manifest["contractSha256"] = QString(64, '0');
        else if (change == "manifest-policy") manifest["generationPolicy"] = "writable";
        else manifest["files"] = QJsonObject{{"../api.h", WorkspaceIo::sha256("original")}};
        QVERIFY(write(workspace.path(), manifestPath, WorkspaceIo::json(manifest)));
    }
    const auto manifestBefore = read(workspace.path(), manifestPath);
    QVERIFY(!ExternalCodeImporter::verifyImport(node, workspace.path(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error));
    QCOMPARE(read(workspace.path(), manifestPath), manifestBefore);
    QVERIFY(!BlueprintValidator::validate(graph(node), {workspace.path()}).isEmpty());
}

void ExternalCodeImporterTest::externalContentsNeverEnterGeneration()
{
    QTemporaryDir source, workspace;
    const auto node = externalNode();
    const auto document = graph(node);
    const QByteArray privateBytes = "SOURCE_SECRET_DO_NOT_SEND_TO_AI";
    QVERIFY(write(source.path(), "api.cpp", privateBytes));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"api.cpp"}, workspace.path(), &error), qPrintable(error));
    const auto manifest = read(workspace.path(), manifestPath);
    const auto ir = IrCompiler::compile(document);
    QVERIFY(!IrCompiler::toCanonicalJson(ir).contains(privateBytes));
    QVERIFY(!PromptCompiler::compileModulePrompt(ir, node.id, &error).has_value());
    const auto prompt = PromptCompiler::compileModulePrompt(ir, "logic", &error);
    QVERIFY2(prompt.has_value(), qPrintable(error));
    QVERIFY(prompt->contains(node.description));
    QVERIFY(!prompt->contains(QString::fromUtf8(privateBytes)));
    QVERIFY2(ProjectScaffolder::create(document, workspace.path(), &error), qPrintable(error));
    GenerationResult result{"logic", "attempt external overwrite", {{"external/vendor/api.cpp", {}, "changed"}}};
    QVERIFY(!GenerationService::persistCandidate(workspace.path(), "batch", result, "fake", *prompt, &error));
    result.nodeId = "vendor";
    result.files.first().relativePath = "src/modules/vendor/implementation/api.cpp";
    QVERIFY(!GenerationService::persistCandidate(workspace.path(), "vendor-batch", result, "fake", *prompt, &error));
    QCOMPARE(read(workspace.path(), "external/vendor/api.cpp"), privateBytes);
    QCOMPARE(read(workspace.path(), manifestPath), manifest);
}

void ExternalCodeImporterTest::deletedManifestCannotBypassValidation_data()
{
    QTest::addColumn<bool>("replaceSource");
    QTest::newRow("manifest-deleted") << false;
    QTest::newRow("manifest-deleted-source-replaced") << true;
}

void ExternalCodeImporterTest::deletedManifestCannotBypassValidation()
{
    QFETCH(bool, replaceSource);
    QTemporaryDir source, workspace;
    const auto node = externalNode();
    QVERIFY(write(source.path(), "api.h", "original"));
    QString error;
    QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), {"api.h"}, workspace.path(), &error), qPrintable(error));
    QVERIFY(BlueprintValidator::validate(graph(node), {workspace.path()}).isEmpty());
    QVERIFY(QFile::remove(workspace.filePath(manifestPath)));
    if (replaceSource) QVERIFY(write(workspace.path(), "external/vendor/api.h", "replacement"));
    QVERIFY(!ExternalCodeImporter::verifyImport(node, workspace.path(), &error));
    const auto diagnostics = BlueprintValidator::validate(graph(node), {workspace.path()});
    bool importRejected = false;
    for (const auto &diagnostic : diagnostics)
        if (diagnostic.code == "external_code.import.invalid" && diagnostic.nodeId == node.id) importRejected = true;
    QVERIFY2(importRejected, "Deleting an import manifest must not downgrade a verified import to unchecked external source");
}

void ExternalCodeImporterTest::rejectsDirectoryLinks_data()
{
    QTest::addColumn<QString>("location");
    for (const char *location : {"source-root", "source-child", "workspace-root", "destination-external", "destination-node", "unlisted-cycle"})
        QTest::newRow(location) << QString::fromLatin1(location);
}

void ExternalCodeImporterTest::rejectsDirectoryLinks()
{
    QFETCH(QString, location);
    QTemporaryDir source, workspace, outside;
    QVERIFY(write(source.path(), "api.h", "original"));
    QVERIFY(write(outside.path(), "api.h", "outside sentinel"));
    const auto node = externalNode();
    QString error;
    QString selectedRoot = source.path(), selectedWorkspace = workspace.path();
    QStringList selectedFiles{"api.h"};
    QString link, target = outside.path();
    if (location == "source-root") {
        link = source.filePath("linked-root"); selectedRoot = link;
    } else if (location == "source-child") {
        link = source.filePath("nested"); selectedFiles = {"api.h", "nested/api.h"};
    } else if (location == "workspace-root") {
        link = workspace.filePath("linked-root"); selectedWorkspace = link;
    } else if (location == "destination-external") link = workspace.filePath("external");
    else if (location == "destination-node") {
        QVERIFY(QDir(workspace.path()).mkpath("external"));
        link = workspace.filePath("external/vendor");
    } else {
        QVERIFY2(ExternalCodeImporter::importFiles(node, source.path(), selectedFiles, workspace.path(), &error), qPrintable(error));
        link = workspace.filePath("external/vendor/cycle");
        target = workspace.filePath("external/vendor");
    }
#ifdef Q_OS_WIN
    if (QProcess::execute("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(target)}) != 0)
        QSKIP("Directory junction creation unavailable");
    const auto cleanup = qScopeGuard([&] { QDir().rmdir(link); });
#else
    if (!QFile::link(target, link)) QSKIP("Directory symlink creation unavailable");
    const auto cleanup = qScopeGuard([&] { QFile::remove(link); });
#endif
    QVERIFY(!ExternalCodeImporter::importFiles(node, selectedRoot, selectedFiles, selectedWorkspace, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(read(outside.path(), "api.h"), "outside sentinel");
    QVERIFY(!QFileInfo::exists(outside.filePath("import-manifest.json")));
    QVERIFY(!QFileInfo::exists(outside.filePath("external/vendor/import-manifest.json")));
    if (location == "unlisted-cycle") {
        QVERIFY(!ExternalCodeImporter::verifyImport(node, workspace.path(), &error));
        QVERIFY(!BlueprintValidator::validate(graph(node), {workspace.path()}).isEmpty());
        QCOMPARE(read(workspace.path(), "external/vendor/api.h"), "original");
    } else QVERIFY(!QFileInfo::exists(workspace.filePath(manifestPath)));
}

QTEST_GUILESS_MAIN(ExternalCodeImporterTest)
#include "tst_external_code_importer.moc"
