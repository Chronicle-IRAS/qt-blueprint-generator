#include "editor/blueprint_scene.h"

#include "editor/edge_item.h"
#include "editor/node_item.h"

#include <QUndoCommand>

#include <QSet>

#include <functional>

namespace {

bool technicallyRepresentable(const BlueprintDocument &document)
{
    QSet<QString> nodeIds;
    for (const BlueprintNode &node : document.nodes) {
        if (node.id.isEmpty() || node.id != node.id.trimmed() || nodeIds.contains(node.id)) {
            return false;
        }
        nodeIds.insert(node.id);
    }
    QSet<QString> edgeIds;
    for (const BlueprintEdge &edge : document.edges) {
        if (edge.id.isEmpty() || edge.id != edge.id.trimmed() || edgeIds.contains(edge.id)
            || !nodeIds.contains(edge.source) || !nodeIds.contains(edge.target)) {
            return false;
        }
        edgeIds.insert(edge.id);
    }
    return true;
}

class SceneCommand final : public QUndoCommand
{
public:
    SceneCommand(QString text, std::function<void()> redo, std::function<void()> undo)
        : QUndoCommand(std::move(text))
        , m_redo(std::move(redo))
        , m_undo(std::move(undo))
    {
    }

    void redo() override { m_redo(); }
    void undo() override { m_undo(); }

private:
    std::function<void()> m_redo;
    std::function<void()> m_undo;
};

} // namespace

BlueprintScene::BlueprintScene(BlueprintDocument *document, QObject *parent)
    : QGraphicsScene(parent)
    , m_document(document)
{
    Q_ASSERT(m_document);
    m_representable = technicallyRepresentable(*m_document);
    setSceneRect(-1000.0, -1000.0, 2000.0, 2000.0);
    if (!m_representable) {
        return;
    }
    for (qsizetype index = 0; index < m_document->nodes.size(); ++index) {
        const BlueprintNode &node = m_document->nodes.at(index);
        if (!hasNode(node.id)) {
            createNodeItem(node, QPointF(index * 240.0, 0.0));
        }
    }
    for (const BlueprintEdge &edge : m_document->edges) {
        if (!m_edges.contains(edge.id) && hasNode(edge.source) && hasNode(edge.target)) {
            createEdgeItem(edge);
        }
    }
}

bool BlueprintScene::addNode(const BlueprintNode &node, const QPointF &position)
{
    if (!m_representable || node.id.isEmpty() || node.id != node.id.trimmed() || hasNode(node.id)) {
        return false;
    }
    const qsizetype index = m_document->nodes.size();
    m_undoStack.push(new SceneCommand(tr("Add node"),
                                      [this, node, index, position] { addNodeDirect(node, index, position); },
                                      [this, id = node.id] { removeNodeDirect(id); }));
    return true;
}

bool BlueprintScene::deleteNode(const QString &nodeId)
{
    const qsizetype index = nodeIndex(nodeId);
    if (index < 0) {
        return false;
    }
    const BlueprintNode node = m_document->nodes.at(index);
    QVector<IndexedEdge> edges;
    for (qsizetype edgeIndex = 0; edgeIndex < m_document->edges.size(); ++edgeIndex) {
        const BlueprintEdge &edge = m_document->edges.at(edgeIndex);
        if (edge.source == nodeId || edge.target == nodeId) {
            edges.append({edge, edgeIndex});
        }
    }
    const QPointF position = nodePosition(nodeId);
    m_undoStack.push(new SceneCommand(
        tr("Delete node"), [this, nodeId] { removeNodeDirect(nodeId); },
        [this, node, index, position, edges] {
            addNodeDirect(node, index, position);
            restoreEdgesDirect(edges);
        }));
    return true;
}

bool BlueprintScene::moveNode(const QString &nodeId, const QPointF &position)
{
    if (!hasNode(nodeId)) {
        return false;
    }
    const QPointF before = nodePosition(nodeId);
    if (before == position) {
        return false;
    }
    m_undoStack.push(new SceneCommand(tr("Move node"),
                                      [this, nodeId, position] { setNodePositionDirect(nodeId, position); },
                                      [this, nodeId, before] { setNodePositionDirect(nodeId, before); }));
    return true;
}

bool BlueprintScene::connectNodes(const QString &sourceId, const QString &targetId, const QString &label)
{
    if (!m_representable || sourceId.isEmpty() || targetId.isEmpty() || sourceId == targetId
        || sourceId != sourceId.trimmed() || targetId != targetId.trimmed() || !hasNode(sourceId)
        || !hasNode(targetId)) {
        return false;
    }
    BlueprintEdge edge;
    edge.id = nextEdgeId();
    edge.source = sourceId;
    edge.target = targetId;
    edge.label = label;
    const qsizetype index = m_document->edges.size();
    m_undoStack.push(new SceneCommand(tr("Connect nodes"),
                                      [this, edge, index] { addEdgeDirect(edge, index); },
                                      [this, id = edge.id] { removeEdgeDirect(id); }));
    return true;
}

