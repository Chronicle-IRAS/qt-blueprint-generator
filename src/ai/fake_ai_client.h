#pragma once

#include "ai/ai_client.h"

#include <QVector>

class FakeAiClient final : public IAiClient
{
public:
    explicit FakeAiClient(QObject *parent = nullptr);

    void setSuccessfulResponse(const QByteArray &modelResponse);
    void setFailure(const AiClientError &error);

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
    AiClientError m_error;
    QVector<AiRequest> m_requests;
};
