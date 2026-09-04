#include "editor/blueprint_scene.h"
#include "generation/ir_compiler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRegularExpression>
#include <QUndoStack>
#include <QtTest>

#include <algorithm>

namespace {

PortSpec port(const QString &name, const QString &type, const QString &description)
{
    return {name, type, description};
}

BlueprintNode node(const QString &id,
                   NodeType type,
                   const QString &name,
                   const QString &description)
{
    BlueprintNode value;
    value.id = id;
    value.type = type;
    value.name = name;
    value.description = description;
    return value;
}

BlueprintEdge edge(const QString &id, const QString &source, const QString &target)
{
    return {id, source, target, QString()};
}

BlueprintDocument documentWithChain()
{
    BlueprintDocument document;
    document.projectId = QStringLiteral("compiler-demo");
    document.projectName = QStringLiteral("Compiler Demo");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");

    BlueprintNode source = node(QStringLiteral("source"),
                                NodeType::LogicModule,
                                QStringLiteral("SourceService"),
                                QStringLiteral("Produces source data"));
    source.outputs = {port(QStringLiteral("sourceOut"),
                           QStringLiteral("QString"),
                           QStringLiteral("Public source value"))};
    source.constraints = {QStringLiteral("private upstream implementation detail")};

    BlueprintNode current = node(QStringLiteral("current"),
                                 NodeType::LogicModule,
                                 QStringLiteral("CurrentService"),
                                 QStringLiteral("Transforms source data"));
    current.inputs = {port(QStringLiteral("request"),
                           QStringLiteral("QString"),
                           QStringLiteral("Transformation request"))};
    current.outputs = {port(QStringLiteral("result"),
                            QStringLiteral("bool"),
                            QStringLiteral("Transformation result"))};
    current.constraints = {QStringLiteral("Do not block the UI thread")};
    current.acceptanceCriteria = {QStringLiteral("Reports a failed transformation")};

    BlueprintNode target = node(QStringLiteral("target"),
                                NodeType::UiPage,
                                QStringLiteral("TargetPage"),
                                QStringLiteral("Displays the result"));
    target.inputs = {port(QStringLiteral("targetIn"),
                          QStringLiteral("bool"),
                          QStringLiteral("Result to display"))};
    target.acceptanceCriteria = {QStringLiteral("private downstream implementation detail")};

    BlueprintNode distant = node(QStringLiteral("distant"),
                                 NodeType::LogicModule,
                                 QStringLiteral("DistantService"),
                                 QStringLiteral("Must not enter the current context"));

    document.nodes = {
        current,
        node(QStringLiteral("end"), NodeType::End, QStringLiteral("End"), QStringLiteral("Done")),
        target,
        source,
        distant,
        node(QStringLiteral("start"),
             NodeType::Start,
             QStringLiteral("Start"),
             QStringLiteral("Launch")),
    };
    document.edges = {
        edge(QStringLiteral("edge-4"), QStringLiteral("distant"), QStringLiteral("end")),
        edge(QStringLiteral("edge-2"), QStringLiteral("source"), QStringLiteral("current")),
        edge(QStringLiteral("edge-1"), QStringLiteral("start"), QStringLiteral("source")),
        edge(QStringLiteral("edge-3"), QStringLiteral("current"), QStringLiteral("target")),
        edge(QStringLiteral("edge-3b"), QStringLiteral("target"), QStringLiteral("distant")),
    };
    return document;
}

QJsonObject moduleById(const QJsonObject &ir, const QString &id)
{
    for (const QJsonValue &value : ir.value(QStringLiteral("modules")).toArray()) {
        const QJsonObject module = value.toObject();
        if (module.value(QStringLiteral("id")).toString() == id) {
            return module;
        }
    }
    return {};
}

QStringList moduleIds(const QJsonObject &ir)
{
    QStringList ids;
    for (const QJsonValue &value : ir.value(QStringLiteral("modules")).toArray()) {
        ids.append(value.toObject().value(QStringLiteral("id")).toString());
    }
    return ids;
}

} // namespace

class IrCompilerTest : public QObject
{
    Q_OBJECT

private slots:
    void sortsModulesAndProducesCanonicalJson();
    void layoutChangesDoNotAffectIr();
    void moduleContextContainsOnlyDirectPublicInterfaces();
    void externalCodeNeighborProvidesDescriptionOnly();
    void emitsOnlyGeneratableModules();
    void producesPortableCppNamespaces_data();
    void producesPortableCppNamespaces();
};

