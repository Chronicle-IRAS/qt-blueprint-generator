#include "blueprint/blueprint_serializer.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QJsonValue>

#include <cmath>
#include <limits>
#include <utility>

namespace {

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

QString fieldPath(const QString &objectPath, const QString &field)
{
    return objectPath + QLatin1Char('.') + field;
}

bool readRequiredString(const QJsonObject &object,
                        const QString &key,
                        const QString &objectPath,
                        QString &result,
                        QString *errorMessage)
{
    const QString path = fieldPath(objectPath, key);
    if (!object.contains(key)) {
        return fail(errorMessage, QStringLiteral("%1 is required").arg(path));
    }

    const QJsonValue value = object.value(key);
    if (!value.isString()) {
        return fail(errorMessage, QStringLiteral("%1 must be a string").arg(path));
    }

    result = value.toString();
    return true;
}

bool readRequiredArray(const QJsonObject &object,
                       const QString &key,
                       const QString &objectPath,
                       QJsonArray &result,
                       QString *errorMessage)
{
    const QString path = fieldPath(objectPath, key);
    if (!object.contains(key)) {
        return fail(errorMessage, QStringLiteral("%1 is required").arg(path));
    }

    const QJsonValue value = object.value(key);
    if (!value.isArray()) {
        return fail(errorMessage, QStringLiteral("%1 must be an array").arg(path));
    }

    result = value.toArray();
    return true;
}

bool readSchemaVersion(const QJsonObject &object, int &result, QString *errorMessage)
{
    const QString key = QStringLiteral("schemaVersion");
    const QString path = QStringLiteral("root.schemaVersion");
    if (!object.contains(key)) {
        return fail(errorMessage, QStringLiteral("%1 is required").arg(path));
    }

    const QJsonValue value = object.value(key);
    const double number = value.toDouble(std::numeric_limits<double>::quiet_NaN());
    if (!value.isDouble() || !std::isfinite(number) || std::trunc(number) != number
        || number < std::numeric_limits<int>::min() || number > std::numeric_limits<int>::max()) {
        return fail(errorMessage, QStringLiteral("%1 must be an integer").arg(path));
    }

    const int schemaVersion = static_cast<int>(number);
    if (schemaVersion != BlueprintDocument::CurrentSchemaVersion) {
        return fail(errorMessage,
                    QStringLiteral("%1 has unsupported version %2; supported version is %3")
                        .arg(path)
                        .arg(schemaVersion)
                        .arg(BlueprintDocument::CurrentSchemaVersion));
    }

    result = schemaVersion;
    return true;
}

QString nodeTypeName(NodeType type)
{
    switch (type) {
    case NodeType::Start:
        return QStringLiteral("Start");
    case NodeType::End:
        return QStringLiteral("End");
    case NodeType::UiPage:
        return QStringLiteral("UiPage");
    case NodeType::LogicModule:
        return QStringLiteral("LogicModule");
    case NodeType::Decision:
        return QStringLiteral("Decision");
    case NodeType::ExternalCode:
        return QStringLiteral("ExternalCode");
    }
    return {};
}

std::optional<NodeType> parseNodeType(const QString &name)
{
    if (name == QStringLiteral("Start")) {
        return NodeType::Start;
    }
    if (name == QStringLiteral("End")) {
        return NodeType::End;
    }
    if (name == QStringLiteral("UiPage")) {
        return NodeType::UiPage;
    }
    if (name == QStringLiteral("LogicModule")) {
        return NodeType::LogicModule;
    }
    if (name == QStringLiteral("Decision")) {
        return NodeType::Decision;
    }
    if (name == QStringLiteral("ExternalCode")) {
        return NodeType::ExternalCode;
    }
    return std::nullopt;
}

QJsonObject portToJson(const PortSpec &port)
{
    return {
        {QStringLiteral("name"), port.name},
        {QStringLiteral("type"), port.type},
        {QStringLiteral("description"), port.description},
    };
}

QJsonArray portsToJson(const QVector<PortSpec> &ports)
{
    QJsonArray result;
    for (const PortSpec &port : ports) {
        result.append(portToJson(port));
    }
    return result;
}

QJsonArray stringsToJson(const QStringList &strings)
{
    QJsonArray result;
    for (const QString &string : strings) {
        result.append(string);
    }
    return result;
}

QJsonObject nodeToJson(const BlueprintNode &node)
{
    return {
        {QStringLiteral("id"), node.id},
        {QStringLiteral("type"), nodeTypeName(node.type)},
        {QStringLiteral("name"), node.name},
        {QStringLiteral("description"), node.description},
        {QStringLiteral("inputs"), portsToJson(node.inputs)},
        {QStringLiteral("outputs"), portsToJson(node.outputs)},
        {QStringLiteral("constraints"), stringsToJson(node.constraints)},
        {QStringLiteral("acceptanceCriteria"), stringsToJson(node.acceptanceCriteria)},
    };
}

QJsonObject edgeToJson(const BlueprintEdge &edge)
{
    return {
        {QStringLiteral("id"), edge.id},
        {QStringLiteral("source"), edge.source},
        {QStringLiteral("target"), edge.target},
        {QStringLiteral("label"), edge.label},
    };
}

bool parsePorts(const QJsonObject &nodeObject,
                const QString &key,
                const QString &nodePath,
                QVector<PortSpec> &result,
                QString *errorMessage)
{
    QJsonArray values;
    if (!readRequiredArray(nodeObject, key, nodePath, values, errorMessage)) {
        return false;
    }

    QVector<PortSpec> ports;
    ports.reserve(values.size());
    const QString portsPath = fieldPath(nodePath, key);
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString portPath = QStringLiteral("%1[%2]").arg(portsPath).arg(index);
        const QJsonValue value = values.at(index);
        if (!value.isObject()) {
            return fail(errorMessage, QStringLiteral("%1 must be an object").arg(portPath));
        }

        const QJsonObject object = value.toObject();
        PortSpec port;
        if (!readRequiredString(object, QStringLiteral("name"), portPath, port.name, errorMessage)
            || !readRequiredString(object, QStringLiteral("type"), portPath, port.type, errorMessage)
            || !readRequiredString(object,
                                   QStringLiteral("description"),
                                   portPath,
                                   port.description,
                                   errorMessage)) {
            return false;
        }
        ports.append(std::move(port));
    }

