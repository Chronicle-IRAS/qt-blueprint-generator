#include "ai/ai_provider_settings.h"
#include "ai/fake_ai_client.h"
#include "app/ai_settings_dialog.h"
#include "app/main_window.h"

#include <QtTest/QtTest>

#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMap>
#include <QMenu>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>

namespace {

template<typename T>
T *requiredChild(QObject *parent, const char *objectName)
{
    T *child = parent->findChild<T *>(QString::fromLatin1(objectName));
    if (!child) {
        QTest::qFail(objectName, __FILE__, __LINE__);
    }
    return child;
}

struct FactoryProbe
{
    int calls = 0;
    AiProviderSettings settings;
    QPointer<FakeAiClient> client;
    AiClientError result;
    bool succeed = true;

    AiSettingsDialog::ClientFactory factory()
    {
        return [this](const AiProviderSettings &value, QObject *parent) {
            ++calls;
            settings = value;
            auto *fake = new FakeAiClient(parent);
            client = fake;
            if (succeed) {
                fake->setSuccessfulResponse(QByteArrayLiteral("ok"));
            } else {
                fake->setFailure(result);
            }
            return fake;
        };
    }
};

QStringList visibleTexts(const QDialog &dialog)
{
    QStringList texts;
    for (const QLabel *label : dialog.findChildren<QLabel *>()) {
        texts.append(label->text());
    }
    for (const QPushButton *button : dialog.findChildren<QPushButton *>()) {
        texts.append(button->text());
    }
    for (const QComboBox *combo : dialog.findChildren<QComboBox *>()) {
        for (int index = 0; index < combo->count(); ++index) {
            texts.append(combo->itemText(index));
        }
    }
    return texts;
}

QMap<QString, QVariant> settingsSnapshot()
{
    QSettings settings;
    QMap<QString, QVariant> snapshot;
    for (const QString &key : settings.allKeys()) {
        snapshot.insert(key, settings.value(key));
    }
    return snapshot;
}

} // namespace

class AiSettingsDialogTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();
    void init();
    void cleanupTestCase();
    void mainWindowProvidesModalAiSettingsAction();
    void showsSecureDefaultsAndEnvironmentStatus();
    void savesValidDraftAndReopensIt();
    void cancelDoesNotPersistDraft();
    void invalidDraftDoesNotCreateClientOrPersist();
    void connectionTestUsesUnsavedDraftWithoutPersistingIt();
    void testingDisablesButtonThenReportsSuccessAndResetsWhenEdited();
    void reportsSafeOfflineFailures_data();
    void reportsSafeOfflineFailures();
    void languageSwitchRetranslatesWithoutLosingDraftIdsOrStatus();

private:
    QTemporaryDir m_settingsDirectory;
};

void AiSettingsDialogTest::initTestCase()
{
    QVERIFY(m_settingsDirectory.isValid());
    QCoreApplication::setOrganizationName(QStringLiteral("QtBlueprintGeneratorTests"));
    QCoreApplication::setApplicationName(QStringLiteral("AiSettingsDialogTest"));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat,
                       QSettings::UserScope,
                       m_settingsDirectory.path());
}

void AiSettingsDialogTest::init()
{
    QSettings().clear();
    qunsetenv("BLUEPRINT_AI_API_KEY");
}

void AiSettingsDialogTest::cleanupTestCase()
{
    QSettings().clear();
    qunsetenv("BLUEPRINT_AI_API_KEY");
}

void AiSettingsDialogTest::mainWindowProvidesModalAiSettingsAction()
{
    MainWindow window;
    window.show();
    auto *menu = requiredChild<QMenu>(&window, "aiMenu");
    auto *action = requiredChild<QAction>(&window, "aiSettingsAction");
    QCOMPARE(menu->title(), QStringLiteral("AI"));
    QCOMPARE(action->text(), QStringLiteral("AI Settings..."));

    bool inspected = false;
    QTimer::singleShot(0, &window, [&] {
        auto *dialog = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        QVERIFY(dialog);
        QCOMPARE(dialog->objectName(), QStringLiteral("aiSettingsDialog"));
        inspected = true;
        dialog->reject();
    });
    action->trigger();
    QVERIFY(inspected);
}

