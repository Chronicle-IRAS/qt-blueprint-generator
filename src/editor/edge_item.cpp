#include "editor/edge_item.h"

#include "editor/node_item.h"

#include <QPainterPath>
#include <QPainter>
#include <QPen>

#include <QtMath>

#include <cmath>

namespace {

QPolygonF arrowPolygonForPath(const QPainterPath &path)
{
    if (path.isEmpty()) {
        return {};
    }
    const QPointF tip = path.pointAtPercent(1.0);
    QPointF direction = tip - path.pointAtPercent(0.96);
    if (qFuzzyIsNull(direction.x()) && qFuzzyIsNull(direction.y())) {
        direction = QPointF(1.0, 0.0);
    }
    const qreal length = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
    direction /= length;
    const QPointF normal(-direction.y(), direction.x());
    return {tip, tip - direction * 11.0 + normal * 5.0, tip - direction * 11.0 - normal * 5.0};
}

} // namespace

EdgeItem::EdgeItem(QString edgeId, QString sourceId, QString targetId, NodeItem *source, NodeItem *target,
                   QString label, QGraphicsItem *parent)
    : QGraphicsPathItem(parent)
    , m_edgeId(std::move(edgeId))
    , m_sourceId(std::move(sourceId))
    , m_targetId(std::move(targetId))
    , m_source(source)
    , m_target(target)
    , m_label(std::move(label))
{
    setPen(QPen(QColor(71, 85, 105), 2.0));
    setZValue(-1.0);
    updatePath();
}

QString EdgeItem::edgeId() const
{
    return m_edgeId;
}

QString EdgeItem::sourceId() const
{
    return m_sourceId;
}

QString EdgeItem::targetId() const
{
    return m_targetId;
}

QString EdgeItem::label() const
{
    return m_label;
}

QPointF EdgeItem::labelPosition() const
{
    return m_labelPosition;
}

QPolygonF EdgeItem::arrowPolygon() const
{
    return arrowPolygonForPath(path());
}

void EdgeItem::updatePath()
{
    if (!m_source || !m_target) {
        return;
    }

    const QPointF source = m_source->outputAnchor();
    const QPointF target = m_target->inputAnchor();
    QPainterPath newPath(source);
    const qreal controlOffset = qMax<qreal>(60.0, qAbs(target.x() - source.x()) / 2.0);
    newPath.cubicTo(source + QPointF(controlOffset, 0.0), target - QPointF(controlOffset, 0.0), target);
    const QPointF newLabelPosition = newPath.pointAtPercent(0.5) + QPointF(0.0, -8.0);
    prepareGeometryChange();
    setPath(newPath);
    m_labelPosition = newLabelPosition;
    update();
}

QRectF EdgeItem::boundingRect() const
{
    QRectF result = QGraphicsPathItem::boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
    const QPolygonF arrow = arrowPolygon();
    if (!arrow.isEmpty()) {
        result = result.united(arrow.boundingRect());
    }
    if (!m_label.isEmpty()) {
        result = result.united(QRectF(m_labelPosition - QPointF(40.0, 12.0), QSizeF(80.0, 24.0)));
    }
    return result;
}

QPainterPath EdgeItem::shape() const
{
    QPainterPath result = QGraphicsPathItem::shape();
    const QPolygonF arrow = arrowPolygon();
    if (!arrow.isEmpty()) {
        QPainterPath arrowPath;
        arrowPath.addPolygon(arrow);
        result.addPath(arrowPath);
    }
    if (!m_label.isEmpty()) {
        result.addRect(QRectF(m_labelPosition - QPointF(40.0, 12.0), QSizeF(80.0, 24.0)));
    }
    return result;
}

void EdgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget)
{
    QGraphicsPathItem::paint(painter, option, widget);
    if (path().isEmpty()) {
        return;
    }
    const QPolygonF arrow = arrowPolygon();
    painter->setPen(Qt::NoPen);
    painter->setBrush(QColor(71, 85, 105));
    painter->drawPolygon(arrow);
    if (!m_label.isEmpty()) {
        painter->setPen(QColor(30, 41, 59));
        painter->setBrush(Qt::NoBrush);
        painter->drawText(QRectF(m_labelPosition - QPointF(40.0, 12.0), QSizeF(80.0, 24.0)),
                          Qt::AlignCenter, m_label);
    }
}
