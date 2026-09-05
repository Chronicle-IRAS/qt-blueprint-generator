#pragma once

#include "ai/ai_client.h"

#include <QByteArray>
#include <QHash>
#include <QMetaType>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QUuid>
#include <QVector>

#include <optional>

struct GeneratedFile
{
    QString relativePath;
    QString absoluteCandidatePath;
    QString content;
};

struct GenerationResult
{
    QString nodeId;
    QString summary;
    QVector<GeneratedFile> files;
};

struct GenerationLimits
{
    qint64 maxResponseBytes = 1024 * 1024;
    qint64 maxFileBytes = 256 * 1024;
    qint64 maxTotalContentBytes = 1024 * 1024;
    qsizetype maxFiles = 32;
    qint64 maxWireResponseBytes = 2 * 1024 * 1024;
};

Q_DECLARE_METATYPE(GeneratedFile)
Q_DECLARE_METATYPE(GenerationResult)

class GenerationService final : public QObject
{
    Q_OBJECT

public:
    explicit GenerationService(IAiClient *client, QObject *parent = nullptr);

    QUuid generate(const QString &prompt,
                   const QString &expectedNodeId,
                   const QString &absoluteCandidateDirectory,
                   const GenerationLimits &limits = {});

    // Read-only validation against a trusted, caller-selected candidate directory.
    // Does not create files. Recheck filesystem boundaries immediately before any later write.
    static std::optional<GenerationResult> parseAndValidate(
        const QByteArray &modelResponse,
        const QString &expectedNodeId,
        const QString &absoluteCandidateDirectory,
        const GenerationLimits &limits = {},
        QString *errorMessage = nullptr);

signals:
    void generationSucceeded(const QUuid &requestId, const GenerationResult &result);
    void generationFailed(const QUuid &requestId, const QString &errorMessage);

private:
    struct RequestContext
    {
        QString expectedNodeId;
        QString absoluteCandidateDirectory;
        GenerationLimits limits;
    };

    void handleResponse(const QUuid &requestId, const QByteArray &modelResponse);
    void handleFailure(const QUuid &requestId, const QString &errorMessage);

    QPointer<IAiClient> m_client;
    QHash<QUuid, RequestContext> m_pending;
};
