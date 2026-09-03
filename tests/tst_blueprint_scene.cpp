#include <QtTest/QtTest>

#include "blueprint/blueprint_document.h"
#include "editor/blueprint_scene.h"

namespace {

BlueprintNode node(const QString &id, const QString &name)
{
    BlueprintNode result;
    result.id = id;
    result.type = NodeType::LogicModule;
    result.name = name;
    return result;
}

} // namespace

class BlueprintSceneTest : public QObject
{
    Q_OBJECT

private slots:
    void addingNodeUpdatesDocumentAndSupportsUndo();
    void movingNodeUpdatesLayoutWithoutChangingSemanticDocument();
    void connectingNodesUpdatesDocumentAndSupportsUndo();
    void deletingNodeRemovesConnectedEdgesAndSupportsUndo();
    void editingNodeTextUpdatesDocumentAndSupportsUndo();
};

void BlueprintSceneTest::addingNodeUpdatesDocumentAndSupportsUndo()
{
    BlueprintDocument document;
    BlueprintScene scene(&document);

    QVERIFY(scene.addNode(node(QStringLiteral("first"), QStringLiteral("First")), QPointF(25, 40)));
    QCOMPARE(document.nodes.size(), 1);
    QCOMPARE(document.nodes.constFirst().id, QStringLiteral("first"));
    QCOMPARE(scene.nodePosition(QStringLiteral("first")), QPointF(25, 40));

    scene.undoStack()->undo();
    QVERIFY(document.nodes.isEmpty());
    QVERIFY(scene.nodeItem(QStringLiteral("first")) == nullptr);

    scene.undoStack()->redo();
    QCOMPARE(document.nodes.size(), 1);
    QCOMPARE(scene.nodePosition(QStringLiteral("first")), QPointF(25, 40));
}

void BlueprintSceneTest::movingNodeUpdatesLayoutWithoutChangingSemanticDocument()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("first"), QStringLiteral("First"))};
    const BlueprintDocument beforeMove = document;
    BlueprintScene scene(&document);

    QVERIFY(scene.moveNode(QStringLiteral("first"), QPointF(120, 80)));
    QCOMPARE(scene.nodePosition(QStringLiteral("first")), QPointF(120, 80));
    QCOMPARE(document, beforeMove);

    scene.undoStack()->undo();
    QCOMPARE(scene.nodePosition(QStringLiteral("first")), QPointF(0, 0));
    QCOMPARE(document, beforeMove);
}

void BlueprintSceneTest::connectingNodesUpdatesDocumentAndSupportsUndo()
{
    BlueprintDocument document;
    document.nodes = {
        node(QStringLiteral("source"), QStringLiteral("Source")),
        node(QStringLiteral("target"), QStringLiteral("Target")),
    };
    BlueprintScene scene(&document);

    QVERIFY(scene.connectNodes(QStringLiteral("source"), QStringLiteral("target"), QStringLiteral("next")));
    QCOMPARE(document.edges.size(), 1);
    QCOMPARE(document.edges.constFirst().source, QStringLiteral("source"));
    QCOMPARE(document.edges.constFirst().target, QStringLiteral("target"));
    QCOMPARE(document.edges.constFirst().label, QStringLiteral("next"));
    QVERIFY(scene.edgeItem(document.edges.constFirst().id) != nullptr);

    scene.undoStack()->undo();
    QVERIFY(document.edges.isEmpty());
    scene.undoStack()->redo();
    QCOMPARE(document.edges.size(), 1);
}

void BlueprintSceneTest::deletingNodeRemovesConnectedEdgesAndSupportsUndo()
{
    BlueprintDocument document;
    document.nodes = {
        node(QStringLiteral("source"), QStringLiteral("Source")),
        node(QStringLiteral("target"), QStringLiteral("Target")),
    };
    document.edges = {{QStringLiteral("flow"), QStringLiteral("source"), QStringLiteral("target"), {}}};
    BlueprintScene scene(&document);

    QVERIFY(scene.deleteNode(QStringLiteral("source")));
    QCOMPARE(document.nodes.size(), 1);
    QCOMPARE(document.nodes.constFirst().id, QStringLiteral("target"));
    QVERIFY(document.edges.isEmpty());

    scene.undoStack()->undo();
    QCOMPARE(document.nodes.size(), 2);
    QCOMPARE(document.nodes.constFirst().id, QStringLiteral("source"));
    QCOMPARE(document.edges.size(), 1);
    QVERIFY(scene.edgeItem(QStringLiteral("flow")) != nullptr);
}

void BlueprintSceneTest::editingNodeTextUpdatesDocumentAndSupportsUndo()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("first"), QStringLiteral("First"))};
    BlueprintScene scene(&document);

    QVERIFY(scene.editNodeText(QStringLiteral("first"), QStringLiteral("Renamed"),
                               QStringLiteral("Updated description")));
    QCOMPARE(document.nodes.constFirst().name, QStringLiteral("Renamed"));
    QCOMPARE(document.nodes.constFirst().description, QStringLiteral("Updated description"));

    scene.undoStack()->undo();
    QCOMPARE(document.nodes.constFirst().name, QStringLiteral("First"));
}

QTEST_MAIN(BlueprintSceneTest)

#include "tst_blueprint_scene.moc"
