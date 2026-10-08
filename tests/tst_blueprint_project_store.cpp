#include <QtTest/QtTest>

#include "blueprint/blueprint_project_store.h"
#include "blueprint/blueprint_serializer.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "generation/ir_compiler.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QGraphicsSceneMouseEvent>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QTemporaryDir>

#include <limits>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
BlueprintDocument document()
{
    BlueprintDocument result;
    result.projectId = QStringLiteral("project-1");
    result.projectName = QStringLiteral("Draft 项目");
    result.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    BlueprintNode first;
    first.id = QStringLiteral("node-1");
    first.type = NodeType::LogicModule;
    first.name = QStringLiteral("First");
    first.description = QStringLiteral("Full description");
    first.inputs = {{"input", "Request", "input description"}};
    first.outputs = {{"output", "Response", "output description"}};
    first.constraints = {"constraint"};
    first.acceptanceCriteria = {"acceptance"};
    BlueprintNode second = first;
    second.id = QStringLiteral("node-2");
    second.type = NodeType::Decision;
    result.nodes = {first, second};
    result.edges = {{"edge-1", "node-1", "node-2", "yes"}};
    return result;
}

QHash<QString, QPointF> positions()
{
    return {{"node-1", {-12.5, 75.25}}, {"node-2", {800.0, -150.0}}};
}

