#include "editor/edge_item.h"

#include "editor/node_item.h"

#include <QPainterPath>
#include <QPen>

EdgeItem::EdgeItem(QString edgeId, QString sourceId, QString targetId, NodeItem *source, NodeItem *target,
                   QGraphicsItem *parent)
    : QGraphicsPathItem(parent)
    , m_edgeId(std::move(edgeId))
    , m_sourceId(std::move(sourceId))
    , m_targetId(std::move(targetId))
    , m_source(source)
    , m_target(target)
{
    setPen(QPen(QColor(71, 85, 105), 2.0));
    setZValue(-1.0);
    updatePath();
}

QString EdgeItem::edgeId() const
{
    return m_edgeId;
}

void EdgeItem::updatePath()
{
    if (!m_source || !m_target) {
        return;
    }

    const QPointF source = m_source->sceneBoundingRect().center() + QPointF(90.0, 0.0);
    const QPointF target = m_target->sceneBoundingRect().center() - QPointF(90.0, 0.0);
    QPainterPath path(source);
    const qreal controlOffset = qMax<qreal>(60.0, qAbs(target.x() - source.x()) / 2.0);
    path.cubicTo(source + QPointF(controlOffset, 0.0), target - QPointF(controlOffset, 0.0), target);
    setPath(path);
}
