#include "ai/ai_provider_settings.h"

#include <QDataStream>
#include <QFile>
#include <QIODevice>
#include <QMap>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

namespace {

QString settingsPath(const QTemporaryDir &directory)
{
    return directory.filePath(QStringLiteral("settings.ini"));
}

QMap<QString, QVariant> settingsSnapshot(const QSettings &settings)
{
    QMap<QString, QVariant> snapshot;
    for (const QString &key : settings.allKeys()) {
        snapshot.insert(key, settings.value(key));
    }
    return snapshot;
}

struct ControlledSettingsBackend
{
    QSettings::SettingsMap persisted;
    int writeAttempts = 0;
    bool rejectNextWrite = false;
};

ControlledSettingsBackend &controlledSettingsBackend()
{
    static ControlledSettingsBackend backend;
    return backend;
}

bool readControlledSettings(QIODevice &device, QSettings::SettingsMap &map)
{
    if (device.size() == 0) {
        map.clear();
        return true;
    }
    QDataStream stream(&device);
    stream >> map;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }
    controlledSettingsBackend().persisted = map;
    return true;
}

bool writeControlledSettings(QIODevice &device, const QSettings::SettingsMap &map)
{
    ControlledSettingsBackend &backend = controlledSettingsBackend();
    ++backend.writeAttempts;
    if (backend.rejectNextWrite) {
        backend.rejectNextWrite = false;
        return false;
    }
    QDataStream stream(&device);
    stream << map;
    if (stream.status() != QDataStream::Ok) {
        return false;
    }
    backend.persisted = map;
    return true;
}

QSettings::Format failingSettingsFormat()
{
    static const QSettings::Format format =
        QSettings::registerFormat(QStringLiteral("task1-failing-settings"),
                                  readControlledSettings,
                                  writeControlledSettings);
    return format;
}

} // namespace

class AiProviderSettingsTest : public QObject
{
    Q_OBJECT

private slots:
    void cleanup();
    void loadsDefaultsWithoutWritingSettings();
    void validatesEndpointAndModel();
    void rejectsInvalidEndpoints_data();
    void rejectsInvalidEndpoints();
    void rejectsInvalidModels_data();
    void rejectsInvalidModels();
    void rejectsUnsupportedProvider();
    void loadsUnknownCredentialSourceAsInvalid();
    void savesOnlyWhitelistedNonSecretSettings();
    void preservesSettingsOwnedByOtherComponents();
    void invalidSettingsAreNotSaved_data();
    void invalidSettingsAreNotSaved();
    void handlesBackendFailureAndStickyStatus();
    void reportsEnvironmentCredentialAvailability();
};

void AiProviderSettingsTest::cleanup()
{
    qunsetenv("BLUEPRINT_AI_API_KEY");
}

void AiProviderSettingsTest::loadsDefaultsWithoutWritingSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(settingsPath(directory), QSettings::IniFormat);

    const AiProviderSettings loaded = AiProviderSettings::load(settings);

    QCOMPARE(loaded.providerId, QStringLiteral("openai-compatible"));
    QCOMPARE(loaded.endpoint,
             QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
    QCOMPARE(loaded.model, QStringLiteral("deepseek-flash"));
    QCOMPARE(loaded.credentialSource, AiCredentialSource::Environment);
    QVERIFY(settings.allKeys().isEmpty());
    QVERIFY(!QFile::exists(settingsPath(directory)));
}

void AiProviderSettingsTest::validatesEndpointAndModel()
{
    AiProviderSettings settings;
    settings.endpoint = QUrl(QStringLiteral("https://provider.example/v1/chat/completions"));
    settings.model = QStringLiteral("model-name");
    QString error = QStringLiteral("stale");

    QVERIFY2(settings.validate(&error), qPrintable(error));
    QVERIFY(error.isEmpty());
}

