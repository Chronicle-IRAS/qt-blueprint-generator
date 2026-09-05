#pragma once

#include "ai/ai_client.h"

#include <QHash>
#include <QPointer>
#include <QUrl>

#include <chrono>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;

class OpenAiCompatibleClient final : public IAiClient
{
public:
    explicit OpenAiCompatibleClient(const QUrl &endpoint,
                                    const QString &model,
                                    QObject *parent = nullptr);
    OpenAiCompatibleClient(const QUrl &endpoint,
                           const QString &model,
                           QNetworkAccessManager *networkAccessManager,
                           QObject *parent = nullptr);
    OpenAiCompatibleClient(const QUrl &endpoint,
                           const QString &model,
                           QNetworkAccessManager *networkAccessManager,
                           std::chrono::milliseconds requestTimeout,
                           QObject *parent = nullptr);
    ~OpenAiCompatibleClient() override;

    void generate(const AiRequest &request) override;

private:
    struct PendingResponse
    {
        QUuid requestId;
        qint64 maxBytes = 0;
        QByteArray body;
        QTimer *deadlineTimer = nullptr;
    };

    void failLater(const QUuid &requestId, const QString &errorMessage);
    void failPending(QNetworkReply *reply,
                     const QString &errorMessage,
                     bool abortReply);
    void readAvailable(QNetworkReply *reply);
    void finish(QNetworkReply *reply);
    bool takePending(QNetworkReply *reply, PendingResponse &pending);
    bool appendWithinLimit(PendingResponse &pending, const QByteArray &chunk) const;

    QUrl m_endpoint;
    QString m_model;
    QPointer<QNetworkAccessManager> m_networkAccessManager;
    std::chrono::milliseconds m_requestTimeout;
    QHash<QNetworkReply *, PendingResponse> m_pending;
};
