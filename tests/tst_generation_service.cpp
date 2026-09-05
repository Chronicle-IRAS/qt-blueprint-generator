#include "ai/fake_ai_client.h"
#include "ai/openai_compatible_client.h"
#include "generation/generation_service.h"

#include <QDir>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>
#include <QProcessEnvironment>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

#include <chrono>
#include <cstring>
#include <functional>
#include <utility>

namespace {

QByteArray response(const QString &nodeId,
                    const QVector<QPair<QString, QString>> &files = {
                        {QStringLiteral("src/widget.cpp"), QStringLiteral("int answer = 42;\n")},
                    })
{
    QJsonArray fileValues;
    for (const auto &file : files) {
        fileValues.append(QJsonObject{
            {QStringLiteral("path"), file.first},
            {QStringLiteral("content"), file.second},
        });
    }

    return QJsonDocument(QJsonObject{
                             {QStringLiteral("nodeId"), nodeId},
                             {QStringLiteral("summary"), QStringLiteral("Generated module")},
                             {QStringLiteral("files"), fileValues},
                         })
        .toJson(QJsonDocument::Compact);
}

QByteArray providerEnvelope(const QByteArray &modelResponse)
{
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("choices"),
                              QJsonArray{QJsonObject{
                                  {QStringLiteral("message"),
                                   QJsonObject{
                                       {QStringLiteral("content"),
                                        QString::fromUtf8(modelResponse)},
                                   }},
                              }}},
                         })
        .toJson(QJsonDocument::Compact);
}

bool directoryIsEmpty(const QString &path)
{
    return QDir(path).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty();
}

std::optional<GenerationResult> parse(const QByteArray &payload,
                                      const QString &root,
                                      QString *errorMessage = nullptr,
                                      const GenerationLimits &limits = {})
{
    return GenerationService::parseAndValidate(
        payload, QStringLiteral("node-1"), root, limits, errorMessage);
}

class ControllableAiClient final : public IAiClient
{
public:
    using IAiClient::IAiClient;

    void generate(const AiRequest &request) override
    {
        requests.append(request);
    }

    QVector<AiRequest> requests;
};

class EnvironmentVariableGuard final
{
public:
    EnvironmentVariableGuard(const QByteArray &name, const QByteArray &value)
        : m_name(name)
        , m_wasSet(qEnvironmentVariableIsSet(name.constData()))
        , m_previousValue(qgetenv(name.constData()))
    {
        qputenv(m_name.constData(), value);
    }

    ~EnvironmentVariableGuard()
    {
        if (m_wasSet) {
            qputenv(m_name.constData(), m_previousValue);
        } else {
            qunsetenv(m_name.constData());
        }
    }

private:
    QByteArray m_name;
    bool m_wasSet = false;
    QByteArray m_previousValue;
};

class DirectoryLinkGuard final
{
public:
    explicit DirectoryLinkGuard(QString path)
        : m_path(std::move(path))
    {
    }

    ~DirectoryLinkGuard()
    {
        if (QFile::remove(m_path)) {
            return;
        }
        const QFileInfo info(m_path);
        QDir(info.absolutePath()).rmdir(info.fileName());
    }

private:
    QString m_path;
};

struct StubNetworkResponse
{
    QByteArray body;
    int statusCode = 200;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    bool finishes = true;
};

struct IsolatedProcessResult
{
    bool started = false;
    bool finished = false;
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    int exitCode = -1;
    QByteArray diagnostics;
};

IsolatedProcessResult runIsolatedTest(const QString &testFunction,
                                      const QString &scenarioVariable)
{
    QProcess process;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    environment.insert(scenarioVariable, QStringLiteral("1"));
    process.setProcessEnvironment(environment);
    process.setProgram(QCoreApplication::applicationFilePath());
    process.setArguments({testFunction});
    process.start();

    IsolatedProcessResult result;
    result.started = process.waitForStarted(2000);
    if (!result.started) {
        result.diagnostics = process.errorString().toUtf8();
        return result;
    }
    result.finished = process.waitForFinished(5000);
    if (!result.finished) {
        process.kill();
        process.waitForFinished(1000);
    }
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.diagnostics = process.readAllStandardOutput() + process.readAllStandardError();
    return result;
}

