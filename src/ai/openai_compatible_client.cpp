#include "ai/openai_compatible_client.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTimer>
#include <QVariant>

#include <limits>
#include <utility>

namespace {

constexpr auto DefaultNetworkTimeout = std::chrono::seconds(30);

AiClientError clientError(AiErrorKind kind, int httpStatus = 0)
{
    QString safeMessage;
    switch (kind) {
    case AiErrorKind::InvalidConfiguration:
        safeMessage = QStringLiteral("AI configuration is invalid");
        break;
    case AiErrorKind::CredentialUnavailable:
        safeMessage = QStringLiteral("AI API credential is unavailable");
        break;
    case AiErrorKind::Network:
        safeMessage = QStringLiteral("AI network request failed");
        break;
    case AiErrorKind::Timeout:
        safeMessage = QStringLiteral("AI request timed out");
        break;
    case AiErrorKind::Authentication:
        safeMessage = QStringLiteral("AI provider authentication failed");
        break;
    case AiErrorKind::PaymentRequired:
        safeMessage = QStringLiteral("AI provider payment is required");
        break;
    case AiErrorKind::EndpointNotFound:
        safeMessage = QStringLiteral("AI endpoint was not found");
        break;
    case AiErrorKind::ModelNotFound:
        safeMessage = QStringLiteral("AI model was not found");
        break;
    case AiErrorKind::RateLimited:
        safeMessage = QStringLiteral("AI provider rate limit was reached");
        break;
    case AiErrorKind::InvalidResponse:
        safeMessage = QStringLiteral("AI provider returned an invalid response");
        break;
    case AiErrorKind::ProviderUnavailable:
        safeMessage = QStringLiteral("AI provider is unavailable");
        break;
    case AiErrorKind::Unknown:
        safeMessage = QStringLiteral("AI request failed");
        break;
    }
    return {kind, httpStatus, safeMessage};
}

bool parseProviderResponse(const QByteArray &body, QByteArray &modelResponse)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }

    const QJsonValue choicesValue = document.object().value(QStringLiteral("choices"));
    if (!choicesValue.isArray()) {
        return false;
    }
    const QJsonArray choices = choicesValue.toArray();
    if (choices.isEmpty() || !choices.at(0).isObject()) {
        return false;
    }

    const QJsonValue messageValue =
        choices.at(0).toObject().value(QStringLiteral("message"));
    if (!messageValue.isObject()) {
        return false;
    }

    const QJsonValue contentValue =
        messageValue.toObject().value(QStringLiteral("content"));
    if (!contentValue.isString()) {
        return false;
    }

    modelResponse = contentValue.toString().toUtf8();
    return true;
}

bool hasTrustedMissingModelCode(const QByteArray &body)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return false;
    }
    const QJsonValue errorValue = document.object().value(QStringLiteral("error"));
    if (!errorValue.isObject()) {
        return false;
    }
    const QJsonValue codeValue = errorValue.toObject().value(QStringLiteral("code"));
    if (!codeValue.isString()) {
        return false;
    }
    const QString code = codeValue.toString().toLower();
    return code == QStringLiteral("model_not_found")
           || code == QStringLiteral("invalid_model")
           || code == QStringLiteral("model_not_exist");
}

AiClientError httpError(int status, const QByteArray &body)
{
    if (status == 401 || status == 403) {
        return clientError(AiErrorKind::Authentication, status);
    }
    if (status == 402) {
        return clientError(AiErrorKind::PaymentRequired, status);
    }
    if (status == 429) {
        return clientError(AiErrorKind::RateLimited, status);
    }
    if (status == 408) {
        return clientError(AiErrorKind::Timeout, status);
    }
    if ((status == 400 || status == 404) && hasTrustedMissingModelCode(body)) {
        return clientError(AiErrorKind::ModelNotFound, status);
    }
    if (status == 404) {
        return clientError(AiErrorKind::EndpointNotFound, status);
    }
    if (status >= 500 && status < 600) {
        return clientError(AiErrorKind::ProviderUnavailable, status);
    }
    return clientError(AiErrorKind::Unknown, status);
}

bool validEndpoint(const QUrl &endpoint)
{
    return endpoint.isValid() && endpoint.scheme().compare(QStringLiteral("https"),
                                                            Qt::CaseInsensitive)
                                       == 0
           && !endpoint.host().isEmpty() && endpoint.userInfo().isEmpty()
           && !endpoint.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
           && !endpoint.hasQuery() && !endpoint.hasFragment();
}

} // namespace

