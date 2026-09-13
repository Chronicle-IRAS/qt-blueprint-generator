#include "ai/ai_provider_settings.h"

#include <QList>
#include <QSettings>
#include <QStringList>
#include <QtGlobal>
#include <QVariant>

namespace {

const QString ProviderIdKey = QStringLiteral("ai/providerId");
const QString EndpointKey = QStringLiteral("ai/endpoint");
const QString ModelKey = QStringLiteral("ai/model");
const QString CredentialSourceKey = QStringLiteral("ai/credentialSource");
const QString LegacyApiKey = QStringLiteral("ai/apiKey");
const QString EnvironmentCredentialSource = QStringLiteral("environment");

struct SettingSnapshot
{
    QString key;
    bool existed = false;
    QVariant value;
};

bool fail(QString *errorMessage, const QString &message)
{
    if (errorMessage != nullptr) {
        *errorMessage = message;
    }
    return false;
}

QList<SettingSnapshot> snapshotModifiedSettings(const QSettings &settings)
{
    const QStringList keys{
        ProviderIdKey,
        EndpointKey,
        ModelKey,
        CredentialSourceKey,
        LegacyApiKey,
    };
    QList<SettingSnapshot> snapshot;
    snapshot.reserve(keys.size());
    for (const QString &key : keys) {
        snapshot.append({key, settings.contains(key), settings.value(key)});
    }
    return snapshot;
}

void restoreModifiedSettings(QSettings &settings, const QList<SettingSnapshot> &snapshot)
{
    for (const SettingSnapshot &entry : snapshot) {
        if (entry.existed) {
            settings.setValue(entry.key, entry.value);
        } else {
            settings.remove(entry.key);
        }
    }
    settings.sync();
}

} // namespace

bool AiProviderSettings::validate(QString *errorMessage) const
{
    if (errorMessage != nullptr) {
        errorMessage->clear();
    }
    if (providerId != QStringLiteral("openai-compatible")) {
        return fail(errorMessage, QStringLiteral("Unsupported AI provider"));
    }
    if (credentialSource != AiCredentialSource::Environment) {
        return fail(errorMessage, QStringLiteral("Unsupported AI credential source"));
    }
    if (!endpoint.isValid() || endpoint.scheme().compare(QStringLiteral("https"),
                                                          Qt::CaseInsensitive)
                                   != 0
        || endpoint.host().isEmpty() || !endpoint.userInfo().isEmpty()
        || endpoint.authority(QUrl::FullyEncoded).contains(QLatin1Char('@'))
        || endpoint.hasQuery() || endpoint.hasFragment()) {
        return fail(errorMessage,
                    QStringLiteral("AI endpoint must be an HTTPS URL with a host and no user info, query, or fragment"));
    }
    if (model.isEmpty() || model != model.trimmed()) {
        return fail(errorMessage,
                    QStringLiteral("AI model must not be empty or contain leading or trailing whitespace"));
    }
    return true;
}

bool AiProviderSettings::credentialAvailable() const
{
    return credentialSource == AiCredentialSource::Environment
           && !qEnvironmentVariableIsEmpty("BLUEPRINT_AI_API_KEY");
}

bool AiProviderSettings::save(QSettings &settings, QString *errorMessage) const
{
    if (settings.status() != QSettings::NoError) {
        return fail(errorMessage, QStringLiteral("AI settings could not be saved"));
    }
    if (!validate(errorMessage)) {
        return false;
    }

    QString storedCredentialSource;
    switch (credentialSource) {
    case AiCredentialSource::Unknown:
        break;
    case AiCredentialSource::Environment:
        storedCredentialSource = EnvironmentCredentialSource;
        break;
    }
    if (storedCredentialSource.isEmpty()) {
        return fail(errorMessage, QStringLiteral("Unsupported AI credential source"));
    }

    const QList<SettingSnapshot> before = snapshotModifiedSettings(settings);
    settings.remove(LegacyApiKey);
    settings.setValue(ProviderIdKey, providerId);
    settings.setValue(EndpointKey, endpoint.toString(QUrl::FullyEncoded));
    settings.setValue(ModelKey, model);
    settings.setValue(CredentialSourceKey, storedCredentialSource);
    settings.sync();
    if (settings.status() != QSettings::NoError) {
        restoreModifiedSettings(settings, before);
        return fail(errorMessage, QStringLiteral("AI settings could not be saved"));
    }
    return true;
}

AiProviderSettings AiProviderSettings::load(const QSettings &settings)
{
    AiProviderSettings result;
    result.providerId = settings.value(ProviderIdKey, result.providerId).toString();
    result.endpoint = QUrl(settings.value(EndpointKey,
                                          result.endpoint.toString(QUrl::FullyEncoded))
                               .toString());
    result.model = settings.value(ModelKey, result.model).toString();
    const QString credentialSource =
        settings.value(CredentialSourceKey, EnvironmentCredentialSource).toString();
    if (credentialSource == EnvironmentCredentialSource) {
        result.credentialSource = AiCredentialSource::Environment;
    } else {
        result.credentialSource = AiCredentialSource::Unknown;
    }
    return result;
}
