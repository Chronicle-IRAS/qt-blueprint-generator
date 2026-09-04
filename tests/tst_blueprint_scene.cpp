#include <QtTest/QtTest>

#include <QGraphicsView>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QWheelEvent>

#include "app/main_window.h"
#include "blueprint/blueprint_document.h"
#include "editor/edge_item.h"
#include "editor/node_item.h"
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
    void explicitConnectionSelectionPreservesDirection();
    void edgeShowsDirectedLabelAndUsesPortAnchors();
    void deletingNodeRemovesConnectedEdgesAndSupportsUndo();
    void editingNodeTextUpdatesDocumentAndSupportsUndo();
    void editingAllNodeFieldsIsOneUndoableCommand();
    void mainWindowPropertyEditorPreservesAllFields();
    void mainWindowCreatesEveryNodeType();
    void mainWindowUsesExplicitConnectionDirection();
    void mainWindowUsesRubberBandDragAndClampedZoom();
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

void BlueprintSceneTest::explicitConnectionSelectionPreservesDirection()
{
    BlueprintDocument document;
    document.nodes = {
        node(QStringLiteral("source"), QStringLiteral("Source")),
        node(QStringLiteral("target"), QStringLiteral("Target")),
    };
    BlueprintScene scene(&document);

    QVERIFY(scene.beginConnection());
    QVERIFY(scene.chooseConnectionNode(QStringLiteral("source")));
    QCOMPARE(scene.connectionSource(), QStringLiteral("source"));
    QVERIFY(scene.chooseConnectionNode(QStringLiteral("target")));
    QCOMPARE(document.edges.size(), 1);
    QCOMPARE(document.edges.constFirst().source, QStringLiteral("source"));
    QCOMPARE(document.edges.constFirst().target, QStringLiteral("target"));
    QVERIFY(scene.connectionSource().isEmpty());
}

void BlueprintSceneTest::edgeShowsDirectedLabelAndUsesPortAnchors()
{
    BlueprintDocument document;
    BlueprintNode source = node(QStringLiteral("source"), QStringLiteral("Source"));
    source.outputs = {{QStringLiteral("result"), QStringLiteral("bool"), QStringLiteral("Decision result")}};
    BlueprintNode target = node(QStringLiteral("target"), QStringLiteral("Target"));
    target.inputs = {{QStringLiteral("input"), QStringLiteral("bool"), QStringLiteral("Incoming value")}};
    document.nodes = {source, target};
    BlueprintScene scene(&document);

    QVERIFY(scene.connectNodes(QStringLiteral("source"), QStringLiteral("target"), QStringLiteral("yes")));
    const BlueprintEdge &edge = document.edges.constFirst();
    EdgeItem *edgeItem = scene.edgeItem(edge.id);
    QVERIFY(edgeItem != nullptr);
    QCOMPARE(edgeItem->sourceId(), QStringLiteral("source"));
    QCOMPARE(edgeItem->targetId(), QStringLiteral("target"));
    QCOMPARE(edgeItem->label(), QStringLiteral("yes"));
    QCOMPARE(edgeItem->path().pointAtPercent(0), scene.nodeItem(QStringLiteral("source"))->outputAnchor());
    QCOMPARE(edgeItem->path().pointAtPercent(1), scene.nodeItem(QStringLiteral("target"))->inputAnchor());
    QVERIFY(edgeItem->boundingRect().contains(edgeItem->labelPosition()));
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

void BlueprintSceneTest::editingAllNodeFieldsIsOneUndoableCommand()
{
    BlueprintDocument document;
    BlueprintNode original = node(QStringLiteral("first"), QStringLiteral("First"));
    original.description = QStringLiteral("Before");
    original.inputs = {{QStringLiteral("old-in"), QStringLiteral("string"), QStringLiteral("Old input")}};
    original.constraints = {QStringLiteral("old constraint")};
    document.nodes = {original};
    BlueprintScene scene(&document);

    BlueprintNode edited = original;
    edited.name = QStringLiteral("Renamed");
    edited.description = QStringLiteral("After");
    edited.inputs = {{QStringLiteral("new-in"), QStringLiteral("int"), QStringLiteral("New input")}};
    edited.outputs = {{QStringLiteral("result"), QStringLiteral("bool"), QStringLiteral("Result")}};
    edited.constraints = {QStringLiteral("new constraint")};
    edited.acceptanceCriteria = {QStringLiteral("criterion one"), QStringLiteral("criterion two")};

    QVERIFY(scene.editNode(QStringLiteral("first"), edited));
    QCOMPARE(document.nodes.constFirst(), edited);
    QCOMPARE(scene.nodeItem(QStringLiteral("first"))->node(), edited);
    QCOMPARE(scene.undoStack()->count(), 1);
    scene.undoStack()->undo();
    QCOMPARE(document.nodes.constFirst(), original);
    QCOMPARE(scene.nodeItem(QStringLiteral("first"))->node(), original);
    scene.undoStack()->redo();
    QCOMPARE(document.nodes.constFirst(), edited);
    QCOMPARE(scene.nodeItem(QStringLiteral("first"))->node(), edited);
}

void BlueprintSceneTest::mainWindowPropertyEditorPreservesAllFields()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    const QString id = window.document().nodes.constFirst().id;
    auto *nameEdit = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
    auto *descriptionEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("nodeDescriptionEdit"));
    auto *inputsEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("nodeInputsEdit"));
    auto *outputsEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("nodeOutputsEdit"));
    auto *constraintsEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("nodeConstraintsEdit"));
    auto *criteriaEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("nodeAcceptanceCriteriaEdit"));
    auto *applyButton = window.findChild<QPushButton *>(QStringLiteral("applyNodePropertiesButton"));
    QVERIFY(nameEdit && descriptionEdit && inputsEdit && outputsEdit && constraintsEdit && criteriaEdit && applyButton);
    const int commandsBeforeEdit = window.scene()->undoStack()->count();

    nameEdit->setText(QStringLiteral("Configured"));
    descriptionEdit->setPlainText(QStringLiteral("Detailed description"));
    inputsEdit->setPlainText(QStringLiteral("[{\"name\":\"payload\",\"type\":\"json\",\"description\":\"Request body\"}]"));
    outputsEdit->setPlainText(QStringLiteral("[{\"name\":\"response\",\"type\":\"json\",\"description\":\"Response body\"}]"));
    constraintsEdit->setPlainText(QStringLiteral("[\"authenticated\",\"rate limited\"]"));
    criteriaEdit->setPlainText(QStringLiteral("[\"returns 200\",\"returns error\"]"));
    QTest::mouseClick(applyButton, Qt::LeftButton);

    BlueprintNode expected = window.document().nodes.constFirst();
    QCOMPARE(expected.id, id);
    QCOMPARE(expected.name, QStringLiteral("Configured"));
    QCOMPARE(expected.inputs.constFirst().description, QStringLiteral("Request body"));
    QCOMPARE(expected.outputs.constFirst().type, QStringLiteral("json"));
    QCOMPARE(expected.constraints, QStringList({QStringLiteral("authenticated"), QStringLiteral("rate limited")}));
    QCOMPARE(expected.acceptanceCriteria, QStringList({QStringLiteral("returns 200"), QStringLiteral("returns error")}));
    QCOMPARE(window.scene()->undoStack()->count(), commandsBeforeEdit + 1);
    window.scene()->undoStack()->undo();
    QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("Logic Module"));
    window.scene()->undoStack()->redo();
    QCOMPARE(window.scene()->nodeItem(id)->node(), expected);
}