QByteArray read(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

bool write(const QString &path, const QByteArray &bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QJsonObject layout(const BlueprintDocument &source)
{
    const auto hash = QCryptographicHash::hash(BlueprintSerializer::toJson(source),
                                              QCryptographicHash::Sha256).toHex();
    return {{"schemaVersion", 1}, {"blueprintSha256", QString::fromLatin1(hash)},
            {"positions", QJsonArray{QJsonObject{{"nodeId", "node-1"}, {"x", 1.0}, {"y", 2.0}}}}};
}
}

class BlueprintProjectStoreTest : public QObject
{
    Q_OBJECT
private slots:
    void roundtripPreservesSemanticsAndPositions();
    void missingAndPartialLayoutUseSceneDefaults();
    void malformedLayout_data();
    void malformedLayout();
    void invalidTechnicalDocuments_data();
    void invalidTechnicalDocuments();
    void invalidPositionsNeverOverwriteExistingFiles();
    void unsafeTargetsAndOversizeFilesAreRejected();
    void protectedWorkspaceLocationsCannotBeSaved();
    void linksAreRejected();
    void failedCommitRestoresPreviousPair();
    void readAndStagingFailuresPreserveExistingFiles();
    void sceneResetClearsHistoryAndConnectionState();
    void sceneResetCancelsAnActivePortGesture();
    void rejectedSceneResetPreservesEverything();
    void layoutSignalsAndUndoNeverChangeSemanticIr();
};

void BlueprintProjectStoreTest::roundtripPreservesSemanticsAndPositions()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = document();
    const auto beforeJson = BlueprintSerializer::toJson(source);
    const auto beforeIr = IrCompiler::compile(source);
    QString error = "stale";
    QVERIFY2(BlueprintProjectStore::save(directory.path(), source, positions(), &error), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(read(directory.filePath("blueprint.json")), beforeJson);
    auto loaded = BlueprintProjectStore::load(directory.path(), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QCOMPARE(loaded->document, source);
    QCOMPARE(loaded->layout, positions());
    QCOMPARE(IrCompiler::compile(loaded->document), beforeIr);
    auto moved = positions();
    moved["node-1"] = {99.0, 88.0};
    QVERIFY(BlueprintProjectStore::save(directory.path(), source, moved, &error));
    QCOMPARE(read(directory.filePath("blueprint.json")), beforeJson);
    QCOMPARE(IrCompiler::compile(source), beforeIr);
}

void BlueprintProjectStoreTest::missingAndPartialLayoutUseSceneDefaults()
{
    QTemporaryDir directory;
    const auto source = document();
    QVERIFY(write(directory.filePath("blueprint.json"), BlueprintSerializer::toJson(source)));
    QString error;
    auto loaded = BlueprintProjectStore::load(directory.path(), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QVERIFY(loaded->layout.isEmpty());
    BlueprintDocument installed;
    BlueprintScene scene(&installed);
    QVERIFY(scene.resetDocument(loaded->document, loaded->layout, &error));
    QCOMPARE(scene.nodePosition("node-1"), QPointF(0, 0));
    QCOMPARE(scene.nodePosition("node-2"), QPointF(240, 0));
    QVERIFY(write(directory.filePath("layout.json"), QJsonDocument(layout(source)).toJson()));
    loaded = BlueprintProjectStore::load(directory.path(), &error);
    QVERIFY2(loaded.has_value(), qPrintable(error));
    QVERIFY(scene.resetDocument(loaded->document, loaded->layout, &error));
    QCOMPARE(scene.nodePosition("node-1"), QPointF(1, 2));
    QCOMPARE(scene.nodePosition("node-2"), QPointF(240, 0));
}

void BlueprintProjectStoreTest::malformedLayout_data()
{
    QTest::addColumn<QByteArray>("bytes");
    const auto source = document();
    auto object = layout(source);
    QTest::newRow("invalid-json") << QByteArray("{");
    QTest::newRow("non-object") << QByteArray("[]");
    object["schemaVersion"] = 2;
    QTest::newRow("unsupported-version") << QJsonDocument(object).toJson();
    object = layout(source); object["schemaVersion"] = "1";
    QTest::newRow("string-version") << QJsonDocument(object).toJson();
    object = layout(source); object.remove("blueprintSha256");
    QTest::newRow("missing-hash") << QJsonDocument(object).toJson();
    object = layout(source); object["blueprintSha256"] = QString(64, '0');
    QTest::newRow("mismatched-hash") << QJsonDocument(object).toJson();
    object = layout(source); object["positions"] = QJsonObject{};
    QTest::newRow("wrong-positions-type") << QJsonDocument(object).toJson();
    for (const auto &entry : QVector<QPair<QString, QJsonArray>>{
             {"duplicate", {QJsonObject{{"nodeId", "node-1"}, {"x", 1}, {"y", 2}},
                            QJsonObject{{"nodeId", "node-1"}, {"x", 3}, {"y", 4}}}},
             {"unknown", {QJsonObject{{"nodeId", "unknown"}, {"x", 1}, {"y", 2}}}},
             {"null-x", {QJsonObject{{"nodeId", "node-1"}, {"x", QJsonValue::Null}, {"y", 2}}}},
             {"string-y", {QJsonObject{{"nodeId", "node-1"}, {"x", 1}, {"y", "2"}}}},
             {"missing-x", {QJsonObject{{"nodeId", "node-1"}, {"y", 2}}}},
             {"non-object-position", {1}},
         }) {
        object = layout(source); object["positions"] = entry.second;
        QTest::newRow(qPrintable(entry.first)) << QJsonDocument(object).toJson();
    }
    QTest::newRow("overflow-number") << QByteArray("{\"schemaVersion\":1,\"positions\":[{\"nodeId\":\"node-1\",\"x\":1e999,\"y\":0}]}");
}

void BlueprintProjectStoreTest::malformedLayout()
{
    QFETCH(QByteArray, bytes);
    QTemporaryDir directory;
    QVERIFY(write(directory.filePath("blueprint.json"), BlueprintSerializer::toJson(document())));
    QVERIFY(write(directory.filePath("layout.json"), bytes));
    QString error;
    QVERIFY(!BlueprintProjectStore::load(directory.path(), &error));
    QVERIFY(!error.isEmpty());
}

void BlueprintProjectStoreTest::invalidTechnicalDocuments_data()
{
    QTest::addColumn<int>("kind");
    for (int kind = 0; kind < 7; ++kind) QTest::newRow(qPrintable(QString::number(kind))) << kind;
}

void BlueprintProjectStoreTest::invalidTechnicalDocuments()
{
    QFETCH(int, kind);
    auto source = document();
    switch (kind) {
    case 0: source.nodes[1].id = source.nodes[0].id; break;
    case 1: source.nodes[0].id.clear(); break;
    case 2: source.nodes[0].id = " node-1"; break;
    case 3: source.edges.append(source.edges.first()); break;
    case 4: source.edges[0].source = "missing"; break;
    case 5: source.schemaVersion = 2; break;
    case 6: source.nodes[0].type = static_cast<NodeType>(999); break;
    }
    QTemporaryDir directory;
    QString error;
    QVERIFY(!BlueprintProjectStore::save(directory.path(), source, {}, &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(!QFile::exists(directory.filePath("blueprint.json")));
    QVERIFY(write(directory.filePath("blueprint.json"), BlueprintSerializer::toJson(source)));
    QVERIFY(!BlueprintProjectStore::load(directory.path(), &error));
}

void BlueprintProjectStoreTest::invalidPositionsNeverOverwriteExistingFiles()
{
    QTemporaryDir directory;
    QString error;
    QVERIFY(BlueprintProjectStore::save(directory.path(), document(), positions(), &error));
    const auto semantic = read(directory.filePath("blueprint.json"));
    const auto sidecar = read(directory.filePath("layout.json"));
    for (const auto &bad : QVector<QHash<QString, QPointF>>{
             {{"missing", {1, 2}}},
             {{"node-1", {std::numeric_limits<double>::infinity(), 0}}},
             {{"node-1", {0, std::numeric_limits<double>::quiet_NaN()}}}}) {
        QVERIFY(!BlueprintProjectStore::save(directory.path(), document(), bad, &error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(read(directory.filePath("blueprint.json")), semantic);
        QCOMPARE(read(directory.filePath("layout.json")), sidecar);
    }
}

void BlueprintProjectStoreTest::unsafeTargetsAndOversizeFilesAreRejected()
{
    QTemporaryDir directory;
    QString error;
    QVERIFY(!BlueprintProjectStore::save({}, document(), {}, &error));
    QVERIFY(!BlueprintProjectStore::save("relative-project", document(), {}, &error));
    QVERIFY(write(directory.filePath("blueprint.json"), "original"));
    QVERIFY(QDir().mkdir(directory.filePath("layout.json")));
    QVERIFY(!BlueprintProjectStore::save(directory.path(), document(), {}, &error));
    QCOMPARE(read(directory.filePath("blueprint.json")), QByteArray("original"));
    QVERIFY(QDir().rmdir(directory.filePath("layout.json")));
    QFile oversized(directory.filePath("blueprint.json"));
    QVERIFY(oversized.open(QIODevice::WriteOnly));
    QVERIFY(oversized.resize(16 * 1024 * 1024 + 1));
    oversized.close();
    QVERIFY(!BlueprintProjectStore::load(directory.path(), &error));
    QVERIFY(!error.isEmpty());
    QVERIFY(write(directory.filePath("blueprint.json"), BlueprintSerializer::toJson(document())));
    oversized.setFileName(directory.filePath("layout.json"));
    QVERIFY(oversized.open(QIODevice::WriteOnly));
    QVERIFY(oversized.resize(8 * 1024 * 1024 + 1));
    oversized.close();
    QVERIFY(!BlueprintProjectStore::load(directory.path(), &error));
}

void BlueprintProjectStoreTest::protectedWorkspaceLocationsCannotBeSaved()
{
    QTemporaryDir directory;
    const QString workspace = directory.filePath("workspace");
    const QString contracts = QDir(workspace).filePath("generated-project/src/contracts");
    QVERIFY(QDir().mkpath(contracts));
    QVERIFY(write(QDir(workspace).filePath("generation-manifest.json"), "protected manifest"));
    QVERIFY(write(QDir(contracts).filePath("blueprint.json"), "protected contract"));
    QVERIFY(write(QDir(contracts).filePath("source-blueprint.json"), "protected semantic source"));
    QString error;
    QVERIFY(!BlueprintProjectStore::save(contracts, document(), positions(), &error));
    QVERIFY(!BlueprintProjectStore::save(workspace, document(), positions(), &error));
    QCOMPARE(read(QDir(workspace).filePath("generation-manifest.json")), QByteArray("protected manifest"));
    QCOMPARE(read(QDir(contracts).filePath("blueprint.json")), QByteArray("protected contract"));
    QCOMPARE(read(QDir(contracts).filePath("source-blueprint.json")), QByteArray("protected semantic source"));
    QVERIFY(!QFile::exists(QDir(contracts).filePath("layout.json")));
    QVERIFY(QFile::remove(QDir(workspace).filePath("generation-manifest.json")));
    QVERIFY(!BlueprintProjectStore::save(contracts, document(), {}, &error));
    const QString sibling = directory.filePath("blueprint-project");
    QVERIFY(QDir().mkdir(sibling));
    QVERIFY2(BlueprintProjectStore::save(sibling, document(), {}, &error), qPrintable(error));
    const QString exported = directory.filePath("exported");
    QVERIFY(QDir().mkdir(exported));
    QVERIFY(write(QDir(exported).filePath("generation-manifest.json"), "export manifest"));
    QVERIFY(write(QDir(exported).filePath("blueprint.json"), BlueprintSerializer::toJson(document())));
    QVERIFY(BlueprintProjectStore::load(exported, &error));
    QVERIFY(!BlueprintProjectStore::save(exported, document(), positions(), &error));
    QVERIFY(!QFile::exists(QDir(exported).filePath("layout.json")));
}

void BlueprintProjectStoreTest::linksAreRejected()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QTemporaryDir external;
    QVERIFY(write(external.filePath("blueprint.json"), BlueprintSerializer::toJson(document())));
    const QString link = directory.filePath("junction");
    QProcess process;
    process.start("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(link),
                              QDir::toNativeSeparators(external.path())});
    QVERIFY(process.waitForFinished());
    if (process.exitCode() != 0) QSKIP("Windows junction creation unavailable");
    QString error;
    const bool saved = BlueprintProjectStore::save(link, document(), {}, &error);
    const bool loaded = BlueprintProjectStore::load(link, &error).has_value();
    const QByteArray retained = read(external.filePath("blueprint.json"));
    const bool layoutCreated = QFile::exists(external.filePath("layout.json"));
    // Remove only the known junction, never recursively traverse its target.
    const QString native = QDir::toNativeSeparators(link);
    const bool removed = RemoveDirectoryW(reinterpret_cast<LPCWSTR>(native.utf16()));
    QVERIFY(removed);
    QVERIFY(!saved);
    QVERIFY(!loaded);
    QCOMPARE(retained, BlueprintSerializer::toJson(document()));
    QVERIFY(!layoutCreated);
#else
    QTemporaryDir directory;
    QTemporaryDir external;
    const QString target = external.filePath("original.json");
    QVERIFY(write(target, "protected"));
    QVERIFY(QFile::link(target, directory.filePath("blueprint.json")));
    QString error;
    QVERIFY(!BlueprintProjectStore::save(directory.path(), document(), {}, &error));
    QVERIFY(!BlueprintProjectStore::load(directory.path(), &error));
    QCOMPARE(read(target), QByteArray("protected"));
#endif
}

void BlueprintProjectStoreTest::failedCommitRestoresPreviousPair()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QString error;
    QVERIFY(BlueprintProjectStore::save(directory.path(), document(), positions(), &error));
    const auto beforeSemantic = read(directory.filePath("blueprint.json"));
    const auto beforeLayout = read(directory.filePath("layout.json"));
    auto changed = document(); changed.projectName = "Changed document";
    auto moved = positions(); moved["node-1"] = {777, 888};
    for (const auto &name : {QStringLiteral("layout.json"), QStringLiteral("blueprint.json")}) {
        const QString native = QDir::toNativeSeparators(directory.filePath(name));
        const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        QVERIFY(handle != INVALID_HANDLE_VALUE);
        const bool saved = BlueprintProjectStore::save(directory.path(), changed, moved, &error);
        const QString failure = error;
        CloseHandle(handle);
        QVERIFY(!saved);
        QVERIFY(!failure.isEmpty());
        if (name == "blueprint.json") QVERIFY2(failure.contains("restored"), qPrintable(failure));
        QCOMPARE(read(directory.filePath("blueprint.json")), beforeSemantic);
        QCOMPARE(read(directory.filePath("layout.json")), beforeLayout);
        QVERIFY(BlueprintProjectStore::load(directory.path(), &error));
    }
    // A new sidecar must also be removed when the semantic commit fails.
    QVERIFY(QFile::remove(directory.filePath("layout.json")));
    const QString native = QDir::toNativeSeparators(directory.filePath("blueprint.json"));
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                                       FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                       OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(handle != INVALID_HANDLE_VALUE);
    const bool saved = BlueprintProjectStore::save(directory.path(), changed, moved, &error);
    CloseHandle(handle);
    QVERIFY(!saved);
    QVERIFY(!QFile::exists(directory.filePath("layout.json")));
    QCOMPARE(read(directory.filePath("blueprint.json")), beforeSemantic);
#else
    QSKIP("Deterministic deny-rename handles require Windows");
#endif
}

void BlueprintProjectStoreTest::readAndStagingFailuresPreserveExistingFiles()
{
#ifdef Q_OS_WIN
    QTemporaryDir directory;
    QString error;
    QVERIFY(BlueprintProjectStore::save(directory.path(), document(), positions(), &error));
    const auto beforeSemantic = read(directory.filePath("blueprint.json"));
    const auto beforeLayout = read(directory.filePath("layout.json"));
    auto changed = document(); changed.projectName = "Changed";
    const QString native = QDir::toNativeSeparators(directory.filePath("layout.json"));
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(native.utf16()), GENERIC_READ,
                                       0, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    QVERIFY(handle != INVALID_HANDLE_VALUE);
    const bool savedLocked = BlueprintProjectStore::save(directory.path(), changed, positions(), &error);
    const bool loadedLocked = BlueprintProjectStore::load(directory.path(), &error).has_value();
    CloseHandle(handle);
    QVERIFY(!savedLocked);
    QVERIFY(!loadedLocked);
    const DWORD attributes = GetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()));
    QVERIFY(attributes != INVALID_FILE_ATTRIBUTES);
    QVERIFY(SetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()), attributes | FILE_ATTRIBUTE_READONLY));
    const bool savedReadOnly = BlueprintProjectStore::save(directory.path(), changed, positions(), &error);
    const QString failure = error;
    const bool restoredAttributes = SetFileAttributesW(reinterpret_cast<LPCWSTR>(native.utf16()), attributes);
    QVERIFY(restoredAttributes);
    QVERIFY(!savedReadOnly);
    QVERIFY2(failure.contains("stage"), qPrintable(failure));
    QCOMPARE(read(directory.filePath("blueprint.json")), beforeSemantic);
    QCOMPARE(read(directory.filePath("layout.json")), beforeLayout);
#else
    QSKIP("Deterministic file permissions and sharing failures use Windows APIs");
#endif
}