class StubNetworkReply final : public QNetworkReply
{
public:
    StubNetworkReply(QNetworkAccessManager::Operation operation,
                     const QNetworkRequest &request,
                     const StubNetworkResponse &response,
                     QObject *parent)
        : QNetworkReply(parent)
        , m_body(response.body)
        , m_networkError(response.networkError)
    {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.statusCode);
        open(QIODevice::ReadOnly | QIODevice::Unbuffered);
        if (response.finishes) {
            QTimer::singleShot(0, this, [this]() { complete(); });
        }
    }

    void abort() override
    {
        if (m_completed) {
            return;
        }
        m_completed = true;
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("stub request aborted"));
        setFinished(true);
        emit finished();
    }

    qint64 bytesAvailable() const override
    {
        return (m_body.size() - m_offset) + QNetworkReply::bytesAvailable();
    }

protected:
    qint64 readData(char *data, qint64 maxSize) override
    {
        if (m_offset >= m_body.size()) {
            return -1;
        }
        const qint64 bytesToRead = qMin(maxSize, m_body.size() - m_offset);
        std::memcpy(data, m_body.constData() + m_offset, static_cast<size_t>(bytesToRead));
        m_offset += bytesToRead;
        return bytesToRead;
    }

private:
    void complete()
    {
        if (m_completed) {
            return;
        }
        if (m_networkError != QNetworkReply::NoError) {
            setError(m_networkError, QStringLiteral("stub network failure"));
        }
        if (!m_body.isEmpty()) {
            emit readyRead();
        }
        if (m_completed) {
            return;
        }
        m_completed = true;
        setFinished(true);
        emit finished();
    }

    QByteArray m_body;
    QNetworkReply::NetworkError m_networkError = QNetworkReply::NoError;
    qint64 m_offset = 0;
    bool m_completed = false;
};

class StubNetworkAccessManager final : public QNetworkAccessManager
{
public:
    using QNetworkAccessManager::QNetworkAccessManager;

    void enqueueResponse(const StubNetworkResponse &response)
    {
        m_responses.append(response);
    }

    QVector<QNetworkRequest> requests;
    QVector<QByteArray> requestBodies;
    std::function<void()> replyFinishedHandler;

protected:
    QNetworkReply *createRequest(Operation operation,
                                 const QNetworkRequest &request,
                                 QIODevice *outgoingData) override
    {
        requests.append(request);
        requestBodies.append(outgoingData == nullptr ? QByteArray{} : outgoingData->readAll());
        const StubNetworkResponse response = m_responses.isEmpty()
                                                 ? StubNetworkResponse{{}, 200,
                                                                       QNetworkReply::NoError,
                                                                       false}
                                                 : m_responses.takeFirst();
        auto *reply = new StubNetworkReply(operation, request, response, this);
        if (replyFinishedHandler) {
            QObject::connect(reply,
                             &QNetworkReply::finished,
                             this,
                             [this]() {
                                 const std::function<void()> handler = replyFinishedHandler;
                                 if (handler) {
                                     handler();
                                 }
                             },
                             Qt::DirectConnection);
        }
        return reply;
    }

private:
    QVector<StubNetworkResponse> m_responses;
};

} // namespace

class GenerationServiceTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void acceptsValidResponseWithoutWritingFiles();
    void rejectsMalformedResponses_data();
    void rejectsMalformedResponses();
    void rejectsWrongNodeId();
    void rejectsUnsafePaths_data();
    void rejectsUnsafePaths();
    void rejectsUnsupportedExtension();
    void enforcesRawResponseLimitAtUtf8ByteBoundary();
    void enforcesSingleFileLimitAtUtf8ByteBoundary();
    void enforcesTotalContentLimitAtUtf8ByteBoundary();
    void enforcesFileCountLimit();
    void rejectsDuplicateNormalizedPaths();
    void requiresAbsoluteCandidateDirectory();
    void rejectsExistingDirectoryLinkOutsideCandidateRoot();
    void fakeClientCompletesSuccessfullyAsynchronously();
    void propagatesClientFailureAsynchronously();
    void correlatesConcurrentResponsesByRequestId();
    void failsPendingRequestsWhenClientIsDestroyed();
    void openAiClientFailsOnceWhenInjectedManagerIsDestroyed();
    void openAiClientEnforcesAbsoluteDeadline();
    void openAiClientReadsApiKeyForEveryOfflineRequest();
    void openAiClientEnforcesWireLimitOffline();
    void openAiClientRejectsInsecureEndpointOffline();
    void openAiClientRejectsRedirectAndInvalidEnvelopeOffline();
    void generationServiceDeletionDuringClientDestroyedFanoutIsSafe();
    void openAiClientDeletionDuringAbortIsSafe();
};

