#include "editor/edge_item.h"

#include "editor/node_item.h"
#include "ui/theme.h"

#include <QPainterPathStroker>
#include <QPainter>
#include <QPen>
#include <QStyleOptionGraphicsItem>

#include <QtMath>

#include <cmath>

namespace {

// Edges are drawn as a few pixel wide curve, so the shape used for mouse picking is a
// widened stroke: a thin line still stays easy to click and select.
constexpr qreal EdgeHitWidth = 12.0;
constexpr qreal LabelWidth = 80.0;
constexpr qreal LabelHeight = 24.0;

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

QPainterPath hitPathFor(const QPainterPath &path)
{
    QPainterPathStroker stroker;
    stroker.setWidth(EdgeHitWidth);
    stroker.setCapStyle(Qt::RoundCap);
    stroker.setJoinStyle(Qt::RoundJoin);
    return stroker.createStroke(path);
}

QRectF labelRectFor(const QPointF &center)
{
    return {center - QPointF(LabelWidth / 2.0, LabelHeight / 2.0), QSizeF(LabelWidth, LabelHeight)};
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
    setPen(QPen(EditorTheme::colors().edge, 2.0));
    setFlag(ItemIsSelectable, true);
    setToolTip(m_label);
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
    const QPainterPath newHitPath = hitPathFor(newPath);
    const QPointF newLabelPosition = newPath.pointAtPercent(0.5) + QPointF(0.0, -8.0);
    prepareGeometryChange();
    setPath(newPath);
    m_hitPath = newHitPath;
    m_labelPosition = newLabelPosition;
    update();
}

QRectF EdgeItem::boundingRect() const
{
    QRectF result = QGraphicsPathItem::boundingRect().adjusted(-2.0, -2.0, 2.0, 2.0);
    // The scene index filters mouse hits by boundingRect(), so the widened hit stroke used by
    // shape() must be inside it instead of relying on the base implementation to include it.
    result = result.united(m_hitPath.boundingRect());
    const QPolygonF arrow = arrowPolygon();
    if (!arrow.isEmpty()) {
        result = result.united(arrow.boundingRect());
    }
    if (!m_label.isEmpty()) {
        result = result.united(labelRectFor(m_labelPosition));
    }
    return result;
}

QPainterPath EdgeItem::shape() const
{
    QPainterPath result = m_hitPath;
    const QPolygonF arrow = arrowPolygon();
    if (!arrow.isEmpty()) {
        result.addPolygon(arrow);
    }
    if (!m_label.isEmpty()) {
        result.addRect(labelRectFor(m_labelPosition));
    }
    return result;
}

void EdgeItem::paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *)
{
    if (path().isEmpty()) {
        return;
    }
    const bool selected = option->state.testFlag(QStyle::State_Selected);
    const auto &colors = EditorTheme::colors();
    const QColor strokeColor = selected ? colors.selection : colors.edge;
    QPen edgePen = pen();
    // The stroke colour is read while painting so an edge follows a theme switch; the pen from
    // the constructor only carries the width.
    edgePen.setColor(strokeColor);
    if (selected) {
        edgePen.setWidthF(edgePen.widthF() + 1.5);
    }

    painter->save();
    painter->setPen(edgePen);
    painter->setBrush(Qt::NoBrush);
    painter->drawPath(path());

    const QPolygonF arrow = arrowPolygon();
    painter->setPen(Qt::NoPen);
    painter->setBrush(strokeColor);
    painter->drawPolygon(arrow);

    if (!m_label.isEmpty()) {
        const QRectF labelRect = labelRectFor(m_labelPosition);
        painter->setPen(QPen(selected ? colors.selection : colors.border, selected ? 2.0 : 1.0));
        painter->setBrush(colors.surface);
        painter->drawRoundedRect(labelRect.adjusted(0.5, 0.5, -0.5, -0.5), 5.0, 5.0);
        painter->setPen(colors.text);
        painter->drawText(labelRect.adjusted(6.0, 0.0, -6.0, 0.0), Qt::AlignCenter,
                          painter->fontMetrics().elidedText(m_label, Qt::ElideRight, 68));
    }
    painter->restore();
}