void AiProviderSettingsTest::rejectsInvalidEndpoints_data()
{
    QTest::addColumn<QUrl>("endpoint");

    QTest::newRow("non-https") << QUrl(QStringLiteral("http://provider.example/chat"));
    QTest::newRow("missing-host") << QUrl(QStringLiteral("https:///chat/completions"));
    QTest::newRow("userinfo") << QUrl(QStringLiteral("https://user@provider.example/chat"));
    QTest::newRow("empty-userinfo") << QUrl(QStringLiteral("https://@provider.example/chat"));
    QTest::newRow("query") << QUrl(QStringLiteral("https://provider.example/chat?mode=test"));
    QTest::newRow("empty-query") << QUrl(QStringLiteral("https://provider.example/chat?"));
    QTest::newRow("fragment") << QUrl(QStringLiteral("https://provider.example/chat#section"));
    QTest::newRow("empty-fragment") << QUrl(QStringLiteral("https://provider.example/chat#"));
}

void AiProviderSettingsTest::rejectsInvalidEndpoints()
{
    QFETCH(QUrl, endpoint);
    AiProviderSettings settings;
    settings.endpoint = endpoint;
    QString error;

    QVERIFY(!settings.validate(&error));
    QVERIFY(!error.isEmpty());
}

void AiProviderSettingsTest::rejectsInvalidModels_data()
{
    QTest::addColumn<QString>("model");

    QTest::newRow("blank") << QStringLiteral("  \t\n");
    QTest::newRow("leading-space") << QStringLiteral(" model-name");
    QTest::newRow("trailing-space") << QStringLiteral("model-name ");
    QTest::newRow("leading-tab") << QStringLiteral("\tmodel-name");
    QTest::newRow("trailing-newline") << QStringLiteral("model-name\n");
}

void AiProviderSettingsTest::rejectsInvalidModels()
{
    QFETCH(QString, model);
    AiProviderSettings settings;
    settings.model = model;
    QString error;

    QVERIFY(!settings.validate(&error));
    QVERIFY(error.contains(QStringLiteral("model"), Qt::CaseInsensitive));
}

void AiProviderSettingsTest::rejectsUnsupportedProvider()
{
    AiProviderSettings settings;
    settings.providerId = QStringLiteral("unsupported-provider");
    QString error;

    QVERIFY(!settings.validate(&error));
    QVERIFY(error.contains(QStringLiteral("provider"), Qt::CaseInsensitive));
}

void AiProviderSettingsTest::loadsUnknownCredentialSourceAsInvalid()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings stored(settingsPath(directory), QSettings::IniFormat);
    stored.setValue(QStringLiteral("ai/credentialSource"), QStringLiteral("future-source"));
    stored.sync();
    qputenv("BLUEPRINT_AI_API_KEY", "present-only-for-this-test-process");

    AiProviderSettings loaded = AiProviderSettings::load(stored);
    QCOMPARE(loaded.credentialSource, AiCredentialSource::Unknown);
    QVERIFY(!loaded.credentialAvailable());
    QString error;
    QVERIFY(!loaded.validate(&error));
    QVERIFY(error.contains(QStringLiteral("credential"), Qt::CaseInsensitive));
    const QMap<QString, QVariant> before = settingsSnapshot(stored);
    QVERIFY(!loaded.save(stored, &error));
    QCOMPARE(settingsSnapshot(stored), before);
}

