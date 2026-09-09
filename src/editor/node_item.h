#pragma once

#include <QGraphicsItem>
#include <QPointF>
#include <QString>

#include "blueprint/blueprint_document.h"

#include <functional>

class QGraphicsSceneMouseEvent;
class BlueprintScene;
class QPainter;
class QStyleOptionGraphicsItem;
class QWidget;

class NodeItem final : public QGraphicsItem
{
public:
    explicit NodeItem(QString nodeId, QString title, QGraphicsItem *parent = nullptr);

    QString nodeId() const;
    const BlueprintNode &node() const;
    QPointF inputAnchor(int index = 0) const;
    QPointF outputAnchor(int index = 0) const;
    QRectF boundingRect() const override;
    void paint(QPainter *painter, const QStyleOptionGraphicsItem *option, QWidget *widget) override;

protected:
    QVariant itemChange(GraphicsItemChange change, const QVariant &value) override;
    void mouseDoubleClickEvent(QGraphicsSceneMouseEvent *event) override;
    void mousePressEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseMoveEvent(QGraphicsSceneMouseEvent *event) override;
    void mouseReleaseEvent(QGraphicsSceneMouseEvent *event) override;

private:
    friend class BlueprintScene;

    void setNode(const BlueprintNode &node);
    void setPositionChangedHandler(std::function<void(const QString &)> handler);
    void setClickedHandler(std::function<void(const QString &)> handler);
    void setDoubleClickedHandler(std::function<void(const QString &)> handler);
    void setMoveFinishedHandler(
        std::function<void(const QString &, const QPointF &, const QPointF &)> handler);

    QString m_nodeId;
    QString m_title;
    QPointF m_dragStart;
    bool m_dragSelectionCollapsed = false;
    std::function<void(const QString &)> m_positionChangedHandler;
    std::function<void(const QString &)> m_clickedHandler;
    std::function<void(const QString &)> m_doubleClickedHandler;
    std::function<void(const QString &, const QPointF &, const QPointF &)> m_moveFinishedHandler;
    BlueprintNode m_node;
};
