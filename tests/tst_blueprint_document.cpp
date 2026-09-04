#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtTest/QtTest>

#include "blueprint/blueprint_document.h"
#include "blueprint/blueprint_serializer.h"

namespace {

BlueprintNode makeNode(NodeType type, const QString &id)
{
    BlueprintNode node;
    node.id = id;
    node.type = type;
    node.name = id + QStringLiteral(" name");
    node.description = id + QStringLiteral(" description");
    node.inputs = {
        {id + QStringLiteral(" input"), QStringLiteral("InputType"), QStringLiteral("input description")},
    };
    node.outputs = {
        {id + QStringLiteral(" output"), QStringLiteral("OutputType"), QStringLiteral("output description")},
    };
    node.constraints = {
        id + QStringLiteral(" constraint one"),
        id + QStringLiteral(" constraint two"),
    };
    node.acceptanceCriteria = {
        id + QStringLiteral(" acceptance one"),
        id + QStringLiteral(" acceptance two"),
    };
    return node;
}

BlueprintDocument makeCompleteDocument()
{
    BlueprintDocument document;
    document.schemaVersion = BlueprintDocument::CurrentSchemaVersion;
    document.projectId = QStringLiteral("complete-project");
    document.projectName = QStringLiteral("Complete Project");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    document.nodes = {
        makeNode(NodeType::Start, QStringLiteral("start")),
        makeNode(NodeType::End, QStringLiteral("end")),
        makeNode(NodeType::UiPage, QStringLiteral("ui-page")),
        makeNode(NodeType::LogicModule, QStringLiteral("logic-module")),
        makeNode(NodeType::Decision, QStringLiteral("decision")),
        makeNode(NodeType::ExternalCode, QStringLiteral("external-code")),
    };

    BlueprintEdge edge;
    edge.id = QStringLiteral("flow-edge");
    edge.source = QStringLiteral("start");
    edge.target = QStringLiteral("ui-page");
    edge.label = QStringLiteral("continue");
    document.edges = {edge};
    return document;
}

QJsonObject validRoot()
{
    QJsonObject port{
        {QStringLiteral("name"), QStringLiteral("request")},
        {QStringLiteral("type"), QStringLiteral("Request")},
        {QStringLiteral("description"), QStringLiteral("input request")},
    };
    QJsonObject node{
        {QStringLiteral("id"), QStringLiteral("start")},
        {QStringLiteral("type"), QStringLiteral("Start")},
        {QStringLiteral("name"), QStringLiteral("Start")},
        {QStringLiteral("description"), QStringLiteral("Entry point")},
        {QStringLiteral("inputs"), QJsonArray{port}},
        {QStringLiteral("outputs"), QJsonArray{}},
        {QStringLiteral("constraints"), QJsonArray{QStringLiteral("stay deterministic")}},
        {QStringLiteral("acceptanceCriteria"), QJsonArray{QStringLiteral("can start")}},
    };
    QJsonObject edge{
        {QStringLiteral("id"), QStringLiteral("edge-1")},
        {QStringLiteral("source"), QStringLiteral("start")},
        {QStringLiteral("target"), QStringLiteral("start")},
        {QStringLiteral("label"), QStringLiteral("loop")},
    };
    return {
        {QStringLiteral("schemaVersion"), BlueprintDocument::CurrentSchemaVersion},
        {QStringLiteral("projectId"), QStringLiteral("project-id")},
        {QStringLiteral("projectName"), QStringLiteral("Project Name")},
        {QStringLiteral("target"), QStringLiteral("qt6-widgets-cpp17-cmake")},
        {QStringLiteral("nodes"), QJsonArray{node}},
        {QStringLiteral("edges"), QJsonArray{edge}},
    };
}

QByteArray encode(const QJsonObject &object)
{
    return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

} // namespace

class BlueprintDocumentTest : public QObject
{
    Q_OBJECT

private slots:
    void serializesAllNodeTypesAndFlowEdge();
    void roundTripPreservesEveryField();
    void rejectsInvalidJson();
    void rejectsUnsupportedSchemaVersions_data();
    void rejectsUnsupportedSchemaVersions();
    void rejectsUnknownNodeType();
    void rejectsMissingRequiredFields_data();
    void rejectsMissingRequiredFields();
    void rejectsWrongTypedRequiredStructures_data();
    void rejectsWrongTypedRequiredStructures();
    void acceptsStructurallyValidDocumentWithoutBusinessValidation();
};

void BlueprintDocumentTest::serializesAllNodeTypesAndFlowEdge()
{
    const BlueprintDocument source = makeCompleteDocument();
    QJsonParseError parseError;
    const QJsonDocument json = QJsonDocument::fromJson(BlueprintSerializer::toJson(source), &parseError);

    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(json.isObject());
    const QJsonObject root = json.object();
    QCOMPARE(root.value(QStringLiteral("schemaVersion")).toInt(), source.schemaVersion);
    QCOMPARE(root.value(QStringLiteral("projectId")).toString(), source.projectId);
    QCOMPARE(root.value(QStringLiteral("projectName")).toString(), source.projectName);
    QCOMPARE(root.value(QStringLiteral("target")).toString(), source.target);

    const QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
    QCOMPARE(nodes.size(), 6);
    const QStringList expectedTypes{
        QStringLiteral("Start"),
        QStringLiteral("End"),
        QStringLiteral("UiPage"),
        QStringLiteral("LogicModule"),
        QStringLiteral("Decision"),
        QStringLiteral("ExternalCode"),
    };
    for (qsizetype index = 0; index < nodes.size(); ++index) {
        QCOMPARE(nodes.at(index).toObject().value(QStringLiteral("type")).toString(),
                 expectedTypes.at(index));
    }

    const QJsonObject richNode = nodes.at(2).toObject();
    QCOMPARE(richNode.value(QStringLiteral("name")).toString(), QStringLiteral("ui-page name"));
    QCOMPARE(richNode.value(QStringLiteral("description")).toString(),
             QStringLiteral("ui-page description"));
    QCOMPARE(richNode.value(QStringLiteral("inputs")).toArray().at(0).toObject(),
             QJsonObject({
                 {QStringLiteral("name"), QStringLiteral("ui-page input")},
                 {QStringLiteral("type"), QStringLiteral("InputType")},
                 {QStringLiteral("description"), QStringLiteral("input description")},
             }));
    QCOMPARE(richNode.value(QStringLiteral("outputs")).toArray().at(0).toObject(),
             QJsonObject({
                 {QStringLiteral("name"), QStringLiteral("ui-page output")},
                 {QStringLiteral("type"), QStringLiteral("OutputType")},
                 {QStringLiteral("description"), QStringLiteral("output description")},
             }));
    QCOMPARE(richNode.value(QStringLiteral("constraints")).toArray(),
             QJsonArray({QStringLiteral("ui-page constraint one"),
                         QStringLiteral("ui-page constraint two")}));
    QCOMPARE(richNode.value(QStringLiteral("acceptanceCriteria")).toArray(),
             QJsonArray({QStringLiteral("ui-page acceptance one"),
                         QStringLiteral("ui-page acceptance two")}));

    const QJsonArray edges = root.value(QStringLiteral("edges")).toArray();
    QCOMPARE(edges.size(), 1);
    QCOMPARE(edges.at(0).toObject(),
             QJsonObject({
                 {QStringLiteral("id"), QStringLiteral("flow-edge")},
                 {QStringLiteral("source"), QStringLiteral("start")},
                 {QStringLiteral("target"), QStringLiteral("ui-page")},
                 {QStringLiteral("label"), QStringLiteral("continue")},
             }));
}

void BlueprintDocumentTest::roundTripPreservesEveryField()
{
    const BlueprintDocument source = makeCompleteDocument();
    QString error = QStringLiteral("stale error");

    const std::optional<BlueprintDocument> restored =
        BlueprintSerializer::fromJson(BlueprintSerializer::toJson(source), &error);

    QVERIFY2(restored.has_value(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QVERIFY(restored.value() == source);
}

void BlueprintDocumentTest::rejectsInvalidJson()
{
    QString error;

    const std::optional<BlueprintDocument> result =
        BlueprintSerializer::fromJson(QByteArrayLiteral("{ definitely not json"), &error);

    QVERIFY(!result.has_value());
    QVERIFY2(!error.isEmpty(), "Invalid JSON must produce a diagnostic");
}

void BlueprintDocumentTest::rejectsUnsupportedSchemaVersions_data()
{
    QTest::addColumn<int>("schemaVersion");

    QTest::addRow("zero") << 0;
    QTest::addRow("negative") << -1;
    QTest::addRow("next-version") << 2;
    QTest::addRow("future-version") << 7;
}

void BlueprintDocumentTest::rejectsUnsupportedSchemaVersions()
{
    QFETCH(int, schemaVersion);
    QJsonObject root = validRoot();
    root.insert(QStringLiteral("schemaVersion"), schemaVersion);
    QString error;

    const std::optional<BlueprintDocument> result = BlueprintSerializer::fromJson(encode(root), &error);

    QVERIFY(!result.has_value());
    QVERIFY2(error.contains(QStringLiteral("root.schemaVersion")), qPrintable(error));
}

void BlueprintDocumentTest::rejectsUnknownNodeType()
{
    QJsonObject root = validRoot();
    QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
    QJsonObject node = nodes.at(0).toObject();
    node.insert(QStringLiteral("type"), QStringLiteral("Mystery"));
    nodes.replace(0, node);
    root.insert(QStringLiteral("nodes"), nodes);
    QString error;

    const std::optional<BlueprintDocument> result = BlueprintSerializer::fromJson(encode(root), &error);

    QVERIFY(!result.has_value());
    QVERIFY2(error.contains(QStringLiteral("root.nodes[0].type")), qPrintable(error));
    QVERIFY2(error.contains(QStringLiteral("Mystery")), qPrintable(error));
}

void BlueprintDocumentTest::rejectsMissingRequiredFields_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("expectedPath");

    const QStringList rootFields{
        QStringLiteral("schemaVersion"),
        QStringLiteral("projectId"),
        QStringLiteral("projectName"),
        QStringLiteral("target"),
        QStringLiteral("nodes"),
        QStringLiteral("edges"),
    };
    for (const QString &field : rootFields) {
        QJsonObject root = validRoot();
        root.remove(field);
        QTest::addRow("root-%s", qPrintable(field)) << encode(root) << QStringLiteral("root.%1").arg(field);
    }

    const QStringList nodeFields{
        QStringLiteral("id"),
        QStringLiteral("type"),
        QStringLiteral("name"),
        QStringLiteral("description"),
        QStringLiteral("inputs"),
        QStringLiteral("outputs"),
        QStringLiteral("constraints"),
        QStringLiteral("acceptanceCriteria"),
    };
    for (const QString &field : nodeFields) {
        QJsonObject root = validRoot();
        QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
        QJsonObject node = nodes.at(0).toObject();
        node.remove(field);
        nodes.replace(0, node);
        root.insert(QStringLiteral("nodes"), nodes);
        QTest::addRow("node-%s", qPrintable(field))
            << encode(root) << QStringLiteral("root.nodes[0].%1").arg(field);
    }

    const QStringList portFields{
        QStringLiteral("name"),
        QStringLiteral("type"),
        QStringLiteral("description"),
    };
    for (const QString &field : portFields) {
        QJsonObject root = validRoot();
        QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
        QJsonObject node = nodes.at(0).toObject();
        QJsonArray inputs = node.value(QStringLiteral("inputs")).toArray();
        QJsonObject port = inputs.at(0).toObject();
        port.remove(field);
        inputs.replace(0, port);
        node.insert(QStringLiteral("inputs"), inputs);
        nodes.replace(0, node);
        root.insert(QStringLiteral("nodes"), nodes);
        QTest::addRow("port-%s", qPrintable(field))
            << encode(root) << QStringLiteral("root.nodes[0].inputs[0].%1").arg(field);
    }

    const QStringList edgeFields{
        QStringLiteral("id"),
        QStringLiteral("source"),
        QStringLiteral("target"),
        QStringLiteral("label"),
    };
    for (const QString &field : edgeFields) {
        QJsonObject root = validRoot();
        QJsonArray edges = root.value(QStringLiteral("edges")).toArray();
        QJsonObject edge = edges.at(0).toObject();
        edge.remove(field);
        edges.replace(0, edge);
        root.insert(QStringLiteral("edges"), edges);
        QTest::addRow("edge-%s", qPrintable(field))
            << encode(root) << QStringLiteral("root.edges[0].%1").arg(field);
    }
}

void BlueprintDocumentTest::rejectsMissingRequiredFields()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, expectedPath);
    QString error;