bool BlueprintScene::beginConnection(const QString &label)
{
    m_connectionMode = true;
    m_connectionSource.clear();
    m_connectionLabel = label;
    return true;
}

void BlueprintScene::cancelConnection()
{
    m_connectionMode = false;
    m_connectionSource.clear();
    m_connectionLabel.clear();
}

bool BlueprintScene::chooseConnectionNode(const QString &nodeId)
{
    if (!m_connectionMode || !hasNode(nodeId)) {
        return false;
    }
    if (m_connectionSource.isEmpty()) {
        m_connectionSource = nodeId;
        return true;
    }
    if (m_connectionSource == nodeId) {
        return false;
    }
    const QString source = m_connectionSource;
    const QString label = m_connectionLabel;
    m_connectionSource.clear();
    m_connectionLabel.clear();
    m_connectionMode = false;
    return connectNodes(source, nodeId, label);
}

QString BlueprintScene::connectionSource() const
{
    return m_connectionSource;
}

bool BlueprintScene::editNodeText(const QString &nodeId, const QString &name, const QString &description)
{
    const qsizetype index = nodeIndex(nodeId);
    if (index < 0) {
        return false;
    }
    BlueprintNode updated = m_document->nodes.at(index);
    updated.name = name;
    updated.description = description;
    return editNode(nodeId, updated);
}

bool BlueprintScene::editNode(const QString &nodeId, const BlueprintNode &updated)
{
    const qsizetype index = nodeIndex(nodeId);
    if (index < 0 || updated.id != nodeId) {
        return false;
    }
    const BlueprintNode old = m_document->nodes.at(index);
    if (old == updated) {
        return false;
    }
    m_undoStack.push(new SceneCommand(
        tr("Edit node"), [this, nodeId, updated] { setNodeDirect(nodeId, updated); },
        [this, nodeId, old] { setNodeDirect(nodeId, old); }));
    return true;
}

bool BlueprintScene::isRepresentable() const
{
    return m_representable;
}

NodeItem *BlueprintScene::nodeItem(const QString &nodeId) const
{
    return m_nodes.value(nodeId, nullptr);
}

EdgeItem *BlueprintScene::edgeItem(const QString &edgeId) const
{
    return m_edges.value(edgeId, nullptr);
}

QPointF BlueprintScene::nodePosition(const QString &nodeId) const
{
    return m_layout.value(nodeId);
}

QUndoStack *BlueprintScene::undoStack()
{
    return &m_undoStack;
}

void BlueprintScene::setChangeHandler(std::function<void()> handler)
{
    m_changeHandler = std::move(handler);
}

bool BlueprintScene::hasNode(const QString &nodeId) const
{
    return m_nodes.contains(nodeId);
}

qsizetype BlueprintScene::nodeIndex(const QString &nodeId) const
{
    for (qsizetype index = 0; index < m_document->nodes.size(); ++index) {
        if (m_document->nodes.at(index).id == nodeId) {
            return index;
        }
    }
    return -1;
}

qsizetype BlueprintScene::edgeIndex(const QString &edgeId) const
{
    for (qsizetype index = 0; index < m_document->edges.size(); ++index) {
        if (m_document->edges.at(index).id == edgeId) {
            return index;
        }
    }
    return -1;
}

QString BlueprintScene::nextEdgeId() const
{
    for (int number = 1;; ++number) {
        const QString id = QStringLiteral("edge-%1").arg(number);
        if (edgeIndex(id) < 0) {
            return id;
        }
    }
}

void BlueprintScene::createNodeItem(const BlueprintNode &node, const QPointF &position)
{
    auto *item = new NodeItem(node.id, node.name);
    item->setNode(node);
    item->setClickedHandler([this](const QString &id) { handleNodeClicked(id); });
    item->setPositionChangedHandler([this](const QString &id) { handleItemPositionChanged(id); });
    item->setMoveFinishedHandler(
        [this](const QString &id, const QPointF &before, const QPointF &after) {
            handleItemMoveFinished(id, before, after);
        });
    addItem(item);
    m_nodes.insert(node.id, item);
    m_layout.insert(node.id, position);
    item->setPos(position);
}

void BlueprintScene::createEdgeItem(const BlueprintEdge &edge)
{
    auto *item = new EdgeItem(edge.id, edge.source, edge.target, nodeItem(edge.source), nodeItem(edge.target),
                              edge.label);
    addItem(item);
    m_edges.insert(edge.id, item);
}

