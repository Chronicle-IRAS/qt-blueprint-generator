#include "ai/openai_compatible_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPointer>
#include <QSignalSpy>
#include <QTimer>
#include <QVector>
#include <QtTest>

#include <chrono>
#include <cstring>
#include <functional>

namespace {

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

struct StubNetworkResponse
{
    QByteArray body;
    int statusCode = 200;
    QNetworkReply::NetworkError networkError = QNetworkReply::NoError;
    bool finishes = true;
    bool hasStatus = true;
    QVector<QByteArray> chunks;
};

class StubNetworkReply final : public QNetworkReply
{
public:
    StubNetworkReply(QNetworkAccessManager::Operation operation,
                     const QNetworkRequest &request,
                     const StubNetworkResponse &response,
                     QObject *parent)
        : QNetworkReply(parent)
        , m_networkError(response.networkError)
        , m_chunks(response.chunks.isEmpty() ? QVector<QByteArray>{response.body}
                                             : response.chunks)
    {
        setOperation(operation);
        setRequest(request);
        setUrl(request.url());
        if (response.hasStatus) {
            setAttribute(QNetworkRequest::HttpStatusCodeAttribute, response.statusCode);
        }
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
        setError(QNetworkReply::OperationCanceledError, QStringLiteral("stub abort secret"));
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
            setError(m_networkError, QStringLiteral("network secret from reply"));
        }
        for (const QByteArray &chunk : std::as_const(m_chunks)) {
            if (!chunk.isEmpty()) {
                m_body.append(chunk);
                emit readyRead();
            }
            if (m_completed) {
                return;
            }
        }
        m_completed = true;
        setFinished(true);
        emit finished();
    }

    QByteArray m_body;
    QNetworkReply::NetworkError m_networkError = QNetworkReply::NoError;
    QVector<QByteArray> m_chunks;
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
                                                                       false,
                                                                       true}
                                                 : m_responses.takeFirst();
        return new StubNetworkReply(operation, request, response, this);
    }

private:
    QVector<StubNetworkResponse> m_responses;
};

QByteArray providerEnvelope(const QByteArray &content)
{
    return QJsonDocument(QJsonObject{
                             {QStringLiteral("choices"),
                              QJsonArray{QJsonObject{
                                  {QStringLiteral("message"),
                                   QJsonObject{{QStringLiteral("content"),
                                                QString::fromUtf8(content)}}},
                              }}},
                         })
        .toJson(QJsonDocument::Compact);
}

AiClientError errorAt(const QSignalSpy &spy, int index = 0)
{
    return qvariant_cast<AiClientError>(spy.at(index).at(1));
}

void verifySanitized(const AiClientError &error)
{
    QVERIFY(!error.safeMessage.isEmpty());
    QVERIFY(!error.safeMessage.contains(QStringLiteral("request-prompt-secret")));
    QVERIFY(!error.safeMessage.contains(QStringLiteral("api-key-secret")));
    QVERIFY(!error.safeMessage.contains(QStringLiteral("provider-body-secret")));
    QVERIFY(!error.safeMessage.contains(QStringLiteral("network secret")));
}

} // namespace

class OpenAiCompatibleClientTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void rejectsInvalidConfigurationWithoutNetwork_data();
    void rejectsInvalidConfigurationWithoutNetwork();
    void rejectsInvalidProbeTokenLimitWithoutNetwork();
    void reportsUnavailableNetworkManagerWithoutStartingRequest();
    void reportsMissingCredentialWithoutNetwork();
    void classifiesNetworkAndTimeoutFailures_data();
    void classifiesNetworkAndTimeoutFailures();
    void classifiesHttpFailures_data();
    void classifiesHttpFailures();
    void recognizesTrustedModelNotFoundCode_data();
    void recognizesTrustedModelNotFoundCode();
    void authenticationStatusCannotBeOverriddenByProviderCode();
    void rejectsInvalidSuccessfulResponses_data();
    void rejectsInvalidSuccessfulResponses();
    void sendsOptionalProbeFieldsOnlyWhenRequested();
    void reportsMissingHttpStatusAsInvalidResponse();
    void enforcesWireLimitWithSanitizedFailure();
    void handlesChunkedBoundaryAndCumulativeOverflowOnce();
    void failsOnceWhenInjectedManagerIsDestroyed();
};

