#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialog>
#include <QDockWidget>
#include <QGraphicsPathItem>
#include <QGraphicsView>
#include <QHash>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QStatusBar>
#include <QTableWidget>
#include <QTimer>
#include <QToolBar>
#include <QWheelEvent>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <functional>

#include "app/main_window.h"
#include "blueprint/blueprint_document.h"
#include "editor/edge_item.h"
#include "editor/node_item.h"
#include "editor/blueprint_scene.h"
#include "editor/node_properties_editor.h"

namespace {

BlueprintNode node(const QString &id, const QString &name)
{
    BlueprintNode result;
    result.id = id;
    result.type = NodeType::LogicModule;
    result.name = name;
    return result;
}

QGraphicsPathItem *portConnectionPreview(const BlueprintScene &scene)
{
    for (QGraphicsItem *item : scene.items()) {
        if (item->data(0).toString() == QStringLiteral("portConnectionPreview")) {
            return dynamic_cast<QGraphicsPathItem *>(item);
        }
    }
    return nullptr;
}

void dragPort(QGraphicsView *view, const QPointF &source, const QPointF &target)
{
    const QPoint sourcePoint = view->mapFromScene(source);
    const QPoint targetPoint = view->mapFromScene(target);
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QTest::mouseMove(view->viewport(), targetPoint, 20);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
}

struct ContextMenuProbe {
    bool opened = false;
    QStringList entries;
    QList<QAction *> actions;
    QHash<QString, QStringList> submenus;
};

QString entryLabel(QAction *action)
{
    return action->isEnabled() ? action->text() : action->text() + QStringLiteral("|disabled");
}

// Opens the widget's real context menu, records it, optionally activates an entry while it
// is open and closes it again.
ContextMenuProbe probeContextMenu(QWidget *widget, const QPoint &position,
                                  const std::function<void(QMenu *)> &interact = {})
{
    ContextMenuProbe probe;
    QTimer::singleShot(0, [&probe, &interact] {
        auto *menu = qobject_cast<QMenu *>(QApplication::activePopupWidget());
        if (!menu) {
            return;
        }
        probe.opened = true;
        probe.actions = menu->actions();
        for (QAction *action : menu->actions()) {
            if (action->isSeparator()) {
                probe.entries.append(QStringLiteral("---"));
                continue;
            }
            probe.entries.append(entryLabel(action));
            if (QMenu *submenu = action->menu()) {
                QStringList submenuEntries;
                for (QAction *submenuAction : submenu->actions()) {
                    submenuEntries.append(entryLabel(submenuAction));
                }
                probe.submenus.insert(action->text(), submenuEntries);
            }
        }
        if (interact) {
            interact(menu);
        }
        menu->close();
    });
    QContextMenuEvent event(QContextMenuEvent::Mouse, position, widget->mapToGlobal(position));
    QApplication::sendEvent(widget, &event);
    return probe;
}

void triggerMenuAction(QMenu *menu, const QString &objectName)
{
    for (QAction *action : menu->actions()) {
        if (action->objectName() == objectName) {
            action->trigger();
            return;
        }
    }
}

void triggerAddNodeEntry(QMenu *menu)
{
    for (QAction *action : menu->actions()) {
        QMenu *submenu = action->menu();
        if (!submenu || submenu->objectName() != QStringLiteral("canvasAddNodeMenu")
            || submenu->actions().isEmpty()) {
            continue;
        }
        submenu->actions().constFirst()->trigger();
        return;
    }
}

// First viewport point without a canvas item, so tests do not hard-code the window layout.
QPoint blankViewportPoint(QGraphicsView *view, const BlueprintScene &scene)
{
    for (int y = 24; y < view->viewport()->height() - 8; y += 16) {
        for (int x = 24; x < view->viewport()->width() - 8; x += 16) {
            const QPoint point(x, y);
            if (!scene.itemAt(view->mapToScene(point), QTransform())) {
                return point;
            }
        }
    }
    return {-1, -1};
}

} // namespace

class BlueprintSceneTest : public QObject
{
    Q_OBJECT

private slots:
    void canvasPolishPreservesGeometryAndFullText();
    void nodeCardSurfacesTypeWithoutChangingGeometry();
    void canvasGridRendersWithoutItemsAtDifferentZooms();
    void addingNodeUpdatesDocumentAndSupportsUndo();
    void movingNodeUpdatesLayoutWithoutChangingSemanticDocument();
    void connectingNodesUpdatesDocumentAndSupportsUndo();
    void explicitConnectionSelectionPreservesDirection();
    void edgeShowsDirectedLabelAndUsesPortAnchors();
    void portDragShowsPreviewAndCreatesUndoableEdge();
    void invalidAndCancelledPortDragsDoNotMutateDocument();
    void portDragPreservesDecisionLabelsAndConnectAction();
    void editingPortsUpdatesIncidentEdgeAnchorsAndUndo();
    void deletingNodeRemovesConnectedEdgesAndSupportsUndo();
    void multiSelectionDragHasOneCoherentUndo();
    void modifierMultiSelectionDragHasOneCoherentUndo();
    void sceneRejectsMalformedTechnicalDocuments();
    void rejectedScenesAreReadOnly();
    void connectionStateCanBeCancelledAndSelfLoopsAreRejected();
    void connectionModeDoubleClickDoesNotAlsoRequestEditing();
    void graphicsBoundsContainPaintedPortAndArrowExtents();
    void editingNodeTextUpdatesDocumentAndSupportsUndo();
    void editingAllNodeFieldsIsOneUndoableCommand();
    void directNodeEditorSaveSynchronizesDocumentCanvasInspectorAndUndo();
    void directNodeEditorCancelDoesNotMutateDocument();
    void mainWindowPropertyEditorPreservesAllFields();
    void propertyDockIsDisabledForMultiSelection();
    void propertyDraftSurvivesLayoutAndStructuredRowsApply();
    void connectionToolbarCanBeCancelled();
    void propertyDockTracksUndoRedoAndDoesNotReapplyStaleValues();
    void mainWindowCreatesEveryNodeType();
    void mainWindowUsesExplicitConnectionDirection();
    void mainWindowCreatesLabeledConnectionsFromToolbar();
    void mainWindowUsesRubberBandDragAndClampedZoom();
    void mainWindowRestoresClosedDocksAndDefaultLayout();
    void mainWindowExposesBuildAndExportInToolbar();
    void keyboardDeleteRemovesSelectedNodesAndIncidentEdges();
    void keyboardDeleteRemovesEverySelectedNode();
    void clickingAnEdgeSelectsItAndDeleteKeepsItsNodes();
    void keyboardDeleteHandlesMixedNodeAndEdgeSelection();
    void deleteKeyInsideTextEditorsDoesNotDeleteCanvasItems();
    void toolbarDeleteSharesTheKeyboardDeletionPath();
    void canvasContextMenuOffersCanvasEntries();
    void canvasContextMenuAddsNodeAtRightClickPosition();
    void nodeContextMenuFollowsGenerateEligibility();
    void nodeContextMenuKeepsTheCurrentSelectionForDelete();
    void edgeContextMenuDeletesOnlyThatEdge();
    void selectAllCanvasEntriesSelectNodesAndEdges();
    void fitViewBringsEveryNodeIntoTheViewport();
    void fitViewKeepsTheZoomLimit();
    void resetViewRestoresTheInitialTransform();
};

