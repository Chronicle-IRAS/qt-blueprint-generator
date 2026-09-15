#include "tools/manual_ai_integration_support.h"

#include "ai/ai_client.h"
#include "ai/fake_ai_client.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <utility>

class CountingAiClient final : public IAiClient
{
public:
    void generate(const AiRequest &) override { ++generateCalls; }

    int generateCalls = 0;
};

class UnrelatedTimeoutThenNetworkClient final : public IAiClient
{
public:
    void generate(const AiRequest &request) override
    {
        QTimer::singleShot(0, this, [this, request]() {
            emit requestFailed(QUuid::createUuid(),
                               {AiErrorKind::Timeout,
                                408,
                                QStringLiteral("unrelated-timeout-sentinel")});
            emit requestFailed(request.requestId,
                               {AiErrorKind::Network,
                                0,
                                QStringLiteral("matching-network-details")});
        });
    }
};

class SynchronousSuccessAiClient final : public IAiClient
{
public:
    explicit SynchronousSuccessAiClient(QByteArray response)
        : m_response(std::move(response))
    {
    }

    void generate(const AiRequest &request) override
    {
        emit responseReady(request.requestId, m_response);
    }

private:
    QByteArray m_response;
};

class SynchronousFailureAiClient final : public IAiClient
{
public:
    void generate(const AiRequest &request) override
    {
        emit requestFailed(request.requestId,
                           {AiErrorKind::Network,
                            0,
                            QStringLiteral("synchronous-provider-detail-sentinel")});
    }
};

ManualAiIntegration::Options optionsForWorkspace(
    const QString &workspace,
    std::chrono::milliseconds timeout = std::chrono::seconds(60))
{
    ManualAiIntegration::Options options;
    options.workspaceOverride = workspace;
    options.timeout = timeout;
    return options;
}

class ManualAiIntegrationTest final : public QObject
{
    Q_OBJECT

private slots:
    void usesDeepSeekDefaults();
    void acceptsEndpointModelAndTimeoutOverrides();
    void missingCredentialStopsBeforeAiRequest();
    void validResponseIsPersistedAsPendingCandidateOnly();
    void malformedResponseReturnsFixedSanitizedMessage();
    void clientTimeoutMapsToTimeoutExitCode();
    void http408MapsToTimeoutExitCode();
    void unrelatedTimeoutDoesNotChangeCurrentRequestExitCode();
    void promptContainsRealContractsAndExactCandidateBoundaries();
    void rejectsMissingWorkspaceOverrideWithoutCreatingIt();
    void rejectsNonEmptyWorkspaceOverrideWithoutChangingIt();
    void rejectsLinkedWorkspaceOverrideBeforeUsingTarget();
    void acceptsExplicitWorkspaceOverrideArgument();
    void supportsSynchronousAiSuccess();
    void supportsSynchronousAiFailure();
    void outerDeadlineMapsToTimeoutExitCode();
};

void ManualAiIntegrationTest::usesDeepSeekDefaults()
{
    ManualAiIntegration::Options options;
    QString error;

    QVERIFY2(ManualAiIntegration::parseArguments({}, &options, &error), qPrintable(error));
    QCOMPARE(options.endpoint,
             QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
    QCOMPARE(options.model, QStringLiteral("deepseek-flash"));
    QCOMPARE(options.timeout, std::chrono::seconds(60));
}

void ManualAiIntegrationTest::outerDeadlineMapsToTimeoutExitCode()
{
    CountingAiClient client;
    ManualAiIntegration::Options options;
    options.timeout = std::chrono::milliseconds(10);

    const ManualAiIntegration::RunResult result =
        ManualAiIntegration::run(options, true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::Timeout);
    QCOMPARE(result.safeMessage,
             QStringLiteral("AI generation exceeded the manual integration timeout"));
    QCOMPARE(client.generateCalls, 1);
}

void ManualAiIntegrationTest::supportsSynchronousAiFailure()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    SynchronousFailureAiClient client;
    ManualAiIntegration::Options options;
    options.timeout = std::chrono::milliseconds(10);
    options.workspaceOverride = workspace.path();

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        options, true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::ProviderFailure);
    QCOMPARE(result.safeMessage, QStringLiteral("AI provider request failed"));
    QVERIFY(!result.safeMessage.contains(QStringLiteral("sentinel")));
}

