#include "generation/runtime_assembler.h"
#include "blueprint/blueprint_validator.h"
#include "generation/workspace_io.h"
#include <QHash>
#include <QRegularExpression>
#include <QSet>

namespace {
bool validText(const QString &text)
{
    for (qsizetype index = 0; index < text.size(); ++index) {
        if (text[index].isHighSurrogate()) {
            if (++index >= text.size() || !text[index].isLowSurrogate()) return false;
        } else if (text[index].isLowSurrogate()) return false;
    }
    return true;
}

// Never interpolate user text as C++ syntax. Hex has no escaping ambiguities, even
// for controls, Unicode separators, quotes, backslashes and adjacent hex digits.
QString textExpression(const QString &text)
{
    return "QString::fromUtf8(QByteArray::fromHex(\"" + QString::fromLatin1(text.toUtf8().toHex()) + "\"))";
}

const QHash<QString, QString> &portTypes()
{
    static const QHash<QString, QString> types{
        {"bool", "QMetaType::Bool"}, {"int", "QMetaType::Int"}, {"double", "QMetaType::Double"},
        {"QString", "QMetaType::QString"}, {"QByteArray", "QMetaType::QByteArray"},
        {"QStringList", "QMetaType::QStringList"}, {"QVariantList", "QMetaType::QVariantList"},
        {"QVariantMap", "QMetaType::QVariantMap"}, {"QVariant", "QMetaType::UnknownType"}};
    return types;
}

bool validatePorts(const BlueprintNode &node, const QVector<PortSpec> &ports, QString *error)
{
    QSet<QString> names;
    for (const PortSpec &port : ports) {
        if (port.name.trimmed().isEmpty() || names.contains(port.name))
            return WorkspaceIo::fail(error, "Runtime port names must be nonempty and unique on node: " + node.id);
        if (!validText(port.name))
            return WorkspaceIo::fail(error, "Runtime port name contains invalid UTF-16 on node: " + node.id);
        if (!portTypes().contains(port.type))
            return WorkspaceIo::fail(error, "Unsupported runtime port type on node " + node.id + ": " + port.type);
        names.insert(port.name);
    }
    return true;
}
}

bool RuntimeAssembler::assemble(const BlueprintDocument &document, const QString &ns,
                                QMap<QString, QByteArray> &files, QString *error)
{
    using WorkspaceIo::fail;
    if (error) error->clear();
    static const QRegularExpression namespacePattern("\\ABlueprint_[A-Za-z_][A-Za-z0-9_]*\\z");
    if (!namespacePattern.match(ns).hasMatch()) return fail(error, "Invalid runtime scaffold namespace");
    int startCount = 0, endCount = 0, pageCount = 0;
    const BlueprintNode *start = nullptr;
    QHash<QString, const BlueprintNode *> nodes;
    for (const BlueprintNode &node : document.nodes) {
        if (node.type != NodeType::Start && node.type != NodeType::End
            && node.type != NodeType::LogicModule && node.type != NodeType::UiPage)
            return fail(error, "Linear runtime supports only Start, LogicModule, terminal UiPage, and End; unsupported node: " + node.id);
        if ((node.type == NodeType::LogicModule || node.type == NodeType::UiPage) && !WorkspaceIo::safeId(node.id))
            return fail(error, "Runtime module IDs must be portable single path segments");
        nodes.insert(node.id, &node);
        if (node.type == NodeType::Start) { ++startCount; start = &node; }
        if (node.type == NodeType::End) ++endCount;
        if (node.type == NodeType::UiPage) ++pageCount;
        if ((node.type == NodeType::Start || node.type == NodeType::End)
            && (!node.inputs.isEmpty() || !node.outputs.isEmpty()))
            return fail(error, "Runtime Start and End nodes cannot declare ports: " + node.id);
        if (node.type == NodeType::UiPage && !node.outputs.isEmpty())
            return fail(error, "Runtime terminal UiPage cannot declare outputs: " + node.id);
        if (!validatePorts(node, node.inputs, error) || !validatePorts(node, node.outputs, error)) return false;
    }
    if (startCount != 1 || endCount != 1 || pageCount > 1)
        return fail(error, "Linear runtime requires exactly one Start, exactly one End, and at most one terminal UiPage");
    const auto diagnostics = BlueprintValidator::validate(document);
    if (!diagnostics.isEmpty()) return fail(error, "Invalid runtime blueprint: " + diagnostics.first().message);
    QHash<QString, QVector<QString>> outgoing, incoming;
    for (const BlueprintEdge &edge : document.edges) {
        if (!edge.label.isEmpty()) return fail(error, "Linear runtime flow edges must have empty labels: " + edge.id);
        outgoing[edge.source].append(edge.target); incoming[edge.target].append(edge.source);
    }
    for (const BlueprintNode &node : document.nodes) {
        if (incoming.value(node.id).size() != (node.type == NodeType::Start ? 0 : 1)
            || outgoing.value(node.id).size() != (node.type == NodeType::End ? 0 : 1))
            return fail(error, "Linear runtime requires one connected chain without branching or merging: " + node.id);
    }
    QVector<const BlueprintNode *> modules;
    QSet<QString> visited;
    const BlueprintNode *current = start;
    const BlueprintNode *previous = nullptr;
    while (current) {
        if (visited.contains(current->id)) return fail(error, "Linear runtime cannot contain cycles");
        visited.insert(current->id);
        if (current->type == NodeType::LogicModule || current->type == NodeType::UiPage) {
            for (const PortSpec &input : current->inputs) {
                bool available = false;
                for (const PortSpec &output : previous->outputs)
                    if (input.name == output.name && input.type == output.type) { available = true; break; }
                if (!available) return fail(error, "Runtime input must match a same-name, same-type output of its direct predecessor: " + current->id + '/' + input.name);
            }
            if (current->type == NodeType::UiPage && nodes.value(outgoing.value(current->id).first())->type != NodeType::End)
                return fail(error, "Runtime UiPage must be terminal, directly before End: " + current->id);
            modules.append(current);
        }
        previous = current;
        current = current->type == NodeType::End ? nullptr : nodes.value(outgoing.value(current->id).first());
    }
    if (visited.size() != document.nodes.size()) return fail(error, "All runtime nodes must belong to the Start-to-End chain");

    const QString header = "#pragma once\n#include <memory>\n#include <QWidget>\n#include <QString>\nnamespace " + ns + R"( {
class WorkflowController final {
public:
    bool start(QString *error = nullptr);
    bool hasPage() const { return bool(m_page); }
private:
    std::unique_ptr<QWidget> m_page;
};
}
)";
    QString source = "#include \"runtime/workflow_controller.h\"\n#include \"contracts/types.h\"\n";
    for (const BlueprintNode *node : modules)
        source += "#include \"modules/" + node->id + "/contract.h\"\n";
    source += "#include <QMetaType>\n#include <QByteArray>\n#include <exception>\n#include <initializer_list>\nnamespace " + ns + R"( {