void BlueprintSceneTest::canvasPolishPreservesGeometryAndFullText()
{
    BlueprintDocument document;
    BlueprintScene scene(&document);
    auto draft = node(QStringLiteral("long"), QString(100, QLatin1Char('W')));
    draft.description = QStringLiteral("Full description");
    draft.inputs = {{QString(80, QLatin1Char('I')), {}, {}}};
    QVERIFY(scene.addNode(draft, {}));
    NodeItem *item = scene.nodeItem(draft.id);
    QVERIFY(item->toolTip().contains(draft.name));
    QVERIFY(item->toolTip().contains(draft.description));
    QVERIFY(item->toolTip().contains(draft.inputs.first().name));
    QVERIFY(item->acceptHoverEvents());
    QCOMPARE(item->boundingRect(), QRectF(-6, -6, 192, 108));
    QCOMPARE(item->outputAnchor(), QPointF(180, 48));
    auto render = [&](QStyle::State state) {
        QImage image(200, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.translate(10, 10);
        QStyleOptionGraphicsItem option;
        option.state = state;
        item->paint(&painter, &option, nullptr);
        return image;
    };
    QVERIFY(render(QStyle::State_None) != render(QStyle::State_MouseOver));
    QVERIFY(render(QStyle::State_None) != render(QStyle::State_Selected));
    QVERIFY(scene.addNode(node(QStringLiteral("target"), QStringLiteral("Target")), {300, 0}));
    const QString label(100, QLatin1Char('L'));
    QVERIFY(scene.connectNodes(draft.id, QStringLiteral("target"), label));
    QCOMPARE(scene.edgeItem(document.edges.first().id)->toolTip(), label);
}

void BlueprintSceneTest::nodeCardSurfacesTypeWithoutChangingGeometry()
{
    BlueprintDocument document;
    BlueprintScene scene(&document);
    BlueprintNode login = node(QStringLiteral("login"), QStringLiteral("LoginService"));
    login.type = NodeType::LogicModule;
    QVERIFY(scene.addNode(login, {}));
    NodeItem *loginItem = scene.nodeItem(login.id);
    QVERIFY(loginItem);
    QVERIFY(loginItem->toolTip().contains(QStringLiteral("Type: Logic Module")));
    QCOMPARE(loginItem->boundingRect(), QRectF(-6, -6, 192, 108));
    QCOMPARE(loginItem->outputAnchor(0), QPointF(180, 48));

    BlueprintNode decision = node(QStringLiteral("decision"), QStringLiteral("LoginService"));
    decision.type = NodeType::Decision;
    QVERIFY(scene.addNode(decision, QPointF(300, 0)));
    NodeItem *decisionItem = scene.nodeItem(decision.id);
    QVERIFY(decisionItem);
    QVERIFY(decisionItem->toolTip().contains(QStringLiteral("Type: Decision")));
    QCOMPARE(decisionItem->boundingRect(), QRectF(-6, -6, 192, 108));
    QCOMPARE(decisionItem->outputAnchor(0) - decisionItem->pos(), QPointF(180, 48));

    auto render = [](NodeItem *target) {
        QImage image(200, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.translate(10, 10);
        QStyleOptionGraphicsItem option;
        option.state = QStyle::State_None;
        target->paint(&painter, &option, nullptr);
        return image;
    };
    QVERIFY(render(loginItem) != render(decisionItem));
}

void BlueprintSceneTest::canvasGridRendersWithoutItemsAtDifferentZooms()
{
    BlueprintDocument document;
    BlueprintScene scene(&document);
    for (qreal extent : {256.0, 1024.0, 1000000.0}) {
        QImage image(256, 256, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        scene.render(&painter, QRectF(0, 0, 256, 256), QRectF(0, 0, extent, extent));
        painter.end();
        QVERIFY(image.pixelColor(10, 10).alpha() > 0);
        bool variation = false;
        for (int x = 1; x < 256; ++x)
            variation |= image.pixelColor(x, 10) != image.pixelColor(0, 10);
        QVERIFY(variation);
        QVERIFY(scene.items().isEmpty());
    }
}

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

void BlueprintSceneTest::portDragShowsPreviewAndCreatesUndoableEdge()
{
    BlueprintDocument document;
    BlueprintNode source = node(QStringLiteral("source"), QStringLiteral("Source"));
    source.outputs = {{QStringLiteral("result"), QStringLiteral("bool"), {}}};
    BlueprintNode target = node(QStringLiteral("target"), QStringLiteral("Target"));
    target.inputs = {{QStringLiteral("value"), QStringLiteral("bool"), {}}};
    document.nodes = {source, target};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.resize(800, 500);
    view.show();
    QApplication::processEvents();
    NodeItem *sourceItem = scene.nodeItem(source.id);
    NodeItem *targetItem = scene.nodeItem(target.id);
    const int commandsBefore = scene.undoStack()->count();
    const QPoint sourcePoint = view.mapFromScene(sourceItem->outputAnchor(0));
    const QPointF middleScene = (sourceItem->outputAnchor(0) + targetItem->inputAnchor(0)) / 2.0;

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QTest::mouseMove(view.viewport(), view.mapFromScene(middleScene), 20);
    QGraphicsPathItem *preview = portConnectionPreview(scene);
    QVERIFY(preview);
    QVERIFY(QLineF(preview->path().currentPosition(), middleScene).length() < 3.0);

    QTest::mouseMove(view.viewport(), view.mapFromScene(targetItem->inputAnchor(0)), 20);
    QCOMPARE(targetItem->highlightedInputPort(), 0);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        view.mapFromScene(targetItem->inputAnchor(0)));

    QVERIFY(portConnectionPreview(scene) == nullptr);
    QCOMPARE(targetItem->highlightedInputPort(), -1);
    QCOMPARE(document.edges.size(), 1);
    QCOMPARE(document.edges.constFirst().source, source.id);
    QCOMPARE(document.edges.constFirst().target, target.id);
    QCOMPARE(scene.undoStack()->count(), commandsBefore + 1);
    scene.undoStack()->undo();
    QVERIFY(document.edges.isEmpty());
    scene.undoStack()->redo();
    QCOMPARE(document.edges.size(), 1);
    QCOMPARE(scene.edgeItem(document.edges.constFirst().id)->path().pointAtPercent(0.0),
             sourceItem->outputAnchor());

    const QPointF oldEnd = scene.edgeItem(document.edges.constFirst().id)->path().pointAtPercent(1.0);
    QVERIFY(scene.moveNode(target.id, targetItem->pos() + QPointF(80.0, 40.0)));
    QCOMPARE(scene.edgeItem(document.edges.constFirst().id)->path().pointAtPercent(1.0),
             targetItem->inputAnchor());
    QVERIFY(scene.edgeItem(document.edges.constFirst().id)->path().pointAtPercent(1.0) != oldEnd);
}

void BlueprintSceneTest::invalidAndCancelledPortDragsDoNotMutateDocument()
{
    BlueprintDocument document;
    BlueprintNode source = node(QStringLiteral("source"), QStringLiteral("Source"));
    source.outputs = {{QStringLiteral("result"), QStringLiteral("bool"), {}}};
    BlueprintNode target = node(QStringLiteral("target"), QStringLiteral("Target"));
    target.inputs = {{QStringLiteral("value"), QStringLiteral("bool"), {}}};
    document.nodes = {source, target};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.resize(800, 500);
    view.show();
    view.setFocus();
    QApplication::processEvents();
    NodeItem *sourceItem = scene.nodeItem(source.id);
    NodeItem *targetItem = scene.nodeItem(target.id);
    QVERIFY(scene.moveNode(source.id, QPointF(120.0, 80.0)));
    scene.undoStack()->clear();
    const BlueprintDocument before = document;
    const int commandsBefore = scene.undoStack()->count();
    const QPoint sourcePoint = view.mapFromScene(sourceItem->outputAnchor(0));
    const QPoint invalidPoint = view.mapFromScene(QPointF(500.0, 300.0));

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QTest::mouseMove(view.viewport(), invalidPoint, 20);
    QVERIFY(portConnectionPreview(scene));
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, invalidPoint);
    QCOMPARE(document, before);
    QCOMPARE(scene.undoStack()->count(), commandsBefore);
    QVERIFY(portConnectionPreview(scene) == nullptr);

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QTest::mouseMove(view.viewport(), view.mapFromScene(targetItem->inputAnchor(0)), 20);
    QCOMPARE(targetItem->highlightedInputPort(), 0);
    QTest::keyClick(view.viewport(), Qt::Key_Escape);
    QVERIFY(portConnectionPreview(scene) == nullptr);
    QCOMPARE(targetItem->highlightedInputPort(), -1);
    QTest::mouseMove(view.viewport(), invalidPoint, 20);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        invalidPoint);
    QCOMPARE(document, before);
    QCOMPARE(sourceItem->pos(), QPointF(120.0, 80.0));
    QCOMPARE(scene.undoStack()->count(), commandsBefore);

    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QTest::mouseMove(view.viewport(), view.mapFromScene(targetItem->inputAnchor(0)), 20);
    QCOMPARE(targetItem->highlightedInputPort(), 0);
    QTest::mouseClick(view.viewport(), Qt::RightButton, Qt::NoModifier,
                      view.mapFromScene(targetItem->inputAnchor(0)));
    QVERIFY(portConnectionPreview(scene) == nullptr);
    QCOMPARE(targetItem->highlightedInputPort(), -1);
    QTest::mouseMove(view.viewport(), invalidPoint, 20);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                        invalidPoint);
    QCOMPARE(document, before);
    QCOMPARE(sourceItem->pos(), QPointF(120.0, 80.0));
    QCOMPARE(scene.undoStack()->count(), commandsBefore);
}

void BlueprintSceneTest::portDragPreservesDecisionLabelsAndConnectAction()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Decision));
    QVERIFY(window.addNodeOfType(NodeType::End));
    QVERIFY(window.addNodeOfType(NodeType::End));
    window.show();
    QApplication::processEvents();
    auto *labelEdit = window.findChild<QLineEdit *>(QStringLiteral("connectionLabelEdit"));
    auto *connectAction = window.findChild<QAction *>(QStringLiteral("beginConnectionAction"));
    QVERIFY(labelEdit && connectAction);
    QGraphicsView *view = window.graphicsView();
    NodeItem *decision = window.scene()->nodeItem(window.document().nodes.at(0).id);
    NodeItem *trueTarget = window.scene()->nodeItem(window.document().nodes.at(1).id);
    NodeItem *falseTarget = window.scene()->nodeItem(window.document().nodes.at(2).id);

    labelEdit->setText(QStringLiteral("true"));
    dragPort(view, decision->outputAnchor(), trueTarget->inputAnchor());
    labelEdit->setText(QStringLiteral("false"));
    dragPort(view, decision->outputAnchor(), falseTarget->inputAnchor());
    QCOMPARE(window.document().edges.size(), 2);
    QCOMPARE(window.document().edges.at(0).label, QStringLiteral("true"));
    QCOMPARE(window.document().edges.at(1).label, QStringLiteral("false"));

    connectAction->trigger();
    dragPort(view, decision->outputAnchor(), trueTarget->inputAnchor());
    QCOMPARE(window.document().edges.size(), 3);
    QVERIFY(window.scene()->connectionSource().isEmpty());
    QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                      view->mapFromScene(falseTarget->sceneBoundingRect().center()));
    QVERIFY(window.scene()->connectionSource().isEmpty());
    QCOMPARE(window.document().edges.size(), 3);

    const QPoint outputPoint = view->mapFromScene(decision->outputAnchor());
    const QPointF middle = (decision->outputAnchor() + falseTarget->inputAnchor()) / 2.0;
    QTest::mousePress(view->viewport(), Qt::LeftButton, Qt::NoModifier, outputPoint);
    QTest::mouseMove(view->viewport(), view->mapFromScene(middle), 20);
    QVERIFY(portConnectionPreview(*window.scene()));
    connectAction->trigger();
    QVERIFY(portConnectionPreview(*window.scene()) == nullptr);
    QTest::mouseRelease(view->viewport(), Qt::LeftButton, Qt::NoModifier,
                        view->mapFromScene(middle));

    QVERIFY(window.scene()->chooseConnectionNode(trueTarget->nodeId()));
    QVERIFY(window.scene()->chooseConnectionNode(falseTarget->nodeId()));
    QCOMPARE(window.document().edges.size(), 4);
}