void IrCompilerTest::sortsModulesAndProducesCanonicalJson()
{
    BlueprintDocument first = documentWithChain();
    BlueprintDocument reordered = first;
    std::reverse(reordered.nodes.begin(), reordered.nodes.end());
    std::reverse(reordered.edges.begin(), reordered.edges.end());

    const QJsonObject firstIr = IrCompiler::compile(first);
    const QJsonObject reorderedIr = IrCompiler::compile(reordered);
    const QByteArray firstJson = IrCompiler::toCanonicalJson(firstIr);
    const QByteArray reorderedJson = IrCompiler::toCanonicalJson(reorderedIr);

    QCOMPARE(moduleIds(firstIr),
             QStringList({QStringLiteral("current"),
                          QStringLiteral("distant"),
                          QStringLiteral("source"),
                          QStringLiteral("target")}));
    QCOMPARE(firstJson, reorderedJson);
    QVERIFY(!firstJson.endsWith('\n'));

    QJsonParseError error;
    const QJsonDocument parsed = QJsonDocument::fromJson(firstJson, &error);
    QCOMPARE(error.error, QJsonParseError::NoError);
    QVERIFY(parsed.isObject());

    const QJsonObject project = firstIr.value(QStringLiteral("project")).toObject();
    QCOMPARE(project.value(QStringLiteral("namespace")).toString(), QStringLiteral("Compiler_Demo"));
    QCOMPARE(project.value(QStringLiteral("target")).toString(),
             QStringLiteral("qt6-widgets-cpp17-cmake"));
    QCOMPARE(project.value(QStringLiteral("framework")).toString(), QStringLiteral("Qt 6 Widgets"));
    QCOMPARE(project.value(QStringLiteral("languageStandard")).toString(), QStringLiteral("C++17"));
    QCOMPARE(project.value(QStringLiteral("buildSystem")).toString(), QStringLiteral("CMake"));
}

void IrCompilerTest::layoutChangesDoNotAffectIr()
{
    BlueprintDocument document = documentWithChain();
    QUndoStack undoStack;
    BlueprintScene scene(&document, &undoStack);
    const QByteArray before = IrCompiler::toCanonicalJson(IrCompiler::compile(document));

    QVERIFY(scene.moveNode(QStringLiteral("current"), QPointF(420.0, -75.0)));
    const QByteArray after = IrCompiler::toCanonicalJson(IrCompiler::compile(document));

    QCOMPARE(after, before);
}

void IrCompilerTest::moduleContextContainsOnlyDirectPublicInterfaces()
{
    const QJsonObject ir = IrCompiler::compile(documentWithChain());
    const QJsonObject current = moduleById(ir, QStringLiteral("current"));
    QVERIFY(!current.isEmpty());

    QCOMPARE(current.value(QStringLiteral("description")).toString(),
             QStringLiteral("Transforms source data"));
    QCOMPARE(current.value(QStringLiteral("constraints")).toArray().first().toString(),
             QStringLiteral("Do not block the UI thread"));
    QCOMPARE(current.value(QStringLiteral("acceptanceCriteria")).toArray().first().toString(),
             QStringLiteral("Reports a failed transformation"));

    const QJsonArray upstream = current.value(QStringLiteral("upstream")).toArray();
    QCOMPARE(upstream.size(), 1);
    QCOMPARE(upstream.first().toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("source"));
    QCOMPARE(upstream.first().toObject().value(QStringLiteral("description")).toString(),
             QStringLiteral("Produces source data"));
    QCOMPARE(upstream.first()
                 .toObject()
                 .value(QStringLiteral("outputs"))
                 .toArray()
                 .first()
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("sourceOut"));
    QVERIFY(!upstream.first().toObject().contains(QStringLiteral("constraints")));
    QVERIFY(!upstream.first().toObject().contains(QStringLiteral("edgeId")));

    const QJsonArray downstream = current.value(QStringLiteral("downstream")).toArray();
    QCOMPARE(downstream.size(), 1);
    QCOMPARE(downstream.first().toObject().value(QStringLiteral("id")).toString(),
             QStringLiteral("target"));
    QCOMPARE(downstream.first().toObject().value(QStringLiteral("description")).toString(),
             QStringLiteral("Displays the result"));
    QCOMPARE(downstream.first()
                 .toObject()
                 .value(QStringLiteral("inputs"))
                 .toArray()
                 .first()
                 .toObject()
                 .value(QStringLiteral("name"))
                 .toString(),
             QStringLiteral("targetIn"));
    QVERIFY(!downstream.first().toObject().contains(QStringLiteral("acceptanceCriteria")));
    QVERIFY(!downstream.first().toObject().contains(QStringLiteral("edgeId")));

    const QByteArray moduleJson = QJsonDocument(current).toJson(QJsonDocument::Compact);
    QVERIFY(!moduleJson.contains("distant"));
    QVERIFY(!moduleJson.contains("start"));
    QVERIFY(!moduleJson.contains("private upstream implementation detail"));
    QVERIFY(!moduleJson.contains("private downstream implementation detail"));
}

