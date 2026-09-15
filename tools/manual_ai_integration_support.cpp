#include "tools/manual_ai_integration_support.h"

#include "ai/ai_provider_settings.h"
#include "ai/ai_client.h"
#include "blueprint/blueprint_validator.h"
#include "generation/generation_service.h"
#include "generation/project_scaffolder.h"
#include "generation/prompt_compiler.h"

#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QHash>
#include <QTemporaryDir>
#include <QTimer>
#include <QUuid>

#include <memory>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace ManualAiIntegration {

bool parseArguments(const QStringList &arguments, Options *options, QString *errorMessage)
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (options == nullptr) {
        if (errorMessage != nullptr) {
            *errorMessage = QStringLiteral("Options output is required");
        }
        return false;
    }
    Options parsed;
    for (qsizetype index = 0; index < arguments.size(); ++index) {
        const QString argument = arguments.at(index);
        if (argument != QStringLiteral("--endpoint")
            && argument != QStringLiteral("--model")
            && argument != QStringLiteral("--timeout-seconds")
            && argument != QStringLiteral("--workspace")) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Unknown argument: %1").arg(argument);
            }
            return false;
        }
        if (++index >= arguments.size()) {
            if (errorMessage != nullptr) {
                *errorMessage = QStringLiteral("Missing value for %1").arg(argument);
            }
            return false;
        }

        const QString value = arguments.at(index);
        if (argument == QStringLiteral("--endpoint")) {
            parsed.endpoint = QUrl(value);
        } else if (argument == QStringLiteral("--model")) {
            parsed.model = value;
        } else if (argument == QStringLiteral("--workspace")) {
            parsed.workspaceOverride = value;
        } else {
            bool validNumber = false;
            const int seconds = value.toInt(&validNumber);
            if (!validNumber || seconds <= 0 || seconds > 600) {
                if (errorMessage != nullptr) {
                    *errorMessage = QStringLiteral("Timeout must be between 1 and 600 seconds");
                }
                return false;
            }
            parsed.timeout = std::chrono::seconds(seconds);
        }
    }

    AiProviderSettings settings;
    settings.endpoint = parsed.endpoint;
    settings.model = parsed.model;
    if (!settings.validate(errorMessage)) {
        return false;
    }
    *options = parsed;
    return true;
}

namespace {

BlueprintDocument manualBlueprint()
{
    BlueprintDocument document;
    document.projectId = QStringLiteral("manual-ai-integration");
    document.projectName = QStringLiteral("Manual AI Integration");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    document.nodes = {
        {QStringLiteral("start"), NodeType::Start},
        {QStringLiteral("manual_logic"),
         NodeType::LogicModule,
         QStringLiteral("Manual logic"),
         QStringLiteral("Implement a minimal deterministic C++ module for manual verification"),
         {},
         {},
         {QStringLiteral("Use only C++17 and Qt 6 APIs already declared by the project")},
         {QStringLiteral("Return at least one compilable implementation or test source file")}},
        {QStringLiteral("end"), NodeType::End},
    };
    document.edges = {
        {QStringLiteral("start-to-logic"),
         QStringLiteral("start"),
         QStringLiteral("manual_logic"),
         {}},
        {QStringLiteral("logic-to-end"),
         QStringLiteral("manual_logic"),
         QStringLiteral("end"),
         {}},
    };
    return document;
}

bool readUtf8File(const QString &path, QString *contents)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    *contents = QString::fromUtf8(file.readAll());
    return file.error() == QFileDevice::NoError;
}

bool pathContainsLinkOrReparsePoint(const QString &path)
{
    QString cursor = QDir::cleanPath(QDir::fromNativeSeparators(path));
    while (true) {
        const QFileInfo info(cursor);
        bool isLink = info.isSymbolicLink();
#ifdef Q_OS_WIN
        const QString nativePath = QDir::toNativeSeparators(cursor);
        const DWORD attributes =
            GetFileAttributesW(reinterpret_cast<LPCWSTR>(nativePath.utf16()));
        isLink = isLink
                 || (attributes != INVALID_FILE_ATTRIBUTES
                     && (attributes & FILE_ATTRIBUTE_REPARSE_POINT));
#endif
        if (isLink) {
            return true;
        }
        const QString parent = info.absolutePath();
        if (parent == cursor) {
            return false;
        }
        cursor = parent;
    }
}

} // namespace

