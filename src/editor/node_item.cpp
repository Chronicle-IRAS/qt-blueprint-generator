#include "editor/node_item.h"

#include <QGraphicsSceneMouseEvent>
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
    setCacheMode(DeviceCoordinateCache);
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
    setToolTip(node.description);
    update();
}

void NodeItem::setTitle(const QString &title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    m_node.name = title;
    update();
}

void NodeItem::setPorts(const QVector<PortSpec> &inputs, const QVector<PortSpec> &outputs)
{
    m_node.inputs = inputs;
    m_node.outputs = outputs;
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

void NodeItem::setPositionChangedHandler(std::function<void(const QString &)> handler)
{
    m_positionChangedHandler = std::move(handler);
}

void NodeItem::setClickedHandler(std::function<void(const QString &)> handler)
{
    m_clickedHandler = std::move(handler);
}

void NodeItem::setMoveFinishedHandler(
    std::function<void(const QString &, const QPointF &, const QPointF &)> handler)
{
    m_moveFinishedHandler = std::move(handler);
}

QRectF NodeItem::boundingRect() const
{
    return {-1.0, -1.0, NodeWidth + 2.0, NodeHeight + 2.0};
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

    painter->setPen(QPen(QColor(30, 41, 59), 1.0));
    painter->setBrush(QColor(226, 232, 240));
    const int inputCount = std::max(1, static_cast<int>(m_node.inputs.size()));
    for (int index = 0; index < m_node.inputs.size(); ++index) {
        const qreal y = NodeHeight * (index + 1.0) / (inputCount + 1.0);
        painter->drawEllipse(QPointF(0.0, y), 5.0, 5.0);
    }
    if (m_node.inputs.isEmpty()) {
        painter->drawEllipse(QPointF(0.0, NodeHeight / 2.0), 5.0, 5.0);
    }
    const int outputCount = std::max(1, static_cast<int>(m_node.outputs.size()));
    for (int index = 0; index < m_node.outputs.size(); ++index) {
        const qreal y = NodeHeight * (index + 1.0) / (outputCount + 1.0);
        painter->drawEllipse(QPointF(NodeWidth, y), 5.0, 5.0);
    }
    if (m_node.outputs.isEmpty()) {
        painter->drawEllipse(QPointF(NodeWidth, NodeHeight / 2.0), 5.0, 5.0);
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
    m_dragStart = pos();
    if (event->button() == Qt::LeftButton && m_clickedHandler) {
        m_clickedHandler(m_nodeId);
    }
    QGraphicsItem::mousePressEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseReleaseEvent(event);
    if (m_dragStart != pos() && m_moveFinishedHandler) {
        m_moveFinishedHandler(m_nodeId, m_dragStart, pos());
    }
}
