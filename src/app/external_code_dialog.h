#pragma once

#include "blueprint/blueprint_document.h"
#include <QDialog>
#include <functional>

class QLabel;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QEvent;

// Owns only a snapshot and workspace operations; never edits the blueprint.
class ExternalCodeDialog final : public QDialog
{
    Q_OBJECT
public:
    ExternalCodeDialog(BlueprintNode node, QString workspace,
                       std::function<bool()> contextCurrent,
                       std::function<bool()> writesAllowed,
                       QWidget *parent = nullptr);
    void invalidateContext();
protected:
    void changeEvent(QEvent *event) override;
private:
    bool checkContext();
    bool checkWriteAccess();
    void selectAndImport(bool replace);
    void verify();
    void previewFile();
    void clearVerifiedContent();
    void retranslateUi();
    enum class Status { NotImported, Verified, Failed, WriteBlocked, Invalid };
    BlueprintNode m_node;
    QString m_workspace;
    std::function<bool()> m_contextCurrent;
    std::function<bool()> m_writesAllowed;
    bool m_invalid = false;
    Status m_status = Status::NotImported;
    QString m_error;
    QLabel *m_binding = nullptr;
    QLabel *m_statusLabel = nullptr;
    QListWidget *m_files = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_reimportButton = nullptr;
    QPushButton *m_verifyButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