void AiProviderSettingsTest::savesOnlyWhitelistedNonSecretSettings()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const QString path = settingsPath(directory);
    QSettings settings(path, QSettings::IniFormat);
    settings.setValue(QStringLiteral("ai/apiKey"), QStringLiteral("legacy-sensitive-value"));
    settings.setValue(QStringLiteral("unrelated/keep"), QStringLiteral("kept"));
    qputenv("BLUEPRINT_AI_API_KEY", "environment-sensitive-value");

    AiProviderSettings value;
    value.endpoint = QUrl(QStringLiteral("https://provider.example/v1/chat/completions"));
    value.model = QStringLiteral("model-name");
    QString error;
    QVERIFY2(value.save(settings, &error), qPrintable(error));
    settings.sync();

    QCOMPARE(settings.value(QStringLiteral("unrelated/keep")).toString(), QStringLiteral("kept"));
    QCOMPARE(settings.value(QStringLiteral("ai/providerId")).toString(),
             QStringLiteral("openai-compatible"));
    QCOMPARE(settings.value(QStringLiteral("ai/endpoint")).toString(),
             QStringLiteral("https://provider.example/v1/chat/completions"));
    QCOMPARE(settings.value(QStringLiteral("ai/model")).toString(), QStringLiteral("model-name"));
    QCOMPARE(settings.value(QStringLiteral("ai/credentialSource")).toString(),
             QStringLiteral("environment"));
    QVERIFY(!settings.contains(QStringLiteral("ai/apiKey")));
    settings.beginGroup(QStringLiteral("ai"));
    QCOMPARE(settings.childKeys(),
             QStringList({QStringLiteral("credentialSource"),
                          QStringLiteral("endpoint"),
                          QStringLiteral("model"),
                          QStringLiteral("providerId")}));
    settings.endGroup();

    QFile file(path);
    QVERIFY2(file.exists(), qPrintable(path));
    QVERIFY2(file.open(QIODevice::ReadOnly), qPrintable(file.errorString()));
    const QByteArray bytes = file.readAll();
    QCOMPARE(file.error(), QFileDevice::NoError);
    QVERIFY(!bytes.contains("legacy-sensitive-value"));
    QVERIFY(!bytes.contains("environment-sensitive-value"));

    const AiProviderSettings loaded = AiProviderSettings::load(settings);
    QCOMPARE(loaded.providerId, value.providerId);
    QCOMPARE(loaded.endpoint, value.endpoint);
    QCOMPARE(loaded.model, value.model);
    QCOMPARE(loaded.credentialSource, value.credentialSource);
}

void AiProviderSettingsTest::preservesSettingsOwnedByOtherComponents()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(settingsPath(directory), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ai/ownedByOtherComponent"), QStringLiteral("keep-ai"));
    settings.setValue(QStringLiteral("unrelated/keep"), QStringLiteral("keep-unrelated"));
    AiProviderSettings value;
    QString error;

    QVERIFY2(value.save(settings, &error), qPrintable(error));

    QCOMPARE(settings.value(QStringLiteral("ai/ownedByOtherComponent")).toString(),
             QStringLiteral("keep-ai"));
    QCOMPARE(settings.value(QStringLiteral("unrelated/keep")).toString(),
             QStringLiteral("keep-unrelated"));
}

void AiProviderSettingsTest::invalidSettingsAreNotSaved_data()
{
    QTest::addColumn<QUrl>("endpoint");
    QTest::addColumn<QString>("model");

    QTest::newRow("invalid-endpoint")
        << QUrl(QStringLiteral("http://provider.example/chat")) << QStringLiteral("model-name");
    QTest::newRow("blank-model")
        << QUrl(QStringLiteral("https://provider.example/chat")) << QStringLiteral("  \t" );
}

void AiProviderSettingsTest::invalidSettingsAreNotSaved()
{
    QFETCH(QUrl, endpoint);
    QFETCH(QString, model);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings(settingsPath(directory), QSettings::IniFormat);
    settings.setValue(QStringLiteral("ai/providerId"), QStringLiteral("existing-provider"));
    settings.setValue(QStringLiteral("ai/apiKey"), QStringLiteral("existing-sensitive-value"));
    settings.setValue(QStringLiteral("ai/ownedByOtherComponent"), QStringLiteral("keep-ai"));
    settings.setValue(QStringLiteral("unrelated/keep"), QStringLiteral("keep-unrelated"));
    settings.sync();
    const QMap<QString, QVariant> before = settingsSnapshot(settings);
    AiProviderSettings value;
    value.endpoint = endpoint;
    value.model = model;
    QString error;

    QVERIFY(!value.save(settings, &error));
    QVERIFY(!error.isEmpty());
    QCOMPARE(settingsSnapshot(settings), before);
}

