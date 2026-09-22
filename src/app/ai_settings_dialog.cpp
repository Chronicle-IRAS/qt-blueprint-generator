#include "app/ai_settings_dialog.h"

#include "ai/ai_connection_tester.h"
#include "ai/openai_compatible_client.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QEvent>
#include <QFormLayout>
#include "ui/theme.h"
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
#include <QSettings>
#include <QVBoxLayout>

#include <utility>

namespace {

AiSettingsDialog::ClientFactory productionClientFactory()
{
    return [](const AiProviderSettings &settings, QObject *parent) {
        return new OpenAiCompatibleClient(settings.endpoint, settings.model, parent);
    };
}

void setFormLabel(QFormLayout *form, QWidget *field, const QString &text)
{
    if (auto *label = qobject_cast<QLabel *>(form->labelForField(field))) {
        label->setText(text);
    }
}

} // namespace

AiSettingsDialog::AiSettingsDialog(QWidget *parent)
    : AiSettingsDialog(productionClientFactory(), parent)
{
}

AiSettingsDialog::AiSettingsDialog(ClientFactory clientFactory, QWidget *parent)
    : QDialog(parent)
    , m_clientFactory(std::move(clientFactory))
{
    setObjectName(QStringLiteral("aiSettingsDialog"));
    setModal(true);
    resize(620, 300);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(EditorTheme::SpaceLarge, EditorTheme::SpaceLarge,
                               EditorTheme::SpaceLarge, EditorTheme::SpaceLarge);
    layout->setSpacing(EditorTheme::SpaceMedium);
    m_form = new QFormLayout;
    m_form->setRowWrapPolicy(QFormLayout::WrapLongRows);
    m_form->setSpacing(EditorTheme::SpaceMedium);

    m_providerCombo = new QComboBox(this);
    m_providerCombo->setObjectName(QStringLiteral("aiProviderCombo"));
    m_providerCombo->addItem(QString(), QStringLiteral("openai-compatible"));
    m_form->addRow(new QLabel(this), m_providerCombo);

    m_endpointEdit = new QLineEdit(this);
    m_endpointEdit->setObjectName(QStringLiteral("aiEndpointEdit"));
    m_form->addRow(new QLabel(this), m_endpointEdit);

    m_modelEdit = new QLineEdit(this);
    m_modelEdit->setObjectName(QStringLiteral("aiModelEdit"));
    m_form->addRow(new QLabel(this), m_modelEdit);

    m_apiKeySourceCombo = new QComboBox(this);
    m_apiKeySourceCombo->setObjectName(QStringLiteral("aiApiKeySourceCombo"));
    m_apiKeySourceCombo->addItem(QString(), QStringLiteral("environment"));
    m_form->addRow(new QLabel(this), m_apiKeySourceCombo);

    m_apiKeyStatusLabel = new QLabel(this);
    m_apiKeyStatusLabel->setWordWrap(true);
    m_apiKeyStatusLabel->setObjectName(QStringLiteral("aiApiKeyStatusLabel"));
    m_form->addRow(new QLabel(this), m_apiKeyStatusLabel);

    auto *connectionWidget = new QWidget(this);
    auto *connectionLayout = new QHBoxLayout(connectionWidget);
    connectionLayout->setContentsMargins(0, 0, 0, 0);
    m_testConnectionButton = new QPushButton(connectionWidget);
    m_testConnectionButton->setObjectName(QStringLiteral("aiTestConnectionButton"));
    m_connectionStatusLabel = new QLabel(connectionWidget);
    m_connectionStatusLabel->setWordWrap(true);
    m_connectionStatusLabel->setObjectName(QStringLiteral("aiConnectionStatusLabel"));
    connectionLayout->addWidget(m_testConnectionButton);
    connectionLayout->addWidget(m_connectionStatusLabel, 1);
    m_form->addRow(new QLabel(this), connectionWidget);
    layout->addLayout(m_form);

    auto *buttons = new QDialogButtonBox(this);
    m_saveButton = new QPushButton;
    m_saveButton->setObjectName(QStringLiteral("aiSettingsSaveButton"));
    m_saveButton->setProperty("role", "primary");
    buttons->addButton(m_saveButton, QDialogButtonBox::AcceptRole);
    m_cancelButton = buttons->addButton(QString(), QDialogButtonBox::RejectRole);
    m_cancelButton->setObjectName(QStringLiteral("aiSettingsCancelButton"));
    layout->addWidget(buttons);

    QSettings settings;
    const AiProviderSettings stored = AiProviderSettings::load(settings);
    const int providerIndex = m_providerCombo->findData(stored.providerId);
    m_providerCombo->setCurrentIndex(providerIndex >= 0 ? providerIndex : 0);
    m_endpointEdit->setText(stored.endpoint.toString(QUrl::FullyEncoded));
    m_modelEdit->setText(stored.model);
    const QString credentialId = stored.credentialSource == AiCredentialSource::Environment
                                     ? QStringLiteral("environment")
                                     : QString();
    const int credentialIndex = m_apiKeySourceCombo->findData(credentialId);
    m_apiKeySourceCombo->setCurrentIndex(credentialIndex >= 0 ? credentialIndex : 0);

    connect(m_testConnectionButton,
            &QPushButton::clicked,
            this,
            &AiSettingsDialog::testConnection);
    connect(m_saveButton, &QPushButton::clicked, this, &AiSettingsDialog::saveSettings);
    connect(m_cancelButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_endpointEdit,
            &QLineEdit::textChanged,
            this,
            [this] { resetConnectionState(); });
    connect(m_modelEdit,
            &QLineEdit::textChanged,
            this,
            [this] { resetConnectionState(); });
    connect(m_providerCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] { resetConnectionState(); });
    connect(m_apiKeySourceCombo,
            &QComboBox::currentIndexChanged,
            this,
            [this] { resetConnectionState(); });

    retranslateUi();
}

void AiSettingsDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

AiProviderSettings AiSettingsDialog::formSettings() const
{
    AiProviderSettings settings;
    settings.providerId = m_providerCombo->currentData().toString();
    settings.endpoint = QUrl(m_endpointEdit->text());
    settings.model = m_modelEdit->text();
    settings.credentialSource =
        m_apiKeySourceCombo->currentData().toString() == QStringLiteral("environment")
            ? AiCredentialSource::Environment
            : AiCredentialSource::Unknown;
    return settings;
}

void AiSettingsDialog::testConnection()
{
    const AiProviderSettings settings = formSettings();
    if (!settings.validate()) {
        setDisplayState(DisplayState::InvalidConfiguration);
        return;
    }

    delete m_tester;
    m_tester = nullptr;
    delete m_client;
    m_client = nullptr;
    IAiClient *client = m_clientFactory ? m_clientFactory(settings, this) : nullptr;
    m_client = client;
    m_tester = new AiConnectionTester(client, this);
    connect(m_tester,
            &AiConnectionTester::stateChanged,
            this,
            [this](AiConnectionState state, const AiClientError &error) {
                switch (state) {
                case AiConnectionState::Testing:
                    setDisplayState(DisplayState::Testing);
                    break;
                case AiConnectionState::Success:
                    setDisplayState(DisplayState::Success);
                    break;
                case AiConnectionState::Failure:
                    setDisplayState(DisplayState::Failure, error.kind);
                    break;
                }
            });
    m_tester->testConnection();
}

void AiSettingsDialog::saveSettings()
{
    const AiProviderSettings settings = formSettings();
    if (!settings.validate()) {
        setDisplayState(DisplayState::InvalidConfiguration);
        return;
    }

    QSettings persistentSettings;
    if (!settings.save(persistentSettings)) {
        setDisplayState(DisplayState::SaveFailure);
        return;
    }
    accept();
}

void AiSettingsDialog::resetConnectionState()
{
    delete m_tester;
    m_tester = nullptr;
    delete m_client;
    m_client = nullptr;
    setDisplayState(DisplayState::NotTested);
}

void AiSettingsDialog::setDisplayState(DisplayState state, AiErrorKind errorKind)
{
    m_displayState = state;
    m_errorKind = errorKind;
    m_testConnectionButton->setEnabled(state != DisplayState::Testing);
    m_connectionStatusLabel->setText(connectionStatusText());
}

QString AiSettingsDialog::connectionStatusText() const
{
    switch (m_displayState) {
    case DisplayState::NotTested:
        return tr("Not tested");
    case DisplayState::Testing:
        return tr("Testing");
    case DisplayState::Success:
        return tr("Success");
    case DisplayState::InvalidConfiguration:
        return tr("Invalid configuration");
    case DisplayState::SaveFailure:
        return tr("Settings could not be saved");
    case DisplayState::Failure:
        break;
    }

    switch (m_errorKind) {
    case AiErrorKind::InvalidConfiguration:
        return tr("Invalid configuration");
    case AiErrorKind::CredentialUnavailable:
        return tr("API key is not available");
    case AiErrorKind::Network:
        return tr("Network error");
    case AiErrorKind::Timeout:
        return tr("Request timed out");
    case AiErrorKind::Authentication:
        return tr("Authentication failed");
    case AiErrorKind::PaymentRequired:
        return tr("Payment required");
    case AiErrorKind::EndpointNotFound:
        return tr("Endpoint not found");
    case AiErrorKind::ModelNotFound:
        return tr("Model not found");
    case AiErrorKind::RateLimited:
        return tr("Rate limit reached");
    case AiErrorKind::InvalidResponse:
        return tr("Invalid response");
    case AiErrorKind::ProviderUnavailable:
        return tr("Provider unavailable");
    case AiErrorKind::Unknown:
        return tr("Connection failed");
    }
    return tr("Connection failed");
}

void AiSettingsDialog::retranslateUi()
{
    setWindowTitle(tr("AI Settings"));
    m_providerCombo->setItemText(0, tr("OpenAI Compatible"));
    m_apiKeySourceCombo->setItemText(
        0,
        tr("Environment variable BLUEPRINT_AI_API_KEY"));
    m_apiKeyStatusLabel->setText(formSettings().credentialAvailable() ? tr("Available")
                                                                      : tr("Not available"));
    m_testConnectionButton->setText(tr("Test Connection"));
    m_connectionStatusLabel->setText(connectionStatusText());
    m_saveButton->setText(tr("Save"));
    m_cancelButton->setText(tr("Cancel"));
    setFormLabel(m_form, m_providerCombo, tr("Provider"));
    setFormLabel(m_form, m_endpointEdit, tr("Chat Completions Endpoint"));
    setFormLabel(m_form, m_modelEdit, tr("Model"));
    setFormLabel(m_form, m_apiKeySourceCombo, tr("API Key Source"));
    setFormLabel(m_form, m_apiKeyStatusLabel, tr("API Key Status"));
    setFormLabel(m_form,
                 m_testConnectionButton->parentWidget(),
                 tr("Connection Status"));
}
