#pragma once

#include "ai/ai_client.h"

#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QUuid>

enum class AiConnectionState
{
    Testing,
    Success,
    Failure,
};

Q_DECLARE_METATYPE(AiConnectionState)

class AiConnectionTester final : public QObject
{
    Q_OBJECT

public:
    explicit AiConnectionTester(IAiClient *client, QObject *parent = nullptr);

    void testConnection();

signals:
    void stateChanged(AiConnectionState state, const AiClientError &error);

private:
    void handleResponse(const QUuid &requestId);
    void handleFailure(const QUuid &requestId, const AiClientError &error);
    void finish(AiConnectionState state, const AiClientError &error);

    QPointer<IAiClient> m_client;
    QUuid m_activeRequestId;
};