void BlueprintSceneTest::editingPortsUpdatesIncidentEdgeAnchorsAndUndo()
{
    BlueprintDocument document;
    BlueprintNode source = node(QStringLiteral("source"), QStringLiteral("Source"));
    source.outputs = {{QStringLiteral("result"), QStringLiteral("bool"), QStringLiteral("Result")}};
    BlueprintNode target = node(QStringLiteral("target"), QStringLiteral("Target"));
    target.inputs = {{QStringLiteral("value"), QStringLiteral("bool"), QStringLiteral("Value")}};
    document.nodes = {source, target};
    BlueprintScene scene(&document);
    QVERIFY(scene.connectNodes(QStringLiteral("source"), QStringLiteral("target")));
    const QString edgeId = document.edges.constFirst().id;
    const QPointF originalStart = scene.edgeItem(edgeId)->path().pointAtPercent(0.0);

    BlueprintNode edited = source;
    edited.outputs = {{QStringLiteral("first"), QStringLiteral("bool"), QStringLiteral("First")},
                      {QStringLiteral("second"), QStringLiteral("bool"), QStringLiteral("Second")}};
    QVERIFY(scene.editNode(QStringLiteral("source"), edited));
    const QPointF editedStart = scene.edgeItem(edgeId)->path().pointAtPercent(0.0);
    QVERIFY(editedStart != originalStart);
    QCOMPARE(editedStart, scene.nodeItem(QStringLiteral("source"))->outputAnchor());

    scene.undoStack()->undo();
    QCOMPARE(scene.edgeItem(edgeId)->path().pointAtPercent(0.0), originalStart);
    scene.undoStack()->redo();
    QCOMPARE(scene.edgeItem(edgeId)->path().pointAtPercent(0.0), editedStart);
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

void BlueprintSceneTest::multiSelectionDragHasOneCoherentUndo()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("first"), QStringLiteral("First")),
                      node(QStringLiteral("second"), QStringLiteral("Second"))};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.setDragMode(QGraphicsView::RubberBandDrag);
    view.resize(800, 500);
    view.show();
    QApplication::processEvents();
    NodeItem *first = scene.nodeItem(QStringLiteral("first"));
    NodeItem *second = scene.nodeItem(QStringLiteral("second"));
    first->setSelected(true);
    second->setSelected(true);
    const QPointF firstBefore = first->pos();
    const QPointF secondBefore = second->pos();
    const QPoint start = view.mapFromScene(first->sceneBoundingRect().center());
    QTest::mousePress(view.viewport(), Qt::LeftButton, Qt::NoModifier, start);
    QTest::mouseMove(view.viewport(), start + QPoint(60, 40), 20);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, start + QPoint(60, 40));
    QVERIFY(first->pos() != firstBefore);
    QCOMPARE(second->pos(), secondBefore);

    scene.undoStack()->undo();
    QCOMPARE(first->pos(), firstBefore);
    QCOMPARE(second->pos(), secondBefore);
}

void BlueprintSceneTest::modifierMultiSelectionDragHasOneCoherentUndo()
{
    const auto verifyDrag = [](Qt::KeyboardModifier modifier, Qt::Key key) {
        BlueprintDocument document;
        document.nodes = {node(QStringLiteral("first"), QStringLiteral("First")),
                          node(QStringLiteral("second"), QStringLiteral("Second"))};
        BlueprintScene scene(&document);
        QGraphicsView view(&scene);
        view.setDragMode(QGraphicsView::RubberBandDrag);
        view.resize(800, 500);
        view.show();
        QApplication::processEvents();
        NodeItem *first = scene.nodeItem(QStringLiteral("first"));
        NodeItem *second = scene.nodeItem(QStringLiteral("second"));
        first->setSelected(true);
        second->setSelected(true);
        const QPointF firstBefore = first->pos();
        const QPointF secondBefore = second->pos();
        const QPoint start = view.mapFromScene(first->sceneBoundingRect().center());
        view.setFocus();
        QTest::keyPress(&view, key);
        QTest::mousePress(view.viewport(), Qt::LeftButton, modifier, start);
        QTest::mouseMove(view.viewport(), start + QPoint(60, 40), 20);
        QTest::mouseRelease(view.viewport(), Qt::LeftButton, modifier, start + QPoint(60, 40));
        QTest::keyRelease(&view, key);
        QVERIFY(first->pos() != firstBefore);
        QCOMPARE(second->pos(), secondBefore);
        QCOMPARE(scene.undoStack()->count(), 1);
        scene.undoStack()->undo();
        QCOMPARE(first->pos(), firstBefore);
        QCOMPARE(second->pos(), secondBefore);
    };
    verifyDrag(Qt::ControlModifier, Qt::Key_Control);
    verifyDrag(Qt::ShiftModifier, Qt::Key_Shift);
}

void BlueprintSceneTest::sceneRejectsMalformedTechnicalDocuments()
{
    BlueprintDocument duplicateNode;
    duplicateNode.nodes = {node(QStringLiteral("same"), QStringLiteral("One")),
                           node(QStringLiteral("same"), QStringLiteral("Two"))};
    BlueprintScene duplicateNodeScene(&duplicateNode);
    QVERIFY(!duplicateNodeScene.isRepresentable());
    QVERIFY(duplicateNodeScene.items().isEmpty());
    QCOMPARE(duplicateNode.nodes.size(), 2);

    BlueprintDocument whitespaceNode;
    whitespaceNode.nodes = {node(QStringLiteral("  "), QStringLiteral("Blank"))};
    BlueprintScene whitespaceNodeScene(&whitespaceNode);
    QVERIFY(!whitespaceNodeScene.isRepresentable());
    QVERIFY(whitespaceNodeScene.items().isEmpty());

    BlueprintDocument duplicateEdge;
    duplicateEdge.nodes = {node(QStringLiteral("a"), QStringLiteral("A")),
                           node(QStringLiteral("b"), QStringLiteral("B"))};
    duplicateEdge.edges = {{QStringLiteral("edge"), QStringLiteral("a"), QStringLiteral("b"), {}},
                           {QStringLiteral("edge"), QStringLiteral("b"), QStringLiteral("a"), {}}};
    BlueprintScene duplicateEdgeScene(&duplicateEdge);
    QVERIFY(!duplicateEdgeScene.isRepresentable());
    QVERIFY(duplicateEdgeScene.items().isEmpty());

    BlueprintDocument emptyEdgeId;
    emptyEdgeId.nodes = duplicateEdge.nodes;
    emptyEdgeId.edges = {{QString(), QStringLiteral("a"), QStringLiteral("b"), {}}};
    BlueprintScene emptyEdgeIdScene(&emptyEdgeId);
    QVERIFY(!emptyEdgeIdScene.isRepresentable());
    QVERIFY(emptyEdgeIdScene.items().isEmpty());

    BlueprintDocument danglingEdge;
    danglingEdge.nodes = {node(QStringLiteral("a"), QStringLiteral("A"))};
    danglingEdge.edges = {{QStringLiteral("edge"), QStringLiteral("a"), QStringLiteral("missing"), {}}};
    BlueprintScene danglingEdgeScene(&danglingEdge);
    QVERIFY(!danglingEdgeScene.isRepresentable());
    QVERIFY(danglingEdgeScene.items().isEmpty());

    BlueprintDocument danglingBoth;
    danglingBoth.nodes = {node(QStringLiteral("a"), QStringLiteral("A"))};
    danglingBoth.edges = {{QStringLiteral("edge"), QStringLiteral("missing-source"),
                           QStringLiteral("missing-target"), {}}};
    BlueprintScene danglingBothScene(&danglingBoth);
    QVERIFY(!danglingBothScene.isRepresentable());
    QVERIFY(danglingBothScene.items().isEmpty());

    BlueprintDocument valid;
    BlueprintScene validScene(&valid);
    QVERIFY(!validScene.addNode(node(QStringLiteral("  "), QStringLiteral("Invalid")), QPointF()));
    QVERIFY(valid.nodes.isEmpty());
}

void BlueprintSceneTest::rejectedScenesAreReadOnly()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("duplicate"), QStringLiteral("One")),
                      node(QStringLiteral("duplicate"), QStringLiteral("Two"))};
    document.edges = {{QStringLiteral("edge"), QStringLiteral("duplicate"), QStringLiteral("missing"), {}}};
    const BlueprintDocument before = document;
    BlueprintScene scene(&document);
    QVERIFY(!scene.isRepresentable());
    QCOMPARE(scene.undoStack()->count(), 0);
    QVERIFY(!scene.addNode(node(QStringLiteral("new"), QStringLiteral("New")), QPointF(1, 1)));
    QVERIFY(!scene.deleteNode(QStringLiteral("duplicate")));
    QVERIFY(!scene.moveNode(QStringLiteral("duplicate"), QPointF(2, 2)));
    QVERIFY(!scene.editNodeText(QStringLiteral("duplicate"), QStringLiteral("Changed"), {}));
    QVERIFY(!scene.editNode(QStringLiteral("duplicate"), node(QStringLiteral("duplicate"), QStringLiteral("Changed"))));
    QVERIFY(!scene.connectNodes(QStringLiteral("duplicate"), QStringLiteral("missing")));
    QVERIFY(!scene.beginConnection(QStringLiteral("stale")));
    QVERIFY(!scene.chooseConnectionNode(QStringLiteral("duplicate")));
    scene.cancelConnection();
    scene.undoStack()->undo();
    scene.undoStack()->redo();
    QCOMPARE(scene.undoStack()->count(), 0);
    QCOMPARE(document, before);
}