void BlueprintProjectStoreTest::sceneResetClearsHistoryAndConnectionState()
{
    auto source = document();
    BlueprintScene scene(&source);
    auto *stack = scene.undoStack();
    QVERIFY(scene.moveNode("node-1", {20, 30}));
    scene.selectAllItems();
    QVERIFY(scene.beginConnection());
    QVERIFY(scene.chooseConnectionNode("node-1"));
    int notifications = 0;
    scene.setSemanticChangeHandler([&] { ++notifications; });
    QSignalSpy layoutChanges(&scene, &BlueprintScene::layoutChanged);
    auto replacement = document();
    replacement.projectId = "replacement";
    QString error;
    QVERIFY2(scene.resetDocument(replacement, positions(), &error), qPrintable(error));
    QCOMPARE(source, replacement);
    QCOMPARE(scene.layoutSnapshot(), positions());
    QCOMPARE(scene.undoStack(), stack);
    QCOMPARE(stack->count(), 0);
    QVERIFY(stack->isClean());
    QVERIFY(scene.selectedItems().isEmpty());
    QVERIFY(scene.connectionSource().isEmpty());
    QVERIFY(!scene.chooseConnectionNode("node-2"));
    QCOMPARE(scene.items().size(), 3);
    QCOMPARE(notifications, 1);
    QCOMPARE(layoutChanges.count(), 0);
    stack->undo();
    QCOMPARE(source, replacement);
}

