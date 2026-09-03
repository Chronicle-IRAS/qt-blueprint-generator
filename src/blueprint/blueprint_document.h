#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

enum class NodeType
{
    Start,
    End,
    UiPage,
    LogicModule,
    Decision,
    ExternalCode,
};

struct PortSpec
{
    QString name;
    QString type;
    QString description;

    bool operator==(const PortSpec &other) const;
    bool operator!=(const PortSpec &other) const;
};

struct BlueprintNode
{
    QString id;
    NodeType type = NodeType::Start;
    QString name;
    QString description;
    QVector<PortSpec> inputs;
    QVector<PortSpec> outputs;
    QStringList constraints;
    QStringList acceptanceCriteria;

    bool operator==(const BlueprintNode &other) const;
    bool operator!=(const BlueprintNode &other) const;
};

struct BlueprintEdge
{
    QString id;
    QString source;
    QString target;
    QString label;

    bool operator==(const BlueprintEdge &other) const;
    bool operator!=(const BlueprintEdge &other) const;
};

struct BlueprintDocument
{
    int schemaVersion = 1;
    QString projectId;
    QString projectName;
    QString target;
    QVector<BlueprintNode> nodes;
    QVector<BlueprintEdge> edges;

    bool operator==(const BlueprintDocument &other) const;
    bool operator!=(const BlueprintDocument &other) const;
};
