#include <QtTest/QtTest>

#include <QDir>
#include <QFile>
#include <QProcess>
#include <QTemporaryDir>

#include "blueprint/blueprint_validator.h"

namespace {

BlueprintNode makeNode(NodeType type, const QString &id)
{
    BlueprintNode node;
    node.id = id;
    node.type = type;
    node.name = id + QStringLiteral(" name");
    node.description = id + QStringLiteral(" description");
    return node;
}

BlueprintEdge makeEdge(const QString &id,
                       const QString &source,
                       const QString &target,
                       const QString &label = {})
{
    return {id, source, target, label};
}

BlueprintDocument makeValidDocument()
{
    BlueprintDocument document;
    document.nodes = {
        makeNode(NodeType::Start, QStringLiteral("start")),
        makeNode(NodeType::Decision, QStringLiteral("decision")),
        makeNode(NodeType::End, QStringLiteral("accepted")),
        makeNode(NodeType::End, QStringLiteral("rejected")),
    };
    document.edges = {
        makeEdge(QStringLiteral("begin"), QStringLiteral("start"), QStringLiteral("decision")),
        makeEdge(QStringLiteral("yes"),
                 QStringLiteral("decision"),
                 QStringLiteral("accepted"),
                 QStringLiteral("true")),
        makeEdge(QStringLiteral("no"),
                 QStringLiteral("decision"),
                 QStringLiteral("rejected"),
                 QStringLiteral("false")),
    };
    return document;
}

QStringList diagnosticCodes(const QVector<BlueprintDiagnostic> &diagnostics)
{
    QStringList codes;
    for (const BlueprintDiagnostic &diagnostic : diagnostics) {
        codes.append(diagnostic.code);
    }
    return codes;
}

bool hasDiagnostic(const QVector<BlueprintDiagnostic> &diagnostics,
                   const QString &code,
                   const QString &nodeId = {},
                   const QString &edgeId = {})
{
    for (const BlueprintDiagnostic &diagnostic : diagnostics) {
        if (diagnostic.code == code && (nodeId.isNull() || diagnostic.nodeId == nodeId)
            && (edgeId.isNull() || diagnostic.edgeId == edgeId)) {
            return true;
        }
    }
    return false;
}

QStringList diagnosticKeys(const QVector<BlueprintDiagnostic> &diagnostics)
{
    QStringList keys;
    for (const BlueprintDiagnostic &diagnostic : diagnostics) {
        keys.append(diagnostic.code + QLatin1Char('|') + diagnostic.nodeId + QLatin1Char('|')
                    + diagnostic.edgeId + QLatin1Char('|') + diagnostic.message);
    }
    return keys;
}

} // namespace

class BlueprintValidatorTest : public QObject
{
    Q_OBJECT

private slots:
    void acceptsValidGraph();
    void rejectsEmptyAndDuplicateIds();
    void requiresExactlyOneStartAndAtLeastOneEnd();
    void rejectsDanglingEdgeReferences();
    void danglingEdgesDoNotSatisfyGraphRules();
    void duplicateNodeIdsDoNotOwnTopology();
    void enforcesNodeDegreeRules();
    void rejectsUnreachableNodes();
    void rejectsDirectedCycles();
    void enforcesDecisionBranches();
    void requiresNamesAndDescriptionsForExecutableNodes();
    void validatesExternalCodeFiles();
    void rejectsExternalCodeLinksOutsideProject();
    void validatesLongGraphsWithoutRecursiveStack();
    void returnsDiagnosticsInDeterministicOrder();
};

void BlueprintValidatorTest::acceptsValidGraph()
{
    const QVector<BlueprintDiagnostic> diagnostics =
        BlueprintValidator::validate(makeValidDocument());

    QVERIFY(diagnostics.isEmpty());
}