void AiSettingsDialogTest::showsSecureDefaultsAndEnvironmentStatus()
{
    {
        AiSettingsDialog withoutCredential;
        QCOMPARE(requiredChild<QLabel>(&withoutCredential, "aiApiKeyStatusLabel")->text(),
                 QStringLiteral("Not available"));
    }
    const QByteArray secret("must-never-appear-in-the-dialog");
    qputenv("BLUEPRINT_AI_API_KEY", secret);
    AiSettingsDialog dialog;

    QCOMPARE(dialog.objectName(), QStringLiteral("aiSettingsDialog"));
    auto *provider = requiredChild<QComboBox>(&dialog, "aiProviderCombo");
    auto *endpoint = requiredChild<QLineEdit>(&dialog, "aiEndpointEdit");
    auto *model = requiredChild<QLineEdit>(&dialog, "aiModelEdit");
    auto *source = requiredChild<QComboBox>(&dialog, "aiApiKeySourceCombo");
    auto *keyStatus = requiredChild<QLabel>(&dialog, "aiApiKeyStatusLabel");
    auto *testButton = requiredChild<QPushButton>(&dialog, "aiTestConnectionButton");
    auto *connectionStatus = requiredChild<QLabel>(&dialog, "aiConnectionStatusLabel");
    auto *save = requiredChild<QPushButton>(&dialog, "aiSettingsSaveButton");
    auto *cancel = requiredChild<QPushButton>(&dialog, "aiSettingsCancelButton");

    QCOMPARE(provider->count(), 1);
    QCOMPARE(provider->currentData().toString(), QStringLiteral("openai-compatible"));
    QCOMPARE(provider->currentText(), QStringLiteral("OpenAI Compatible"));
    QCOMPARE(endpoint->text(), QStringLiteral("https://api.deepseek.com/chat/completions"));
    QCOMPARE(model->text(), QStringLiteral("deepseek-flash"));
    QCOMPARE(source->count(), 1);
    QCOMPARE(source->currentData().toString(), QStringLiteral("environment"));
    QCOMPARE(source->currentText(), QStringLiteral("Environment variable BLUEPRINT_AI_API_KEY"));
    QCOMPARE(keyStatus->text(), QStringLiteral("Available"));
    QCOMPARE(connectionStatus->text(), QStringLiteral("Not tested"));
    QCOMPARE(testButton->text(), QStringLiteral("Test Connection"));
    QCOMPARE(save->text(), QStringLiteral("Save"));
    QCOMPARE(cancel->text(), QStringLiteral("Cancel"));
    QCOMPARE(dialog.findChildren<QLineEdit *>().size(), 2);
    const QString allVisible = visibleTexts(dialog).join(QLatin1Char('\n'));
    QVERIFY(!allVisible.contains(QString::fromUtf8(secret)));
}

void AiSettingsDialogTest::savesValidDraftAndReopensIt()
{
    AiSettingsDialog dialog;
    auto *endpoint = requiredChild<QLineEdit>(&dialog, "aiEndpointEdit");
    auto *model = requiredChild<QLineEdit>(&dialog, "aiModelEdit");
    endpoint->setText(QStringLiteral("https://provider.example/v1/chat/completions"));
    model->setText(QStringLiteral("model-one"));

    QTest::mouseClick(requiredChild<QPushButton>(&dialog, "aiSettingsSaveButton"),
                      Qt::LeftButton);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QSettings stored;
    QCOMPARE(stored.value(QStringLiteral("ai/providerId")).toString(),
             QStringLiteral("openai-compatible"));
    QCOMPARE(stored.value(QStringLiteral("ai/endpoint")).toString(), endpoint->text());
    QCOMPARE(stored.value(QStringLiteral("ai/model")).toString(), model->text());
    QCOMPARE(stored.value(QStringLiteral("ai/credentialSource")).toString(),
             QStringLiteral("environment"));
    QVERIFY(!stored.contains(QStringLiteral("ai/apiKey")));

    AiSettingsDialog reopened;
    QCOMPARE(requiredChild<QLineEdit>(&reopened, "aiEndpointEdit")->text(), endpoint->text());
    QCOMPARE(requiredChild<QLineEdit>(&reopened, "aiModelEdit")->text(), model->text());
}

void AiSettingsDialogTest::cancelDoesNotPersistDraft()
{
    QSettings before;
    before.setValue(QStringLiteral("unrelated/keep"), QStringLiteral("yes"));
    before.sync();
    AiSettingsDialog dialog;
    requiredChild<QLineEdit>(&dialog, "aiEndpointEdit")
        ->setText(QStringLiteral("https://cancelled.example/v1/chat/completions"));
    requiredChild<QLineEdit>(&dialog, "aiModelEdit")->setText(QStringLiteral("cancelled-model"));

    QTest::mouseClick(requiredChild<QPushButton>(&dialog, "aiSettingsCancelButton"),
                      Qt::LeftButton);

    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QSettings after;
    QCOMPARE(after.allKeys(), QStringList({QStringLiteral("unrelated/keep")}));
    QCOMPARE(after.value(QStringLiteral("unrelated/keep")).toString(), QStringLiteral("yes"));
}