void ManualAiIntegrationTest::supportsSynchronousAiSuccess()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    SynchronousSuccessAiClient client(QByteArrayLiteral(
        R"({"nodeId":"manual_logic","summary":"Synchronous candidate","files":[{"path":"tests/manual_logic/synchronous.cpp","content":"int main() { return 0; }\n"}]})"));
    ManualAiIntegration::Options options;
    options.timeout = std::chrono::milliseconds(10);
    options.workspaceOverride = workspace.path();

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        options, true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::Success);
    QVERIFY(QFileInfo::exists(
        workspace.filePath(QStringLiteral("candidates/") + result.generationId
                           + QStringLiteral("/manual_logic/tests/manual_logic/synchronous.cpp"))));
}

void ManualAiIntegrationTest::acceptsExplicitWorkspaceOverrideArgument()
{
    ManualAiIntegration::Options options;
    QString error;

    QVERIFY2(ManualAiIntegration::parseArguments(
                 {QStringLiteral("--workspace"), QStringLiteral("C:/manual-empty-workspace")},
                 &options,
                 &error),
             qPrintable(error));
    QCOMPARE(options.workspaceOverride, QStringLiteral("C:/manual-empty-workspace"));
}

void ManualAiIntegrationTest::rejectsLinkedWorkspaceOverrideBeforeUsingTarget()
{
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    const QString link = root.filePath(QStringLiteral("linked-workspace"));
#ifdef Q_OS_WIN
    if (QProcess::execute(QStringLiteral("cmd.exe"),
                          {QStringLiteral("/c"),
                           QStringLiteral("mklink"),
                           QStringLiteral("/J"),
                           QDir::toNativeSeparators(link),
                           QDir::toNativeSeparators(outside.path())})
        != 0) {
        QSKIP("Directory junction creation unavailable");
    }
    const auto cleanup = qScopeGuard([&]() { QDir().rmdir(link); });
#else
    if (!QFile::link(outside.path(), link)) {
        QSKIP("Directory symbolic link creation unavailable");
    }
    const auto cleanup = qScopeGuard([&]() { QFile::remove(link); });
#endif
    CountingAiClient client;
    ManualAiIntegration::Options options;
    options.timeout = std::chrono::milliseconds(10);
    options.workspaceOverride = link;

    const ManualAiIntegration::RunResult result =
        ManualAiIntegration::run(options, true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::WorkspaceFailure);
    QCOMPARE(result.safeMessage,
             QStringLiteral("Workspace override must not contain links or reparse points"));
    QCOMPARE(client.generateCalls, 0);
    QVERIFY(QDir(outside.path())
                .entryList(QDir::AllEntries | QDir::Hidden | QDir::System
                               | QDir::NoDotAndDotDot)
                .isEmpty());
}

void ManualAiIntegrationTest::rejectsNonEmptyWorkspaceOverrideWithoutChangingIt()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    QFile sentinel(workspace.filePath(QStringLiteral("outside-sentinel.txt")));
    QVERIFY(sentinel.open(QIODevice::WriteOnly));
    QCOMPARE(sentinel.write("unchanged"), qint64(9));
    sentinel.close();
    CountingAiClient client;
    ManualAiIntegration::Options options;
    options.timeout = std::chrono::milliseconds(10);
    options.workspaceOverride = workspace.path();

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        options, true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::WorkspaceFailure);
    QCOMPARE(client.generateCalls, 0);
    QVERIFY(!QFileInfo::exists(workspace.filePath(QStringLiteral("generation-manifest.json"))));
    QVERIFY(sentinel.open(QIODevice::ReadOnly));
    QCOMPARE(sentinel.readAll(), QByteArray("unchanged"));
}

