#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <chrono>

class IAiClient;

namespace ManualAiIntegration {

struct Options
{
    QUrl endpoint{QStringLiteral("https://api.deepseek.com/chat/completions")};
    QString model{QStringLiteral("deepseek-flash")};
    std::chrono::milliseconds timeout{std::chrono::seconds(60)};
    QString workspaceOverride;
};

enum class ExitCode : int
{
    Success = 0,
    Usage = 2,
    CredentialUnavailable = 3,
    InvalidBlueprint = 4,
    WorkspaceFailure = 5,
    PromptFailure = 6,
    ProviderFailure = 7,
    Timeout = 8,
    InvalidResponse = 9,
    PersistenceFailure = 10,
};

struct RunResult
{
    ExitCode exitCode = ExitCode::Success;
    QString safeMessage;
    QString workspace;
    QString generationId;
};

bool parseArguments(const QStringList &arguments,
                    Options *options,
                    QString *errorMessage = nullptr);
RunResult run(const Options &options,
              bool credentialAvailable,
              IAiClient *client);

} // namespace ManualAiIntegration
