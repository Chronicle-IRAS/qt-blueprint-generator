#include "editor/edge_item.h"

#include "editor/node_item.h"

#include <QPainterPath>
#include <QPainter>
#include <QPen>

#include <QtMath>

#include <cmath>

EdgeItem::EdgeItem(QString edgeId, QString sourceId, QString targetId, NodeItem *source, NodeItem *target,
                   QGraphicsItem *parent)
    : EdgeItem(std::move(edgeId), std::move(sourceId), std::move(targetId), source, target, {}, parent)
{
}

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

void EdgeItem::updatePath()
{
    if (!m_source || !m_target) {
        return;
    }

    const QPointF source = m_source->outputAnchor();
    const QPointF target = m_target->inputAnchor();
    QPainterPath path(source);
    const qreal controlOffset = qMax<qreal>(60.0, qAbs(target.x() - source.x()) / 2.0);
    path.cubicTo(source + QPointF(controlOffset, 0.0), target - QPointF(controlOffset, 0.0), target);
    prepareGeometryChange();
    setPath(path);
    m_labelPosition = path.pointAtPercent(0.5) + QPointF(0.0, -8.0);
    update();
}

QRectF EdgeItem::boundingRect() const
{
    QRectF result = QGraphicsPathItem::boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
    if (!m_label.isEmpty()) {
        result = result.united(QRectF(m_labelPosition - QPointF(40.0, 12.0), QSizeF(80.0, 24.0)));
    }
    return result;
}

QPainterPath EdgeItem::shape() const
{
    QPainterPath result = QGraphicsPathItem::shape();
    if (!path().isEmpty()) {
        const QPointF tip = path().pointAtPercent(1.0);
        QPointF direction = tip - path().pointAtPercent(0.96);
        if (qFuzzyIsNull(direction.x()) && qFuzzyIsNull(direction.y())) {
            direction = QPointF(1.0, 0.0);
        }
        const qreal length = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
        direction /= length;
        const QPointF normal(-direction.y(), direction.x());
        QPainterPath arrow;
        arrow.moveTo(tip);
        arrow.lineTo(tip - direction * 11.0 + normal * 5.0);
        arrow.lineTo(tip - direction * 11.0 - normal * 5.0);
        arrow.closeSubpath();
        result.addPath(arrow);
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
    const QPointF tip = path().pointAtPercent(1.0);
    QPointF direction = tip - path().pointAtPercent(0.96);
    if (qFuzzyIsNull(direction.x()) && qFuzzyIsNull(direction.y())) {
        direction = QPointF(1.0, 0.0);
    }
    const qreal length = std::sqrt(direction.x() * direction.x() + direction.y() * direction.y());
    direction /= length;
    const QPointF normal(-direction.y(), direction.x());
    QPolygonF arrow{tip, tip - direction * 11.0 + normal * 5.0,
                    tip - direction * 11.0 - normal * 5.0};
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
