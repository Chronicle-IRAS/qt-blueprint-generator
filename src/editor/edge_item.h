#pragma once

#include <QGraphicsPathItem>
#include <QPolygonF>
#include <QString>

class NodeItem;
class QPainterPath;
class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class EdgeItem final : public QGraphicsPathItem
{
public:
    EdgeItem(QString edgeId, QString sourceId, QString targetId, NodeItem *source, NodeItem *target,
             QString label = {}, QGraphicsItem *parent = nullptr);

    QString edgeId() const;
    QString sourceId() const;
    QString targetId() const;
    QString label() const;
    QPointF labelPosition() const;
    QPolygonF arrowPolygon() const;
    void updatePath();

    QRectF boundingRect() const override;
    QPainterPath shape() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

private:
    QString m_edgeId;
    QString m_sourceId;
    QString m_targetId;
    NodeItem *m_source = nullptr;
    NodeItem *m_target = nullptr;
    QString m_label;
    QPointF m_labelPosition;
};