void BlueprintSceneTest::mainWindowCreatesEveryNodeType()
{
    MainWindow window;
    const QVector<NodeType> types{
        NodeType::Start,
        NodeType::End,
        NodeType::UiPage,
        NodeType::LogicModule,
        NodeType::Decision,
        NodeType::ExternalCode,
    };

    for (const NodeType type : types) {
        QVERIFY(window.addNodeOfType(type));
    }

    QCOMPARE(window.document().nodes.size(), types.size());
    for (qsizetype index = 0; index < types.size(); ++index) {
        QCOMPARE(window.document().nodes.at(index).type, types.at(index));
    }
}

void BlueprintSceneTest::mainWindowUsesExplicitConnectionDirection()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    const QString source = window.document().nodes.at(0).id;
    const QString target = window.document().nodes.at(1).id;

    QVERIFY(window.scene()->beginConnection());
    QGraphicsView *view = window.graphicsView();
    const QPoint sourcePoint = view->mapFromScene(window.scene()->nodeItem(source)->sceneBoundingRect().center());
    const QPoint targetPoint = view->mapFromScene(window.scene()->nodeItem(target)->sceneBoundingRect().center());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QCOMPARE(window.scene()->connectionSource(), source);
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    QCOMPARE(window.document().edges.size(), 1);
    QCOMPARE(window.document().edges.constFirst().source, source);
    QCOMPARE(window.document().edges.constFirst().target, target);
}

void BlueprintSceneTest::mainWindowUsesRubberBandDragAndClampedZoom()
{
    MainWindow window;
    QGraphicsView *view = window.graphicsView();
    QCOMPARE(view->dragMode(), QGraphicsView::RubberBandDrag);
    const qreal initialZoom = view->transform().m11();

    for (int i = 0; i < 100; ++i) {
        QWheelEvent event(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, -120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &event);
    }
    QVERIFY(view->transform().m11() >= 0.25);
    QVERIFY(view->transform().m11() <= 3.0);
    QVERIFY(view->transform().m11() < initialZoom);

    for (int i = 0; i < 100; ++i) {
        QWheelEvent event(QPointF(10, 10), QPointF(10, 10), QPoint(), QPoint(0, 120),
                          Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view->viewport(), &event);
    }
    QVERIFY(view->transform().m11() >= 0.25);
    QVERIFY(view->transform().m11() <= 3.0);
}

QTEST_MAIN(BlueprintSceneTest)

#include "tst_blueprint_scene.moc"
