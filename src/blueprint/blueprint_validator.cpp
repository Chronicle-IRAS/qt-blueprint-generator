#include "blueprint/blueprint_validator.h"

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QQueue>
#include <QSet>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {

#ifdef Q_OS_WIN
constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseInsensitive;
#else
constexpr Qt::CaseSensitivity pathCaseSensitivity = Qt::CaseSensitive;
#endif

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

bool isStrictChildPath(const QString &path, const QString &parentPath)
{
    const QString normalizedPath = QDir::fromNativeSeparators(QDir::cleanPath(path));
    const QString normalizedParent = QDir::fromNativeSeparators(QDir::cleanPath(parentPath));
    return normalizedPath.startsWith(normalizedParent + QLatin1Char('/'), pathCaseSensitivity);
}

QString canonicalExistingPath(const QString &path)
{
#ifdef Q_OS_WIN
    const QString nativePath = QDir::toNativeSeparators(path);
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(nativePath.utf16()),
                                      0,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                      nullptr,
                                      OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS,
                                      nullptr);
    if (handle == INVALID_HANDLE_VALUE) {
        return {};
    }

    const DWORD requiredLength =
        GetFinalPathNameByHandleW(handle, nullptr, 0, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    if (requiredLength == 0) {
        CloseHandle(handle);
        return {};
    }
    QString resolvedPath(static_cast<qsizetype>(requiredLength), Qt::Uninitialized);
    const DWORD writtenLength = GetFinalPathNameByHandleW(
        handle,
        reinterpret_cast<LPWSTR>(resolvedPath.data()),
        requiredLength,
        FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(handle);
    if (writtenLength == 0 || writtenLength >= requiredLength) {
        return {};
    }
    resolvedPath.truncate(static_cast<qsizetype>(writtenLength));
    resolvedPath = QDir::fromNativeSeparators(resolvedPath);
    if (resolvedPath.startsWith(QStringLiteral("//?/UNC/"), Qt::CaseInsensitive)) {
        resolvedPath = QStringLiteral("//") + resolvedPath.mid(8);
    } else if (resolvedPath.startsWith(QStringLiteral("//?/"), Qt::CaseInsensitive)) {
        resolvedPath.remove(0, 4);
    }
    return QDir::cleanPath(resolvedPath);
#else
    return QFileInfo(path).canonicalFilePath();
#endif
}

enum class SourceDirectoryResult {
    Found,
    Missing,
    UnsafePath,
};

SourceDirectoryResult inspectSourceDirectory(const QString &directoryPath,
                                             const QString &canonicalDirectoryPath)
{
    static const QSet<QString> allowedSuffixes = {
        QStringLiteral("h"), QStringLiteral("hpp"), QStringLiteral("cpp"), QStringLiteral("cc")};
    const QFileInfoList files =
        QDir(directoryPath).entryInfoList(QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &file : files) {
        if (file.isFile() && allowedSuffixes.contains(file.suffix().toLower())) {
            const QString canonicalFilePath = canonicalExistingPath(file.absoluteFilePath());
            if (canonicalFilePath.isEmpty()
                || !isStrictChildPath(canonicalFilePath, canonicalDirectoryPath)) {
                return SourceDirectoryResult::UnsafePath;
            }
            return SourceDirectoryResult::Found;
        }
    }
    return SourceDirectoryResult::Missing;
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
    if (!isStrictChildPath(nodeDirectory, externalRoot)) {
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

    const QString canonicalExternalRoot = canonicalExistingPath(externalRoot);
    const QString canonicalNodeDirectory = canonicalExistingPath(nodeDirectory);
    if (canonicalExternalRoot.isEmpty() || canonicalNodeDirectory.isEmpty()
        || !isStrictChildPath(canonicalNodeDirectory, canonicalExternalRoot)) {
        addDiagnostic(diagnostics,
                      QStringLiteral("external_code.path.invalid"),
                      QStringLiteral("External code directory resolves outside the external directory"),
                      node.id);
        return;
    }

    const SourceDirectoryResult sourceResult =
        inspectSourceDirectory(nodeDirectory, canonicalNodeDirectory);
    if (sourceResult == SourceDirectoryResult::UnsafePath) {
        addDiagnostic(diagnostics,
                      QStringLiteral("external_code.path.invalid"),
                      QStringLiteral("External code source resolves outside its node directory"),
                      node.id);
    } else if (sourceResult == SourceDirectoryResult::Missing) {
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
    QHash<QString, int> nodeIdCounts;
    for (const BlueprintNode &node : document.nodes) {
        if (!node.id.trimmed().isEmpty()) {
            ++nodeIdCounts[node.id];
        }
    }

    QSet<QString> seenNodeIds;
    QHash<QString, const BlueprintNode *> nodesById;
    for (const BlueprintNode &node : document.nodes) {
        if (node.id.trimmed().isEmpty()) {
            addDiagnostic(diagnostics,
                          QStringLiteral("node.id.empty"),
                          QStringLiteral("Node ID must not be empty"),
                          node.id);
        } else if (seenNodeIds.contains(node.id)) {
            addDiagnostic(diagnostics,
                          QStringLiteral("node.id.duplicate"),
                          QStringLiteral("Node ID '%1' is duplicated").arg(node.id),
                          node.id);
        }
        if (!node.id.trimmed().isEmpty()) {
            seenNodeIds.insert(node.id);
        }
        if (!node.id.trimmed().isEmpty() && nodeIdCounts.value(node.id) == 1) {
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
        const int sourceCount = nodeIdCounts.value(edge.source);
        const int targetCount = nodeIdCounts.value(edge.target);
        if (sourceCount == 0) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.source.missing"),
                          QStringLiteral("Edge source '%1' does not exist").arg(edge.source),
                          {},
                          edge.id);
        } else if (sourceCount > 1) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.source.ambiguous"),
                          QStringLiteral("Edge source '%1' matches more than one node").arg(edge.source),
                          {},
                          edge.id);
        }
        if (targetCount == 0) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.target.missing"),
                          QStringLiteral("Edge target '%1' does not exist").arg(edge.target),
                          {},
                          edge.id);
        } else if (targetCount > 1) {
            addDiagnostic(diagnostics,
                          QStringLiteral("edge.target.ambiguous"),
                          QStringLiteral("Edge target '%1' matches more than one node").arg(edge.target),
                          {},
                          edge.id);
        }
        if (sourceCount == 1 && targetCount == 1) {
            outgoing[edge.source].append(&edge);
            incoming[edge.target].append(&edge);
        }
    }

    for (const BlueprintNode &node : document.nodes) {
        const bool hasUniqueId = !node.id.trimmed().isEmpty() && nodeIdCounts.value(node.id) == 1;
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
        if (!hasUniqueId) {
            continue;
        }

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

    struct TraversalFrame {
        QString nodeId;
        qsizetype nextEdgeIndex = 0;
    };
    QHash<QString, int> colors;
    for (const BlueprintNode &node : document.nodes) {
        if (!node.id.trimmed().isEmpty() && nodesById.value(node.id) == &node
            && colors.value(node.id, 0) == 0) {
            QVector<TraversalFrame> stack;
            colors[node.id] = 1;
            stack.append({node.id, 0});
            while (!stack.isEmpty()) {
                TraversalFrame &frame = stack.last();
                const auto outgoingIt = outgoing.constFind(frame.nodeId);
                if (outgoingIt == outgoing.cend()
                    || frame.nextEdgeIndex >= outgoingIt.value().size()) {
                    colors[frame.nodeId] = 2;
                    stack.removeLast();
                    continue;
                }

                const BlueprintEdge *edge = outgoingIt.value().at(frame.nextEdgeIndex);
                ++frame.nextEdgeIndex;
                const int targetColor = colors.value(edge->target, 0);
                if (targetColor == 0) {
                    colors[edge->target] = 1;
                    stack.append({edge->target, 0});
                } else if (targetColor == 1) {
                    addDiagnostic(diagnostics,
                                  QStringLiteral("graph.cycle"),
                                  QStringLiteral("Directed cycle detected"),
                                  edge->target,
                                  edge->id);
                }
            }
        }
    }

    return diagnostics;
}