void GenerationServiceTest::initTestCase()
{
    qRegisterMetaType<GenerationResult>();
}

void GenerationServiceTest::acceptsValidResponseWithoutWritingFiles()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error = QStringLiteral("stale");

    const std::optional<GenerationResult> result = parse(response(QStringLiteral("node-1")),
                                                         root.path(),
                                                         &error);

    QVERIFY2(result.has_value(), qPrintable(error));
    QVERIFY(error.isEmpty());
    QCOMPARE(result->nodeId, QStringLiteral("node-1"));
    QCOMPARE(result->summary, QStringLiteral("Generated module"));
    QCOMPARE(result->files.size(), 1);
    QCOMPARE(result->files.constFirst().relativePath, QStringLiteral("src/widget.cpp"));
    QCOMPARE(result->files.constFirst().content, QStringLiteral("int answer = 42;\n"));
    const QString expectedTarget =
        QDir::cleanPath(QDir(root.path()).absoluteFilePath(QStringLiteral("src/widget.cpp")));
    QCOMPARE(result->files.constFirst().absoluteCandidatePath, expectedTarget);
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::rejectsMalformedResponses_data()
{
    QTest::addColumn<QByteArray>("payload");

    QTest::newRow("invalid-json") << QByteArray("{not json");
    QTest::newRow("markdown-fence")
        << QByteArray("```json\n{\"nodeId\":\"node-1\",\"summary\":\"x\",\"files\":[]}\n```");
    QTest::newRow("root-array") << QByteArray("[]");
    QTest::newRow("missing-summary")
        << QByteArray(R"({"nodeId":"node-1","files":[]})");
    QTest::newRow("wrong-files-type")
        << QByteArray(R"({"nodeId":"node-1","summary":"x","files":{}})");
    QTest::newRow("missing-file-content")
        << QByteArray(R"({"nodeId":"node-1","summary":"x","files":[{"path":"x.cpp"}]})");
    QTest::newRow("wrong-file-path-type")
        << QByteArray(R"({"nodeId":"node-1","summary":"x","files":[{"path":1,"content":"x"}]})");
    QTest::newRow("unknown-root-field")
        << QByteArray(R"({"nodeId":"node-1","summary":"x","files":[{"path":"x.cpp","content":"x"}],"extra":true})");
    QTest::newRow("unknown-file-field")
        << QByteArray(R"({"nodeId":"node-1","summary":"x","files":[{"path":"x.cpp","content":"x","extra":true}]})");
}