void ManualAiIntegrationTest::rejectsMissingWorkspaceOverrideWithoutCreatingIt()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString missing = root.filePath(QStringLiteral("must-not-be-created"));
    CountingAiClient client;
    ManualAiIntegration::Options options;
    options.timeout = std::chrono::milliseconds(10);
    options.workspaceOverride = missing;

    const ManualAiIntegration::RunResult result =
        ManualAiIntegration::run(options, true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::WorkspaceFailure);
    QCOMPARE(client.generateCalls, 0);
    QVERIFY(!QFileInfo::exists(missing));
}

void ManualAiIntegrationTest::promptContainsRealContractsAndExactCandidateBoundaries()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    FakeAiClient client;
    client.setSuccessfulResponse(QByteArrayLiteral(
        R"({"nodeId":"manual_logic","summary":"Candidate","files":[{"path":"tests/manual_logic/manual_logic_test.cpp","content":"int main() { return 0; }\n"}]})"));

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        optionsForWorkspace(workspace.path()), true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::Success);
    QCOMPARE(client.requests().size(), 1);
    const QString prompt = client.requests().first().prompt;
    QVERIFY(prompt.contains(QStringLiteral("\"id\":\"manual_logic\"")));
    QVERIFY(prompt.contains(QStringLiteral("\"type\":\"LogicModule\"")));
    QVERIFY(prompt.contains(QStringLiteral("namespace Blueprint_Manual_AI_Integration")));
    QVERIFY(prompt.contains(QStringLiteral("src/modules/manual_logic/implementation/")));
    QVERIFY(prompt.contains(QStringLiteral("tests/manual_logic/")));
    QVERIFY(prompt.contains(QStringLiteral(".h, .hpp, .cpp, .cc")));
}

void ManualAiIntegrationTest::http408MapsToTimeoutExitCode()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    FakeAiClient client;
    client.setFailure({AiErrorKind::Network, 408, QStringLiteral("HTTP body sentinel")});

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        optionsForWorkspace(workspace.path()), true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::Timeout);
    QCOMPARE(result.safeMessage, QStringLiteral("AI request timed out"));
    QVERIFY(!result.safeMessage.contains(QStringLiteral("sentinel")));
}

void ManualAiIntegrationTest::unrelatedTimeoutDoesNotChangeCurrentRequestExitCode()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    UnrelatedTimeoutThenNetworkClient client;

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        optionsForWorkspace(workspace.path()), true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::ProviderFailure);
    QCOMPARE(result.safeMessage, QStringLiteral("AI provider request failed"));
    QVERIFY(!result.safeMessage.contains(QStringLiteral("unrelated-timeout-sentinel")));
}

void ManualAiIntegrationTest::clientTimeoutMapsToTimeoutExitCode()
{
    QTemporaryDir workspace;
    QVERIFY(workspace.isValid());
    FakeAiClient client;
    client.setFailure({AiErrorKind::Timeout, 408, QStringLiteral("provider timeout details")});

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        optionsForWorkspace(workspace.path()), true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::Timeout);
    QCOMPARE(result.safeMessage, QStringLiteral("AI request timed out"));
    QVERIFY(!result.safeMessage.contains(QStringLiteral("provider timeout details")));
}

void ManualAiIntegrationTest::malformedResponseReturnsFixedSanitizedMessage()
{
    const QString sentinel = QStringLiteral("sentinel-secret-node-and-path");
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    QVERIFY(QDir().mkpath(workspace));
    FakeAiClient client;
    client.setSuccessfulResponse(
        QStringLiteral(
            R"({"nodeId":"%1","summary":"%1","files":[{"path":"../%1.cpp","content":"%1"}]})")
            .arg(sentinel)
            .toUtf8());

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        optionsForWorkspace(workspace), true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::InvalidResponse);
    QCOMPARE(result.safeMessage, QStringLiteral("AI response failed strict validation"));
    QVERIFY(!result.safeMessage.contains(sentinel));
}

