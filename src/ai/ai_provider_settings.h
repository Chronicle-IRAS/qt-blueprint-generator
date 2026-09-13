#pragma once

#include <QString>
#include <QUrl>

class QSettings;

enum class AiCredentialSource
{
    Unknown,
    Environment,
};

struct AiProviderSettings
{
    QString providerId = QStringLiteral("openai-compatible");
    QUrl endpoint = QUrl(QStringLiteral("https://api.deepseek.com/chat/completions"));
    QString model = QStringLiteral("deepseek-flash");
    AiCredentialSource credentialSource = AiCredentialSource::Environment;

    bool validate(QString *errorMessage = nullptr) const;
    bool credentialAvailable() const;
    // QSettings status is sticky after a backend error. Destroy and recreate the
    // QSettings object before retrying save() after such an error.
    bool save(QSettings &settings, QString *errorMessage = nullptr) const;

    static AiProviderSettings load(const QSettings &settings);
};
