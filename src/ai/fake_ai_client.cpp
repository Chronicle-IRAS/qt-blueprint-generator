#include "ai/fake_ai_client.h"

#include <QTimer>

#include <utility>

FakeAiClient::FakeAiClient(QObject *parent)
    : IAiClient(parent)
{
}

void FakeAiClient::setSuccessfulResponse(const QByteArray &modelResponse)
{
    m_resultType = ResultType::Success;
    m_modelResponse = modelResponse;
    m_errorMessage.clear();
}

void FakeAiClient::setFailure(const QString &errorMessage)
{
    m_resultType = ResultType::Failure;
    m_modelResponse.clear();
    m_errorMessage = errorMessage;
}

void FakeAiClient::generate(const AiRequest &request)
{
    m_requests.append(request);

    const ResultType resultType = m_resultType;
    const QByteArray modelResponse = m_modelResponse;
    const QString errorMessage = m_errorMessage;
    const QUuid requestId = request.requestId;
    QTimer::singleShot(0, this, [this, resultType, modelResponse, errorMessage, requestId]() {
        if (resultType == ResultType::Success) {
            emit responseReady(requestId, modelResponse);
            return;
        }
        emit requestFailed(requestId, errorMessage);
    });
}

const QVector<AiRequest> &FakeAiClient::requests() const
{
    return m_requests;
}