    result = std::move(ports);
    return true;
}

bool parseStringList(const QJsonObject &nodeObject,
                     const QString &key,
                     const QString &nodePath,
                     QStringList &result,
                     QString *errorMessage)
{
    QJsonArray values;
    if (!readRequiredArray(nodeObject, key, nodePath, values, errorMessage)) {
        return false;
    }

    QStringList strings;
    strings.reserve(values.size());
    const QString listPath = fieldPath(nodePath, key);
    for (qsizetype index = 0; index < values.size(); ++index) {
        const QString valuePath = QStringLiteral("%1[%2]").arg(listPath).arg(index);
        const QJsonValue value = values.at(index);
        if (!value.isString()) {
            return fail(errorMessage, QStringLiteral("%1 must be a string").arg(valuePath));
        }
        strings.append(value.toString());
    }

    result = std::move(strings);
    return true;
}

bool parseNode(const QJsonObject &object,
               const QString &nodePath,
               BlueprintNode &result,
               QString *errorMessage)
{
    BlueprintNode node;
    QString typeName;
    if (!readRequiredString(object, QStringLiteral("id"), nodePath, node.id, errorMessage)
        || !readRequiredString(object, QStringLiteral("type"), nodePath, typeName, errorMessage)
        || !readRequiredString(object, QStringLiteral("name"), nodePath, node.name, errorMessage)
        || !readRequiredString(object,
                               QStringLiteral("description"),
                               nodePath,
                               node.description,
                               errorMessage)) {
        return false;
    }

    const std::optional<NodeType> type = parseNodeType(typeName);
    if (!type.has_value()) {
        return fail(errorMessage,
                    QStringLiteral("%1 has unknown node type '%2'")
                        .arg(fieldPath(nodePath, QStringLiteral("type")), typeName));
    }
    node.type = type.value();

    if (!parsePorts(object, QStringLiteral("inputs"), nodePath, node.inputs, errorMessage)
        || !parsePorts(object, QStringLiteral("outputs"), nodePath, node.outputs, errorMessage)
        || !parseStringList(object,
                            QStringLiteral("constraints"),
                            nodePath,
                            node.constraints,
                            errorMessage)
        || !parseStringList(object,
                            QStringLiteral("acceptanceCriteria"),
                            nodePath,
                            node.acceptanceCriteria,
                            errorMessage)) {
        return false;
    }

    result = std::move(node);
    return true;
}

