#pragma once

#include <QGraphicsItem>
#include <QPointF>
#include <QString>

#include <functional>

class QGraphicsSceneMouseEvent;
class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class NodeItem final : public QGraphicsItem
{
public:
    explicit NodeItem(QString nodeId, QString title, QGraphicsItem *parent = nullptr);

    QString nodeId() const;
    void setTitle(const QString &title);
    void setPositionChangedHandler(std::function<void(const QString &)> handler);
    void setMoveFinishedHandler(
        std::function<void(const QString &, const QPointF &, const QPointF &)> handler);

    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    QString m_nodeId;
    QString m_title;
    QPointF m_dragStart;
    std::function<void(const QString &)> m_positionChangedHandler;
    std::function<void(const QString &, const QPointF &, const QPointF &)> m_moveFinishedHandler;
};