void BlueprintValidatorTest::rejectsEmptyAndDuplicateIds()
{
    BlueprintDocument document = makeValidDocument();
    document.nodes[0].id.clear();
    document.nodes[2].id = document.nodes[1].id;
    document.edges[0].id.clear();
    document.edges[2].id = document.edges[1].id;

    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(document);

    const QStringList codes = diagnosticCodes(diagnostics);
    QVERIFY(codes.contains(QStringLiteral("node.id.empty")));
    QVERIFY(codes.contains(QStringLiteral("node.id.duplicate")));
    QVERIFY(codes.contains(QStringLiteral("edge.id.empty")));
    QVERIFY(codes.contains(QStringLiteral("edge.id.duplicate")));

    const BlueprintDiagnostic &emptyNode = diagnostics.at(codes.indexOf(QStringLiteral("node.id.empty")));
    QVERIFY(emptyNode.nodeId.isEmpty());
    QVERIFY(!emptyNode.message.isEmpty());
    const BlueprintDiagnostic &duplicateEdge =
        diagnostics.at(codes.indexOf(QStringLiteral("edge.id.duplicate")));
    QCOMPARE(duplicateEdge.edgeId, QStringLiteral("yes"));
    QVERIFY(!duplicateEdge.message.isEmpty());
}

void BlueprintValidatorTest::requiresExactlyOneStartAndAtLeastOneEnd()
{
    BlueprintDocument noStart = makeValidDocument();
    noStart.nodes[0].type = NodeType::LogicModule;
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(noStart),
                          QStringLiteral("graph.start.missing")));

    BlueprintDocument multipleStarts = makeValidDocument();
    multipleStarts.nodes[1].type = NodeType::Start;
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(multipleStarts),
                          QStringLiteral("graph.start.multiple")));

    BlueprintDocument noEnd = makeValidDocument();
    noEnd.nodes[2].type = NodeType::LogicModule;
    noEnd.nodes[3].type = NodeType::LogicModule;
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(noEnd),
                          QStringLiteral("graph.end.missing")));
}

void BlueprintValidatorTest::rejectsDanglingEdgeReferences()
{
    BlueprintDocument document = makeValidDocument();
    document.edges.append(makeEdge(QStringLiteral("bad-source"),
                                   QStringLiteral("ghost"),
                                   QStringLiteral("accepted")));
    document.edges.append(makeEdge(QStringLiteral("bad-target"),
                                   QStringLiteral("decision"),
                                   QStringLiteral("ghost")));

    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(document);

    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("edge.source.missing"),
                          {},
                          QStringLiteral("bad-source")));
    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("edge.target.missing"),
                          {},
                          QStringLiteral("bad-target")));
}

void BlueprintValidatorTest::danglingEdgesDoNotSatisfyGraphRules()
{
    BlueprintDocument document;
    document.nodes = {
        makeNode(NodeType::Start, QStringLiteral("start")),
        makeNode(NodeType::LogicModule, QStringLiteral("logic")),
        makeNode(NodeType::End, QStringLiteral("end")),
    };
    document.edges = {
        makeEdge(QStringLiteral("start-logic"), QStringLiteral("start"), QStringLiteral("logic")),
        makeEdge(QStringLiteral("logic-ghost"), QStringLiteral("logic"), QStringLiteral("ghost")),
        makeEdge(QStringLiteral("ghost-end"), QStringLiteral("ghost"), QStringLiteral("end")),
    };

    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(document);
    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("node.outgoing.missing"),
                          QStringLiteral("logic")));
    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("node.incoming.missing"),
                          QStringLiteral("end")));

    BlueprintDocument decision = makeValidDocument();
    decision.edges[1].target = QStringLiteral("missing-true");
    decision.edges[2].target = QStringLiteral("missing-false");
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(decision),
                          QStringLiteral("decision.outgoing.count"),
                          QStringLiteral("decision")));
}