void IrCompilerTest::externalCodeNeighborProvidesDescriptionOnly()
{
    BlueprintDocument document;
    document.projectId = QStringLiteral("external-context");
    document.projectName = QStringLiteral("External Context");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");

    BlueprintNode external = node(QStringLiteral("legacy"),
                                  NodeType::ExternalCode,
                                  QStringLiteral("LegacyTokenProvider"),
                                  QStringLiteral("Provides tokens through the documented legacy API"));
    external.constraints = {QStringLiteral("private filesystem location")};
    external.acceptanceCriteria = {QStringLiteral("private legacy test procedure")};
    BlueprintNode consumer = node(QStringLiteral("consumer"),
                                  NodeType::LogicModule,
                                  QStringLiteral("TokenConsumer"),
                                  QStringLiteral("Consumes a legacy token"));
    document.nodes = {consumer, external};
    document.edges = {{QStringLiteral("legacy-edge"),
                       QStringLiteral("legacy"),
                       QStringLiteral("consumer"),
                       QString()}};

    const QJsonObject module = moduleById(IrCompiler::compile(document), QStringLiteral("consumer"));
    const QJsonObject upstream =
        module.value(QStringLiteral("upstream")).toArray().first().toObject();

    QCOMPARE(upstream.value(QStringLiteral("type")).toString(),
             QStringLiteral("ExternalCode"));
    QCOMPARE(upstream.value(QStringLiteral("description")).toString(),
             QStringLiteral("Provides tokens through the documented legacy API"));
    QVERIFY(upstream.value(QStringLiteral("outputs")).toArray().isEmpty());
    const QByteArray json = QJsonDocument(upstream).toJson(QJsonDocument::Compact);
    QVERIFY(!json.contains("private filesystem location"));
    QVERIFY(!json.contains("private legacy test procedure"));
    QVERIFY(!upstream.contains(QStringLiteral("edgeId")));
}

void IrCompilerTest::emitsOnlyGeneratableModules()
{
    BlueprintDocument document;
    document.projectId = QStringLiteral("types");
    document.projectName = QStringLiteral("Types");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    document.nodes = {
        node(QStringLiteral("external"),
             NodeType::ExternalCode,
             QStringLiteral("External"),
             QStringLiteral("Existing code")),
        node(QStringLiteral("logic"),
             NodeType::LogicModule,
             QStringLiteral("Logic"),
             QStringLiteral("Business logic")),
        node(QStringLiteral("end"), NodeType::End, QStringLiteral("End"), QStringLiteral("Done")),
        node(QStringLiteral("page"),
             NodeType::UiPage,
             QStringLiteral("Page"),
             QStringLiteral("User interface")),
        node(QStringLiteral("start"),
             NodeType::Start,
             QStringLiteral("Start"),
             QStringLiteral("Launch")),
        node(QStringLiteral("decision"),
             NodeType::Decision,
             QStringLiteral("Decision"),
             QStringLiteral("Chooses a branch")),
    };

    QCOMPARE(moduleIds(IrCompiler::compile(document)),
             QStringList({QStringLiteral("decision"),
                          QStringLiteral("logic"),
                          QStringLiteral("page")}));
}

void IrCompilerTest::producesPortableCppNamespaces_data()
{
    QTest::addColumn<QString>("projectName");
    QTest::addColumn<QString>("expectedNamespace");

    QTest::newRow("keyword") << QStringLiteral("class") << QStringLiteral("Project_class");
    QTest::newRow("digit-prefix") << QStringLiteral("9 lives")
                                   << QStringLiteral("Project_9_lives");
    QTest::newRow("punctuation") << QStringLiteral("My--App") << QStringLiteral("My_App");
    QTest::newRow("reserved-prefix") << QStringLiteral("__Widget") << QStringLiteral("Widget");
    QTest::newRow("non-ascii") << QStringLiteral("示例项目")
                               << QStringLiteral("GeneratedProject");
}

void IrCompilerTest::producesPortableCppNamespaces()
{
    QFETCH(QString, projectName);
    QFETCH(QString, expectedNamespace);

    BlueprintDocument document;
    document.projectId = QStringLiteral("fallback-id");
    document.projectName = projectName;
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");

    const QJsonObject project =
        IrCompiler::compile(document).value(QStringLiteral("project")).toObject();
    const QString result = project.value(QStringLiteral("namespace")).toString();

    QCOMPARE(result, expectedNamespace);
    QVERIFY(QRegularExpression(QStringLiteral("^[A-Za-z][A-Za-z0-9_]*$"))
                .match(result)
                .hasMatch());
    QVERIFY(!result.contains(QStringLiteral("__")));
}

QTEST_MAIN(IrCompilerTest)

#include "tst_ir_compiler.moc"