void BlueprintSceneTest::connectionStateCanBeCancelledAndSelfLoopsAreRejected()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("source"), QStringLiteral("Source")),
                      node(QStringLiteral("target"), QStringLiteral("Target"))};
    BlueprintScene scene(&document);
    QVERIFY(scene.beginConnection(QStringLiteral("stale")));
    QVERIFY(scene.chooseConnectionNode(QStringLiteral("source")));
    scene.cancelConnection();
    QVERIFY(scene.connectionSource().isEmpty());
    QVERIFY(scene.beginConnection(QStringLiteral("fresh")));
    QVERIFY(scene.chooseConnectionNode(QStringLiteral("source")));
    QVERIFY(scene.chooseConnectionNode(QStringLiteral("target")));
    QCOMPARE(document.edges.constFirst().label, QStringLiteral("fresh"));
    QVERIFY(!scene.connectNodes(QStringLiteral("target"), QStringLiteral("target")));

    QVERIFY(scene.beginConnection(QStringLiteral("pending")));
    QVERIFY(scene.chooseConnectionNode(QStringLiteral("target")));
    QVERIFY(scene.deleteNode(QStringLiteral("target")));
    QVERIFY(scene.connectionSource().isEmpty());
}

void BlueprintSceneTest::connectionModeDoubleClickDoesNotAlsoRequestEditing()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("source"), QStringLiteral("Source")),
                      node(QStringLiteral("target"), QStringLiteral("Target"))};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.resize(800, 500);
    view.show();
    QApplication::processEvents();
    QSignalSpy editRequests(&scene, &BlueprintScene::nodeEditRequested);
    const QPoint sourcePoint = view.mapFromScene(
        scene.nodeItem(QStringLiteral("source"))->sceneBoundingRect().center());
    const QPoint sourceOutputPoint = view.mapFromScene(
        scene.nodeItem(QStringLiteral("source"))->outputAnchor());
    const QPoint targetPoint = view.mapFromScene(
        scene.nodeItem(QStringLiteral("target"))->sceneBoundingRect().center());

    QVERIFY(scene.beginConnection());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QCOMPARE(scene.connectionSource(), QStringLiteral("source"));
    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourcePoint);
    QCOMPARE(editRequests.count(), 0);
    QCOMPARE(scene.connectionSource(), QStringLiteral("source"));

    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    QCOMPARE(document.edges.size(), 1);
    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    QCOMPARE(editRequests.count(), 0);

    QVERIFY(scene.beginConnection());
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourceOutputPoint);
    QVERIFY(scene.connectionSource().isEmpty());
    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourceOutputPoint);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, sourceOutputPoint);
    QCOMPARE(editRequests.count(), 0);
    QCOMPARE(document.edges.size(), 1);

    QTest::qWait(QApplication::doubleClickInterval() + 10);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    QTest::mouseDClick(view.viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    QTest::mouseRelease(view.viewport(), Qt::LeftButton, Qt::NoModifier, targetPoint);
    QCOMPARE(editRequests.count(), 1);
}

void BlueprintSceneTest::graphicsBoundsContainPaintedPortAndArrowExtents()
{
    BlueprintDocument document;
    BlueprintNode source = node(QStringLiteral("source"), QStringLiteral("Source"));
    source.outputs = {{QStringLiteral("result"), QStringLiteral("bool"), {}}};
    BlueprintNode target = node(QStringLiteral("target"), QStringLiteral("Target"));
    target.inputs = {{QStringLiteral("value"), QStringLiteral("bool"), {}}};
    document.nodes = {source, target};
    BlueprintScene scene(&document);
    QVERIFY(scene.connectNodes(QStringLiteral("source"), QStringLiteral("target"), QStringLiteral("yes")));
    NodeItem *sourceItem = scene.nodeItem(QStringLiteral("source"));
    const QPointF localPort = sourceItem->mapFromScene(sourceItem->outputAnchor());
    QVERIFY(sourceItem->boundingRect().contains(localPort - QPointF(5.0, 5.0)));
    QVERIFY(sourceItem->boundingRect().contains(localPort + QPointF(5.0, 5.0)));
    EdgeItem *edge = scene.edgeItem(document.edges.constFirst().id);
    QVERIFY(edge->boundingRect().contains(edge->arrowPolygon().boundingRect()));
    QVERIFY(edge->boundingRect().contains(edge->labelPosition()));
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

void BlueprintSceneTest::directNodeEditorSaveSynchronizesDocumentCanvasInspectorAndUndo()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    window.show();
    QApplication::processEvents();

    const QString id = window.document().nodes.constFirst().id;
    const BlueprintNode original = window.document().nodes.constFirst();
    const int commandsBeforeEdit = window.scene()->undoStack()->count();
    auto *inspectorName = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
    auto *inspector = window.findChild<NodePropertiesEditor *>(
        QStringLiteral("inspectorNodePropertiesEditor"));
    QVERIFY(inspectorName && inspector);

    QString dialogTypeText;
    QTimer::singleShot(0, &window, [&window, &dialogTypeText] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("nodeEditDialog"));
        auto *editor = dialog->findChild<NodePropertiesEditor *>(
            QStringLiteral("directNodePropertiesEditor"));
        auto *save = dialog->findChild<QPushButton *>(QStringLiteral("saveNodeEditButton"));
        const bool editorComplete = editor && save;
        if (!editorComplete) {
            dialog->reject();
        }
        QVERIFY(editorComplete);
        auto *directType = editor->findChild<QLabel *>(QStringLiteral("directNodeTypeValue"));
        if (!directType) {
            dialog->reject();
        }
        QVERIFY(directType);
        dialogTypeText = directType->text();
        BlueprintNode edited;
        edited.name = QStringLiteral("Canvas configured");
        edited.description = QStringLiteral("Edited without leaving the blueprint");
        edited.inputs = {{QStringLiteral("request"), QStringLiteral("json"),
                          QStringLiteral("Request")}};
        edited.outputs = {{QStringLiteral("result"), QStringLiteral("bool"),
                           QStringLiteral("Result")}};
        edited.constraints = {QStringLiteral("authenticated"), QStringLiteral("rate limited")};
        edited.acceptanceCriteria = {QStringLiteral("returns result"),
                                     QStringLiteral("reports errors")};
        editor->setNode(edited);
        QTest::mouseClick(save, Qt::LeftButton);
        if (dialog->isVisible()) {
            dialog->reject();
        }
    });

    const QPoint nodeCenter = window.graphicsView()->mapFromScene(
        window.scene()->nodeItem(id)->sceneBoundingRect().center());
    QTest::mouseDClick(window.graphicsView()->viewport(), Qt::LeftButton, Qt::NoModifier,
                       nodeCenter);
    QApplication::processEvents();

    const BlueprintNode edited = window.document().nodes.constFirst();
    QCOMPARE(dialogTypeText, QStringLiteral("Logic Module"));
    QCOMPARE(edited.name, QStringLiteral("Canvas configured"));
    QCOMPARE(edited.type, NodeType::LogicModule);
    QCOMPARE(edited.description, QStringLiteral("Edited without leaving the blueprint"));
    QCOMPARE(edited.inputs.constFirst().name, QStringLiteral("request"));
    QCOMPARE(edited.outputs.constFirst().type, QStringLiteral("bool"));
    QCOMPARE(edited.constraints,
             QStringList({QStringLiteral("authenticated"), QStringLiteral("rate limited")}));
    QCOMPARE(edited.acceptanceCriteria,
             QStringList({QStringLiteral("returns result"), QStringLiteral("reports errors")}));
    QCOMPARE(window.scene()->nodeItem(id)->node(), edited);
    QCOMPARE(inspectorName->text(), edited.name);
    BlueprintNode inspectorNode;
    inspector->applyTo(&inspectorNode);
    QCOMPARE(inspectorNode.inputs, edited.inputs);
    QCOMPARE(window.scene()->undoStack()->count(), commandsBeforeEdit + 1);

    window.scene()->undoStack()->undo();
    QCOMPARE(window.document().nodes.constFirst(), original);
    QCOMPARE(window.document().nodes.constFirst().name, original.name);
    QCOMPARE(window.document().nodes.constFirst().type, NodeType::LogicModule);
    QCOMPARE(window.scene()->nodeItem(id)->node(), original);
    QCOMPARE(inspectorName->text(), original.name);
    window.scene()->undoStack()->redo();
    QCOMPARE(window.document().nodes.constFirst(), edited);
    QCOMPARE(window.scene()->nodeItem(id)->node(), edited);
    QCOMPARE(inspectorName->text(), edited.name);
}

