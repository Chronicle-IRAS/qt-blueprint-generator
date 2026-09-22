#include "app/validation_diagnostics_dialog.h"
#include "ui/theme.h"

#include <QDialogButtonBox>
#include <QEvent>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QVBoxLayout>

ValidationDiagnosticsDialog::ValidationDiagnosticsDialog(
    const QVector<BlueprintDiagnostic> &diagnostics, QWidget *parent)
    : QDialog(parent)
    , m_diagnostics(diagnostics)
{
    setObjectName(QStringLiteral("validationDiagnosticsDialog"));
    resize(560, 420);

    auto *layout = new QVBoxLayout(this);
    layout->setSpacing(EditorTheme::SpaceMedium);

    m_summaryLabel = new QLabel(this);
    m_summaryLabel->setObjectName(QStringLiteral("validationDiagnosticsSummary"));
    m_summaryLabel->setWordWrap(true);
    layout->addWidget(m_summaryLabel);

    // Every diagnostic is shown; the editor scrolls when there are many of them.
    m_details = new QPlainTextEdit(this);
    m_details->setObjectName(QStringLiteral("validationDiagnosticsText"));
    m_details->setReadOnly(true);
    m_details->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    layout->addWidget(m_details);

    // A custom button keeps our own translated text; QDialogButtonBox re-translates
    // its standard buttons itself on a language change.
    auto *buttons = new QDialogButtonBox(QDialogButtonBox::NoButton, Qt::Horizontal, this);
    m_closeButton = new QPushButton(this);
    m_closeButton->setObjectName(QStringLiteral("closeValidationDiagnosticsButton"));
    buttons->addButton(m_closeButton, QDialogButtonBox::RejectRole);
    layout->addWidget(buttons);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    retranslateUi();
}

QString ValidationDiagnosticsDialog::describe(const BlueprintDiagnostic &diagnostic)
{
    QStringList lines;
    if (!diagnostic.nodeId.isEmpty()) {
        lines.append(tr("node: %1").arg(diagnostic.nodeId));
    }
    if (!diagnostic.edgeId.isEmpty()) {
        lines.append(tr("edge: %1").arg(diagnostic.edgeId));
    }
    lines.append(diagnostic.message);
    return lines.join(QLatin1Char('\n'));
}

void ValidationDiagnosticsDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        retranslateUi();
    }
}

void ValidationDiagnosticsDialog::retranslateUi()
{
    setWindowTitle(tr("Blueprint validation failed"));
    m_summaryLabel->setText(tr("Fix these blueprint problems before generating:"));
    m_closeButton->setText(tr("Close"));

    // The id lines carry translated labels, so the body follows the language too.
    QStringList blocks;
    blocks.reserve(m_diagnostics.size());
    for (const BlueprintDiagnostic &diagnostic : m_diagnostics) {
        blocks.append(describe(diagnostic));
    }
    m_details->setPlainText(blocks.join(QStringLiteral("\n\n")));
}
