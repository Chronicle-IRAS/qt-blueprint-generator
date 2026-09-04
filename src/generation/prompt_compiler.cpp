#include "generation/prompt_compiler.h"

#include "generation/ir_compiler.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QStringList>

namespace {

QString projectValue(const QJsonObject &project, const char *key)
{
    return project.value(QLatin1String(key)).toString();
}

QString projectConventions(const QJsonObject &project)
{
    QStringList conventions;
    for (const QJsonValue &value : project.value(QStringLiteral("codingConventions")).toArray()) {
        conventions.append(value.toString());
    }
    return conventions.join(QStringLiteral(", "));
}

std::optional<QJsonObject> findModule(const QJsonObject &ir, const QString &nodeId)
{
    for (const QJsonValue &value : ir.value(QStringLiteral("modules")).toArray()) {
        const QJsonObject module = value.toObject();
        if (module.value(QStringLiteral("id")).toString() == nodeId) {
            return module;
        }
    }
    return std::nullopt;
}

QString outputContract(const QString &nodeId)
{
    const QJsonObject file{
        {QStringLiteral("path"), QStringLiteral("relative/path/to/file")},
        {QStringLiteral("content"), QStringLiteral("complete file content")},
    };
    const QJsonObject contract{
        {QStringLiteral("nodeId"), nodeId},
        {QStringLiteral("summary"), QStringLiteral("short implementation summary")},
        {QStringLiteral("files"), QJsonArray{file}},
    };
    return QString::fromUtf8(QJsonDocument(contract).toJson(QJsonDocument::Compact));
}

} // namespace

QString PromptCompiler::compileProjectPrompt(const QJsonObject &ir)
{
    const QJsonObject project = ir.value(QStringLiteral("project")).toObject();
    return QStringLiteral("Project summary\n"
                          "- Name: %1\n"
                          "- Namespace: %2\n"
                          "- Target: %3\n"
                          "- Framework: %4\n"
                          "- Language standard: %5\n"
                          "- Build system: %6\n"
                          "- Coding conventions: %7\n"
                          "- Overall goal: %8")
        .arg(projectValue(project, "name"),
             projectValue(project, "namespace"),
             projectValue(project, "target"),
             projectValue(project, "framework"),
             projectValue(project, "languageStandard"),
             projectValue(project, "buildSystem"),
             projectConventions(project),
             projectValue(project, "goal"));
}

std::optional<QString> PromptCompiler::compileModulePrompt(const QJsonObject &ir,
                                                           const QString &nodeId,
                                                           QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }

    const std::optional<QJsonObject> module = findModule(ir, nodeId);
    if (!module.has_value()) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("IR contains no generatable module with node ID '%1'")
                                .arg(nodeId);
        }
        return std::nullopt;
    }

    const QString moduleJson = QString::fromUtf8(IrCompiler::toCanonicalJson(*module));
    return QStringLiteral("System constraints\n"
                          "- Generate exactly one module for Qt 6 Widgets.\n"
                          "- Use C++17 and integrate with CMake.\n"
                          "- Do not use undeclared dependencies.\n"
                          "- Respect every constraint and acceptance criterion in the module IR.\n\n"
                          "%1\n\n"
                          "Current module IR (including only direct upstream and downstream public "
                          "interfaces)\n"
                          "%2\n\n"
                          "Output contract\n"
                          "%3\n"
                          "Return strict JSON only. Do not use Markdown code fences. Do not add text "
                          "outside the JSON object.")
        .arg(compileProjectPrompt(ir), moduleJson, outputContract(nodeId));
}