void GenerationServiceTest::rejectsMalformedResponses()
{
    QFETCH(QByteArray, payload);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;

    QVERIFY(!parse(payload, root.path(), &error).has_value());
    QVERIFY(!error.isEmpty());
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::rejectsWrongNodeId()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;

    QVERIFY(!parse(response(QStringLiteral("other-node")), root.path(), &error).has_value());
    QVERIFY(error.contains(QStringLiteral("nodeId")));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::rejectsUnsafePaths_data()
{
    QTest::addColumn<QString>("path");

    QTest::newRow("unix-absolute") << QStringLiteral("/outside.cpp");
    QTest::newRow("windows-absolute") << QStringLiteral("C:/outside.cpp");
    QTest::newRow("drive-relative") << QStringLiteral("C:outside.cpp");
    QTest::newRow("unc") << QStringLiteral("\\\\server\\share\\outside.cpp");
    QTest::newRow("parent-segment") << QStringLiteral("src/../outside.cpp");
    QTest::newRow("backslash-parent-segment") << QStringLiteral("src\\..\\outside.cpp");
    QTest::newRow("current-segment") << QStringLiteral("src/./outside.cpp");
    QTest::newRow("leading-parent") << QStringLiteral("../outside.cpp");
}

void GenerationServiceTest::rejectsUnsafePaths()
{
    QFETCH(QString, path);
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;

    QVERIFY(!parse(response(QStringLiteral("node-1"), {{path, QStringLiteral("x")}}),
                   root.path(),
                   &error)
                 .has_value());
    QVERIFY(!error.isEmpty());
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::rejectsUnsupportedExtension()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QString error;

    QVERIFY(!parse(response(QStringLiteral("node-1"),
                            {{QStringLiteral("src/widget.txt"), QStringLiteral("x")}}),
                   root.path(),
                   &error)
                 .has_value());
    QVERIFY(error.contains(QStringLiteral("extension"), Qt::CaseInsensitive));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::enforcesRawResponseLimitAtUtf8ByteBoundary()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload = response(QStringLiteral("node-1"));
    GenerationLimits limits;
    limits.maxResponseBytes = payload.size();
    QString error;

    QVERIFY2(parse(payload, root.path(), &error, limits).has_value(), qPrintable(error));
    limits.maxResponseBytes = payload.size() - 1;
    QVERIFY(!parse(payload, root.path(), &error, limits).has_value());
    QVERIFY(error.contains(QStringLiteral("response"), Qt::CaseInsensitive));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::enforcesSingleFileLimitAtUtf8ByteBoundary()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString content = QString::fromUtf8("\xF0\x9F\x99\x82");
    const QByteArray payload =
        response(QStringLiteral("node-1"), {{QStringLiteral("emoji.cpp"), content}});
    GenerationLimits limits;
    limits.maxFileBytes = content.toUtf8().size();
    QString error;

    QVERIFY2(parse(payload, root.path(), &error, limits).has_value(), qPrintable(error));
    --limits.maxFileBytes;
    QVERIFY(!parse(payload, root.path(), &error, limits).has_value());
    QVERIFY(error.contains(QStringLiteral("file"), Qt::CaseInsensitive));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::enforcesTotalContentLimitAtUtf8ByteBoundary()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString first = QString::fromUtf8("\xE4\xB8\xAD");
    const QString second = QString::fromUtf8("\xF0\x9F\x99\x82");
    const QByteArray payload = response(
        QStringLiteral("node-1"),
        {{QStringLiteral("first.cpp"), first}, {QStringLiteral("second.h"), second}});
    GenerationLimits limits;
    limits.maxTotalContentBytes = first.toUtf8().size() + second.toUtf8().size();
    QString error;

    QVERIFY2(parse(payload, root.path(), &error, limits).has_value(), qPrintable(error));
    --limits.maxTotalContentBytes;
    QVERIFY(!parse(payload, root.path(), &error, limits).has_value());
    QVERIFY(error.contains(QStringLiteral("total"), Qt::CaseInsensitive));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::enforcesFileCountLimit()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    QVector<QPair<QString, QString>> files;
    for (int index = 0; index < 33; ++index) {
        files.append({QStringLiteral("src/file_%1.cpp").arg(index), QStringLiteral("x")});
    }
    GenerationLimits limits;
    limits.maxFiles = 33;
    QString error;

    QVERIFY2(parse(response(QStringLiteral("node-1"), files), root.path(), &error, limits)
                 .has_value(),
             qPrintable(error));
    limits.maxFiles = 32;
    QVERIFY(!parse(response(QStringLiteral("node-1"), files), root.path(), &error, limits)
                 .has_value());
    QVERIFY(error.contains(QStringLiteral("files"), Qt::CaseInsensitive));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::rejectsDuplicateNormalizedPaths()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QByteArray payload = response(
        QStringLiteral("node-1"),
        {{QStringLiteral("src/widget.cpp"), QStringLiteral("one")},
         {QStringLiteral("src\\widget.cpp"), QStringLiteral("two")}});
    QString error;

    QVERIFY(!parse(payload, root.path(), &error).has_value());
    QVERIFY(error.contains(QStringLiteral("duplicate"), Qt::CaseInsensitive));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::requiresAbsoluteCandidateDirectory()
{
    QString error;

    QVERIFY(!parse(response(QStringLiteral("node-1")), QStringLiteral("relative/root"), &error)
                 .has_value());
    QVERIFY(error.contains(QStringLiteral("absolute"), Qt::CaseInsensitive));
}

void GenerationServiceTest::rejectsExistingDirectoryLinkOutsideCandidateRoot()
{
    QTemporaryDir root;
    QTemporaryDir outside;
    QVERIFY(root.isValid());
    QVERIFY(outside.isValid());
    const QString linkPath = QDir(root.path()).absoluteFilePath(QStringLiteral("linked"));

#ifdef Q_OS_WIN
    const int linkResult =
        QProcess::execute(QStringLiteral("cmd.exe"),
                          {QStringLiteral("/c"),
                           QStringLiteral("mklink"),
                           QStringLiteral("/J"),
                           QDir::toNativeSeparators(linkPath),
                           QDir::toNativeSeparators(outside.path())});
    if (linkResult != 0) {
        QSKIP("This Windows environment cannot create a temporary directory junction");
    }
#else
    if (!QFile::link(outside.path(), linkPath)) {
        QSKIP("This environment cannot create a temporary directory symlink");
    }
#endif
    DirectoryLinkGuard linkGuard(linkPath);
    QString error;

    QVERIFY(!parse(response(QStringLiteral("node-1"),
                            {{QStringLiteral("linked/generated.cpp"),
                              QStringLiteral("int generated;\n")}}),
                   root.path(),
                   &error)
                 .has_value());
    QVERIFY(error.contains(QStringLiteral("candidate"), Qt::CaseInsensitive));
    QVERIFY(QFileInfo(outside.path()).isDir());
}

void GenerationServiceTest::fakeClientCompletesSuccessfullyAsynchronously()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    FakeAiClient client;
    client.setSuccessfulResponse(response(QStringLiteral("node-1")));
    GenerationService service(&client);
    QSignalSpy successSpy(&service, &GenerationService::generationSucceeded);
    QSignalSpy failureSpy(&service, &GenerationService::generationFailed);

    const QUuid requestId = service.generate(QStringLiteral("prompt"),
                                             QStringLiteral("node-1"),
                                             root.path());

    QVERIFY(!requestId.isNull());
    QCOMPARE(client.requests().size(), 1);
    QCOMPARE(client.requests().constFirst().requestId, requestId);
    QCOMPARE(client.requests().constFirst().prompt, QStringLiteral("prompt"));
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(failureSpy.count(), 0);
    QVERIFY(successSpy.wait(1000));
    QCOMPARE(successSpy.count(), 1);
    QCOMPARE(failureSpy.count(), 0);
    QCOMPARE(successSpy.constFirst().at(0).toUuid(), requestId);
    const GenerationResult result = qvariant_cast<GenerationResult>(successSpy.constFirst().at(1));
    QCOMPARE(result.nodeId, QStringLiteral("node-1"));
    QCOMPARE(result.files.size(), 1);
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::propagatesClientFailureAsynchronously()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    FakeAiClient client;
    client.setFailure(QStringLiteral("offline"));
    GenerationService service(&client);
    QSignalSpy successSpy(&service, &GenerationService::generationSucceeded);
    QSignalSpy failureSpy(&service, &GenerationService::generationFailed);

    const QUuid requestId = service.generate(QStringLiteral("prompt"),
                                             QStringLiteral("node-1"),
                                             root.path());

    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(failureSpy.count(), 0);
    QVERIFY(failureSpy.wait(1000));
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(failureSpy.constFirst().at(0).toUuid(), requestId);
    QVERIFY(failureSpy.constFirst().at(1).toString().contains(QStringLiteral("offline")));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::correlatesConcurrentResponsesByRequestId()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    ControllableAiClient client;
    GenerationService service(&client);
    QSignalSpy successSpy(&service, &GenerationService::generationSucceeded);

    const QUuid first =
        service.generate(QStringLiteral("first"), QStringLiteral("node-1"), root.path());
    const QUuid second =
        service.generate(QStringLiteral("second"), QStringLiteral("node-2"), root.path());
    QCOMPARE(client.requests.size(), 2);

    emit client.responseReady(second, response(QStringLiteral("node-2")));
    emit client.responseReady(first, response(QStringLiteral("node-1")));

    QCOMPARE(successSpy.count(), 2);
    QCOMPARE(successSpy.at(0).at(0).toUuid(), second);
    QCOMPARE(qvariant_cast<GenerationResult>(successSpy.at(0).at(1)).nodeId,
             QStringLiteral("node-2"));
    QCOMPARE(successSpy.at(1).at(0).toUuid(), first);
    QCOMPARE(qvariant_cast<GenerationResult>(successSpy.at(1).at(1)).nodeId,
             QStringLiteral("node-1"));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::failsPendingRequestsWhenClientIsDestroyed()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    auto *client = new ControllableAiClient;
    GenerationService service(client);
    QSignalSpy successSpy(&service, &GenerationService::generationSucceeded);
    QSignalSpy failureSpy(&service, &GenerationService::generationFailed);
    const QUuid requestId =
        service.generate(QStringLiteral("prompt"), QStringLiteral("node-1"), root.path());

    delete client;

    QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 1, 200);
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(failureSpy.constFirst().at(0).toUuid(), requestId);
    QVERIFY(failureSpy.constFirst().at(1).toString().contains(QStringLiteral("unavailable"),
                                                              Qt::CaseInsensitive));
    QVERIFY(directoryIsEmpty(root.path()));
}