void ManualAiIntegrationTest::validResponseIsPersistedAsPendingCandidateOnly()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString workspace = root.filePath(QStringLiteral("manual-workspace"));
    QVERIFY(QDir().mkpath(workspace));
    FakeAiClient client;
    client.setSuccessfulResponse(QByteArrayLiteral(
        R"({"nodeId":"manual_logic","summary":"Manual candidate","files":[{"path":"src/modules/manual_logic/implementation/manual_logic.cpp","content":"int manual_candidate() { return 1; }\n"}]})"));

    const ManualAiIntegration::RunResult result = ManualAiIntegration::run(
        optionsForWorkspace(workspace), true, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::Success);
    QCOMPARE(result.workspace, QDir::cleanPath(workspace));
    QCOMPARE(client.requests().size(), 1);
    QVERIFY(!QFileInfo::exists(
        root.filePath(QStringLiteral("manual-workspace/generated-project/src/modules/"
                                     "manual_logic/implementation/manual_logic.cpp"))));
    QVERIFY(QFileInfo::exists(
        root.filePath(QStringLiteral("manual-workspace/candidates/")
                      + result.generationId
                      + QStringLiteral("/manual_logic/src/modules/manual_logic/implementation/"
                                       "manual_logic.cpp"))));

    QFile manifestFile(root.filePath(
        QStringLiteral("manual-workspace/generation-manifest.json")));
    QVERIFY(manifestFile.open(QIODevice::ReadOnly));
    const QJsonObject manifest = QJsonDocument::fromJson(manifestFile.readAll()).object();
    QVERIFY(manifest.value(QStringLiteral("files")).toObject().isEmpty());
    const QJsonObject batch =
        manifest.value(QStringLiteral("batches")).toObject().value(result.generationId).toObject();
    QCOMPARE(batch.value(QStringLiteral("state")).toString(), QStringLiteral("active"));
    QCOMPARE(batch.value(QStringLiteral("files"))
                 .toObject()
                 .value(QStringLiteral(
                     "src/modules/manual_logic/implementation/manual_logic.cpp"))
                 .toObject()
                 .value(QStringLiteral("state"))
                 .toString(),
             QStringLiteral("pending"));
}

void ManualAiIntegrationTest::missingCredentialStopsBeforeAiRequest()
{
    CountingAiClient client;

    const ManualAiIntegration::RunResult result =
        ManualAiIntegration::run(ManualAiIntegration::Options{}, false, &client);

    QCOMPARE(result.exitCode, ManualAiIntegration::ExitCode::CredentialUnavailable);
    QCOMPARE(client.generateCalls, 0);
    QVERIFY(result.workspace.isEmpty());
    QVERIFY(!result.safeMessage.isEmpty());
}

void ManualAiIntegrationTest::acceptsEndpointModelAndTimeoutOverrides()
{
    ManualAiIntegration::Options options;
    QString error;

    QVERIFY2(ManualAiIntegration::parseArguments(
                 {QStringLiteral("--endpoint"),
                  QStringLiteral("https://gateway.example.test/v1/chat/completions"),
                  QStringLiteral("--model"),
                  QStringLiteral("custom-model"),
                  QStringLiteral("--timeout-seconds"),
                  QStringLiteral("17")},
                 &options,
                 &error),
             qPrintable(error));
    QCOMPARE(options.endpoint,
             QUrl(QStringLiteral("https://gateway.example.test/v1/chat/completions")));
    QCOMPARE(options.model, QStringLiteral("custom-model"));
    QCOMPARE(options.timeout, std::chrono::seconds(17));
}

QTEST_GUILESS_MAIN(ManualAiIntegrationTest)

#include "tst_manual_ai_integration.moc"