void BlueprintValidatorTest::duplicateNodeIdsDoNotOwnTopology()
{
    BlueprintDocument document;
    document.nodes = {
        makeNode(NodeType::Start, QStringLiteral("start")),
        makeNode(NodeType::LogicModule, QStringLiteral("duplicate")),
        makeNode(NodeType::End, QStringLiteral("duplicate")),
        makeNode(NodeType::End, QStringLiteral("end")),
    };
    document.edges = {
        makeEdge(QStringLiteral("start-duplicate"),
                 QStringLiteral("start"),
                 QStringLiteral("duplicate")),
        makeEdge(QStringLiteral("duplicate-end"),
                 QStringLiteral("duplicate"),
                 QStringLiteral("end")),
    };

    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(document);
    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("edge.target.ambiguous"),
                          {},
                          QStringLiteral("start-duplicate")));
    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("edge.source.ambiguous"),
                          {},
                          QStringLiteral("duplicate-end")));
    QVERIFY(!hasDiagnostic(diagnostics,
                           QStringLiteral("end.outgoing"),
                           QStringLiteral("duplicate")));
}

void BlueprintValidatorTest::enforcesNodeDegreeRules()
{
    BlueprintDocument startIncoming = makeValidDocument();
    startIncoming.edges.append(makeEdge(QStringLiteral("into-start"),
                                        QStringLiteral("accepted"),
                                        QStringLiteral("start")));
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(startIncoming),
                          QStringLiteral("start.incoming"),
                          QStringLiteral("start")));

    BlueprintDocument startOutgoing = makeValidDocument();
    startOutgoing.edges.append(makeEdge(QStringLiteral("second-start-edge"),
                                        QStringLiteral("start"),
                                        QStringLiteral("accepted")));
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(startOutgoing),
                          QStringLiteral("start.outgoing.count"),
                          QStringLiteral("start")));

    BlueprintDocument endOutgoing = makeValidDocument();
    endOutgoing.edges.append(makeEdge(QStringLiteral("after-end"),
                                      QStringLiteral("accepted"),
                                      QStringLiteral("rejected")));
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(endOutgoing),
                          QStringLiteral("end.outgoing"),
                          QStringLiteral("accepted")));

    BlueprintDocument missingOutgoing = makeValidDocument();
    missingOutgoing.nodes[1].type = NodeType::LogicModule;
    missingOutgoing.edges.removeAt(2);
    missingOutgoing.edges.removeAt(1);
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(missingOutgoing),
                          QStringLiteral("node.outgoing.missing"),
                          QStringLiteral("decision")));

    BlueprintDocument missingIncoming = makeValidDocument();
    missingIncoming.nodes.append(makeNode(NodeType::LogicModule, QStringLiteral("orphan")));
    missingIncoming.edges.append(makeEdge(QStringLiteral("orphan-exit"),
                                          QStringLiteral("orphan"),
                                          QStringLiteral("accepted")));
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(missingIncoming),
                          QStringLiteral("node.incoming.missing"),
                          QStringLiteral("orphan")));
}

void BlueprintValidatorTest::rejectsUnreachableNodes()
{
    BlueprintDocument document = makeValidDocument();
    document.nodes.append(makeNode(NodeType::LogicModule, QStringLiteral("island-a")));
    document.nodes.append(makeNode(NodeType::LogicModule, QStringLiteral("island-b")));
    document.edges.append(makeEdge(QStringLiteral("island-forward"),
                                   QStringLiteral("island-a"),
                                   QStringLiteral("island-b")));
    document.edges.append(makeEdge(QStringLiteral("island-back"),
                                   QStringLiteral("island-b"),
                                   QStringLiteral("island-a")));

    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(document);

    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("node.unreachable"),
                          QStringLiteral("island-a")));
    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("node.unreachable"),
                          QStringLiteral("island-b")));
}

void BlueprintValidatorTest::rejectsDirectedCycles()
{
    BlueprintDocument document;
    document.nodes = {
        makeNode(NodeType::Start, QStringLiteral("start")),
        makeNode(NodeType::LogicModule, QStringLiteral("a")),
        makeNode(NodeType::LogicModule, QStringLiteral("b")),
        makeNode(NodeType::End, QStringLiteral("end")),
    };
    document.edges = {
        makeEdge(QStringLiteral("start-a"), QStringLiteral("start"), QStringLiteral("a")),
        makeEdge(QStringLiteral("a-b"), QStringLiteral("a"), QStringLiteral("b")),
        makeEdge(QStringLiteral("b-a"), QStringLiteral("b"), QStringLiteral("a")),
        makeEdge(QStringLiteral("a-end"), QStringLiteral("a"), QStringLiteral("end")),
    };

    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(document);

    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("graph.cycle"),
                          QStringLiteral("a"),
                          QStringLiteral("b-a")));
}

