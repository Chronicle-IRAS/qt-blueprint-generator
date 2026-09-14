#pragma once

#include "ai/ai_client.h"
#include "ai/ai_provider_settings.h"

#include <QDialog>
#include <QPointer>

#include <functional>

class AiConnectionTester;
class QComboBox;
class QEvent;
class QFormLayout;
class QLabel;
class QLineEdit;
class QPushButton;

class AiSettingsDialog final : public QDialog
{
    Q_OBJECT

public:
    using ClientFactory =
        std::function<IAiClient *(const AiProviderSettings &settings, QObject *parent)>;

    explicit AiSettingsDialog(QWidget *parent = nullptr);
    explicit AiSettingsDialog(ClientFactory clientFactory, QWidget *parent = nullptr);

protected:
    void changeEvent(QEvent *event) override;

private:
    enum class DisplayState
    {
        NotTested,
        Testing,
        Success,
        Failure,
        InvalidConfiguration,
        SaveFailure,
    };

    AiProviderSettings formSettings() const;
    void testConnection();
    void saveSettings();
    void resetConnectionState();
    void setDisplayState(DisplayState state, AiErrorKind errorKind = AiErrorKind::Unknown);
    void retranslateUi();
    QString connectionStatusText() const;

    ClientFactory m_clientFactory;
    QFormLayout *m_form = nullptr;
    QComboBox *m_providerCombo = nullptr;
    QLineEdit *m_endpointEdit = nullptr;
    QLineEdit *m_modelEdit = nullptr;
    QComboBox *m_apiKeySourceCombo = nullptr;
    QLabel *m_apiKeyStatusLabel = nullptr;
    QPushButton *m_testConnectionButton = nullptr;
    QLabel *m_connectionStatusLabel = nullptr;
    QPushButton *m_saveButton = nullptr;
    QPushButton *m_cancelButton = nullptr;
    QPointer<IAiClient> m_client;
    QPointer<AiConnectionTester> m_tester;
    DisplayState m_displayState = DisplayState::NotTested;
    AiErrorKind m_errorKind = AiErrorKind::Unknown;
};