void AiSettingsDialogTest::invalidDraftDoesNotCreateClientOrPersist()
{
    FactoryProbe probe;
    AiSettingsDialog dialog(probe.factory());
    requiredChild<QLineEdit>(&dialog, "aiEndpointEdit")
        ->setText(QStringLiteral("http://insecure.example/chat/completions"));

    QTest::mouseClick(requiredChild<QPushButton>(&dialog, "aiTestConnectionButton"),
                      Qt::LeftButton);

    QCOMPARE(probe.calls, 0);
    QCOMPARE(requiredChild<QLabel>(&dialog, "aiConnectionStatusLabel")->text(),
             QStringLiteral("Invalid configuration"));
    QVERIFY(requiredChild<QPushButton>(&dialog, "aiTestConnectionButton")->isEnabled());
    QTest::mouseClick(requiredChild<QPushButton>(&dialog, "aiSettingsSaveButton"),
                      Qt::LeftButton);
    QCOMPARE(dialog.result(), 0);
    QVERIFY(QSettings().allKeys().isEmpty());
}

void AiSettingsDialogTest::connectionTestUsesUnsavedDraftWithoutPersistingIt()
{
    AiProviderSettings persistedA;
    persistedA.endpoint = QUrl(QStringLiteral("https://stored-a.example/v1/chat/completions"));
    persistedA.model = QStringLiteral("stored-model-a");
    QSettings persistentSettings;
    QString saveError;
    QVERIFY2(persistedA.save(persistentSettings, &saveError), qPrintable(saveError));
    persistentSettings.sync();
    const QMap<QString, QVariant> before = settingsSnapshot();
    QCOMPARE(before.size(), 4);
    QCOMPARE(before.value(QStringLiteral("ai/endpoint")).toString(),
             persistedA.endpoint.toString(QUrl::FullyEncoded));
    QCOMPARE(before.value(QStringLiteral("ai/model")).toString(), persistedA.model);

    FactoryProbe probe;
    AiSettingsDialog dialog(probe.factory());
    const QUrl draftEndpointB(QStringLiteral("https://unsaved-b.example/chat/completions"));
    const QString draftModelB = QStringLiteral("unsaved-model-b");
    requiredChild<QLineEdit>(&dialog, "aiEndpointEdit")
        ->setText(draftEndpointB.toString(QUrl::FullyEncoded));
    requiredChild<QLineEdit>(&dialog, "aiModelEdit")->setText(draftModelB);
    QCOMPARE(settingsSnapshot(), before);

    QTest::mouseClick(requiredChild<QPushButton>(&dialog, "aiTestConnectionButton"),
                      Qt::LeftButton);

    QCOMPARE(probe.calls, 1);
    QCOMPARE(probe.settings.providerId, QStringLiteral("openai-compatible"));
    QCOMPARE(probe.settings.endpoint, draftEndpointB);
    QCOMPARE(probe.settings.model, draftModelB);
    QCOMPARE(probe.settings.credentialSource, AiCredentialSource::Environment);
    QTRY_COMPARE_WITH_TIMEOUT(
        requiredChild<QLabel>(&dialog, "aiConnectionStatusLabel")->text(),
        QStringLiteral("Success"),
        500);
    QCOMPARE(settingsSnapshot(), before);
    QCOMPARE(settingsSnapshot().keys(),
             QStringList({QStringLiteral("ai/credentialSource"),
                          QStringLiteral("ai/endpoint"),
                          QStringLiteral("ai/model"),
                          QStringLiteral("ai/providerId")}));
}

void AiSettingsDialogTest::testingDisablesButtonThenReportsSuccessAndResetsWhenEdited()
{
    FactoryProbe probe;
    AiSettingsDialog dialog(probe.factory());
    auto *button = requiredChild<QPushButton>(&dialog, "aiTestConnectionButton");
    auto *status = requiredChild<QLabel>(&dialog, "aiConnectionStatusLabel");

    QTest::mouseClick(button, Qt::LeftButton);

    QCOMPARE(status->text(), QStringLiteral("Testing"));
    QVERIFY(!button->isEnabled());
    QCOMPARE(probe.calls, 1);
    QCOMPARE(probe.settings.endpoint,
             QUrl(QStringLiteral("https://api.deepseek.com/chat/completions")));
    QCOMPARE(probe.settings.model, QStringLiteral("deepseek-flash"));
    QVERIFY(probe.client);
    QCOMPARE(probe.client->requests().size(), 1);
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("Success"), 500);
    QVERIFY(button->isEnabled());

    requiredChild<QLineEdit>(&dialog, "aiModelEdit")->setText(QStringLiteral("changed-model"));
    QCOMPARE(status->text(), QStringLiteral("Not tested"));
}

