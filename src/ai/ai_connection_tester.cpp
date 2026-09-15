#include "ai/ai_connection_tester.h"

#include <QTimer>

namespace {

AiClientError unavailableError()
{
    return {AiErrorKind::Network,
            0,
            QStringLiteral("AI client is unavailable")};
}

} // namespace

AiConnectionTester::AiConnectionTester(IAiClient *client, QObject *parent)
    : QObject(parent)
    , m_client(client)
{
    if (client == nullptr) {
        return;
    }
    connect(client,
            &IAiClient::responseReady,
            this,
            [this](const QUuid &requestId, const QByteArray &) {
                handleResponse(requestId);
            });
    connect(client,
            &IAiClient::requestFailed,
            this,
            &AiConnectionTester::handleFailure);
    connect(client, &QObject::destroyed, this, [this]() {
        if (!m_activeRequestId.isNull()) {
            finish(AiConnectionState::Failure, unavailableError());
        }
    });
}

void AiConnectionTester::testConnection()
{
    if (!m_activeRequestId.isNull()) {
        return;
    }

    m_activeRequestId = QUuid::createUuid();
    const QPointer<AiConnectionTester> guard(this);
    emit stateChanged(AiConnectionState::Testing, {});
    if (guard.isNull()) {
        return;
    }
    if (m_client.isNull()) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_activeRequestId.isNull()) {
                finish(AiConnectionState::Failure, unavailableError());
            }
        });
        return;
    }

    AiRequest request;
    request.requestId = m_activeRequestId;
    request.prompt = QStringLiteral("Reply with OK.");
    request.maxWireResponseBytes = 64 * 1024;
    request.maxTokens = 8;
    request.disableThinking = true;
    m_client->generate(request);
}

void AiConnectionTester::handleResponse(const QUuid &requestId)
{
    if (requestId != m_activeRequestId) {
        return;
    }
    finish(AiConnectionState::Success, {});
}

void AiConnectionTester::handleFailure(const QUuid &requestId,
                                       const AiClientError &error)
{
    if (requestId != m_activeRequestId) {
        return;
    }
    finish(AiConnectionState::Failure, error);
}

void AiConnectionTester::finish(AiConnectionState state, const AiClientError &error)
{
    if (m_activeRequestId.isNull()) {
        return;
    }
    m_activeRequestId = QUuid{};
    emit stateChanged(state, error);
}
