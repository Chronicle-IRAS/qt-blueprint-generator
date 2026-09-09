#include "editor/node_item.h"

#include <QGraphicsSceneMouseEvent>
#include <QGraphicsScene>
#include <QLineF>
#include <QObject>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

#include <algorithm>

namespace {
constexpr qreal NodeWidth = 180.0;
constexpr qreal NodeHeight = 96.0;
}

NodeItem::NodeItem(QString nodeId, QString title, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_nodeId(std::move(nodeId))
    , m_title(std::move(title))
{
    m_node.id = m_nodeId;
    m_node.name = m_title;
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCacheMode(NoCache);
}

QString NodeItem::nodeId() const
{
    return m_nodeId;
}

const BlueprintNode &NodeItem::node() const
{
    return m_node;
}

void NodeItem::setNode(const BlueprintNode &node)
{
    m_node = node;
    m_nodeId = node.id;
    m_title = node.name;
    if (m_highlightedInputPort >= std::max(1, static_cast<int>(node.inputs.size()))) {
        m_highlightedInputPort = -1;
    }
    setToolTip(node.description);
    update();
}

QPointF NodeItem::inputAnchor(int index) const
{
    const int count = std::max(1, static_cast<int>(m_node.inputs.size()));
    const int clamped = std::clamp(index, 0, count - 1);
    return mapToScene(QPointF(0.0, NodeHeight * (clamped + 1.0) / (count + 1.0)));
}

QPointF NodeItem::outputAnchor(int index) const
{
    const int count = std::max(1, static_cast<int>(m_node.outputs.size()));
    const int clamped = std::clamp(index, 0, count - 1);
    return mapToScene(QPointF(NodeWidth, NodeHeight * (clamped + 1.0) / (count + 1.0)));
}

int NodeItem::inputPortAt(const QPointF &localPosition) const
{
    const int count = std::max(1, static_cast<int>(m_node.inputs.size()));
    for (int index = 0; index < count; ++index) {
        const QPointF center(0.0, NodeHeight * (index + 1.0) / (count + 1.0));
        if (QLineF(center, localPosition).length() <= 10.0) {
            return index;
        }
    }
    return -1;
}

int NodeItem::outputPortAt(const QPointF &localPosition) const
{
    const int count = std::max(1, static_cast<int>(m_node.outputs.size()));
    for (int index = 0; index < count; ++index) {
        const QPointF center(NodeWidth, NodeHeight * (index + 1.0) / (count + 1.0));
        if (QLineF(center, localPosition).length() <= 10.0) {
            return index;
        }
    }
    return -1;
}

int NodeItem::highlightedInputPort() const
{
    return m_highlightedInputPort;
}

void NodeItem::setPositionChangedHandler(std::function<void(const QString &)> handler)
{
    m_positionChangedHandler = std::move(handler);
}

void NodeItem::setClickedHandler(std::function<void(const QString &)> handler)
{
    m_clickedHandler = std::move(handler);
}

void NodeItem::setPortDragHandlers(std::function<void(const QString &, int)> started,
                                   std::function<void(const QPointF &)> moved,
                                   std::function<void(const QPointF &)> finished,
                                   std::function<void()> cancelled)
{
    m_portDragStartedHandler = std::move(started);
    m_portDragMovedHandler = std::move(moved);
    m_portDragFinishedHandler = std::move(finished);
    m_portDragCancelledHandler = std::move(cancelled);
}

void NodeItem::setHighlightedInputPort(int index)
{
    if (m_highlightedInputPort == index) {
        return;
    }
    m_highlightedInputPort = index;
    update();
}

void NodeItem::cancelPortDrag()
{
    m_portDragActive = false;
    // A cancelled drag still receives the eventual left-button release because
    // this item owns the mouse grab. Treat that release as a no-op, not a move.
    m_dragStart = pos();
}

void NodeItem::setMoveFinishedHandler(
    std::function<void(const QString &, const QPointF &, const QPointF &)> handler)
{
    m_moveFinishedHandler = std::move(handler);
}

QRectF NodeItem::boundingRect() const
{
    return {-6.0, -6.0, NodeWidth + 12.0, NodeHeight + 12.0};
}

void NodeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    const QRectF body(0.0, 0.0, NodeWidth, NodeHeight);
    const bool selected = option->state.testFlag(QStyle::State_Selected);
    painter->setPen(QPen(selected ? QColor(42, 130, 218) : QColor(71, 85, 105), selected ? 2.5 : 1.5));
    painter->setBrush(QColor(248, 250, 252));
    painter->drawRoundedRect(body, 8.0, 8.0);

    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(37, 99, 235));
    painter->drawRoundedRect(QRectF(0.0, 0.0, NodeWidth, 28.0), 8.0, 8.0);
    painter->drawRect(0, 18, static_cast<int>(NodeWidth), 10);

    painter->setPen(Qt::white);
    painter->drawText(QRectF(10.0, 0.0, NodeWidth - 20.0, 28.0), Qt::AlignVCenter | Qt::AlignLeft,
                      m_title);
    painter->setPen(QColor(71, 85, 105));
    painter->drawText(QRectF(10.0, 30.0, 75.0, 18.0), Qt::AlignLeft | Qt::AlignVCenter,
                      QObject::tr("Input"));
    painter->drawText(QRectF(NodeWidth - 85.0, 30.0, 75.0, 18.0),
                      Qt::AlignRight | Qt::AlignVCenter, QObject::tr("Output"));
    painter->setPen(QColor(30, 41, 59));
    for (int index = 0; index < m_node.inputs.size() && index < 2; ++index) {
        painter->drawText(QRectF(10.0, 48.0 + index * 18.0, 72.0, 18.0),
                          Qt::AlignLeft | Qt::AlignVCenter, m_node.inputs.at(index).name);
    }
    for (int index = 0; index < m_node.outputs.size() && index < 2; ++index) {
        painter->drawText(QRectF(NodeWidth - 82.0, 48.0 + index * 18.0, 72.0, 18.0),
                          Qt::AlignRight | Qt::AlignVCenter, m_node.outputs.at(index).name);
    }

    const int inputCount = std::max(1, static_cast<int>(m_node.inputs.size()));
    for (int index = 0; index < inputCount; ++index) {
        const qreal y = NodeHeight * (index + 1.0) / (inputCount + 1.0);
        const bool highlighted = index == m_highlightedInputPort;
        painter->setPen(QPen(highlighted ? QColor(22, 163, 74) : QColor(30, 41, 59),
                             highlighted ? 2.5 : 1.0));
        painter->setBrush(highlighted ? QColor(187, 247, 208) : QColor(226, 232, 240));
        painter->drawEllipse(QPointF(0.0, y), 5.0, 5.0);
    }
    painter->setPen(QPen(QColor(30, 41, 59), 1.0));
    painter->setBrush(QColor(226, 232, 240));
    const int outputCount = std::max(1, static_cast<int>(m_node.outputs.size()));
    for (int index = 0; index < outputCount; ++index) {
        const qreal y = NodeHeight * (index + 1.0) / (outputCount + 1.0);
        painter->drawEllipse(QPointF(NodeWidth, y), 5.0, 5.0);
    }
}