void BlueprintValidatorTest::enforcesDecisionBranches()
{
    BlueprintDocument wrongCount = makeValidDocument();
    wrongCount.edges.removeLast();
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(wrongCount),
                          QStringLiteral("decision.outgoing.count"),
                          QStringLiteral("decision")));

    BlueprintDocument wrongLabels = makeValidDocument();
    wrongLabels.edges[2].label = QStringLiteral("true");
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(wrongLabels),
                          QStringLiteral("decision.labels"),
                          QStringLiteral("decision")));
}

void BlueprintValidatorTest::requiresNamesAndDescriptionsForExecutableNodes()
{
    BlueprintDocument document = makeValidDocument();
    document.nodes.insert(2, makeNode(NodeType::UiPage, QStringLiteral("page")));
    document.nodes.insert(3, makeNode(NodeType::LogicModule, QStringLiteral("logic")));
    document.nodes[2].name = QStringLiteral(" \t ");
    document.nodes[3].description = QStringLiteral("\n");
    document.edges[1].target = QStringLiteral("page");
    document.edges.insert(2,
                          makeEdge(QStringLiteral("page-logic"),
                                   QStringLiteral("page"),
                                   QStringLiteral("logic")));
    document.edges.insert(3,
                          makeEdge(QStringLiteral("logic-accepted"),
                                   QStringLiteral("logic"),
                                   QStringLiteral("accepted")));

    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(document);

    QVERIFY(hasDiagnostic(diagnostics, QStringLiteral("node.name.empty"), QStringLiteral("page")));
    QVERIFY(hasDiagnostic(diagnostics,
                          QStringLiteral("node.description.empty"),
                          QStringLiteral("logic")));
}

void BlueprintValidatorTest::validatesExternalCodeFiles()
{
    BlueprintDocument document;
    document.nodes = {
        makeNode(NodeType::Start, QStringLiteral("start")),
        makeNode(NodeType::ExternalCode, QStringLiteral("external-code")),
        makeNode(NodeType::End, QStringLiteral("end")),
    };
    document.edges = {
        makeEdge(QStringLiteral("start-external"),
                 QStringLiteral("start"),
                 QStringLiteral("external-code")),
        makeEdge(QStringLiteral("external-end"),
                 QStringLiteral("external-code"),
                 QStringLiteral("end")),
    };

    QVERIFY(hasDiagnostic(BlueprintValidator::validate(document),
                          QStringLiteral("external_code.context.missing"),
                          QStringLiteral("external-code")));

    QTemporaryDir project;
    QVERIFY(project.isValid());
    BlueprintValidationContext context{project.path()};
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(document, context),
                          QStringLiteral("external_code.directory.missing"),
                          QStringLiteral("external-code")));

    BlueprintDocument traversal = document;
    traversal.nodes[1].id = QStringLiteral("../escaped");
    traversal.edges[0].target = traversal.nodes[1].id;
    traversal.edges[1].source = traversal.nodes[1].id;
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(traversal, context),
                          QStringLiteral("external_code.path.invalid"),
                          QStringLiteral("../escaped")));

    QDir projectDir(project.path());
    QVERIFY(projectDir.mkpath(QStringLiteral("external/external-code")));
    QFile textFile(project.filePath(QStringLiteral("external/external-code/readme.txt")));
    QVERIFY(textFile.open(QIODevice::WriteOnly));
    textFile.write("not source code");
    textFile.close();
    QVERIFY(hasDiagnostic(BlueprintValidator::validate(document, context),
                          QStringLiteral("external_code.file.missing"),
                          QStringLiteral("external-code")));

    QFile sourceFile(project.filePath(QStringLiteral("external/external-code/implementation.hpp")));
    QVERIFY(sourceFile.open(QIODevice::WriteOnly));
    sourceFile.write("#pragma once\n");
    sourceFile.close();
    QVERIFY(BlueprintValidator::validate(document, context).isEmpty());
}

