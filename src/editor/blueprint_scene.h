#pragma once

#include <QGraphicsScene>
#include <QHash>
#include <QUndoStack>

#include <functional>

#include "blueprint/blueprint_document.h"

class EdgeItem;
class NodeItem;
class QGraphicsPathItem;
class QGraphicsSceneMouseEvent;
class QKeyEvent;

class BlueprintScene final : public QGraphicsScene
{
    Q_OBJECT

public:
    explicit BlueprintScene(BlueprintDocument *document, QObject *parent = nullptr);

    bool addNode(const BlueprintNode &node, const QPointF &position);
    bool deleteNode(const QString &nodeId);
    bool moveNode(const QString &nodeId, const QPointF &position);
    bool connectNodes(const QString &sourceId, const QString &targetId, const QString &label = {});
    bool editNodeText(const QString &nodeId, const QString &name, const QString &description);
    bool editNode(const QString &nodeId, const BlueprintNode &node);
    bool isRepresentable() const;

    bool beginConnection(const QString &label = {});
    bool chooseConnectionNode(const QString &nodeId);
    QString connectionSource() const;
    void cancelConnection();
    void setDragConnectionLabel(const QString &label);

    NodeItem *nodeItem(const QString &nodeId) const;
    EdgeItem *edgeItem(const QString &edgeId) const;
    QPointF nodePosition(const QString &nodeId) const;
    QUndoStack *undoStack();
    void setSemanticChangeHandler(std::function<void()> handler);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;

private:
    struct IndexedEdge {
        BlueprintEdge edge;
        qsizetype index = 0;
    };

    bool hasNode(const QString &nodeId) const;
    qsizetype nodeIndex(const QString &nodeId) const;
    qsizetype edgeIndex(const QString &edgeId) const;
    QString nextEdgeId() const;
    void createNodeItem(const BlueprintNode &node, const QPointF &position);
    void createEdgeItem(const BlueprintEdge &edge);
    void addNodeDirect(const BlueprintNode &node, qsizetype index, const QPointF &position);
    void removeNodeDirect(const QString &nodeId, QVector<IndexedEdge> *removedEdges = nullptr);
    void restoreEdgesDirect(const QVector<IndexedEdge> &edges);
    void addEdgeDirect(const BlueprintEdge &edge, qsizetype index);
    void removeEdgeDirect(const QString &edgeId);
    void setNodePositionDirect(const QString &nodeId, const QPointF &position);
    void setNodeDirect(const QString &nodeId, const BlueprintNode &node);
    void updateEdgesForNode(const QString &nodeId);
    void handleItemPositionChanged(const QString &nodeId);
    void handleItemMoveFinished(const QString &nodeId, const QPointF &before, const QPointF &after);
    void handleNodeClicked(const QString &nodeId);
    void beginPortDrag(const QString &nodeId, int outputIndex);
    void updatePortDrag(const QPointF &scenePosition);
    void finishPortDrag(const QPointF &scenePosition);
    void cancelPortDrag();
    NodeItem *inputTargetAt(const QPointF &scenePosition, int *inputIndex) const;
    void setHoveredInput(NodeItem *node, int inputIndex);
    void updateTemporaryConnection(const QPointF &endPosition);
    void notifySemanticChanged();

    BlueprintDocument *m_document = nullptr;
    bool m_representable = true;
    std::function<void()> m_semanticChangeHandler;
    QUndoStack m_undoStack;
    QHash<QString, NodeItem *> m_nodes;
    QHash<QString, EdgeItem *> m_edges;
    QHash<QString, QPointF> m_layout;
    QString m_connectionSource;
    QString m_connectionLabel;
    QString m_dragConnectionLabel;
    QString m_portDragSource;
    int m_portDragOutput = -1;
    QGraphicsPathItem *m_temporaryConnection = nullptr;
    NodeItem *m_hoveredInputNode = nullptr;
    int m_hoveredInputPort = -1;
    bool m_connectionMode = false;
};
