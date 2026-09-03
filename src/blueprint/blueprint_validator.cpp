#include "blueprint/blueprint_validator.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QQueue>
#include <QSet>

#include <functional>

namespace {

void addDiagnostic(QVector<BlueprintDiagnostic> &diagnostics,
                   const QString &code,
                   const QString &message,
                   const QString &nodeId = {},
                   const QString &edgeId = {})
{
    diagnostics.append({code, nodeId, edgeId, message});
}

bool requiresTextContract(NodeType type)
{
    return type == NodeType::UiPage || type == NodeType::LogicModule
        || type == NodeType::ExternalCode;
}

bool hasAllowedSourceFile(const QString &directoryPath)
{
    static const QSet<QString> allowedSuffixes = {
        QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("cpp"), QStringLiteral("cc")};
    const QFileInfoList files =
        QDir(directoryPath).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &file : files) {
        if (file.isFile() && allowedSuffixes.contains(file.suffix().toLower())) {
            return true;
        }
    }
    return false;
}

void validateExternalCode(const BlueprintNode &node,
                          const BlueprintValidationContext &context,
                          QVector<BlueprintDiagnostic> &diagnostics)
{
    if (context.projectRoot.trimmed().isEmpty()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("external_code.context.missing"),
                      QStringLiteral("A project root is required to validate external code"),
                      node.id);
        return;
    }

    const QString externalRoot = QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(context.projectRoot).absoluteFilePath(QStringLiteral("external"))));
    const QString nodeDirectory = QDir::fromNativeSeparators(
        QDir::cleanPath(QDir(externalRoot).absoluteFilePath(node.id)));
    if (!nodeDirectory.startsWith(externalRoot + QLatin1Char('/'), Qt::CaseInsensitive)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("external_code.path.invalid"),
                      QStringLiteral("External code node id resolves outside the external directory"),
                      node.id);
        return;
    }

    const QFileInfo directoryInfo(nodeDirectory);
    if (!directoryInfo.exists() || !directoryInfo.isDir()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("external_code.directory.missing"),
                      QStringLiteral("External code directory does not exist"),
                      node.id);
        return;
    }
    if (!hasAllowedSourceFile(nodeDirectory)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("external_code.file.missing"),
                      QStringLiteral("External code directory contains no supported source file"),
                      node.id);
    }
}

} // namespace

