#include "generation/ir_compiler.h"
#include "generation/prompt_compiler.h"

#include <QtTest>

namespace {

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

BlueprintDocument promptDocument()
{
    BlueprintDocument document;
    document.projectId = QStringLiteral("prompt-demo");
    document.projectName = QStringLiteral("Prompt Demo");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");

    BlueprintNode source = node(QStringLiteral("source"),
                                NodeType::LogicModule,
                                QStringLiteral("SourceService"),
                                QStringLiteral("Provides requests"));
    source.outputs = {{QStringLiteral("request"),
                       QStringLiteral("Request"),
                       QStringLiteral("Public request contract")}};

    BlueprintNode current = node(QStringLiteral("current"),
                                 NodeType::LogicModule,
                                 QStringLiteral("CurrentService"),
                                 QStringLiteral("Processes one request"));
    current.inputs = {{QStringLiteral("request"),
                       QStringLiteral("Request"),
                       QStringLiteral("Request to process")}};
    current.outputs = {{QStringLiteral("accepted"),
                        QStringLiteral("bool"),
                        QStringLiteral("Whether processing succeeded")}};
    current.constraints = {QStringLiteral("Never block the UI thread")};
    current.acceptanceCriteria = {QStringLiteral("Rejects an empty request")};

    BlueprintNode target = node(QStringLiteral("target"),
                                NodeType::UiPage,
                                QStringLiteral("TargetPage"),
                                QStringLiteral("Displays the result"));
    target.inputs = {{QStringLiteral("accepted"),
                      QStringLiteral("bool"),
                      QStringLiteral("Result to display")}};

    BlueprintNode distant = node(QStringLiteral("distant"),
                                 NodeType::LogicModule,
                                 QStringLiteral("DistantSecretModule"),
                                 QStringLiteral("Must not appear in the current prompt"));

    document.nodes = {source, current, target, distant};
    document.edges = {
        {QStringLiteral("edge-source"),
         QStringLiteral("source"),
         QStringLiteral("current"),
         QString()},
        {QStringLiteral("edge-target"),
         QStringLiteral("current"),
         QStringLiteral("target"),
         QString()},
    };
    return document;
}

} // namespace

class PromptCompilerTest : public QObject
{
    Q_OBJECT

private slots:
    void projectPromptContainsOnlyProjectSummary();
    void modulePromptContainsContractAndStrictOutputRules();
    void modulePromptUsesOnlyNecessaryContext();
    void rejectsUnknownModule();
};

void PromptCompilerTest::projectPromptContainsOnlyProjectSummary()
{
    const QJsonObject ir = IrCompiler::compile(promptDocument());
    const QString prompt = PromptCompiler::compileProjectPrompt(ir);

    QVERIFY(prompt.contains(QStringLiteral("Prompt Demo")));
    QVERIFY(prompt.contains(QStringLiteral("Prompt_Demo")));
    QVERIFY(prompt.contains(QStringLiteral("qt6-widgets-cpp17-cmake")));
    QVERIFY(prompt.contains(QStringLiteral("Generate Prompt Demo from the blueprint module contracts.")));
    QVERIFY(!prompt.contains(QStringLiteral("CurrentService")));
    QVERIFY(!prompt.contains(QStringLiteral("DistantSecretModule")));
}

void PromptCompilerTest::modulePromptContainsContractAndStrictOutputRules()
{
    const QJsonObject ir = IrCompiler::compile(promptDocument());
    QString error = QStringLiteral("stale");
    const std::optional<QString> compiled =
        PromptCompiler::compileModulePrompt(ir, QStringLiteral("current"), &error);

    QVERIFY(compiled.has_value());
    QVERIFY(error.isEmpty());
    const QString &prompt = *compiled;
    QVERIFY(prompt.contains(QStringLiteral("CurrentService")));
    QVERIFY(prompt.contains(QStringLiteral("Processes one request")));
    QVERIFY(prompt.contains(QStringLiteral("Never block the UI thread")));
    QVERIFY(prompt.contains(QStringLiteral("Rejects an empty request")));
    QVERIFY(prompt.contains(QStringLiteral("Qt 6 Widgets")));
    QVERIFY(prompt.contains(QStringLiteral("C++17")));
    QVERIFY(prompt.contains(QStringLiteral("CMake")));
    QVERIFY(prompt.contains(QStringLiteral("Do not use undeclared dependencies")));
    QVERIFY(prompt.contains(QStringLiteral("Return strict JSON only")));
    QVERIFY(prompt.contains(QStringLiteral("Do not use Markdown code fences")));
    QVERIFY(prompt.contains(QStringLiteral("\"nodeId\"")));
    QVERIFY(prompt.contains(QStringLiteral("\"summary\"")));
    QVERIFY(prompt.contains(QStringLiteral("\"files\"")));
    QVERIFY(prompt.contains(QStringLiteral("\"path\"")));
    QVERIFY(prompt.contains(QStringLiteral("\"content\"")));
}

void PromptCompilerTest::modulePromptUsesOnlyNecessaryContext()
{
    const QJsonObject ir = IrCompiler::compile(promptDocument());
    const std::optional<QString> compiled =
        PromptCompiler::compileModulePrompt(ir, QStringLiteral("current"));

    QVERIFY(compiled.has_value());
    QVERIFY(compiled->contains(QStringLiteral("SourceService")));
    QVERIFY(compiled->contains(QStringLiteral("Public request contract")));
    QVERIFY(compiled->contains(QStringLiteral("TargetPage")));
    QVERIFY(compiled->contains(QStringLiteral("Result to display")));
    QVERIFY(!compiled->contains(QStringLiteral("DistantSecretModule")));
    QCOMPARE(*compiled,
             *PromptCompiler::compileModulePrompt(ir, QStringLiteral("current")));
}

void PromptCompilerTest::rejectsUnknownModule()
{
    const QJsonObject ir = IrCompiler::compile(promptDocument());
    QString error;
    const std::optional<QString> compiled =
        PromptCompiler::compileModulePrompt(ir, QStringLiteral("missing"), &error);

    QVERIFY(!compiled.has_value());
    QVERIFY(error.contains(QStringLiteral("missing")));
}

QTEST_MAIN(PromptCompilerTest)

#include "tst_prompt_compiler.moc"