void GenerationServiceTest::openAiClientFailsOnceWhenInjectedManagerIsDestroyed()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "test-secret");
    auto *manager = new StubNetworkAccessManager;
    manager->enqueueResponse({{}, 200, QNetworkReply::NoError, false});
    auto *client = new OpenAiCompatibleClient(QUrl(QStringLiteral("https://example.invalid/v1/chat")),
                                              QStringLiteral("test-model"),
                                              manager);
    QSignalSpy successSpy(client, &IAiClient::responseReady);
    QSignalSpy failureSpy(client, &IAiClient::requestFailed);
    const QUuid requestId = QUuid::createUuid();
    client->generate({requestId, QStringLiteral("prompt"), 1024});
    QCOMPARE(manager->requests.size(), 1);

    delete manager;
    QTest::qWait(50);

    if (failureSpy.count() != 1) {
        QFAIL("destroying the injected network manager left the request pending");
    }
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(failureSpy.constFirst().at(0).toUuid(), requestId);
    delete client;
}

void GenerationServiceTest::openAiClientEnforcesAbsoluteDeadline()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "test-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({{}, 200, QNetworkReply::NoError, false});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/v1/chat")),
                                  QStringLiteral("test-model"),
                                  &manager,
                                  std::chrono::milliseconds(30));
    QSignalSpy successSpy(&client, &IAiClient::responseReady);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);
    const QUuid requestId = QUuid::createUuid();

    client.generate({requestId, QStringLiteral("prompt"), 1024});

    QVERIFY(failureSpy.wait(500));
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(failureSpy.constFirst().at(0).toUuid(), requestId);
    QVERIFY(failureSpy.constFirst().at(1).toString().contains(QStringLiteral("timed out"),
                                                              Qt::CaseInsensitive));
    QTest::qWait(50);
    QCOMPARE(failureSpy.count(), 1);
}