    const std::optional<BlueprintDocument> result = BlueprintSerializer::fromJson(json, &error);

    QVERIFY(!result.has_value());
    QVERIFY2(error.contains(expectedPath), qPrintable(error));
}

void BlueprintDocumentTest::rejectsWrongTypedRequiredStructures_data()
{
    QTest::addColumn<QByteArray>("json");
    QTest::addColumn<QString>("expectedPath");

    QTest::addRow("root-is-array") << QJsonDocument(QJsonArray{}).toJson(QJsonDocument::Compact)
                                    << QStringLiteral("root");

    QJsonObject root = validRoot();
    root.insert(QStringLiteral("schemaVersion"), QStringLiteral("one"));
    QTest::addRow("schema-version-is-string") << encode(root) << QStringLiteral("root.schemaVersion");

    root = validRoot();
    root.insert(QStringLiteral("schemaVersion"), 1.5);
    QTest::addRow("schema-version-is-fractional") << encode(root) << QStringLiteral("root.schemaVersion");

    root = validRoot();
    root.insert(QStringLiteral("projectId"), true);
    QTest::addRow("project-id-is-boolean") << encode(root) << QStringLiteral("root.projectId");

    root = validRoot();
    root.insert(QStringLiteral("nodes"), QStringLiteral("not an array"));
    QTest::addRow("nodes-is-string") << encode(root) << QStringLiteral("root.nodes");

    root = validRoot();
    root.insert(QStringLiteral("nodes"), QJsonArray{QStringLiteral("not an object")});
    QTest::addRow("node-is-string") << encode(root) << QStringLiteral("root.nodes[0]");

    root = validRoot();
    {
        QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
        QJsonObject node = nodes.at(0).toObject();
        node.insert(QStringLiteral("outputs"), 42);
        nodes.replace(0, node);
        root.insert(QStringLiteral("nodes"), nodes);
    }
    QTest::addRow("outputs-is-number") << encode(root) << QStringLiteral("root.nodes[0].outputs");

    root = validRoot();
    {
        QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
        QJsonObject node = nodes.at(0).toObject();
        QJsonArray inputs = node.value(QStringLiteral("inputs")).toArray();
        QJsonObject port = inputs.at(0).toObject();
        port.insert(QStringLiteral("description"), false);
        inputs.replace(0, port);
        node.insert(QStringLiteral("inputs"), inputs);
        nodes.replace(0, node);
        root.insert(QStringLiteral("nodes"), nodes);
    }
    QTest::addRow("port-description-is-boolean")
        << encode(root) << QStringLiteral("root.nodes[0].inputs[0].description");

    root = validRoot();
    {
        QJsonArray nodes = root.value(QStringLiteral("nodes")).toArray();
        QJsonObject node = nodes.at(0).toObject();
        node.insert(QStringLiteral("constraints"), QJsonArray{QStringLiteral("valid"), 12});
        nodes.replace(0, node);
        root.insert(QStringLiteral("nodes"), nodes);
    }
    QTest::addRow("constraint-is-number")
        << encode(root) << QStringLiteral("root.nodes[0].constraints[1]");

    root = validRoot();
    root.insert(QStringLiteral("edges"), QJsonArray{true});
    QTest::addRow("edge-is-boolean") << encode(root) << QStringLiteral("root.edges[0]");

    root = validRoot();
    {
        QJsonArray edges = root.value(QStringLiteral("edges")).toArray();
        QJsonObject edge = edges.at(0).toObject();
        edge.insert(QStringLiteral("source"), QJsonArray{});
        edges.replace(0, edge);
        root.insert(QStringLiteral("edges"), edges);
    }
    QTest::addRow("edge-source-is-array")
        << encode(root) << QStringLiteral("root.edges[0].source");
}

void BlueprintDocumentTest::rejectsWrongTypedRequiredStructures()
{
    QFETCH(QByteArray, json);
    QFETCH(QString, expectedPath);
    QString error;

    const std::optional<BlueprintDocument> result = BlueprintSerializer::fromJson(json, &error);

    QVERIFY(!result.has_value());
    QVERIFY2(error.contains(expectedPath), qPrintable(error));
}

void BlueprintDocumentTest::acceptsStructurallyValidDocumentWithoutBusinessValidation()
{
    QJsonObject root = validRoot();
    root.insert(QStringLiteral("nodes"), QJsonArray{});
    root.insert(QStringLiteral("edges"), QJsonArray{});
    QString error;

    const std::optional<BlueprintDocument> result = BlueprintSerializer::fromJson(encode(root), &error);

    QVERIFY2(result.has_value(), qPrintable(error));
    QVERIFY(result->nodes.isEmpty());
    QVERIFY(result->edges.isEmpty());
}

QTEST_APPLESS_MAIN(BlueprintDocumentTest)

#include "tst_blueprint_document.moc"