void BlueprintSceneTest::directNodeEditorCancelDoesNotMutateDocument()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    window.show();
    QApplication::processEvents();

    const QString id = window.document().nodes.constFirst().id;
    const BlueprintDocument before = window.document();
    const int commandsBefore = window.scene()->undoStack()->count();
    QTimer::singleShot(0, &window, [&window] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("directNodeNameEdit"));
        auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("cancelNodeEditButton"));
        const bool editorComplete = name && cancel;
        if (!editorComplete) {
            dialog->reject();
        }
        QVERIFY(editorComplete);
        name->setText(QStringLiteral("Must not be saved"));
        QTest::mouseClick(cancel, Qt::LeftButton);
    });

    const QPoint nodeCenter = window.graphicsView()->mapFromScene(
        window.scene()->nodeItem(id)->sceneBoundingRect().center());
    QTest::mouseDClick(window.graphicsView()->viewport(), Qt::LeftButton, Qt::NoModifier,
                       nodeCenter);
    QApplication::processEvents();
    QCOMPARE(window.document(), before);
    QCOMPARE(window.scene()->undoStack()->count(), commandsBefore);

    QTimer::singleShot(0, &window, [&window] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        auto *name = dialog->findChild<QLineEdit *>(QStringLiteral("directNodeNameEdit"));
        auto *cancel = dialog->findChild<QPushButton *>(QStringLiteral("cancelNodeEditButton"));
        const bool editorComplete = name && cancel;
        if (!editorComplete) {
            dialog->reject();
        }
        QVERIFY(editorComplete);
        name->setText(QStringLiteral("Also discarded"));
        QTest::mouseClick(cancel, Qt::LeftButton);
    });
    QTest::mouseDClick(window.graphicsView()->viewport(), Qt::LeftButton, Qt::NoModifier,
                       nodeCenter);
    QApplication::processEvents();
    QCOMPARE(window.document(), before);
    QCOMPARE(window.scene()->undoStack()->count(), commandsBefore);
}

void BlueprintSceneTest::mainWindowPropertyEditorPreservesAllFields()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    const QString id = window.document().nodes.constFirst().id;
    auto *editor = window.findChild<NodePropertiesEditor *>(
        QStringLiteral("inspectorNodePropertiesEditor"));
    auto *applyButton = window.findChild<QPushButton *>(QStringLiteral("applyNodePropertiesButton"));
    QVERIFY(editor && applyButton);
    const int commandsBeforeEdit = window.scene()->undoStack()->count();

    BlueprintNode draft = window.document().nodes.constFirst();
    draft.name = QStringLiteral("Configured");
    draft.description = QStringLiteral("Detailed description");
    draft.inputs = {{QStringLiteral("payload"), QStringLiteral("json"),
                     QStringLiteral("Request body")}};
    draft.outputs = {{QStringLiteral("response"), QStringLiteral("json"),
                      QStringLiteral("Response body")}};
    draft.constraints = {QStringLiteral("authenticated"), QStringLiteral("rate limited")};
    draft.acceptanceCriteria = {QStringLiteral("returns 200"), QStringLiteral("returns error")};
    editor->setNode(draft);
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

void BlueprintSceneTest::propertyDockTracksUndoRedoAndDoesNotReapplyStaleValues()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    const QString id = window.document().nodes.constFirst().id;
    auto *nameEdit = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
    auto *editor = window.findChild<NodePropertiesEditor *>(
        QStringLiteral("inspectorNodePropertiesEditor"));
    auto *applyButton = window.findChild<QPushButton *>(QStringLiteral("applyNodePropertiesButton"));
    QVERIFY(nameEdit && editor && applyButton);
    BlueprintNode changed = window.document().nodes.constFirst();
    changed.name = QStringLiteral("Changed");
    changed.inputs = {{QStringLiteral("value"), QStringLiteral("int"), QStringLiteral("Value")}};
    editor->setNode(changed);
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("Changed"));
    window.scene()->undoStack()->undo();
    QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("Logic Module"));
    QCOMPARE(nameEdit->text(), QStringLiteral("Logic Module"));
    BlueprintNode inspectorNode;
    editor->applyTo(&inspectorNode);
    QVERIFY(inspectorNode.inputs.isEmpty());
    const int commandCountAfterUndo = window.scene()->undoStack()->count();
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("Logic Module"));
    QCOMPARE(window.scene()->undoStack()->count(), commandCountAfterUndo);
    window.scene()->undoStack()->redo();
    QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("Changed"));
    editor->applyTo(&inspectorNode);
    QCOMPARE(inspectorNode.inputs.constFirst().name, QStringLiteral("value"));
    QCOMPARE(window.scene()->nodeItem(id)->node(), window.document().nodes.constFirst());
}

void BlueprintSceneTest::propertyDockIsDisabledForMultiSelection()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    const BlueprintDocument before = window.document();
    window.scene()->nodeItem(before.nodes.at(0).id)->setSelected(true);
    window.scene()->nodeItem(before.nodes.at(1).id)->setSelected(true);
    auto *nameEdit = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
    auto *applyButton = window.findChild<QPushButton *>(QStringLiteral("applyNodePropertiesButton"));
    QVERIFY(nameEdit && applyButton);
    QVERIFY(!nameEdit->isEnabled());
    QVERIFY(!applyButton->isEnabled() || nameEdit->text().isEmpty());
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QCOMPARE(window.document(), before);
}

void BlueprintSceneTest::propertyDraftSurvivesLayoutAndStructuredRowsApply()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    const QString id = window.document().nodes.constFirst().id;
    auto *nameEdit = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
    auto *inputsSection = window.findChild<QWidget *>(QStringLiteral("inspectorNodeInputsEditor"));
    auto *applyButton = window.findChild<QPushButton *>(QStringLiteral("applyNodePropertiesButton"));
    QVERIFY(nameEdit && inputsSection && applyButton);
    auto *inputsTable = inputsSection->findChild<QTableWidget *>(QStringLiteral("portItemsTable"));
    auto *addInput = inputsSection->findChild<QPushButton *>(QStringLiteral("addPortButton"));
    QVERIFY(inputsTable && addInput);
    nameEdit->setText(QStringLiteral("Unapplied"));
    QTest::mouseClick(addInput, Qt::LeftButton);
    inputsTable->item(0, 0)->setText(QStringLiteral("draft"));
    inputsTable->item(0, 1)->setText(QStringLiteral("string"));
    inputsTable->item(0, 2)->setText(QStringLiteral("Draft"));
    QVERIFY(window.scene()->moveNode(id, QPointF(400, 200)));
    QCOMPARE(nameEdit->text(), QStringLiteral("Unapplied"));
    QCOMPARE(inputsTable->item(0, 0)->text(), QStringLiteral("draft"));
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QCOMPARE(window.document().nodes.constFirst().name, QStringLiteral("Unapplied"));
    QCOMPARE(window.document().nodes.constFirst().inputs.constFirst().name, QStringLiteral("draft"));

    auto *deleteInput = qobject_cast<QPushButton *>(inputsTable->cellWidget(0, 3));
    QVERIFY(deleteInput);
    QTest::mouseClick(deleteInput, Qt::LeftButton);
    QTest::mouseClick(applyButton, Qt::LeftButton);
    QVERIFY(window.document().nodes.constFirst().inputs.isEmpty());
    QVERIFY(window.statusBar()->currentMessage().isEmpty());
}

