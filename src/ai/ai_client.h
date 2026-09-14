#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>

#include <optional>

enum class AiErrorKind
{
    InvalidConfiguration,
    CredentialUnavailable,
    Network,
    Timeout,
    Authentication,
    PaymentRequired,
    EndpointNotFound,
    ModelNotFound,
    RateLimited,
    InvalidResponse,
    ProviderUnavailable,
    Unknown,
};

struct AiClientError
{
    AiErrorKind kind = AiErrorKind::Unknown;
    int httpStatus = 0;
    QString safeMessage;
};

struct AiRequest
{
    QUuid requestId;
    QString prompt;
    qint64 maxWireResponseBytes = 2 * 1024 * 1024;
    std::optional<int> maxTokens;
    std::optional<bool> disableThinking;
};

Q_DECLARE_METATYPE(AiErrorKind)
Q_DECLARE_METATYPE(AiClientError)
Q_DECLARE_METATYPE(AiRequest)

class IAiClient : public QObject
{
    Q_OBJECT

public:
    explicit IAiClient(QObject *parent = nullptr)
        : QObject(parent)
    {
    }

    ~IAiClient() override = default;

    virtual void generate(const AiRequest &request) = 0;

signals:
    void responseReady(const QUuid &requestId, const QByteArray &modelResponse);
    void requestFailed(const QUuid &requestId, const AiClientError &error);
};