QVariant NodeItem::itemChange(GraphicsItemChange change, const QVariant &value)
{
    const QVariant result = QGraphicsItem::itemChange(change, value);
    if (change == ItemPositionHasChanged && m_positionChangedHandler) {
        m_positionChangedHandler(m_nodeId);
    }
    return result;
}

void NodeItem::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() == Qt::RightButton && m_portDragActive) {
        m_portDragActive = false;
        if (m_portDragCancelledHandler) {
            m_portDragCancelledHandler();
        }
        event->accept();
        return;
    }
    if (event->button() == Qt::LeftButton) {
        const int outputIndex = outputPortAt(event->pos());
        if (outputIndex >= 0 && m_portDragStartedHandler) {
            m_portDragActive = true;
            m_portDragStartedHandler(m_nodeId, outputIndex);
            event->accept();
            return;
        }
    }
    m_dragStart = pos();
    m_dragSelectionCollapsed = false;
    if (event->button() == Qt::LeftButton && m_clickedHandler) {
        m_clickedHandler(m_nodeId);
    }
    QGraphicsItem::mousePressEvent(event);
}

void NodeItem::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_portDragActive && event->buttons().testFlag(Qt::LeftButton)) {
        if (m_portDragMovedHandler) {
            m_portDragMovedHandler(event->scenePos());
        }
        event->accept();
        return;
    }
    if (event->buttons().testFlag(Qt::LeftButton) && !m_dragSelectionCollapsed && scene()
        && isSelected()) {
        const QList<QGraphicsItem *> selected = scene()->selectedItems();
        for (QGraphicsItem *item : selected) {
            if (item != this) {
                item->setSelected(false);
            }
        }
        m_dragSelectionCollapsed = true;
    }
    QGraphicsItem::mouseMoveEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    if (m_portDragActive && event->button() == Qt::LeftButton) {
        m_portDragActive = false;
        if (m_portDragFinishedHandler) {
            m_portDragFinishedHandler(event->scenePos());
        }
        event->accept();
        return;
    }
    QGraphicsItem::mouseReleaseEvent(event);
    if (m_dragStart != pos() && m_moveFinishedHandler) {
        m_moveFinishedHandler(m_nodeId, m_dragStart, pos());
    }
}