void GenerationServiceTest::openAiClientReadsApiKeyForEveryOfflineRequest()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "first-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({providerEnvelope(QByteArray("first-model-response"))});
    manager.enqueueResponse({providerEnvelope(QByteArray("second-model-response"))});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/v1/chat")),
                                  QStringLiteral("test-model"),
                                  &manager);
    QSignalSpy successSpy(&client, &IAiClient::responseReady);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);
    const QUuid firstId = QUuid::createUuid();
    const QUuid secondId = QUuid::createUuid();

    client.generate({firstId, QStringLiteral("first prompt"), 4096});
    qputenv("BLUEPRINT_AI_API_KEY", "second-secret");
    client.generate({secondId, QStringLiteral("second prompt"), 4096});

    QTRY_COMPARE_WITH_TIMEOUT(successSpy.count(), 2, 500);
    QCOMPARE(failureSpy.count(), 0);
    QCOMPARE(successSpy.at(0).at(0).toUuid(), firstId);
    QCOMPARE(successSpy.at(0).at(1).toByteArray(), QByteArray("first-model-response"));
    QCOMPARE(successSpy.at(1).at(0).toUuid(), secondId);
    QCOMPARE(successSpy.at(1).at(1).toByteArray(), QByteArray("second-model-response"));
    QCOMPARE(manager.requests.size(), 2);
    QCOMPARE(manager.requests.at(0).rawHeader("Authorization"), QByteArray("Bearer first-secret"));
    QCOMPARE(manager.requests.at(1).rawHeader("Authorization"), QByteArray("Bearer second-secret"));
    for (const QNetworkRequest &request : manager.requests) {
        QCOMPARE(request.url().scheme(), QStringLiteral("https"));
        QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
                 static_cast<int>(QNetworkRequest::ManualRedirectPolicy));
        QCOMPARE(request.maximumRedirectsAllowed(), 0);
        QVERIFY(request.transferTimeout() > 0);
    }

    QCOMPARE(manager.requestBodies.size(), 2);
    const QJsonObject firstBody = QJsonDocument::fromJson(manager.requestBodies.at(0)).object();
    QCOMPARE(firstBody.value(QStringLiteral("model")).toString(), QStringLiteral("test-model"));
    QCOMPARE(firstBody.value(QStringLiteral("stream")).toBool(), false);
    const QJsonObject firstMessage =
        firstBody.value(QStringLiteral("messages")).toArray().at(0).toObject();
    QCOMPARE(firstMessage.value(QStringLiteral("role")).toString(), QStringLiteral("user"));
    QCOMPARE(firstMessage.value(QStringLiteral("content")).toString(),
             QStringLiteral("first prompt"));
}

