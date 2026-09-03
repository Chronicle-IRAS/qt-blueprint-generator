#pragma once

#include <QGraphicsPathItem>
#include <QString>

class NodeItem;

class EdgeItem final : public QGraphicsPathItem
{
public:
    EdgeItem(QString edgeId, QString sourceId, QString targetId, NodeItem *source, NodeItem *target,
             QGraphicsItem *parent = nullptr);

    QString edgeId() const;
    void updatePath();

private:
    QString m_edgeId;
    QString m_sourceId;
    QString m_targetId;
    NodeItem *m_source = nullptr;
    NodeItem *m_target = nullptr;
};