void BlueprintProjectStoreTest::sceneResetCancelsAnActivePortGesture()
{
    auto source = document();
    BlueprintScene scene(&source);
    auto *item = scene.nodeItem("node-1");
    const QPointF anchor = item->outputAnchor();
    QGraphicsSceneMouseEvent press(QEvent::GraphicsSceneMousePress);
    press.setButton(Qt::LeftButton);
    press.setButtons(Qt::LeftButton);
    press.setScenePos(anchor);
    press.setPos(item->mapFromScene(anchor));
    QVERIFY(scene.sendEvent(item, &press));
    QCOMPARE(scene.items().size(), 4); // Two nodes, one edge and the active port preview.
    QString error;
    QVERIFY(scene.resetDocument(document(), positions(), &error));
    QCOMPARE(scene.items().size(), 3);
    QVERIFY(scene.mouseGrabberItem() == nullptr);
    QVERIFY(scene.connectionSource().isEmpty());
    QCOMPARE(scene.undoStack()->count(), 0);
    QVERIFY(scene.connectNodes("node-2", "node-1"));
    QCOMPARE(source.edges.size(), 2);
}

void BlueprintProjectStoreTest::rejectedSceneResetPreservesEverything()
{
    auto source = document();
    BlueprintScene scene(&source);
    QVERIFY(scene.moveNode("node-1", {20, 30}));
    scene.selectAllItems();
    scene.beginConnection(); scene.chooseConnectionNode("node-1");
    const auto original = source;
    const auto originalLayout = scene.layoutSnapshot();
    const auto originalItems = scene.items();
    auto bad = document(); bad.nodes.append(bad.nodes.first());
    QString error;
    QVERIFY(!scene.resetDocument(bad, positions(), &error));
    QVERIFY(!scene.resetDocument(document(), {{"unknown", {1, 2}}}, &error));
    QCOMPARE(source, original);
    QCOMPARE(scene.layoutSnapshot(), originalLayout);
    QCOMPARE(scene.items(), originalItems);
    QCOMPARE(scene.undoStack()->count(), 1);
    QCOMPARE(scene.selectedItems().size(), 3);
    QCOMPARE(scene.connectionSource(), QString("node-1"));
    scene.undoStack()->undo();
    QCOMPARE(scene.nodePosition("node-1"), QPointF(0, 0));
}

void BlueprintProjectStoreTest::layoutSignalsAndUndoNeverChangeSemanticIr()
{
    auto source = document();
    const auto original = source;
    const auto ir = IrCompiler::compile(source);
    BlueprintScene scene(&source);
    int semanticChanges = 0;
    scene.setSemanticChangeHandler([&] { ++semanticChanges; });
    QSignalSpy changes(&scene, &BlueprintScene::layoutChanged);
    scene.nodeItem("node-1")->setPos({10, 20});
    QCOMPARE(scene.layoutSnapshot().value("node-1"), QPointF(10, 20));
    QCOMPARE(changes.count(), 1);
    QVERIFY(scene.moveNode("node-1", {30, 40}));
    scene.undoStack()->setClean();
    scene.undoStack()->undo();
    scene.undoStack()->redo();
    QCOMPARE(source, original);
    QCOMPARE(IrCompiler::compile(source), ir);
    QCOMPARE(semanticChanges, 0);
    QVERIFY(changes.count() >= 4);
}

QTEST_MAIN(BlueprintProjectStoreTest)
#include "tst_blueprint_project_store.moc"