void GenerationServiceTest::openAiClientEnforcesWireLimitOffline()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "test-secret");
    const QByteArray envelope = providerEnvelope(QByteArray("model-response"));
    StubNetworkAccessManager manager;
    manager.enqueueResponse({envelope});
    manager.enqueueResponse({envelope});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/v1/chat")),
                                  QStringLiteral("test-model"),
                                  &manager);
    QSignalSpy successSpy(&client, &IAiClient::responseReady);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);
    const QUuid boundaryId = QUuid::createUuid();
    const QUuid oversizedId = QUuid::createUuid();

    client.generate({boundaryId, QStringLiteral("prompt"), envelope.size()});
    client.generate({oversizedId, QStringLiteral("prompt"), envelope.size() - 1});

    QTRY_COMPARE_WITH_TIMEOUT(successSpy.count() + failureSpy.count(), 2, 500);
    QCOMPARE(successSpy.count(), 1);
    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(successSpy.constFirst().at(0).toUuid(), boundaryId);
    QCOMPARE(failureSpy.constFirst().at(0).toUuid(), oversizedId);
    QVERIFY(failureSpy.constFirst().at(1).toString().contains(QStringLiteral("wire"),
                                                              Qt::CaseInsensitive));
    QTest::qWait(50);
    QCOMPARE(successSpy.count() + failureSpy.count(), 2);
}

void GenerationServiceTest::openAiClientRejectsInsecureEndpointOffline()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "test-secret");
    StubNetworkAccessManager manager;
    OpenAiCompatibleClient client(QUrl(QStringLiteral("http://example.invalid/v1/chat")),
                                  QStringLiteral("test-model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("prompt"), 1024});

    QCOMPARE(failureSpy.count(), 0);
    QVERIFY(failureSpy.wait(500));
    QCOMPARE(manager.requests.size(), 0);
    QVERIFY(failureSpy.constFirst().at(1).toString().contains(QStringLiteral("HTTPS")));
}

void GenerationServiceTest::openAiClientRejectsRedirectAndInvalidEnvelopeOffline()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "test-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({QByteArray("{}"), 302});
    manager.enqueueResponse({QByteArray(R"({"choices":[{"message":{"content":{}}}]})")});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/v1/chat")),
                                  QStringLiteral("test-model"),
                                  &manager);
    QSignalSpy successSpy(&client, &IAiClient::responseReady);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("prompt"), 4096});
    client.generate({QUuid::createUuid(), QStringLiteral("prompt"), 4096});

    QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 2, 500);
    QCOMPARE(successSpy.count(), 0);
    QCOMPARE(manager.requests.size(), 2);
    QVERIFY(failureSpy.at(0).at(1).toString().contains(QStringLiteral("302")));
    QVERIFY(failureSpy.at(1).at(1).toString().contains(QStringLiteral("content"),
                                                       Qt::CaseInsensitive));
}

