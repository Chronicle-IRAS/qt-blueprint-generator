#include "editor/node_item.h"

#include <QGraphicsSceneMouseEvent>
#include <QObject>
#include <QPainter>
#include <QStyleOptionGraphicsItem>

namespace {
constexpr qreal NodeWidth = 180.0;
constexpr qreal NodeHeight = 96.0;
}

NodeItem::NodeItem(QString nodeId, QString title, QGraphicsItem *parent)
    : QGraphicsItem(parent)
    , m_nodeId(std::move(nodeId))
    , m_title(std::move(title))
{
    setFlags(ItemIsMovable | ItemIsSelectable | ItemSendsGeometryChanges);
    setCacheMode(DeviceCoordinateCache);
}

QString NodeItem::nodeId() const
{
    return m_nodeId;
}

void NodeItem::setTitle(const QString &title)
{
    if (m_title == title) {
        return;
    }
    m_title = title;
    update();
}

void NodeItem::setPositionChangedHandler(std::function<void(const QString &)> handler)
{
    m_positionChangedHandler = std::move(handler);
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
    painter->drawText(QRectF(10.0, 38.0, NodeWidth - 20.0, 22.0), Qt::AlignLeft | Qt::AlignVCenter,
                      QObject::tr("Input                 Output"));

    painter->setPen(QPen(QColor(30, 41, 59), 1.0));
    painter->setBrush(QColor(226, 232, 240));
    painter->drawEllipse(QPointF(0.0, NodeHeight / 2.0), 5.0, 5.0);
    painter->drawEllipse(QPointF(NodeWidth, NodeHeight / 2.0), 5.0, 5.0);
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
    QGraphicsItem::mousePressEvent(event);
}

void NodeItem::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsItem::mouseReleaseEvent(event);
    if (m_dragStart != pos() && m_moveFinishedHandler) {
        m_moveFinishedHandler(m_nodeId, m_dragStart, pos());
    }
}