void BlueprintSceneTest::connectionToolbarCanBeCancelled()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    const QString source = window.document().nodes.at(0).id;
    auto *labelEdit = window.findChild<QLineEdit *>(QStringLiteral("connectionLabelEdit"));
    auto *connectionAction = window.findChild<QAction *>(QStringLiteral("beginConnectionAction"));
    auto *cancelAction = window.findChild<QAction *>(QStringLiteral("cancelConnectionAction"));
    QVERIFY(labelEdit && connectionAction && cancelAction);
    labelEdit->setText(QStringLiteral("stale"));
    connectionAction->trigger();
    const QPoint point = window.graphicsView()->mapFromScene(window.scene()->nodeItem(source)->sceneBoundingRect().center());
    QTest::mouseClick(window.graphicsView()->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(window.scene()->connectionSource(), source);
    cancelAction->trigger();
    QVERIFY(window.scene()->connectionSource().isEmpty());
    QVERIFY(window.statusBar()->currentMessage().isEmpty());
    connectionAction->trigger();
    QTest::mouseClick(window.graphicsView()->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    QCOMPARE(window.scene()->connectionSource(), source);
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

void BlueprintSceneTest::mainWindowCreatesLabeledConnectionsFromToolbar()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    const QString source = window.document().nodes.at(0).id;
    const QString target = window.document().nodes.at(1).id;
    auto *labelEdit = window.findChild<QLineEdit *>(QStringLiteral("connectionLabelEdit"));
    auto *connectionAction = window.findChild<QAction *>(QStringLiteral("beginConnectionAction"));
    QVERIFY(labelEdit != nullptr);
    QVERIFY(connectionAction != nullptr);

    QGraphicsView *view = window.graphicsView();
    const auto clickNode = [&](const QString &id) {
        const QPoint point = view->mapFromScene(window.scene()->nodeItem(id)->sceneBoundingRect().center());
        QTest::mouseClick(view->viewport(), Qt::LeftButton, Qt::NoModifier, point);
    };
    labelEdit->setText(QStringLiteral("true"));
    connectionAction->trigger();
    clickNode(source);
    clickNode(target);
    QCOMPARE(window.document().edges.size(), 1);
    QCOMPARE(window.document().edges.constFirst().source, source);
    QCOMPARE(window.document().edges.constFirst().target, target);
    QCOMPARE(window.document().edges.constFirst().label, QStringLiteral("true"));
    QCOMPARE(window.scene()->edgeItem(window.document().edges.constFirst().id)->label(), QStringLiteral("true"));
    QVERIFY(window.scene()->connectionSource().isEmpty());

    labelEdit->setText(QStringLiteral("false"));
    connectionAction->trigger();
    clickNode(target);
    clickNode(source);
    QCOMPARE(window.document().edges.size(), 2);
    QCOMPARE(window.document().edges.constLast().source, target);
    QCOMPARE(window.document().edges.constLast().target, source);
    QCOMPARE(window.document().edges.constLast().label, QStringLiteral("false"));
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

void BlueprintSceneTest::mainWindowRestoresClosedDocksAndDefaultLayout()
{
    MainWindow window;
    window.show();
    QCoreApplication::processEvents();

    auto *viewMenu = window.findChild<QMenu *>(QStringLiteral("viewMenu"));
    auto *propertiesDock = window.findChild<QDockWidget *>(QStringLiteral("propertiesDock"));
    auto *buildDock = window.findChild<QDockWidget *>(QStringLiteral("buildExportDock"));
    auto *propertiesAction = window.findChild<QAction *>(QStringLiteral("propertiesDockAction"));
    auto *buildAction = window.findChild<QAction *>(QStringLiteral("buildExportDockAction"));
    auto *resetAction = window.findChild<QAction *>(QStringLiteral("resetLayoutAction"));
    QVERIFY(viewMenu && propertiesDock && buildDock && propertiesAction && buildAction
            && resetAction);
    QVERIFY(viewMenu->actions().contains(propertiesAction));
    QVERIFY(viewMenu->actions().contains(buildAction));
    QVERIFY(viewMenu->actions().contains(resetAction));

    propertiesDock->close();
    buildDock->close();
    QTRY_VERIFY(propertiesDock->isHidden());
    QTRY_VERIFY(buildDock->isHidden());
    QTRY_VERIFY(!propertiesAction->isChecked());
    QTRY_VERIFY(!buildAction->isChecked());

    propertiesAction->trigger();
    buildAction->trigger();
    QTRY_VERIFY(propertiesDock->isVisible());
    QTRY_VERIFY(buildDock->isVisible());
    QVERIFY(propertiesAction->isChecked());
    QVERIFY(buildAction->isChecked());

    propertiesDock->setFloating(true);
    window.addDockWidget(Qt::LeftDockWidgetArea, buildDock);
    buildDock->close();
    QTRY_VERIFY(propertiesDock->isFloating());
    QTRY_VERIFY(buildDock->isHidden());
    QTRY_VERIFY(!buildAction->isChecked());
    resetAction->trigger();

    QCOMPARE(window.dockWidgetArea(propertiesDock), Qt::RightDockWidgetArea);
    QCOMPARE(window.dockWidgetArea(buildDock), Qt::BottomDockWidgetArea);
    QVERIFY(!propertiesDock->isFloating());
    QVERIFY(!buildDock->isFloating());
    QVERIFY(propertiesDock->isVisible());
    QVERIFY(buildDock->isVisible());
    QVERIFY(propertiesAction->isChecked());
    QVERIFY(buildAction->isChecked());
}

void BlueprintSceneTest::mainWindowExposesBuildAndExportInToolbar()
{
    MainWindow window;
    auto *toolbar = window.findChild<QToolBar *>(QStringLiteral("blueprintToolbar"));
    auto *buildAction = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    auto *exportAction = window.findChild<QAction *>(QStringLiteral("toolbarExportAction"));
    auto *workspaceEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *exportEdit = window.findChild<QLineEdit *>(QStringLiteral("exportTargetEdit"));
    auto *buildLog = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(toolbar && buildAction && exportAction && workspaceEdit && exportEdit && buildLog);
    QVERIFY(toolbar->actions().contains(buildAction));
    QVERIFY(toolbar->actions().contains(exportAction));

    workspaceEdit->clear();
    buildAction->trigger();
    QVERIFY(buildLog->toPlainText().contains(QStringLiteral("Build request rejected")));

    buildLog->clear();
    exportEdit->clear();
    exportAction->trigger();
    QVERIFY(buildLog->toPlainText().contains(QStringLiteral("Export failed")));
}

void BlueprintSceneTest::keyboardDeleteRemovesSelectedNodesAndIncidentEdges()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("source"), QStringLiteral("Source")),
                      node(QStringLiteral("target"), QStringLiteral("Target"))};
    document.edges = {{QStringLiteral("flow"), QStringLiteral("source"), QStringLiteral("target"), {}}};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.resize(800, 500);
    view.show();
    view.setFocus();
    QApplication::processEvents();

    // Nothing is selected yet, so Delete must not touch the document.
    QTest::keyClick(view.viewport(), Qt::Key_Delete);
    QCOMPARE(document.nodes.size(), 2);
    QCOMPARE(document.edges.size(), 1);
    QCOMPARE(scene.undoStack()->count(), 0);

    scene.nodeItem(QStringLiteral("source"))->setSelected(true);
    QTest::keyClick(view.viewport(), Qt::Key_Delete);
    QCOMPARE(document.nodes.size(), 1);
    QCOMPARE(document.nodes.constFirst().id, QStringLiteral("target"));
    QVERIFY(document.edges.isEmpty());
    QVERIFY(scene.nodeItem(QStringLiteral("source")) == nullptr);
    QVERIFY(scene.edgeItem(QStringLiteral("flow")) == nullptr);

    scene.undoStack()->undo();
    QCOMPARE(document.nodes.size(), 2);
    QCOMPARE(document.nodes.constFirst().id, QStringLiteral("source"));
    QCOMPARE(document.edges.size(), 1);
    QVERIFY(scene.nodeItem(QStringLiteral("source")) != nullptr);
    QVERIFY(scene.edgeItem(QStringLiteral("flow")) != nullptr);

    scene.undoStack()->redo();
    QCOMPARE(document.nodes.size(), 1);
    QVERIFY(document.edges.isEmpty());
    QVERIFY(scene.edgeItem(QStringLiteral("flow")) == nullptr);
}

void BlueprintSceneTest::keyboardDeleteRemovesEverySelectedNode()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("first"), QStringLiteral("First")),
                      node(QStringLiteral("second"), QStringLiteral("Second")),
                      node(QStringLiteral("third"), QStringLiteral("Third"))};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.resize(800, 500);
    view.show();
    view.setFocus();
    QApplication::processEvents();
    const BlueprintDocument before = document;

    scene.nodeItem(QStringLiteral("first"))->setSelected(true);
    scene.nodeItem(QStringLiteral("third"))->setSelected(true);
    QTest::keyClick(view.viewport(), Qt::Key_Delete);
    QCOMPARE(document.nodes.size(), 1);
    QCOMPARE(document.nodes.constFirst().id, QStringLiteral("second"));
    QVERIFY(scene.nodeItem(QStringLiteral("first")) == nullptr);
    QVERIFY(scene.nodeItem(QStringLiteral("third")) == nullptr);

    scene.undoStack()->undo();
    scene.undoStack()->undo();
    QCOMPARE(document, before);
    QVERIFY(scene.nodeItem(QStringLiteral("first")) != nullptr);
    QVERIFY(scene.nodeItem(QStringLiteral("third")) != nullptr);
}

void BlueprintSceneTest::clickingAnEdgeSelectsItAndDeleteKeepsItsNodes()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("source"), QStringLiteral("Source")),
                      node(QStringLiteral("target"), QStringLiteral("Target"))};
    document.edges = {{QStringLiteral("flow"), QStringLiteral("source"), QStringLiteral("target"), {}}};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.resize(800, 500);
    view.show();
    view.setFocus();
    QApplication::processEvents();
    EdgeItem *edge = scene.edgeItem(QStringLiteral("flow"));
    QVERIFY(edge != nullptr);
    // The scene index filters mouse hits by boundingRect(), so the widened hit stroke has to
    // stay inside it or the outer band of the click area would silently not be clickable.
    QVERIFY(edge->boundingRect().contains(edge->shape().boundingRect()));

    // Five pixels off the curve still hit the line, so a thin edge stays clickable.
    const QPointF curve = edge->path().pointAtPercent(0.5);
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      view.mapFromScene(curve + QPointF(0.0, 5.0)));
    QVERIFY(edge->isSelected());
    QCOMPARE(scene.selectedItems().size(), 1);
    QVERIFY(scene.selectedItems().constFirst() == edge);
    QVERIFY(!scene.nodeItem(QStringLiteral("source"))->isSelected());

    // Outside the widened stroke the curve is not picked up any more.
    scene.clearSelection();
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      view.mapFromScene(curve + QPointF(0.0, 8.0)));
    QVERIFY(!edge->isSelected());
    QVERIFY(scene.selectedItems().isEmpty());

    const auto render = [&](QStyle::State state) {
        QImage image(300, 120, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::transparent);
        QPainter painter(&image);
        painter.translate(10, 10);
        QStyleOptionGraphicsItem option;
        option.state = state;
        edge->paint(&painter, &option, nullptr);
        return image;
    };
    QVERIFY(render(QStyle::State_None) != render(QStyle::State_Selected));

    // Clicking a node still takes precedence over the selected edge.
    QTest::mouseClick(view.viewport(), Qt::LeftButton, Qt::NoModifier,
                      view.mapFromScene(scene.nodeItem(QStringLiteral("target"))->sceneBoundingRect().center()));
    QVERIFY(scene.nodeItem(QStringLiteral("target"))->isSelected());
    QVERIFY(!edge->isSelected());

    scene.clearSelection();
    edge->setSelected(true);
    QTest::keyClick(view.viewport(), Qt::Key_Delete);
    QVERIFY(document.edges.isEmpty());
    QCOMPARE(document.nodes.size(), 2);
    QVERIFY(scene.edgeItem(QStringLiteral("flow")) == nullptr);
    QVERIFY(scene.nodeItem(QStringLiteral("source")) != nullptr);
    QVERIFY(scene.nodeItem(QStringLiteral("target")) != nullptr);

    scene.undoStack()->undo();
    QCOMPARE(document.edges.size(), 1);
    QVERIFY(scene.edgeItem(QStringLiteral("flow")) != nullptr);

    scene.undoStack()->redo();
    QVERIFY(document.edges.isEmpty());
    QVERIFY(scene.edgeItem(QStringLiteral("flow")) == nullptr);
}