void GenerationServiceTest::generationServiceDeletionDuringClientDestroyedFanoutIsSafe()
{
    const QString scenarioVariable =
        QStringLiteral("BLUEPRINT_TEST_SERVICE_DELETE_DURING_CLIENT_DESTROYED");
    if (qEnvironmentVariableIsSet(qPrintable(scenarioVariable))) {
        QTemporaryDir root;
        QVERIFY(root.isValid());
        auto *client = new ControllableAiClient;
        auto *service = new GenerationService(client);
        service->generate(QStringLiteral("first"), QStringLiteral("node-1"), root.path());
        service->generate(QStringLiteral("second"), QStringLiteral("node-2"), root.path());
        int failureCount = 0;
        int replacementFailureCount = 0;
        bool reusedDeletedAddress = false;
        GenerationService *replacement = nullptr;
        QVector<GenerationService *> otherReplacements;
        QObject::connect(service,
                         &GenerationService::generationFailed,
                         [&service,
                          &failureCount,
                          &replacementFailureCount,
                          &reusedDeletedAddress,
                          &replacement,
                          &otherReplacements](const QUuid &, const QString &) {
                             ++failureCount;
                             GenerationService *deletedService = service;
                             delete service;
                             service = nullptr;
                             for (int attempt = 0; attempt < 4096; ++attempt) {
                                 GenerationService *candidate = new GenerationService(nullptr);
                                 if (candidate == deletedService) {
                                     replacement = candidate;
                                     reusedDeletedAddress = true;
                                     break;
                                 }
                                 otherReplacements.append(candidate);
                             }
                             if (replacement == nullptr) {
                                 return;
                             }
                             QObject::connect(replacement,
                                              &GenerationService::generationFailed,
                                              [&replacementFailureCount](const QUuid &,
                                                                         const QString &) {
                                                  ++replacementFailureCount;
                                              });
                         });

        delete client;

        QCOMPARE(failureCount, 1);
        QVERIFY(service == nullptr);
        QVERIFY(reusedDeletedAddress);
        QCOMPARE(replacementFailureCount, 0);
        delete replacement;
        for (GenerationService *otherReplacement : otherReplacements) {
            delete otherReplacement;
        }
        return;
    }

    const IsolatedProcessResult result =
        runIsolatedTest(QStringLiteral("generationServiceDeletionDuringClientDestroyedFanoutIsSafe"),
                        scenarioVariable);
    QVERIFY2(result.started, result.diagnostics.constData());
    QVERIFY2(result.finished, result.diagnostics.constData());
    QVERIFY2(result.exitStatus == QProcess::NormalExit, result.diagnostics.constData());
    QVERIFY2(result.exitCode == 0, result.diagnostics.constData());
}

void GenerationServiceTest::openAiClientDeletionDuringAbortIsSafe()
{
    const QString scenarioVariable =
        QStringLiteral("BLUEPRINT_TEST_CLIENT_DELETE_DURING_ABORT");
    if (qEnvironmentVariableIsSet(qPrintable(scenarioVariable))) {
        EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "test-secret");
        StubNetworkAccessManager manager;
        manager.enqueueResponse({{}, 200, QNetworkReply::NoError, false});
        OpenAiCompatibleClient *client = nullptr;
        OpenAiCompatibleClient *replacement = nullptr;
        QVector<OpenAiCompatibleClient *> otherReplacements;
        int replacementFailureCount = 0;
        bool reusedDeletedAddress = false;
        manager.replyFinishedHandler = [&]() {
            OpenAiCompatibleClient *deletedClient = client;
            delete client;
            client = nullptr;
            for (int attempt = 0; attempt < 4096; ++attempt) {
                auto *candidate = new OpenAiCompatibleClient(
                    QUrl(QStringLiteral("https://example.invalid/v1/chat")),
                    QStringLiteral("test-model"),
                    &manager);
                if (candidate == deletedClient) {
                    replacement = candidate;
                    reusedDeletedAddress = true;
                    break;
                }
                otherReplacements.append(candidate);
            }
            if (replacement != nullptr) {
                QObject::connect(replacement,
                                 &IAiClient::requestFailed,
                                 [&replacementFailureCount](const QUuid &, const QString &) {
                                     ++replacementFailureCount;
                                 });
            }
        };
        client = new OpenAiCompatibleClient(
            QUrl(QStringLiteral("https://example.invalid/v1/chat")),
            QStringLiteral("test-model"),
            &manager,
            std::chrono::milliseconds(30));
        client->generate({QUuid::createUuid(), QStringLiteral("prompt"), 1024});

        QTRY_VERIFY_WITH_TIMEOUT(client == nullptr, 500);
        QVERIFY(reusedDeletedAddress);
        QCOMPARE(replacementFailureCount, 0);
        manager.replyFinishedHandler = {};
        delete replacement;
        for (OpenAiCompatibleClient *otherReplacement : otherReplacements) {
            delete otherReplacement;
        }
        return;
    }

    const IsolatedProcessResult result =
        runIsolatedTest(QStringLiteral("openAiClientDeletionDuringAbortIsSafe"),
                        scenarioVariable);
    QVERIFY2(result.started, result.diagnostics.constData());
    QVERIFY2(result.finished, result.diagnostics.constData());
    QVERIFY2(result.exitStatus == QProcess::NormalExit, result.diagnostics.constData());
    QVERIFY2(result.exitCode == 0, result.diagnostics.constData());
}

QTEST_GUILESS_MAIN(GenerationServiceTest)

#include "tst_generation_service.moc"