OpenAiCompatibleClient::OpenAiCompatibleClient(const QUrl &endpoint,
                                               const QString &model,
                                               QObject *parent)
    : IAiClient(parent)
    , m_endpoint(endpoint)
    , m_model(model)
    , m_networkAccessManager(new QNetworkAccessManager(this))
    , m_requestTimeout(DefaultNetworkTimeout)
{
}

OpenAiCompatibleClient::OpenAiCompatibleClient(const QUrl &endpoint,
                                               const QString &model,
                                               QNetworkAccessManager *networkAccessManager,
                                               QObject *parent)
    : OpenAiCompatibleClient(endpoint,
                             model,
                             networkAccessManager,
                             DefaultNetworkTimeout,
                             parent)
{
}

OpenAiCompatibleClient::OpenAiCompatibleClient(const QUrl &endpoint,
                                               const QString &model,
                                               QNetworkAccessManager *networkAccessManager,
                                               std::chrono::milliseconds requestTimeout,
                                               QObject *parent)
    : IAiClient(parent)
    , m_endpoint(endpoint)
    , m_model(model)
    , m_networkAccessManager(networkAccessManager)
    , m_requestTimeout(requestTimeout)
{
}

OpenAiCompatibleClient::~OpenAiCompatibleClient()
{
    const QList<QNetworkReply *> replies = m_pending.keys();
    for (QNetworkReply *reply : replies) {
        PendingResponse pending;
        takePending(reply, pending);
        disconnect(reply, nullptr, this, nullptr);
        reply->abort();
        reply->deleteLater();
    }
}

void OpenAiCompatibleClient::generate(const AiRequest &request)
{
    if (request.requestId.isNull()) {
        failLater(request.requestId, clientError(AiErrorKind::InvalidConfiguration));
        return;
    }
    if (request.maxWireResponseBytes < 0) {
        failLater(request.requestId, clientError(AiErrorKind::InvalidConfiguration));
        return;
    }
    if (m_requestTimeout.count() <= 0) {
        failLater(request.requestId, clientError(AiErrorKind::InvalidConfiguration));
        return;
    }
    if (!validEndpoint(m_endpoint)) {
        failLater(request.requestId, clientError(AiErrorKind::InvalidConfiguration));
        return;
    }
    if (m_model.isEmpty() || m_model != m_model.trimmed()) {
        failLater(request.requestId, clientError(AiErrorKind::InvalidConfiguration));
        return;
    }
    if (m_networkAccessManager.isNull()) {
        failLater(request.requestId, clientError(AiErrorKind::Network));
        return;
    }
    if (request.maxTokens.has_value() && *request.maxTokens <= 0) {
        failLater(request.requestId, clientError(AiErrorKind::InvalidConfiguration));
        return;
    }

    const QByteArray apiKey = qgetenv("BLUEPRINT_AI_API_KEY");
    if (apiKey.isEmpty()) {
        failLater(request.requestId, clientError(AiErrorKind::CredentialUnavailable));
        return;
    }

    QNetworkRequest networkRequest(m_endpoint);
    networkRequest.setHeader(QNetworkRequest::ContentTypeHeader,
                             QStringLiteral("application/json"));
    networkRequest.setRawHeader("Authorization", QByteArray("Bearer ") + apiKey);
    networkRequest.setAttribute(QNetworkRequest::RedirectPolicyAttribute,
                                QNetworkRequest::ManualRedirectPolicy);
    networkRequest.setMaximumRedirectsAllowed(0);
    networkRequest.setTransferTimeout(m_requestTimeout);

    QJsonObject requestBody{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{
             {QStringLiteral("role"), QStringLiteral("user")},
             {QStringLiteral("content"), request.prompt},
         }}},
        {QStringLiteral("stream"), false},
    };
    if (request.maxTokens.has_value()) {
        requestBody.insert(QStringLiteral("max_tokens"), *request.maxTokens);
    }
    if (request.disableThinking.has_value()) {
        requestBody.insert(QStringLiteral("thinking"),
                           QJsonObject{{QStringLiteral("type"),
                                        *request.disableThinking
                                            ? QStringLiteral("disabled")
                                            : QStringLiteral("enabled")}});
    }
    QNetworkReply *reply =
        m_networkAccessManager->post(networkRequest,
                                     QJsonDocument(requestBody).toJson(QJsonDocument::Compact));

    PendingResponse pending;
    pending.requestId = request.requestId;
    pending.maxBytes = request.maxWireResponseBytes;
    pending.deadlineTimer = new QTimer(this);
    pending.deadlineTimer->setSingleShot(true);
    m_pending.insert(reply, std::move(pending));
    if (request.maxWireResponseBytes < std::numeric_limits<qint64>::max()) {
        reply->setReadBufferSize(request.maxWireResponseBytes + 1);
    }

    connect(reply, &QNetworkReply::readyRead, this, [this, reply]() { readAvailable(reply); });
    connect(reply, &QNetworkReply::finished, this, [this, reply]() { finish(reply); });
    connect(reply, &QObject::destroyed, this, [this, reply]() {
        failPending(reply, clientError(AiErrorKind::Network), false);
    });
    QTimer *deadlineTimer = m_pending.value(reply).deadlineTimer;
    connect(deadlineTimer, &QTimer::timeout, this, [this, reply]() {
        failPending(reply, clientError(AiErrorKind::Timeout), true);
    });
    deadlineTimer->start(m_requestTimeout);
}

