#pragma once

#include "ai/ai_client.h"

#include <QVector>

class FakeAiClient final : public IAiClient
{
public:
    explicit FakeAiClient(QObject *parent = nullptr);

    void setSuccessfulResponse(const QByteArray &modelResponse);
    void setFailure(const QString &errorMessage);

    void generate(const AiRequest &request) override;

    const QVector<AiRequest> &requests() const;

private:
    enum class ResultType
    {
        Success,
        Failure,
    };

    ResultType m_resultType = ResultType::Success;
    QByteArray m_modelResponse;
    QString m_errorMessage;
    QVector<AiRequest> m_requests;
};