namespace {
bool startupFailure(QString *error, const QString &message) {
    if (error) *error = message;
    return false;
}
struct OutputPort { QString name; int type; };
bool checkOutputs(const Outputs &outputs, std::initializer_list<OutputPort> expected,
                  const QString &node, QString *error) {
    if (outputs.size() != qsizetype(expected.size()))
        return startupFailure(error, node + ": output keys do not match the declared ports");
    for (const auto &port : expected) {
        const auto value = outputs.constFind(port.name);
        if (value == outputs.cend() || !value->isValid())
            return startupFailure(error, node + ": missing or invalid output " + port.name);
        if (port.type != QMetaType::UnknownType && value->metaType().id() != port.type)
            return startupFailure(error, node + ": output type mismatch for " + port.name);
    }
    return true;
}
}
bool WorkflowController::start(QString *error) {
    if (error) error->clear();
    m_page.reset();
    try {
)";
    for (qsizetype index = 0; index < modules.size(); ++index) {
        const BlueprintNode &node = *modules[index];
        const QString number = QString::number(index);
        const QString inputs = "inputs" + number, outputs = "outputs" + number;
        source += "        Inputs " + inputs + ";\n";
        for (const PortSpec &port : node.inputs) {
            const QString key = textExpression(port.name);
            source += "        " + inputs + ".insert(" + key + ", outputs" + QString::number(index - 1) + ".value(" + key + "));\n";
        }
        const QString factory = "createModule_" + WorkspaceIo::sha256(node.id.toUtf8()).left(16);
        if (node.type == NodeType::LogicModule) {
            const QString module = "module" + number;
            source += "        auto " + module + " = " + factory + "(nullptr);\n"
                + "        if (!" + module + ") return startupFailure(error, " + textExpression(node.id + ": factory returned null") + ");\n"
                + "        const Outputs " + outputs + " = " + module + "->execute(" + inputs + ");\n"
                + "        if (!checkOutputs(" + outputs + ", {";
            QStringList requirements;
            for (const PortSpec &port : node.outputs)
                requirements.append("{" + textExpression(port.name) + ", " + portTypes().value(port.type) + "}");
            source += requirements.join(", ") + "}, " + textExpression(node.id) + ", error)) return false;\n";
        } else {
            source += "        m_page = " + factory + "(" + inputs + ", nullptr);\n"
                + "        if (!m_page) return startupFailure(error, " + textExpression(node.id + ": factory returned null") + ");\n"
                + "        m_page->setAttribute(Qt::WA_DeleteOnClose, false);\n"
                + "        m_page->show();\n";
        }
    }
    source += R"(        return true;
    } catch (const std::exception &exception) {
        return startupFailure(error, QString("Module exception: ") + QString::fromUtf8(exception.what()));
    } catch (...) {
        return startupFailure(error, "Unknown module exception");
    }
}
}
)";
    const QString main = "#include <QApplication>\n#include <QDebug>\n#include <cstdlib>\n#include \"runtime/workflow_controller.h\"\n"
          "int main(int argc, char **argv) {\n    QApplication app(argc, argv);\n    " + ns + "::WorkflowController controller;\n"
        + "    QString error;\n    if (!controller.start(&error)) {\n        qCritical().noquote() << \"Runtime startup failed:\" << error;\n        return EXIT_FAILURE;\n    }\n"
        + "    return controller.hasPage() ? app.exec() : EXIT_SUCCESS;\n}\n";
    files["src/runtime/workflow_controller.h"] = header.toUtf8();
    files["src/runtime/workflow_controller.cpp"] = source.toUtf8();
    files["src/main.cpp"] = main.toUtf8();
    return true;
}
