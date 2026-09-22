#pragma once

#include "blueprint/blueprint_validator.h"

#include <QDialog>

class QEvent;
class QLabel;
class QPlainTextEdit;
class QPushButton;

// Read-only report of the BlueprintValidator diagnostics that blocked generation.
// The diagnostic bodies come from the validator as plain text; only the labels are
// translated.
class ValidationDiagnosticsDialog final : public QDialog
{
    Q_OBJECT

public:
    explicit ValidationDiagnosticsDialog(const QVector<BlueprintDiagnostic> &diagnostics,
                                        QWidget *parent = nullptr);

    // One readable block per diagnostic: optional node id, optional edge id, message.
    static QString describe(const BlueprintDiagnostic &diagnostic);

protected:
    void changeEvent(QEvent *event) override;

private:
    void retranslateUi();

    QVector<BlueprintDiagnostic> m_diagnostics;
    QLabel *m_summaryLabel = nullptr;
    QPlainTextEdit *m_details = nullptr;
    QPushButton *m_closeButton = nullptr;
};