bool parseEdge(const QJsonObject &object,
               const QString &edgePath,
               BlueprintEdge &result,
               QString *errorMessage)
{
    BlueprintEdge edge;
    if (!readRequiredString(object, QStringLiteral("id"), edgePath, edge.id, errorMessage)
        || !readRequiredString(object,
                               QStringLiteral("source"),
                               edgePath,
                               edge.source,
                               errorMessage)
        || !readRequiredString(object,
                               QStringLiteral("target"),
                               edgePath,
                               edge.target,
                               errorMessage)
        || !readRequiredString(object, QStringLiteral("label"), edgePath, edge.label, errorMessage)) {
        return false;
    }

    result = std::move(edge);
    return true;
}

} // namespace

QByteArray BlueprintSerializer::toJson(const BlueprintDocument &document)
{
    QJsonArray nodes;
    for (const BlueprintNode &node : document.nodes) {
        nodes.append(nodeToJson(node));
    }

    QJsonArray edges;
    for (const BlueprintEdge &edge : document.edges) {
        edges.append(edgeToJson(edge));
    }

    const QJsonObject root{
        {QStringLiteral("schemaVersion"), document.schemaVersion},
        {QStringLiteral("projectId"), document.projectId},
        {QStringLiteral("projectName"), document.projectName},
        {QStringLiteral("target"), document.target},
        {QStringLiteral("nodes"), nodes},
        {QStringLiteral("edges"), edges},
    };
    return QJsonDocument(root).toJson(QJsonDocument::Indented);
}

std::optional<BlueprintDocument> BlueprintSerializer::fromJson(const QByteArray &json,
                                                               QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    QJsonParseError parseError;
    const QJsonDocument jsonDocument = QJsonDocument::fromJson(json, &parseError);
    if (parseError.error != QJsonParseError::NoError) {
        fail(errorMessage,
             QStringLiteral("Invalid JSON at offset %1: %2")
                 .arg(parseError.offset)
                 .arg(parseError.errorString()));
        return std::nullopt;
    }
    if (!jsonDocument.isObject()) {
        fail(errorMessage, QStringLiteral("root must be an object"));
        return std::nullopt;
    }

    const QJsonObject root = jsonDocument.object();
    BlueprintDocument document;
    if (!readSchemaVersion(root, document.schemaVersion, errorMessage)
        || !readRequiredString(root,
                               QStringLiteral("projectId"),
                               QStringLiteral("root"),
                               document.projectId,
                               errorMessage)
        || !readRequiredString(root,
                               QStringLiteral("projectName"),
                               QStringLiteral("root"),
                               document.projectName,
                               errorMessage)
        || !readRequiredString(root,
                               QStringLiteral("target"),
                               QStringLiteral("root"),
                               document.target,
                               errorMessage)) {
        return std::nullopt;
    }

    QJsonArray nodeValues;
    if (!readRequiredArray(root,
                           QStringLiteral("nodes"),
                           QStringLiteral("root"),
                           nodeValues,
                           errorMessage)) {
        return std::nullopt;
    }
    document.nodes.reserve(nodeValues.size());
    for (qsizetype index = 0; index < nodeValues.size(); ++index) {
        const QString nodePath = QStringLiteral("root.nodes[%1]").arg(index);
        const QJsonValue value = nodeValues.at(index);
        if (!value.isObject()) {
            fail(errorMessage, QStringLiteral("%1 must be an object").arg(nodePath));
            return std::nullopt;
        }

        BlueprintNode node;
        if (!parseNode(value.toObject(), nodePath, node, errorMessage)) {
            return std::nullopt;
        }
        document.nodes.append(std::move(node));
    }

    QJsonArray edgeValues;
    if (!readRequiredArray(root,
                           QStringLiteral("edges"),
                           QStringLiteral("root"),
                           edgeValues,
                           errorMessage)) {
        return std::nullopt;
    }
    document.edges.reserve(edgeValues.size());
    for (qsizetype index = 0; index < edgeValues.size(); ++index) {
        const QString edgePath = QStringLiteral("root.edges[%1]").arg(index);
        const QJsonValue value = edgeValues.at(index);
        if (!value.isObject()) {
            fail(errorMessage, QStringLiteral("%1 must be an object").arg(edgePath));
            return std::nullopt;
        }

        BlueprintEdge edge;
        if (!parseEdge(value.toObject(), edgePath, edge, errorMessage)) {
            return std::nullopt;
        }
        document.edges.append(std::move(edge));
    }

    return document;
}