QVector<BlueprintDiagnostic> BlueprintValidator::validate(
    const BlueprintDocument &document,
    const BlueprintValidationContext &context)
{
    QVector<BlueprintDiagnostic> diagnostics;
    QSet<QString> nodeIds;
    QHash<QString, const BlueprintNode *> nodesById;
    for (const BlueprintNode &node : document.nodes) {
        if (node.id.trimmed().isEmpty()) {
            addDiagnostic(diagnostics,
                          QStringLiteral("node.id.empty"),
                          QStringLiteral("Node ID must not be empty"),
                          node.id);
        } else if (nodeIds.contains(node.id)) {
            addDiagnostic(diagnostics,
                          QStringLiteral("node.id.duplicate"),
                          QStringLiteral("Node ID '%1' is duplicated").arg(node.id),
                          node.id);
        } else {
            nodeIds.insert(node.id);
            nodesById.insert(node.id, &node);
        }
    }

    QSet<QString> edgeIds;
    for (const BlueprintEdge &edge : document.edges) {
        if (edge.id.trimmed().isEmpty()) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.id.empty"),
                          QStringLiteral("Edge ID must not be empty"),
                          {},
                          edge.id);
        } else if (edgeIds.contains(edge.id)) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.id.duplicate"),
                          QStringLiteral("Edge ID '%1' is duplicated").arg(edge.id),
                          {},
                          edge.id);
        } else {
            edgeIds.insert(edge.id);
        }
    }

    QVector<const BlueprintNode *> startNodes;
    int endCount = 0;
    for (const BlueprintNode &node : document.nodes) {
        if (node.type == NodeType::Start) {
            startNodes.append(&node);
        } else if (node.type == NodeType::End) {
            ++endCount;
        }
    }
    if (startNodes.isEmpty()) {
        addDiagnostic(diagnostics,
                      QStringLiteral("graph.start.missing"),
                      QStringLiteral("The graph must contain exactly one Start node"));
    } else if (startNodes.size() > 1) {
        addDiagnostic(diagnostics,
                      QStringLiteral("graph.start.multiple"),
                      QStringLiteral("The graph contains more than one Start node"));
    }
    if (endCount == 0) {
        addDiagnostic(diagnostics,
                      QStringLiteral("graph.end.missing"),
                      QStringLiteral("The graph must contain at least one End node"));
    }

    QHash<QString, QVector<const BlueprintEdge *>> outgoing;
    QHash<QString, QVector<const BlueprintEdge *>> incoming;
    for (const BlueprintEdge &edge : document.edges) {
        if (!nodesById.contains(edge.source)) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.source.missing"),
                          QStringLiteral("Edge source '%1' does not exist").arg(edge.source),
                          {},
                          edge.id);
        } else {
            outgoing[edge.source].append(&edge);
        }
        if (!nodesById.contains(edge.target)) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.target.missing"),
                          QStringLiteral("Edge target '%1' does not exist").arg(edge.target),
                          {},
                          edge.id);
        } else {
            incoming[edge.target].append(&edge);
        }
    }

    for (const BlueprintNode &node : document.nodes) {
        const int incomingCount = incoming.value(node.id).size();
        const int outgoingCount = outgoing.value(node.id).size();
        if (node.type == NodeType::Start) {
            if (incomingCount != 0) {
                addDiagnostic(diagnostics,
                              QStringLiteral("start.incoming"),
                              QStringLiteral("Start node must not have incoming edges"),
                              node.id);
            }
            if (outgoingCount != 1) {
                addDiagnostic(diagnostics,
                              QStringLiteral("start.outgoing.count"),
                              QStringLiteral("Start node must have exactly one outgoing edge"),
                              node.id);
            }
        } else if (incomingCount == 0) {
            addDiagnostic(diagnostics,
                          QStringLiteral("node.incoming.missing"),
                          QStringLiteral("Non-Start node must have an incoming edge"),
                          node.id);
        }

        if (node.type == NodeType::End) {
            if (outgoingCount != 0) {
                addDiagnostic(diagnostics,
                              QStringLiteral("end.outgoing"),
                              QStringLiteral("End node must not have outgoing edges"),
                              node.id);
            }
        } else if (outgoingCount == 0) {
            addDiagnostic(diagnostics,
                          QStringLiteral("node.outgoing.missing"),
                          QStringLiteral("A required outgoing edge is missing"),
                          node.id);
        }

        if (node.type == NodeType::Decision) {
            const QVector<const BlueprintEdge *> decisionEdges = outgoing.value(node.id);
            if (decisionEdges.size() != 2) {
                addDiagnostic(diagnostics,
                              QStringLiteral("decision.outgoing.count"),
                              QStringLiteral("Decision node must have exactly two outgoing edges"),
                              node.id);
            } else {
                QSet<QString> labels;
                for (const BlueprintEdge *edge : decisionEdges) {
                    labels.insert(edge->label);
                }
                if (labels.size() != 2 || !labels.contains(QStringLiteral("true"))
                    || !labels.contains(QStringLiteral("false"))) {
                    addDiagnostic(diagnostics,
                                  QStringLiteral("decision.labels"),
                                  QStringLiteral("Decision branches must be labelled true and false"),
                                  node.id);
                }
            }
        }

        if (requiresTextContract(node.type)) {
            if (node.name.trimmed().isEmpty()) {
                addDiagnostic(diagnostics,
                              QStringLiteral("node.name.empty"),
                              QStringLiteral("Node name must not be empty"),
                              node.id);
            }
            if (node.description.trimmed().isEmpty()) {
                addDiagnostic(diagnostics,
                              QStringLiteral("node.description.empty"),
                              QStringLiteral("Node description must not be empty"),
                              node.id);
            }
        }
        if (node.type == NodeType::ExternalCode) {
            validateExternalCode(node, context, diagnostics);
        }
    }

    if (startNodes.size() == 1 && !startNodes.first()->id.trimmed().isEmpty()
        && nodesById.contains(startNodes.first()->id)) {
        QSet<QString> reachable;
        QQueue<QString> pending;
        reachable.insert(startNodes.first()->id);
        pending.enqueue(startNodes.first()->id);
        while (!pending.isEmpty()) {
            const QString current = pending.dequeue();
            for (const BlueprintEdge *edge : outgoing.value(current)) {
                if (nodesById.contains(edge->target) && !reachable.contains(edge->target)) {
                    reachable.insert(edge->target);
                    pending.enqueue(edge->target);
                }
            }
        }
        for (const BlueprintNode &node : document.nodes) {
            if (!node.id.trimmed().isEmpty() && nodesById.value(node.id) == &node
                && !reachable.contains(node.id)) {
                addDiagnostic(diagnostics,
                              QStringLiteral("node.unreachable"),
                              QStringLiteral("Node is not reachable from Start"),
                              node.id);
            }
        }
    }

    QHash<QString, int> colors;
    std::function<void(const QString &)> visit = [&](const QString &nodeId) {
        colors[nodeId] = 1;
        for (const BlueprintEdge *edge : outgoing.value(nodeId)) {
            if (!nodesById.contains(edge->target)) {
                continue;
            }
            const int targetColor = colors.value(edge->target, 0);
            if (targetColor == 0) {
                visit(edge->target);
            } else if (targetColor == 1) {
                addDiagnostic(diagnostics,
                              QStringLiteral("graph.cycle"),
                              QStringLiteral("Directed cycle detected"),
                              edge->target,
                              edge->id);
            }
        }
        colors[nodeId] = 2;
    };
    for (const BlueprintNode &node : document.nodes) {
        if (!node.id.trimmed().isEmpty() && nodesById.value(node.id) == &node
            && colors.value(node.id, 0) == 0) {
            visit(node.id);
        }
    }

    return diagnostics;
}
