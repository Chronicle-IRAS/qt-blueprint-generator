#include "tools/manual_ai_integration_support.h"

#include "ai/openai_compatible_client.h"

#include <QCoreApplication>
#include <QDir>
#include <QNetworkAccessManager>
#include <QTextStream>
#include <QtGlobal>

namespace {

void printUsage(QTextStream &stream, const QString &program)
{
    stream << "Usage: " << program
           << " [--endpoint HTTPS_URL] [--model MODEL] [--timeout-seconds 1..600]"
              " [--workspace EXISTING_EMPTY_DIRECTORY]\n";
}

} // namespace

int main(int argc, char *argv[])
{
    QCoreApplication application(argc, argv);
    const QStringList arguments = application.arguments().mid(1);
    QTextStream output(stdout);
    QTextStream errors(stderr);

    if (arguments == QStringList{QStringLiteral("--help")}) {
        printUsage(output, application.applicationName());
        return static_cast<int>(ManualAiIntegration::ExitCode::Success);
    }

    ManualAiIntegration::Options options;
    QString error;
    if (!ManualAiIntegration::parseArguments(arguments, &options, &error)) {
        errors << "Configuration error: " << error << '\n';
        printUsage(errors, application.applicationName());
        return static_cast<int>(ManualAiIntegration::ExitCode::Usage);
    }

    QNetworkAccessManager networkAccessManager;
    OpenAiCompatibleClient client(options.endpoint,
                                  options.model,
                                  &networkAccessManager,
                                  options.timeout);
    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        options,
        !qEnvironmentVariableIsEmpty("BLUEPRINT_AI_API_KEY"),
        &client);

    QTextStream &status = result.exitCode == ManualAiIntegration::ExitCode::Success
                              ? output
                              : errors;
    status << result.safeMessage << '\n';
    if (result.exitCode == ManualAiIntegration::ExitCode::Success) {
        status << "Workspace: " << QDir::toNativeSeparators(result.workspace) << '\n'
               << "Candidate generation: " << result.generationId << '\n'
               << "No generated file was accepted automatically.\n";
    }
    return static_cast<int>(result.exitCode);
}