RunResult run(const Options &options,
              bool credentialAvailable,
              IAiClient *client)
{
    if (!credentialAvailable) {
        return {ExitCode::CredentialUnavailable,
                QStringLiteral("BLUEPRINT_AI_API_KEY is not available"),
                {},
                {}};
    }
    if (client == nullptr) {
        return {ExitCode::ProviderFailure,
                QStringLiteral("AI client is unavailable"),
                {},
                {}};
    }

    AiProviderSettings settings;
    settings.endpoint = options.endpoint;
    settings.model = options.model;
    QString error;
    if (!settings.validate(&error) || options.timeout.count() <= 0) {
        return {ExitCode::Usage,
                error.isEmpty() ? QStringLiteral("Timeout must be positive") : error,
                {},
                {}};
    }

    const BlueprintDocument blueprint = manualBlueprint();
    const QVector<BlueprintDiagnostic> diagnostics = BlueprintValidator::validate(blueprint);
    if (!diagnostics.isEmpty()) {
        return {ExitCode::InvalidBlueprint,
                QStringLiteral("Manual blueprint failed validation"),
                {},
                {}};
    }

    std::unique_ptr<QTemporaryDir> temporaryWorkspace;
    QString workspace = options.workspaceOverride;
    if (workspace.isEmpty()) {
        temporaryWorkspace = std::make_unique<QTemporaryDir>(
            QDir::tempPath() + QStringLiteral("/blueprint-ai-manual-XXXXXX"));
        if (!temporaryWorkspace->isValid()) {
            return {ExitCode::WorkspaceFailure,
                    QStringLiteral("Could not create a temporary workspace"),
                    {},
                    {}};
        }
        workspace = temporaryWorkspace->path();
        if (pathContainsLinkOrReparsePoint(workspace)) {
            return {ExitCode::WorkspaceFailure,
                    QStringLiteral("Temporary workspace path contains a link or reparse point"),
                    {},
                    {}};
        }
    } else {
        workspace = QDir::cleanPath(QDir::fromNativeSeparators(workspace));
        if (!QDir::isAbsolutePath(workspace)) {
            return {ExitCode::WorkspaceFailure,
                    QStringLiteral("Workspace override must be an existing absolute directory"),
                    {},
                    {}};
        }
        if (pathContainsLinkOrReparsePoint(workspace)) {
            return {ExitCode::WorkspaceFailure,
                    QStringLiteral(
                        "Workspace override must not contain links or reparse points"),
                    {},
                    {}};
        }
        if (!QFileInfo::exists(workspace) || !QFileInfo(workspace).isDir()) {
            return {ExitCode::WorkspaceFailure,
                    QStringLiteral("Workspace override must be an existing absolute directory"),
                    {},
                    {}};
        }
        if (!QDir(workspace)
                 .entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                                | QDir::NoDotAndDotDot)
                 .isEmpty()) {
            return {ExitCode::WorkspaceFailure,
                    QStringLiteral("Workspace override must be empty"),
                    {},
                    {}};
        }
    }

    if (!ProjectScaffolder::create(blueprint, workspace, &error)) {
        return {ExitCode::WorkspaceFailure, error, {}, {}};
    }

    const QString projectRoot = QDir(workspace).filePath(QStringLiteral("generated-project"));
    QString irText;
    if (!readUtf8File(QDir(projectRoot).filePath(
                          QStringLiteral("src/contracts/blueprint.json")),
                      &irText)) {
        return {ExitCode::PromptFailure,
                QStringLiteral("Could not read the scaffolded blueprint IR"),
                {},
                {}};
    }
    QJsonParseError parseError;
    const QJsonDocument irDocument = QJsonDocument::fromJson(irText.toUtf8(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !irDocument.isObject()) {
        return {ExitCode::PromptFailure,
                QStringLiteral("Scaffolded blueprint IR is invalid"),
                {},
                {}};
    }

    constexpr auto NodeId = "manual_logic";
    std::optional<QString> prompt = PromptCompiler::compileModulePrompt(
        irDocument.object(), QString::fromLatin1(NodeId), &error);
    QString publicTypes;
    QString nodeContract;
    if (!prompt.has_value()
        || !readUtf8File(QDir(projectRoot).filePath(
                             QStringLiteral("src/contracts/types.h")),
                         &publicTypes)
        || !readUtf8File(QDir(projectRoot).filePath(
                             QStringLiteral("src/modules/manual_logic/contract.h")),
                         &nodeContract)) {
        return {ExitCode::PromptFailure,
                error.isEmpty() ? QStringLiteral("Could not compile the module prompt") : error,
                {},
                {}};
    }
    *prompt += QStringLiteral("\n\nPublic types:\n") + publicTypes
               + QStringLiteral("\nNode contract:\n") + nodeContract
               + QStringLiteral(
                   "\nCandidate file boundary:\n"
                   "- Every returned path must start with exactly one allowed root:\n"
                   "  - src/modules/manual_logic/implementation/\n"
                   "  - tests/manual_logic/\n"
                   "- Allowed extensions: .h, .hpp, .cpp, .cc\n"
                   "- Do not return protected scaffold files or paths outside these roots.\n");

    const QString generationId = QStringLiteral("manual_")
                                 + QUuid::createUuid().toString(QUuid::Id128);
    const QString candidateDirectory = QDir(workspace).filePath(
        QStringLiteral("candidates/%1/%2").arg(generationId, QString::fromLatin1(NodeId)));
    GenerationService service(client);
    QEventLoop waitLoop;
    QTimer deadline;
    deadline.setSingleShot(true);
    std::optional<GenerationResult> generated;
    bool timedOut = false;
    bool completed = false;
    QHash<QUuid, AiClientError> providerErrors;

    QObject::connect(&service,
                     &GenerationService::generationSucceeded,
                     &waitLoop,
                     [&](const QUuid &, const GenerationResult &result) {
                         generated = result;
                         completed = true;
                         waitLoop.quit();
                     });
    QObject::connect(&service,
                     &GenerationService::generationFailed,
                     &waitLoop,
                     [&](const QUuid &, const QString &) {
                         completed = true;
                         waitLoop.quit();
                     });
    QObject::connect(client,
                     &IAiClient::requestFailed,
                     &waitLoop,
                     [&](const QUuid &requestId, const AiClientError &clientError) {
                         providerErrors.insert(requestId, clientError);
                     });
    QObject::connect(&deadline, &QTimer::timeout, &waitLoop, [&]() {
        timedOut = true;
        completed = true;
        waitLoop.quit();
    });

    const QUuid requestId = service.generate(*prompt,
                                             QString::fromLatin1(NodeId),
                                             candidateDirectory);
    if (!completed) {
        deadline.start(options.timeout);
        waitLoop.exec();
    }
    deadline.stop();

    if (timedOut) {
        return {ExitCode::Timeout,
                QStringLiteral("AI generation exceeded the manual integration timeout"),
                {},
                {}};
    }
    if (!generated.has_value()) {
        const auto providerError = providerErrors.constFind(requestId);
        if (providerError != providerErrors.cend()) {
            const bool providerTimeout = providerError->kind == AiErrorKind::Timeout
                                         || providerError->httpStatus == 408;
            return {providerTimeout ? ExitCode::Timeout : ExitCode::ProviderFailure,
                    providerTimeout ? QStringLiteral("AI request timed out")
                                    : QStringLiteral("AI provider request failed"),
                    {},
                    {}};
        }
        return {ExitCode::InvalidResponse,
                QStringLiteral("AI response failed strict validation"),
                {},
                {}};
    }
    if (!GenerationService::persistCandidate(workspace,
                                             generationId,
                                             *generated,
                                             options.model,
                                             *prompt,
                                             &error)) {
        return {ExitCode::PersistenceFailure,
                QStringLiteral("Validated candidate could not be persisted safely"),
                {},
                {}};
    }

    if (temporaryWorkspace != nullptr) {
        temporaryWorkspace->setAutoRemove(false);
    }
    return {ExitCode::Success,
            QStringLiteral("Validated candidate persisted; manual review is required"),
            workspace,
            generationId};
}

} // namespace ManualAiIntegration
