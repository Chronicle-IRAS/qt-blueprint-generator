#pragma once

#include <QByteArray>
#include <QMetaType>
#include <QObject>
#include <QString>
#include <QUuid>

struct AiRequest
{
    QUuid requestId;
    QString prompt;
    qint64 maxWireResponseBytes = 2 * 1024 * 1024;
};

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
    void requestFailed(const QUuid &requestId, const QString &errorMessage);
};