void OpenAiCompatibleClientTest::initTestCase()
{
    qRegisterMetaType<AiClientError>();
}

void OpenAiCompatibleClientTest::rejectsInvalidConfigurationWithoutNetwork_data()
{
    QTest::addColumn<QUrl>("endpoint");
    QTest::addColumn<QString>("model");
    QTest::addColumn<qint64>("wireLimit");
    QTest::addColumn<int>("timeoutMs");
    QTest::addColumn<bool>("nullRequestId");

    QTest::newRow("insecure-endpoint") << QUrl(QStringLiteral("http://example.invalid/chat"))
                                        << QStringLiteral("model") << qint64(1024) << 100 << false;
    QTest::newRow("endpoint-user-info") << QUrl(QStringLiteral("https://user@example.invalid/chat"))
                                        << QStringLiteral("model") << qint64(1024) << 100 << false;
    QTest::newRow("endpoint-query") << QUrl(QStringLiteral("https://example.invalid/chat?secret=1"))
                                    << QStringLiteral("model") << qint64(1024) << 100 << false;
    QTest::newRow("empty-model") << QUrl(QStringLiteral("https://example.invalid/chat"))
                                 << QString() << qint64(1024) << 100 << false;
    QTest::newRow("negative-wire-limit") << QUrl(QStringLiteral("https://example.invalid/chat"))
                                         << QStringLiteral("model") << qint64(-1) << 100 << false;
    QTest::newRow("invalid-timeout") << QUrl(QStringLiteral("https://example.invalid/chat"))
                                     << QStringLiteral("model") << qint64(1024) << 0 << false;
    QTest::newRow("null-request-id") << QUrl(QStringLiteral("https://example.invalid/chat"))
                                     << QStringLiteral("model") << qint64(1024) << 100 << true;
}

void OpenAiCompatibleClientTest::rejectsInvalidConfigurationWithoutNetwork()
{
    QFETCH(QUrl, endpoint);
    QFETCH(QString, model);
    QFETCH(qint64, wireLimit);
    QFETCH(int, timeoutMs);
    QFETCH(bool, nullRequestId);
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    OpenAiCompatibleClient client(endpoint,
                                  model,
                                  &manager,
                                  std::chrono::milliseconds(timeoutMs));
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({nullRequestId ? QUuid{} : QUuid::createUuid(),
                     QStringLiteral("request-prompt-secret"),
                     wireLimit});

    QVERIFY(failureSpy.wait(500));
    QCOMPARE(manager.requests.size(), 0);
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::InvalidConfiguration);
    QCOMPARE(error.httpStatus, 0);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::rejectsInvalidProbeTokenLimitWithoutNetwork()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);
    AiRequest request{QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024};
    request.maxTokens = 0;

    client.generate(request);

    QVERIFY(failureSpy.wait(500));
    QCOMPARE(manager.requests.size(), 0);
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::InvalidConfiguration);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::reportsUnavailableNetworkManagerWithoutStartingRequest()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  static_cast<QNetworkAccessManager *>(nullptr));
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::Network);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::reportsMissingCredentialWithoutNetwork()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", QByteArray{});
    StubNetworkAccessManager manager;
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024});

    QVERIFY(failureSpy.wait(500));
    QCOMPARE(manager.requests.size(), 0);
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::CredentialUnavailable);
    QCOMPARE(error.httpStatus, 0);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::classifiesNetworkAndTimeoutFailures_data()
{
    QTest::addColumn<QNetworkReply::NetworkError>("networkError");
    QTest::addColumn<AiErrorKind>("expectedKind");

    QTest::newRow("network") << QNetworkReply::HostNotFoundError << AiErrorKind::Network;
    QTest::newRow("qt-timeout") << QNetworkReply::TimeoutError << AiErrorKind::Timeout;
    QTest::newRow("transfer-timeout-cancel") << QNetworkReply::OperationCanceledError
                                              << AiErrorKind::Timeout;
}