void AiSettingsDialogTest::reportsSafeOfflineFailures_data()
{
    QTest::addColumn<AiErrorKind>("kind");
    QTest::addColumn<QString>("expected");

    QTest::newRow("credential") << AiErrorKind::CredentialUnavailable
                                  << QStringLiteral("API key is not available");
    QTest::newRow("network") << AiErrorKind::Network << QStringLiteral("Network error");
    QTest::newRow("timeout") << AiErrorKind::Timeout << QStringLiteral("Request timed out");
    QTest::newRow("authentication") << AiErrorKind::Authentication
                                      << QStringLiteral("Authentication failed");
    QTest::newRow("payment") << AiErrorKind::PaymentRequired
                               << QStringLiteral("Payment required");
    QTest::newRow("endpoint") << AiErrorKind::EndpointNotFound
                                << QStringLiteral("Endpoint not found");
    QTest::newRow("model") << AiErrorKind::ModelNotFound << QStringLiteral("Model not found");
    QTest::newRow("rate-limit") << AiErrorKind::RateLimited
                                  << QStringLiteral("Rate limit reached");
    QTest::newRow("response") << AiErrorKind::InvalidResponse
                                << QStringLiteral("Invalid response");
    QTest::newRow("provider") << AiErrorKind::ProviderUnavailable
                                << QStringLiteral("Provider unavailable");
    QTest::newRow("unknown") << AiErrorKind::Unknown << QStringLiteral("Connection failed");
}

void AiSettingsDialogTest::reportsSafeOfflineFailures()
{
    QFETCH(AiErrorKind, kind);
    QFETCH(QString, expected);
    FactoryProbe probe;
    probe.succeed = false;
    probe.result = {kind, 418, QStringLiteral("secret-provider-message")};
    AiSettingsDialog dialog(probe.factory());
    auto *status = requiredChild<QLabel>(&dialog, "aiConnectionStatusLabel");

    QTest::mouseClick(requiredChild<QPushButton>(&dialog, "aiTestConnectionButton"),
                      Qt::LeftButton);

    QTRY_COMPARE_WITH_TIMEOUT(status->text(), expected, 500);
    QVERIFY(!status->text().contains(QStringLiteral("secret-provider-message")));
    QVERIFY(requiredChild<QPushButton>(&dialog, "aiTestConnectionButton")->isEnabled());
}

void AiSettingsDialogTest::languageSwitchRetranslatesWithoutLosingDraftIdsOrStatus()
{
    MainWindow window;
    FactoryProbe probe;
    AiSettingsDialog dialog(probe.factory(), &window);
    dialog.show();
    QApplication::processEvents();
    auto *provider = requiredChild<QComboBox>(&dialog, "aiProviderCombo");
    auto *source = requiredChild<QComboBox>(&dialog, "aiApiKeySourceCombo");
    auto *endpoint = requiredChild<QLineEdit>(&dialog, "aiEndpointEdit");
    auto *model = requiredChild<QLineEdit>(&dialog, "aiModelEdit");
    auto *status = requiredChild<QLabel>(&dialog, "aiConnectionStatusLabel");
    endpoint->setText(QStringLiteral("https://draft.example/v1/chat/completions"));
    model->setText(QStringLiteral("draft-model"));
    QTest::mouseClick(requiredChild<QPushButton>(&dialog, "aiTestConnectionButton"),
                      Qt::LeftButton);
    QTRY_COMPARE_WITH_TIMEOUT(status->text(), QStringLiteral("Success"), 500);

    QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
    QApplication::processEvents();

    QCOMPARE(dialog.windowTitle(), QStringLiteral("AI 设置"));
    QCOMPARE(provider->currentText(), QStringLiteral("OpenAI 兼容接口"));
    QCOMPARE(provider->currentData().toString(), QStringLiteral("openai-compatible"));
    QCOMPARE(source->currentText(), QStringLiteral("环境变量 BLUEPRINT_AI_API_KEY"));
    QCOMPARE(source->currentData().toString(), QStringLiteral("environment"));
    QCOMPARE(endpoint->text(), QStringLiteral("https://draft.example/v1/chat/completions"));
    QCOMPARE(model->text(), QStringLiteral("draft-model"));
    QCOMPARE(status->text(), QStringLiteral("成功"));
    QCOMPARE(requiredChild<QPushButton>(&dialog, "aiTestConnectionButton")->text(),
             QStringLiteral("测试连接"));
    QCOMPARE(requiredChild<QPushButton>(&dialog, "aiSettingsSaveButton")->text(),
             QStringLiteral("保存"));
    QCOMPARE(requiredChild<QPushButton>(&dialog, "aiSettingsCancelButton")->text(),
             QStringLiteral("取消"));
    QCOMPARE(requiredChild<QMenu>(&window, "aiMenu")->title(), QStringLiteral("AI"));
    QCOMPARE(requiredChild<QAction>(&window, "aiSettingsAction")->text(),
             QStringLiteral("AI 设置..."));

    QVERIFY(window.setLanguage(QStringLiteral("en")));
}

QTEST_MAIN(AiSettingsDialogTest)

#include "tst_ai_settings_dialog.moc"