void BlueprintSceneTest::keyboardDeleteHandlesMixedNodeAndEdgeSelection()
{
    BlueprintDocument document;
    document.nodes = {node(QStringLiteral("first"), QStringLiteral("First")),
                      node(QStringLiteral("second"), QStringLiteral("Second")),
                      node(QStringLiteral("third"), QStringLiteral("Third"))};
    document.edges = {{QStringLiteral("one"), QStringLiteral("first"), QStringLiteral("second"), {}},
                      {QStringLiteral("two"), QStringLiteral("second"), QStringLiteral("third"), {}}};
    BlueprintScene scene(&document);
    QGraphicsView view(&scene);
    view.resize(800, 500);
    view.show();
    view.setFocus();
    QApplication::processEvents();

    scene.edgeItem(QStringLiteral("two"))->setSelected(true);
    scene.nodeItem(QStringLiteral("first"))->setSelected(true);
    QTest::keyClick(view.viewport(), Qt::Key_Delete);

    QCOMPARE(document.nodes.size(), 2);
    QCOMPARE(document.nodes.constFirst().id, QStringLiteral("second"));
    QCOMPARE(document.nodes.constLast().id, QStringLiteral("third"));
    QVERIFY(document.edges.isEmpty());
    QVERIFY(scene.nodeItem(QStringLiteral("first")) == nullptr);
    QVERIFY(scene.edgeItem(QStringLiteral("one")) == nullptr);
    QVERIFY(scene.edgeItem(QStringLiteral("two")) == nullptr);
    QCOMPARE(scene.undoStack()->count(), 2);

    scene.undoStack()->undo();
    scene.undoStack()->undo();
    QCOMPARE(document.nodes.size(), 3);
    QCOMPARE(document.edges.size(), 2);
    QCOMPARE(scene.edgeItem(QStringLiteral("one"))->edgeId(), QStringLiteral("one"));
    QCOMPARE(scene.edgeItem(QStringLiteral("two"))->edgeId(), QStringLiteral("two"));
    QCOMPARE(scene.nodeItem(QStringLiteral("first"))->nodeId(), QStringLiteral("first"));

    scene.undoStack()->redo();
    scene.undoStack()->redo();
    QCOMPARE(document.nodes.size(), 2);
    QVERIFY(document.edges.isEmpty());
    QVERIFY(scene.edgeItem(QStringLiteral("one")) == nullptr);
    QVERIFY(scene.edgeItem(QStringLiteral("two")) == nullptr);
}

void BlueprintSceneTest::deleteKeyInsideTextEditorsDoesNotDeleteCanvasItems()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    window.show();
    QApplication::processEvents();
    const BlueprintDocument before = window.document();
    const QString nodeId = before.nodes.constFirst().id;
    QVERIFY(window.scene()->nodeItem(nodeId)->isSelected());

    auto *nameEdit = window.findChild<QLineEdit *>(QStringLiteral("nodeNameEdit"));
    auto *descriptionEdit = window.findChild<QPlainTextEdit *>(QStringLiteral("nodeDescriptionEdit"));
    QVERIFY(nameEdit != nullptr);
    QVERIFY(descriptionEdit != nullptr);

    nameEdit->setText(QStringLiteral("ab"));
    nameEdit->setCursorPosition(1);
    nameEdit->setFocus();
    QTest::keyClick(nameEdit, Qt::Key_Delete);
    QCOMPARE(nameEdit->text(), QStringLiteral("a"));
    QCOMPARE(window.document(), before);
    QVERIFY(window.scene()->nodeItem(nodeId) != nullptr);

    descriptionEdit->setPlainText(QStringLiteral("first line"));
    QTextCursor cursor = descriptionEdit->textCursor();
    cursor.setPosition(5);
    descriptionEdit->setTextCursor(cursor);
    descriptionEdit->setFocus();
    QTest::keyClick(descriptionEdit, Qt::Key_Delete);
    QCOMPARE(descriptionEdit->toPlainText(), QStringLiteral("firstline"));
    QCOMPARE(window.document(), before);
    QVERIFY(window.scene()->nodeItem(nodeId) != nullptr);
}

void BlueprintSceneTest::toolbarDeleteSharesTheKeyboardDeletionPath()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    auto *deleteAction = window.findChild<QAction *>(QStringLiteral("deleteSelectionAction"));
    QVERIFY(deleteAction != nullptr);
    window.show();
    QApplication::processEvents();
    const BlueprintDocument before = window.document();
    const int commandsBefore = window.scene()->undoStack()->count();
    const QString first = before.nodes.at(0).id;
    const QString second = before.nodes.at(1).id;
    const auto selectBoth = [&] {
        window.scene()->clearSelection();
        window.scene()->nodeItem(first)->setSelected(true);
        window.scene()->nodeItem(second)->setSelected(true);
    };

    selectBoth();
    window.graphicsView()->setFocus();
    QTest::keyClick(window.graphicsView()->viewport(), Qt::Key_Delete);
    const BlueprintDocument afterKeyboard = window.document();
    QVERIFY(afterKeyboard.nodes.isEmpty());
    QCOMPARE(window.scene()->undoStack()->count(), commandsBefore + 2);

    // Each deleted item keeps its own undo step, matching the pre-existing toolbar behaviour.
    window.scene()->undoStack()->undo();
    window.scene()->undoStack()->undo();
    QCOMPARE(window.document(), before);

    selectBoth();
    deleteAction->trigger();
    QCOMPARE(window.document(), afterKeyboard);
}

void BlueprintSceneTest::canvasContextMenuOffersCanvasEntries()
{
    MainWindow window;
    window.show();
    QApplication::processEvents();
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QGraphicsView *view = window.graphicsView();
    const QString nodeId = window.document().nodes.constFirst().id;
    window.scene()->nodeItem(nodeId)->setSelected(true);
    const QPoint blank = blankViewportPoint(view, *window.scene());
    QVERIFY(blank.x() >= 0);

    // A real right press must not drop the selection before the menu even opens.
    QTest::mousePress(view->viewport(), Qt::RightButton, Qt::NoModifier, blank);
    QTest::mouseRelease(view->viewport(), Qt::RightButton, Qt::NoModifier, blank);
    QVERIFY(window.scene()->nodeItem(nodeId)->isSelected());

    const ContextMenuProbe probe = probeContextMenu(view->viewport(), blank);
    QVERIFY(probe.opened);
    QCOMPARE(probe.entries,
             QStringList({QStringLiteral("Add node"), QStringLiteral("---"),
                          QStringLiteral("Select All"), QStringLiteral("Fit View"),
                          QStringLiteral("Reset View")}));
    QCOMPARE(probe.submenus.value(QStringLiteral("Add node")),
             QStringList({QStringLiteral("Start"), QStringLiteral("End"), QStringLiteral("UI Page"),
                          QStringLiteral("Logic Module"), QStringLiteral("Decision"),
                          QStringLiteral("External Code")}));
    // A right click on empty canvas must not disturb the current selection.
    QVERIFY(window.scene()->nodeItem(nodeId)->isSelected());
    QCOMPARE(window.document().nodes.size(), 1);
}

void BlueprintSceneTest::canvasContextMenuAddsNodeAtRightClickPosition()
{
    MainWindow window;
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    const QPoint blank = blankViewportPoint(view, *window.scene());
    QVERIFY(blank.x() >= 0);
    const QPointF clickPosition = view->mapToScene(blank);

    const ContextMenuProbe probe = probeContextMenu(view->viewport(), blank, triggerAddNodeEntry);
    QVERIFY(probe.opened);
    QCOMPARE(window.document().nodes.size(), 1);
    QCOMPARE(window.document().nodes.constFirst().type, NodeType::Start);
    const QString id = window.document().nodes.constFirst().id;
    QVERIFY(window.scene()->nodeItem(id)->isSelected());
    // The node appears where the click happened instead of at the view centre.
    QVERIFY(QLineF(window.scene()->nodePosition(id), clickPosition).length() <= 60.0);

    window.scene()->undoStack()->undo();
    QVERIFY(window.document().nodes.isEmpty());
    QVERIFY(window.scene()->nodeItem(id) == nullptr);
}

void BlueprintSceneTest::nodeContextMenuFollowsGenerateEligibility()
{
    const auto generatable = [](NodeType type) {
        return type == NodeType::UiPage || type == NodeType::LogicModule || type == NodeType::Decision;
    };
    const QVector<NodeType> types{NodeType::Start, NodeType::End, NodeType::UiPage,
                                  NodeType::LogicModule, NodeType::Decision, NodeType::ExternalCode};
    for (const NodeType type : types) {
        MainWindow window;
        window.show();
        QApplication::processEvents();
        QVERIFY(window.addNodeOfType(type));
        QGraphicsView *view = window.graphicsView();
        const QString id = window.document().nodes.constFirst().id;
        NodeItem *item = window.scene()->nodeItem(id);
        window.scene()->clearSelection();

        const ContextMenuProbe probe = probeContextMenu(
            view->viewport(), view->mapFromScene(item->sceneBoundingRect().center()));
        QVERIFY(probe.opened);
        QStringList expected{QStringLiteral("Edit node")};
        expected.append(generatable(type) ? QStringLiteral("Generate selected node")
                                          : QStringLiteral("Generate selected node|disabled"));
        expected.append(QStringLiteral("---"));
        expected.append(QStringLiteral("Delete"));
        QCOMPARE(probe.entries, expected);
        // Right clicking an unselected node makes it the current object.
        QVERIFY(item->isSelected());
        // The menu reuses the shared toolbar actions instead of duplicating them.
        auto *deleteAction = window.findChild<QAction *>(QStringLiteral("deleteSelectionAction"));
        auto *generateAction = window.findChild<QAction *>(QStringLiteral("generateSelectedNodeAction"));
        QVERIFY(deleteAction && generateAction);
        QVERIFY(probe.actions.contains(deleteAction));
        QVERIFY(probe.actions.contains(generateAction));
        QCOMPARE(generateAction->isEnabled(), generatable(type));
    }
}