void OpenAiCompatibleClientTest::classifiesNetworkAndTimeoutFailures()
{
    QFETCH(QNetworkReply::NetworkError, networkError);
    QFETCH(AiErrorKind, expectedKind);
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({QByteArray("provider-body-secret"), 0, networkError, true, false});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, expectedKind);
    QCOMPARE(error.httpStatus, 0);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::classifiesHttpFailures_data()
{
    QTest::addColumn<int>("status");
    QTest::addColumn<AiErrorKind>("expectedKind");

    QTest::newRow("unauthorized") << 401 << AiErrorKind::Authentication;
    QTest::newRow("forbidden") << 403 << AiErrorKind::Authentication;
    QTest::newRow("payment") << 402 << AiErrorKind::PaymentRequired;
    QTest::newRow("endpoint") << 404 << AiErrorKind::EndpointNotFound;
    QTest::newRow("rate-limited") << 429 << AiErrorKind::RateLimited;
    QTest::newRow("request-timeout") << 408 << AiErrorKind::Timeout;
    QTest::newRow("server-error") << 500 << AiErrorKind::ProviderUnavailable;
    QTest::newRow("provider-unavailable") << 503 << AiErrorKind::ProviderUnavailable;
    QTest::newRow("other") << 400 << AiErrorKind::Unknown;
}

void OpenAiCompatibleClientTest::classifiesHttpFailures()
{
    QFETCH(int, status);
    QFETCH(AiErrorKind, expectedKind);
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({QByteArray("provider-body-secret"), status});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, expectedKind);
    QCOMPARE(error.httpStatus, status);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::recognizesTrustedModelNotFoundCode_data()
{
    QTest::addColumn<int>("status");

    QTest::newRow("bad-request") << 400;
    QTest::newRow("not-found") << 404;
}

void OpenAiCompatibleClientTest::recognizesTrustedModelNotFoundCode()
{
    QFETCH(int, status);
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({QByteArray(R"({"error":{"code":"model_not_found","message":"provider-body-secret"}})"),
                             status});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::ModelNotFound);
    QCOMPARE(error.httpStatus, status);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::authenticationStatusCannotBeOverriddenByProviderCode()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({QByteArray(R"({"error":{"code":"model_not_found","message":"provider-body-secret"}})"),
                             401});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::Authentication);
    QCOMPARE(error.httpStatus, 401);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::rejectsInvalidSuccessfulResponses_data()
{
    QTest::addColumn<QByteArray>("body");

    QTest::newRow("invalid-json") << QByteArray("provider-body-secret");
    QTest::newRow("missing-choices") << QByteArray(R"({"provider-body-secret":true})");
    QTest::newRow("invalid-content")
        << QByteArray(R"({"choices":[{"message":{"content":{"provider-body-secret":true}}}]})");
}

void OpenAiCompatibleClientTest::rejectsInvalidSuccessfulResponses()
{
    QFETCH(QByteArray, body);
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({body, 200});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 4096});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::InvalidResponse);
    QCOMPARE(error.httpStatus, 200);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::sendsOptionalProbeFieldsOnlyWhenRequested()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({providerEnvelope(QByteArray("normal"))});
    manager.enqueueResponse({providerEnvelope(QByteArray("probe"))});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy successSpy(&client, &IAiClient::responseReady);
    AiRequest normal{QUuid::createUuid(), QStringLiteral("normal prompt"), 4096};
    AiRequest probe{QUuid::createUuid(), QStringLiteral("probe prompt"), 4096};
    probe.maxTokens = 8;
    probe.disableThinking = true;

    client.generate(normal);
    client.generate(probe);

    QTRY_COMPARE_WITH_TIMEOUT(successSpy.count(), 2, 500);
    QCOMPARE(manager.requestBodies.size(), 2);
    const QJsonObject normalBody = QJsonDocument::fromJson(manager.requestBodies.at(0)).object();
    QVERIFY(!normalBody.contains(QStringLiteral("max_tokens")));
    QVERIFY(!normalBody.contains(QStringLiteral("thinking")));
    const QJsonObject probeBody = QJsonDocument::fromJson(manager.requestBodies.at(1)).object();
    QCOMPARE(probeBody.value(QStringLiteral("max_tokens")).toInt(), 8);
    QCOMPARE(probeBody.value(QStringLiteral("thinking")).toObject().value(QStringLiteral("type")).toString(),
             QStringLiteral("disabled"));
}