void BlueprintScene::addNodeDirect(const BlueprintNode &node, qsizetype index, const QPointF &position)
{
    if (!m_representable || node.id.isEmpty() || node.id != node.id.trimmed() || hasNode(node.id)) {
        return;
    }
    m_document->nodes.insert(index, node);
    createNodeItem(node, position);
    notifyChanged();
}

void BlueprintScene::removeNodeDirect(const QString &nodeId, QVector<IndexedEdge> *removedEdges)
{
    const qsizetype index = nodeIndex(nodeId);
    if (index < 0) {
        return;
    }
    if (nodeId == m_connectionSource) {
        cancelConnection();
    }

    QVector<IndexedEdge> localEdges;
    for (qsizetype edgePosition = 0; edgePosition < m_document->edges.size(); ++edgePosition) {
        const BlueprintEdge &edge = m_document->edges.at(edgePosition);
        if (edge.source == nodeId || edge.target == nodeId) {
            localEdges.append({edge, edgePosition});
        }
    }
    for (qsizetype edgePosition = localEdges.size() - 1; edgePosition >= 0; --edgePosition) {
        removeEdgeDirect(localEdges.at(edgePosition).edge.id);
    }
    if (removedEdges) {
        *removedEdges = localEdges;
    }

    NodeItem *item = m_nodes.take(nodeId);
    if (item) {
        removeItem(item);
        delete item;
    }
    m_layout.remove(nodeId);
    m_document->nodes.removeAt(index);
    notifyChanged();
}

void BlueprintScene::restoreEdgesDirect(const QVector<IndexedEdge> &edges)
{
    for (const IndexedEdge &indexedEdge : edges) {
        addEdgeDirect(indexedEdge.edge, indexedEdge.index);
    }
}

void BlueprintScene::addEdgeDirect(const BlueprintEdge &edge, qsizetype index)
{
    if (!m_representable || edge.id.isEmpty() || edge.id != edge.id.trimmed() || m_edges.contains(edge.id)
        || !hasNode(edge.source) || !hasNode(edge.target)) {
        return;
    }
    m_document->edges.insert(index, edge);
    createEdgeItem(edge);
    notifyChanged();
}

void BlueprintScene::removeEdgeDirect(const QString &edgeId)
{
    const qsizetype index = edgeIndex(edgeId);
    if (index < 0) {
        return;
    }
    EdgeItem *item = m_edges.take(edgeId);
    if (item) {
        removeItem(item);
        delete item;
    }
    m_document->edges.removeAt(index);
    notifyChanged();
}

void BlueprintScene::setNodePositionDirect(const QString &nodeId, const QPointF &position)
{
    NodeItem *item = nodeItem(nodeId);
    if (!item) {
        return;
    }
    m_layout.insert(nodeId, position);
    item->setPos(position);
    updateEdgesForNode(nodeId);
    notifyChanged();
}

void BlueprintScene::setNodeDirect(const QString &nodeId, const BlueprintNode &node)
{
    const qsizetype index = nodeIndex(nodeId);
    if (index < 0 || node.id != nodeId) {
        return;
    }
    m_document->nodes[index] = node;
    if (NodeItem *item = nodeItem(nodeId)) {
        item->setNode(node);
    }
    updateEdgesForNode(nodeId);
    notifyChanged();
}

void BlueprintScene::updateEdgesForNode(const QString &nodeId)
{
    for (const BlueprintEdge &edge : m_document->edges) {
        if (edge.source == nodeId || edge.target == nodeId) {
            if (EdgeItem *item = edgeItem(edge.id)) {
                item->updatePath();
            }
        }
    }
}

void BlueprintScene::handleItemPositionChanged(const QString &nodeId)
{
    if (NodeItem *item = nodeItem(nodeId)) {
        m_layout.insert(nodeId, item->pos());
        updateEdgesForNode(nodeId);
    }
}

void BlueprintScene::handleItemMoveFinished(const QString &nodeId, const QPointF &before, const QPointF &after)
{
    if (hasNode(nodeId) && before != after) {
        m_undoStack.push(new SceneCommand(
            tr("Move node"), [this, nodeId, after] { setNodePositionDirect(nodeId, after); },
            [this, nodeId, before] { setNodePositionDirect(nodeId, before); }));
    }
}

void BlueprintScene::handleNodeClicked(const QString &nodeId)
{
    if (m_connectionMode) {
        chooseConnectionNode(nodeId);
    }
}

void BlueprintScene::notifyChanged()
{
    if (m_changeHandler) {
        m_changeHandler();
    }
}
