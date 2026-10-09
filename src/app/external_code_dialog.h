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
    void chooseSourceRoot();
    void addSourceFiles();
    void importSelection(bool replace);
    void updateSelectionControls();
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
    bool m_knownImport = false;
    QString m_sourceRoot;
    quint64 m_selectionRevision = 0;
    Status m_status = Status::NotImported;
    QString m_error;
    QLabel *m_binding = nullptr;
    QLabel *m_statusLabel = nullptr;
    QLabel *m_selectionLabel = nullptr;
    QListWidget *m_pendingFiles = nullptr;
    QListWidget *m_files = nullptr;
    QPlainTextEdit *m_preview = nullptr;
    QPushButton *m_importButton = nullptr;
    QPushButton *m_chooseRootButton = nullptr;
    QPushButton *m_addFilesButton = nullptr;
    QPushButton *m_removeSelectionButton = nullptr;
    QPushButton *m_clearSelectionButton = nullptr;
    QPushButton *m_reimportButton = nullptr;
    QPushButton *m_verifyButton = nullptr;
    QPushButton *m_closeButton = nullptr;
};