void OpenAiCompatibleClientTest::reportsMissingHttpStatusAsInvalidResponse()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({providerEnvelope(QByteArray("provider-body-secret")),
                             0,
                             QNetworkReply::NoError,
                             true,
                             false});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 4096});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::InvalidResponse);
    QCOMPARE(error.httpStatus, 0);
    verifySanitized(error);
}

void OpenAiCompatibleClientTest::enforcesWireLimitWithSanitizedFailure()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    StubNetworkAccessManager manager;
    manager.enqueueResponse({providerEnvelope(QByteArray("provider-body-secret"))});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 4});

    QVERIFY(failureSpy.wait(500));
    const AiClientError error = errorAt(failureSpy);
    QCOMPARE(error.kind, AiErrorKind::InvalidResponse);
    verifySanitized(error);
    QTest::qWait(25);
    QCOMPARE(failureSpy.count(), 1);
}

void OpenAiCompatibleClientTest::handlesChunkedBoundaryAndCumulativeOverflowOnce()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    const QByteArray envelope = providerEnvelope(QByteArray("provider-body-secret"));
    const qsizetype firstBreak = envelope.size() / 3;
    const qsizetype secondBreak = (envelope.size() * 2) / 3;
    const QVector<QByteArray> chunks{
        envelope.first(firstBreak),
        envelope.sliced(firstBreak, secondBreak - firstBreak),
        envelope.sliced(secondBreak),
    };
    StubNetworkAccessManager manager;
    manager.enqueueResponse({{}, 200, QNetworkReply::NoError, true, true, chunks});
    manager.enqueueResponse({{}, 200, QNetworkReply::NoError, true, true, chunks});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  &manager);
    QSignalSpy successSpy(&client, &IAiClient::responseReady);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);
    const QUuid boundaryId = QUuid::createUuid();
    const QUuid oversizedId = QUuid::createUuid();

    client.generate({boundaryId, QStringLiteral("request-prompt-secret"), envelope.size()});
    client.generate({oversizedId,
                     QStringLiteral("request-prompt-secret"),
                     envelope.size() - 1});

    QTRY_COMPARE_WITH_TIMEOUT(successSpy.count() + failureSpy.count(), 2, 500);
    QCOMPARE(successSpy.count(), 1);
    QCOMPARE(failureSpy.count(), 1);
    QCOMPARE(successSpy.constFirst().at(0).toUuid(), boundaryId);
    QCOMPARE(failureSpy.constFirst().at(0).toUuid(), oversizedId);
    QCOMPARE(errorAt(failureSpy).kind, AiErrorKind::InvalidResponse);
    verifySanitized(errorAt(failureSpy));
    QTest::qWait(25);
    QCOMPARE(successSpy.count() + failureSpy.count(), 2);
}

void OpenAiCompatibleClientTest::failsOnceWhenInjectedManagerIsDestroyed()
{
    EnvironmentVariableGuard apiKey("BLUEPRINT_AI_API_KEY", "api-key-secret");
    auto *manager = new StubNetworkAccessManager;
    manager->enqueueResponse({{}, 200, QNetworkReply::NoError, false, true});
    OpenAiCompatibleClient client(QUrl(QStringLiteral("https://example.invalid/chat")),
                                  QStringLiteral("model"),
                                  manager);
    QSignalSpy failureSpy(&client, &IAiClient::requestFailed);

    client.generate({QUuid::createUuid(), QStringLiteral("request-prompt-secret"), 1024});
    delete manager;

    QTRY_COMPARE_WITH_TIMEOUT(failureSpy.count(), 1, 500);
    QCOMPARE(errorAt(failureSpy).kind, AiErrorKind::Network);
    verifySanitized(errorAt(failureSpy));
    QTest::qWait(25);
    QCOMPARE(failureSpy.count(), 1);
}

QTEST_GUILESS_MAIN(OpenAiCompatibleClientTest)

#include "tst_openai_compatible_client.moc"