void BlueprintValidatorTest::rejectsExternalCodeLinksOutsideProject()
{
    QTemporaryDir project;
    QTemporaryDir outside;
    QVERIFY(project.isValid());
    QVERIFY(outside.isValid());

    const QString externalRoot = project.filePath(QStringLiteral("external"));
    QVERIFY(QDir().mkpath(externalRoot));
    const QString linkedNode = QDir(externalRoot).filePath(QStringLiteral("linked-node"));
#ifdef Q_OS_WIN
    const int linkExitCode = QProcess::execute(
        QStringLiteral("cmd.exe"),
        {QStringLiteral("/c"),
         QStringLiteral("mklink"),
         QStringLiteral("/J"),
         QDir::toNativeSeparators(linkedNode),
         QDir::toNativeSeparators(outside.path())});
    if (linkExitCode != 0) {
        QSKIP("Directory junctions are unavailable on this platform");
    }
#else
    if (!QFile::link(outside.path(), linkedNode)) {
        QSKIP("Directory symbolic links are unavailable on this platform");
    }
#endif

    QFile outsideSource(outside.filePath(QStringLiteral("outside.cpp")));
    QVERIFY(outsideSource.open(QIODevice::WriteOnly));
    outsideSource.write("// outside\n");
    outsideSource.close();

    BlueprintDocument document;
    document.nodes = {
        makeNode(NodeType::Start, QStringLiteral("start")),
        makeNode(NodeType::ExternalCode, QStringLiteral("linked-node")),
        makeNode(NodeType::End, QStringLiteral("end")),
    };
    document.edges = {
        makeEdge(QStringLiteral("start-linked"), QStringLiteral("start"), QStringLiteral("linked-node")),
        makeEdge(QStringLiteral("linked-end"), QStringLiteral("linked-node"), QStringLiteral("end")),
    };

    const bool rejected = hasDiagnostic(BlueprintValidator::validate(document, {project.path()}),
                                        QStringLiteral("external_code.path.invalid"),
                                        QStringLiteral("linked-node"));
#ifdef Q_OS_WIN
    QVERIFY(QDir().rmdir(linkedNode));
#else
    QVERIFY(QFile::remove(linkedNode));
#endif
    QVERIFY(rejected);
}

void BlueprintValidatorTest::validatesLongGraphsWithoutRecursiveStack()
{
    constexpr int nodeCount = 5000;
    BlueprintDocument document;
    document.nodes.reserve(nodeCount);
    document.edges.reserve(nodeCount - 1);
    document.nodes.append(makeNode(NodeType::Start, QStringLiteral("node-0")));
    for (int index = 1; index < nodeCount - 1; ++index) {
        document.nodes.append(
            makeNode(NodeType::LogicModule, QStringLiteral("node-%1").arg(index)));
    }
    document.nodes.append(
        makeNode(NodeType::End, QStringLiteral("node-%1").arg(nodeCount - 1)));
    for (int index = 0; index < nodeCount - 1; ++index) {
        document.edges.append(makeEdge(QStringLiteral("edge-%1").arg(index),
                                       QStringLiteral("node-%1").arg(index),
                                       QStringLiteral("node-%1").arg(index + 1)));
    }

    QVERIFY(BlueprintValidator::validate(document).isEmpty());
}

void BlueprintValidatorTest::returnsDiagnosticsInDeterministicOrder()
{
    BlueprintDocument document = makeValidDocument();
    document.nodes[0].id.clear();
    document.nodes[2].id = document.nodes[1].id;
    document.edges[0].id.clear();
    document.edges[2].id = document.edges[1].id;

    const QStringList first = diagnosticKeys(BlueprintValidator::validate(document));
    const QStringList second = diagnosticKeys(BlueprintValidator::validate(document));

    QCOMPARE(first, second);
    QCOMPARE(first.first(), QStringLiteral("node.id.empty|||Node ID must not be empty"));
}

QTEST_APPLESS_MAIN(BlueprintValidatorTest)

#include "tst_blueprint_validator.moc"