void OpenAiCompatibleClient::failLater(const QUuid &requestId, const AiClientError &error)
{
    QTimer::singleShot(0, this, [this, requestId, error]() {
        emit requestFailed(requestId, error);
    });
}

bool OpenAiCompatibleClient::appendWithinLimit(PendingResponse &pending,
                                               const QByteArray &chunk) const
{
    const qint64 chunkSize = chunk.size();
    const qint64 currentSize = pending.body.size();
    if (chunkSize > pending.maxBytes || currentSize > pending.maxBytes - chunkSize) {
        return false;
    }
    pending.body.append(chunk);
    return true;
}

bool OpenAiCompatibleClient::takePending(QNetworkReply *reply, PendingResponse &pending)
{
    auto pendingIt = m_pending.find(reply);
    if (pendingIt == m_pending.end()) {
        return false;
    }
    pending = std::move(pendingIt.value());
    m_pending.erase(pendingIt);
    if (pending.deadlineTimer != nullptr) {
        pending.deadlineTimer->stop();
        pending.deadlineTimer->deleteLater();
        pending.deadlineTimer = nullptr;
    }
    return true;
}

void OpenAiCompatibleClient::failPending(QNetworkReply *reply,
                                         const AiClientError &error,
                                         bool abortReply)
{
    PendingResponse pending;
    if (!takePending(reply, pending)) {
        return;
    }
    if (abortReply) {
        const QPointer<OpenAiCompatibleClient> clientGuard(this);
        reply->abort();
        if (clientGuard.isNull()) {
            return;
        }
    }
    emit requestFailed(pending.requestId, error);
}

void OpenAiCompatibleClient::readAvailable(QNetworkReply *reply)
{
    auto pendingIt = m_pending.find(reply);
    if (pendingIt == m_pending.end()) {
        reply->readAll();
        return;
    }

    if (appendWithinLimit(pendingIt.value(), reply->readAll())) {
        return;
    }

    failPending(reply,
                clientError(AiErrorKind::InvalidResponse),
                true);
}

void OpenAiCompatibleClient::finish(QNetworkReply *reply)
{
    reply->deleteLater();
    auto pendingIt = m_pending.find(reply);
    if (pendingIt == m_pending.end()) {
        return;
    }

    if (!appendWithinLimit(pendingIt.value(), reply->readAll())) {
        failPending(reply,
                    clientError(AiErrorKind::InvalidResponse),
                    false);
        return;
    }

    PendingResponse pending;
    takePending(reply, pending);

    const QVariant statusValue =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (reply->error() == QNetworkReply::TimeoutError
        || (reply->error() == QNetworkReply::OperationCanceledError
            && !statusValue.isValid())) {
        emit requestFailed(pending.requestId, clientError(AiErrorKind::Timeout));
        return;
    }
    if (reply->error() != QNetworkReply::NoError) {
        if (statusValue.isValid()) {
            const int status = statusValue.toInt();
            if (status < 200 || status >= 300) {
                emit requestFailed(pending.requestId, httpError(status, pending.body));
                return;
            }
        }
        emit requestFailed(pending.requestId, clientError(AiErrorKind::Network));
        return;
    }

    if (!statusValue.isValid()) {
        emit requestFailed(pending.requestId, clientError(AiErrorKind::InvalidResponse));
        return;
    }
    const int status = statusValue.toInt();
    if (status < 200 || status >= 300) {
        emit requestFailed(pending.requestId, httpError(status, pending.body));
        return;
    }

    QByteArray modelResponse;
    if (!parseProviderResponse(pending.body, modelResponse)) {
        emit requestFailed(pending.requestId,
                           clientError(AiErrorKind::InvalidResponse, status));
        return;
    }

    emit responseReady(pending.requestId, modelResponse);
}