void BlueprintSceneTest::nodeContextMenuKeepsTheCurrentSelectionForDelete()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::LogicModule));
    QVERIFY(window.addNodeOfType(NodeType::Decision));
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    const BlueprintDocument before = window.document();
    const QString first = before.nodes.at(0).id;
    const QString second = before.nodes.at(1).id;
    NodeItem *firstItem = window.scene()->nodeItem(first);
    NodeItem *secondItem = window.scene()->nodeItem(second);
    const QPoint firstPoint = view->mapFromScene(firstItem->sceneBoundingRect().center());

    window.scene()->clearSelection();
    secondItem->setSelected(true);
    ContextMenuProbe probe = probeContextMenu(view->viewport(), firstPoint);
    QVERIFY(probe.opened);
    QVERIFY(firstItem->isSelected());
    QVERIFY(!secondItem->isSelected());

    // A node that already belongs to a multi selection keeps it, so Delete removes every
    // selected object instead of only the clicked one.
    secondItem->setSelected(true);
    probe = probeContextMenu(view->viewport(), firstPoint, [](QMenu *menu) {
        triggerMenuAction(menu, QStringLiteral("deleteSelectionAction"));
    });
    QVERIFY(probe.opened);
    QVERIFY(window.document().nodes.isEmpty());
    QVERIFY(window.scene()->nodeItem(first) == nullptr);
    QVERIFY(window.scene()->nodeItem(second) == nullptr);

    window.scene()->undoStack()->undo();
    window.scene()->undoStack()->undo();
    QCOMPARE(window.document(), before);
    QVERIFY(window.scene()->nodeItem(first) != nullptr);
    QVERIFY(window.scene()->nodeItem(second) != nullptr);
}

void BlueprintSceneTest::edgeContextMenuDeletesOnlyThatEdge()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    const BlueprintDocument before = window.document();
    QVERIFY(window.scene()->connectNodes(before.nodes.at(0).id, before.nodes.at(1).id,
                                         QStringLiteral("next")));
    const QString edgeId = window.document().edges.constFirst().id;
    EdgeItem *edge = window.scene()->edgeItem(edgeId);
    QVERIFY(edge != nullptr);
    window.scene()->clearSelection();

    const QPointF curve = edge->path().pointAtPercent(0.5);
    // Right clicking the unselected edge makes it the current object.
    ContextMenuProbe probe = probeContextMenu(view->viewport(), view->mapFromScene(curve));
    QVERIFY(probe.opened);
    QCOMPARE(probe.entries, QStringList({QStringLiteral("Delete")}));
    QVERIFY(edge->isSelected());
    QVERIFY(!window.scene()->nodeItem(before.nodes.at(0).id)->isSelected());

    // Delete removes only the edge and keeps both nodes.
    probe = probeContextMenu(view->viewport(), view->mapFromScene(curve), [](QMenu *menu) {
        triggerMenuAction(menu, QStringLiteral("deleteSelectionAction"));
    });
    QVERIFY(probe.opened);
    QVERIFY(window.document().edges.isEmpty());
    QCOMPARE(window.document().nodes.size(), 2);
    QVERIFY(window.scene()->nodeItem(before.nodes.at(0).id) != nullptr);
    QVERIFY(window.scene()->nodeItem(before.nodes.at(1).id) != nullptr);
    QVERIFY(window.scene()->edgeItem(edgeId) == nullptr);

    window.scene()->undoStack()->undo();
    QCOMPARE(window.document().edges.size(), 1);
    EdgeItem *restored = window.scene()->edgeItem(edgeId);
    QVERIFY(restored != nullptr);
    window.scene()->undoStack()->redo();
    QVERIFY(window.document().edges.isEmpty());
    QVERIFY(window.scene()->edgeItem(edgeId) == nullptr);

    // A right click on an edge that is already part of a multi selection keeps it, so the
    // shared Delete would act on every selected object.
    window.scene()->undoStack()->undo();
    restored = window.scene()->edgeItem(edgeId);
    QVERIFY(restored != nullptr);
    window.scene()->nodeItem(before.nodes.at(0).id)->setSelected(true);
    restored->setSelected(true);
    probe = probeContextMenu(view->viewport(),
                             view->mapFromScene(restored->path().pointAtPercent(0.5)));
    QVERIFY(probe.opened);
    QCOMPARE(window.scene()->selectedItems().size(), 2);
}

void BlueprintSceneTest::selectAllCanvasEntriesSelectNodesAndEdges()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    QVERIFY(window.document().nodes.size() == 2);
    QVERIFY(window.scene()->connectNodes(window.document().nodes.at(0).id,
                                         window.document().nodes.at(1).id));
    const BlueprintDocument before = window.document();
    const QString edgeId = before.edges.constFirst().id;
    window.scene()->clearSelection();
    const QPoint blank = blankViewportPoint(view, *window.scene());
    QVERIFY(blank.x() >= 0);

    const ContextMenuProbe probe = probeContextMenu(view->viewport(), blank, [](QMenu *menu) {
        triggerMenuAction(menu, QStringLiteral("selectAllCanvasAction"));
    });
    QVERIFY(probe.opened);
    // Select All covers nodes and edges, matching what Delete and the edge menus work on.
    QCOMPARE(window.scene()->selectedItems().size(), 3);
    QVERIFY(window.scene()->edgeItem(edgeId)->isSelected());

    view->setFocus();
    QTest::keyClick(view->viewport(), Qt::Key_Delete);
    QVERIFY(window.document().nodes.isEmpty());
    QVERIFY(window.document().edges.isEmpty());

    for (int step = 0; step < 3; ++step) {
        window.scene()->undoStack()->undo();
    }
    QCOMPARE(window.document(), before);
}

void BlueprintSceneTest::fitViewBringsEveryNodeIntoTheViewport()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    const BlueprintDocument before = window.document();
    QVERIFY(window.scene()->moveNode(before.nodes.at(0).id, QPointF(-400.0, -300.0)));
    QVERIFY(window.scene()->moveNode(before.nodes.at(1).id, QPointF(700.0, 480.0)));
    const QPoint blank = blankViewportPoint(view, *window.scene());
    QVERIFY(blank.x() >= 0);

    const ContextMenuProbe probe = probeContextMenu(view->viewport(), blank, [](QMenu *menu) {
        triggerMenuAction(menu, QStringLiteral("fitCanvasViewAction"));
    });
    QVERIFY(probe.opened);
    for (const BlueprintNode &node : window.document().nodes) {
        const QRect nodeRect = view->mapFromScene(
            window.scene()->nodeItem(node.id)->sceneBoundingRect()).boundingRect();
        QVERIFY(view->viewport()->rect().contains(nodeRect));
    }
    // The zoom stays inside the limits the wheel already enforces.
    QVERIFY(view->transform().m11() >= 0.25);
    QVERIFY(view->transform().m11() <= 3.0);
}

void BlueprintSceneTest::fitViewKeepsTheZoomLimit()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    QVERIFY(window.addNodeOfType(NodeType::End));
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    const BlueprintDocument before = window.document();
    // Far enough apart that fitting them would zoom far below the wheel limit.
    QVERIFY(window.scene()->moveNode(before.nodes.at(0).id, QPointF(-20000.0, -20000.0)));
    QVERIFY(window.scene()->moveNode(before.nodes.at(1).id, QPointF(20000.0, 20000.0)));
    const QPoint blank = blankViewportPoint(view, *window.scene());
    QVERIFY(blank.x() >= 0);

    const ContextMenuProbe probe = probeContextMenu(view->viewport(), blank, [](QMenu *menu) {
        triggerMenuAction(menu, QStringLiteral("fitCanvasViewAction"));
    });
    QVERIFY(probe.opened);
    QCOMPARE(view->transform().m11(), 0.25);
}

void BlueprintSceneTest::resetViewRestoresTheInitialTransform()
{
    MainWindow window;
    QVERIFY(window.addNodeOfType(NodeType::Start));
    window.show();
    QApplication::processEvents();
    QGraphicsView *view = window.graphicsView();
    view->scale(2.0, 2.0);
    view->centerOn(QPointF(600.0, 600.0));
    QCOMPARE(view->transform().m11(), 2.0);
    const QPoint blank = blankViewportPoint(view, *window.scene());
    QVERIFY(blank.x() >= 0);

    const ContextMenuProbe probe = probeContextMenu(view->viewport(), blank, [](QMenu *menu) {
        triggerMenuAction(menu, QStringLiteral("resetCanvasViewAction"));
    });
    QVERIFY(probe.opened);
    QCOMPARE(view->transform().m11(), 1.0);
    const QPointF centre = view->mapToScene(view->viewport()->rect().center());
    QVERIFY(QLineF(centre, window.scene()->sceneRect().center()).length() < 5.0);
}

QTEST_MAIN(BlueprintSceneTest)

#include "tst_blueprint_scene.moc"
