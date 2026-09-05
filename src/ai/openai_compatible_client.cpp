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

QString providerResponseError(const QByteArray &body, QByteArray &modelResponse)
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(body, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        return QStringLiteral("AI endpoint returned an invalid JSON response");
    }

    const QJsonValue choicesValue = document.object().value(QStringLiteral("choices"));
    if (!choicesValue.isArray()) {
        return QStringLiteral("AI endpoint response is missing choices");
    }
    const QJsonArray choices = choicesValue.toArray();
    if (choices.isEmpty() || !choices.at(0).isObject()) {
        return QStringLiteral("AI endpoint response has no usable choice");
    }

    const QJsonValue messageValue =
        choices.at(0).toObject().value(QStringLiteral("message"));
    if (!messageValue.isObject()) {
        return QStringLiteral("AI endpoint response choice is missing a message");
    }

    const QJsonValue contentValue =
        messageValue.toObject().value(QStringLiteral("content"));
    if (!contentValue.isString()) {
        return QStringLiteral("AI endpoint response message content must be a string");
    }

    modelResponse = contentValue.toString().toUtf8();
    return {};
}

bool validEndpoint(const QUrl &endpoint)
{
    return endpoint.isValid() && endpoint.scheme().compare(QStringLiteral("https"),
                                                            Qt::CaseInsensitive)
                                       == 0
           && !endpoint.host().isEmpty() && endpoint.userInfo().isEmpty();
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
        failLater(request.requestId, QStringLiteral("AI request ID must not be null"));
        return;
    }
    if (request.maxWireResponseBytes < 0) {
        failLater(request.requestId,
                  QStringLiteral("AI response size limit must not be negative"));
        return;
    }
    if (m_requestTimeout.count() <= 0) {
        failLater(request.requestId,
                  QStringLiteral("AI request timeout must be positive"));
        return;
    }
    if (!validEndpoint(m_endpoint)) {
        failLater(request.requestId,
                  QStringLiteral("AI endpoint must be a valid HTTPS URL without user info"));
        return;
    }
    if (m_model.trimmed().isEmpty()) {
        failLater(request.requestId, QStringLiteral("AI model name must not be empty"));
        return;
    }
    if (m_networkAccessManager.isNull()) {
        failLater(request.requestId, QStringLiteral("AI network manager is unavailable"));
        return;
    }

    const QByteArray apiKey = qgetenv("BLUEPRINT_AI_API_KEY");
    if (apiKey.isEmpty()) {
        failLater(request.requestId,
                  QStringLiteral("BLUEPRINT_AI_API_KEY is not configured"));
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

    const QJsonObject requestBody{
        {QStringLiteral("model"), m_model},
        {QStringLiteral("messages"),
         QJsonArray{QJsonObject{
             {QStringLiteral("role"), QStringLiteral("user")},
             {QStringLiteral("content"), request.prompt},
         }}},
        {QStringLiteral("stream"), false},
    };
    QNetworkReply *reply =
        m_networkAccessManager->post(networkRequest,
                                     QJsonDocument(requestBody).toJson(QJsonDocument::Compact));
    if (reply == nullptr) {
        failLater(request.requestId, QStringLiteral("AI request could not be started"));
        return;
    }

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
        failPending(reply, QStringLiteral("AI network reply became unavailable"), false);
    });
    QTimer *deadlineTimer = m_pending.value(reply).deadlineTimer;
    connect(deadlineTimer, &QTimer::timeout, this, [this, reply]() {
        failPending(reply, QStringLiteral("AI network request timed out"), true);
    });
    deadlineTimer->start(m_requestTimeout);
}

void OpenAiCompatibleClient::failLater(const QUuid &requestId, const QString &errorMessage)
{
    QTimer::singleShot(0, this, [this, requestId, errorMessage]() {
        emit requestFailed(requestId, errorMessage);
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
                                         const QString &errorMessage,
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
    emit requestFailed(pending.requestId, errorMessage);
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
                QStringLiteral("AI endpoint response exceeds the configured wire size limit"),
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
                    QStringLiteral("AI endpoint response exceeds the configured wire size limit"),
                    false);
        return;
    }

    PendingResponse pending;
    takePending(reply, pending);

    if (reply->error() != QNetworkReply::NoError) {
        emit requestFailed(pending.requestId,
                           QStringLiteral("AI network request failed: %1").arg(reply->errorString()));
        return;
    }

    const QVariant statusValue =
        reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
    if (!statusValue.isValid()) {
        emit requestFailed(pending.requestId,
                           QStringLiteral("AI endpoint returned no HTTP status"));
        return;
    }
    const int status = statusValue.toInt();
    if (status < 200 || status >= 300) {
        emit requestFailed(pending.requestId,
                           QStringLiteral("AI endpoint returned HTTP status %1").arg(status));
        return;
    }

    QByteArray modelResponse;
    const QString errorMessage = providerResponseError(pending.body, modelResponse);
    if (!errorMessage.isEmpty()) {
        emit requestFailed(pending.requestId, errorMessage);
        return;
    }

    emit responseReady(pending.requestId, modelResponse);
}