void AiProviderSettingsTest::handlesBackendFailureAndStickyStatus()
{
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    ControlledSettingsBackend &backend = controlledSettingsBackend();
    backend = {};
    const QSettings::Format format = failingSettingsFormat();
    QVERIFY(format != QSettings::InvalidFormat);
    const QString path =
        directory.filePath(QStringLiteral("settings.task1-failing-settings"));
    AiProviderSettings replacement;
    replacement.endpoint = QUrl(QStringLiteral("https://provider.example/chat"));
    replacement.model = QStringLiteral("replacement-model");
    QSettings::SettingsMap before;

    {
        QSettings settings(path, format);
        settings.setFallbacksEnabled(false);
        settings.setValue(QStringLiteral("ai/providerId"), QStringLiteral("previous-provider"));
        settings.setValue(QStringLiteral("ai/apiKey"),
                          QStringLiteral("previous-sensitive-value"));
        settings.setValue(QStringLiteral("ai/ownedByOtherComponent"),
                          QStringLiteral("keep-ai"));
        settings.setValue(QStringLiteral("unrelated/keep"), QStringLiteral("keep-unrelated"));
        settings.sync();
        QCOMPARE(settings.status(), QSettings::NoError);
        before = settingsSnapshot(settings);
        QCOMPARE(backend.persisted, before);
        const int writesBeforeSave = backend.writeAttempts;
        backend.rejectNextWrite = true;
        QString error;

        QVERIFY(!replacement.save(settings, &error));
        QCOMPARE(error, QStringLiteral("AI settings could not be saved"));
        QCOMPARE(settingsSnapshot(settings), before);
        QCOMPARE(backend.writeAttempts, writesBeforeSave + 2);
        QCOMPARE(backend.persisted, before);
        QCOMPARE(settings.status(), QSettings::AccessError);

        const int writesBeforeRetry = backend.writeAttempts;
        const QMap<QString, QVariant> beforeRetry = settingsSnapshot(settings);
        error = QStringLiteral("stale error");

        QVERIFY(!replacement.save(settings, &error));
        QCOMPARE(error, QStringLiteral("AI settings could not be saved"));
        QCOMPARE(backend.writeAttempts, writesBeforeRetry);
        QCOMPARE(settingsSnapshot(settings), beforeRetry);
        QCOMPARE(backend.persisted, before);
    }

    QSettings recreated(path, format);
    recreated.setFallbacksEnabled(false);
    QCOMPARE(recreated.status(), QSettings::NoError);
    const int writesBeforeRecreatedSave = backend.writeAttempts;
    QString error;

    QVERIFY2(replacement.save(recreated, &error), qPrintable(error));
    QCOMPARE(backend.writeAttempts, writesBeforeRecreatedSave + 1);
    QCOMPARE(recreated.value(QStringLiteral("ai/providerId")).toString(),
             QStringLiteral("openai-compatible"));
    QCOMPARE(recreated.value(QStringLiteral("ai/endpoint")).toString(),
             QStringLiteral("https://provider.example/chat"));
    QCOMPARE(recreated.value(QStringLiteral("ai/model")).toString(),
             QStringLiteral("replacement-model"));
    QCOMPARE(recreated.value(QStringLiteral("ai/credentialSource")).toString(),
             QStringLiteral("environment"));
    QVERIFY(!recreated.contains(QStringLiteral("ai/apiKey")));
    QCOMPARE(recreated.value(QStringLiteral("ai/ownedByOtherComponent")).toString(),
             QStringLiteral("keep-ai"));
    QCOMPARE(recreated.value(QStringLiteral("unrelated/keep")).toString(),
             QStringLiteral("keep-unrelated"));
    QCOMPARE(backend.persisted, QSettings::SettingsMap(settingsSnapshot(recreated)));
}

void AiProviderSettingsTest::reportsEnvironmentCredentialAvailability()
{
    AiProviderSettings settings;

    qunsetenv("BLUEPRINT_AI_API_KEY");
    QVERIFY(!settings.credentialAvailable());

    qputenv("BLUEPRINT_AI_API_KEY", "present-only-for-this-test-process");
    QVERIFY(settings.credentialAvailable());

    qputenv("BLUEPRINT_AI_API_KEY", "");
    QVERIFY(!settings.credentialAvailable());
}

QTEST_GUILESS_MAIN(AiProviderSettingsTest)

#include "tst_ai_provider_settings.moc"
