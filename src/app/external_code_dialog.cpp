#include "app/external_code_dialog.h"
#include "blueprint/blueprint_serializer.h"
#include "ui/theme.h"
#include "workspace/external_code_importer.h"
#include <QDir>
#include <QEvent>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QJsonDocument>
#include <QJsonArray>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QVBoxLayout>

ExternalCodeDialog::ExternalCodeDialog(BlueprintNode node, QString workspace,
                                     std::function<bool()> contextCurrent,
                                     std::function<bool()> writesAllowed, QWidget *parent)
    : QDialog(parent), m_node(std::move(node)), m_workspace(std::move(workspace)),
      m_contextCurrent(std::move(contextCurrent)), m_writesAllowed(std::move(writesAllowed))
{
    setObjectName(QStringLiteral("externalCodeDialog"));
    resize(760, 600);
    auto *layout = new QVBoxLayout(this);
    m_binding = new QLabel(this);
    m_binding->setObjectName(QStringLiteral("externalCodeBinding"));
    m_binding->setTextFormat(Qt::PlainText);
    m_binding->setWordWrap(true);
    m_binding->setTextInteractionFlags(Qt::TextSelectableByMouse);
    layout->addWidget(m_binding);
    // The complete applied contract is visible without suggesting it is inferred from source.
    BlueprintDocument contract;
    contract.nodes = {m_node};
    auto *contractView = new QPlainTextEdit(this);
    contractView->setObjectName(QStringLiteral("externalAppliedContract"));
    contractView->setReadOnly(true);
    contractView->setFont(EditorTheme::codeFont());
    contractView->setPlainText(QString::fromUtf8(QJsonDocument(
        QJsonDocument::fromJson(BlueprintSerializer::toJson(contract)).object().value("nodes").toArray().first().toObject())
            .toJson(QJsonDocument::Indented)));
    contractView->setMaximumHeight(120);
    layout->addWidget(contractView);
    m_statusLabel = new QLabel(this);
    m_statusLabel->setObjectName(QStringLiteral("externalImportStatus"));
    m_statusLabel->setTextFormat(Qt::PlainText);
    m_statusLabel->setWordWrap(true);
    layout->addWidget(m_statusLabel);
    m_files = new QListWidget(this);
    m_files->setObjectName(QStringLiteral("externalImportedFiles"));
    m_files->setMaximumHeight(140);
    layout->addWidget(m_files);
    m_preview = new QPlainTextEdit(this);
    m_preview->setObjectName(QStringLiteral("externalSourcePreview"));
    m_preview->setReadOnly(true);
    m_preview->setFont(EditorTheme::codeFont());
    layout->addWidget(m_preview, 1);
    auto *buttons = new QHBoxLayout;
    m_importButton = new QPushButton(this);
    m_importButton->setObjectName(QStringLiteral("importExternalCodeButton"));
    m_reimportButton = new QPushButton(this);
    m_reimportButton->setObjectName(QStringLiteral("reimportExternalCodeButton"));
    m_verifyButton = new QPushButton(this);
    m_verifyButton->setObjectName(QStringLiteral("verifyExternalCodeButton"));
    m_closeButton = new QPushButton(this);
    for (auto *button : {m_importButton, m_reimportButton, m_verifyButton}) buttons->addWidget(button);
    buttons->addStretch();
    buttons->addWidget(m_closeButton);
    layout->addLayout(buttons);
    connect(m_importButton, &QPushButton::clicked, this, [this] { selectAndImport(false); });
    connect(m_reimportButton, &QPushButton::clicked, this, [this] { selectAndImport(true); });
    connect(m_verifyButton, &QPushButton::clicked, this, &ExternalCodeDialog::verify);
    connect(m_closeButton, &QPushButton::clicked, this, &QDialog::reject);
    connect(m_files, &QListWidget::currentRowChanged, this, &ExternalCodeDialog::previewFile);
    retranslateUi();
    verify();
}

void ExternalCodeDialog::clearVerifiedContent()
{
    m_files->clear();
    m_preview->clear();
}

void ExternalCodeDialog::invalidateContext()
{
    if (m_invalid) return;
    m_invalid = true;
    m_status = Status::Invalid;
    clearVerifiedContent();
    m_importButton->setEnabled(false);
    m_reimportButton->setEnabled(false);
    m_verifyButton->setEnabled(false);
    // End any currently executing nested picker/confirmation before dismissal.
    const auto dialogs = findChildren<QDialog *>(QString(), Qt::FindDirectChildrenOnly);
    for (auto *dialog : dialogs) dialog->reject();
    retranslateUi();
    reject();
}

bool ExternalCodeDialog::checkContext()
{
    if (m_invalid) return false;
    if (m_contextCurrent()) return true;
    invalidateContext();
    return false;
}

bool ExternalCodeDialog::checkWriteAccess()
{
    if (!checkContext()) return false;
    if (m_writesAllowed()) return true;
    m_status = Status::WriteBlocked;
    retranslateUi();
    return false;
}

void ExternalCodeDialog::selectAndImport(bool replace)
{
    if (!checkWriteAccess()) return;
    QPointer<ExternalCodeDialog> self(this);
    // Heap children and QPointer protect against parent destruction inside exec().
    QPointer<QFileDialog> rootPicker(new QFileDialog(this, tr("Choose external source root")));
    rootPicker->setObjectName(QStringLiteral("externalSourceRootDialog"));
    rootPicker->setOption(QFileDialog::DontResolveSymlinks);
    rootPicker->setFileMode(QFileDialog::Directory);
    rootPicker->setOption(QFileDialog::ShowDirsOnly);
    const int rootResult = rootPicker->exec();
    if (!self) return;
    if (!checkContext()) return;
    const QString root = rootPicker && rootResult == QDialog::Accepted
                             ? rootPicker->selectedFiles().value(0) : QString();
    if (rootPicker) rootPicker->deleteLater();
    if (root.isEmpty() || !checkWriteAccess()) return;
    QPointer<QFileDialog> filePicker(new QFileDialog(this, tr("Choose external source files"), root));
    filePicker->setObjectName(QStringLiteral("externalSourceFilesDialog"));
    filePicker->setOption(QFileDialog::DontResolveSymlinks);
    filePicker->setFileMode(QFileDialog::ExistingFiles);
    filePicker->setNameFilter(tr("C/C++ sources (*.h *.hpp *.c *.cpp *.cc *.cxx)"));
    const int fileResult = filePicker->exec();
    if (!self) return;
    if (!checkContext()) return;
    const QStringList absoluteFiles = filePicker && fileResult == QDialog::Accepted
                                         ? filePicker->selectedFiles() : QStringList();
    if (filePicker) filePicker->deleteLater();
    if (absoluteFiles.isEmpty() || !checkWriteAccess()) return;
    QStringList relativeFiles;
    for (const auto &path : absoluteFiles) relativeFiles << QDir(root).relativeFilePath(path);
    if (replace) {
        QPointer<QMessageBox> prompt(new QMessageBox(QMessageBox::Warning, tr("Replace imported sources"),
            tr("Replace the original tracked files with the selected source bytes and bind them to the current applied contract? The tracked file set must stay the same."),
            QMessageBox::NoButton, this));
        prompt->setObjectName(QStringLiteral("externalReimportConfirmation"));
        prompt->setTextFormat(Qt::PlainText);
        auto *confirm = prompt->addButton(tr("Replace tracked files"), QMessageBox::AcceptRole);
        confirm->setObjectName(QStringLiteral("confirmExternalReimportButton"));
        auto *cancel = prompt->addButton(tr("Cancel"), QMessageBox::RejectRole);
        cancel->setObjectName(QStringLiteral("cancelExternalReimportButton"));
        prompt->setDefaultButton(cancel);
        prompt->setEscapeButton(cancel);
        prompt->exec();
        if (!self) return;
        if (!checkContext()) return;
        const bool confirmed = prompt && prompt->clickedButton() == confirm;
        if (prompt) prompt->deleteLater();
        if (!confirmed) return;
    }
    if (!checkWriteAccess()) return;
    QString error;
    const bool ok = replace
        ? ExternalCodeImporter::reimportFiles(m_node, root, relativeFiles, m_workspace, &error)
        : ExternalCodeImporter::importFiles(m_node, root, relativeFiles, m_workspace, &error);
    if (!ok) {
        clearVerifiedContent();
        m_error = error;
        m_status = Status::Failed;
        retranslateUi();
        return;
    }
    verify();
}

void ExternalCodeDialog::verify()
{
    if (!checkContext()) return;
    const QString selected = m_files->currentItem() ? m_files->currentItem()->data(Qt::UserRole).toString() : QString();
    QJsonObject manifest;
    QString error;
    if (!ExternalCodeImporter::importManifest(m_node, m_workspace, manifest, &error)) {
        clearVerifiedContent();
        m_status = Status::Failed;
        m_error = error;
    } else {
        clearVerifiedContent();
        const auto files = manifest.value("files").toObject();
        for (auto it = files.begin(); it != files.end(); ++it) {
            auto *item = new QListWidgetItem(QStringLiteral("%1  |  SHA-256: %2").arg(it.key(), it.value().toString()), m_files);
            item->setData(Qt::UserRole, it.key());
            if (it.key() == selected) m_files->setCurrentItem(item);
        }
        m_status = Status::Verified;
        m_error.clear();
    }
    retranslateUi();
}

void ExternalCodeDialog::previewFile()
{
    m_preview->clear();
    if (!checkContext() || !m_files->currentItem()) return;
    QByteArray bytes;
    QString error;
    if (!ExternalCodeImporter::readImportedFile(m_node, m_workspace,
            m_files->currentItem()->data(Qt::UserRole).toString(), bytes, &error)) {
        clearVerifiedContent();
        m_status = Status::Failed;
        m_error = error;
        retranslateUi();
        return;
    }
    m_preview->setPlainText(QString::fromUtf8(bytes));
}

void ExternalCodeDialog::retranslateUi()
{
    setWindowTitle(tr("External code management"));
    m_binding->setText(tr("Applied node: %1 (%2)\nBound workspace: %3").arg(m_node.id, m_node.name, m_workspace));
    m_importButton->setText(tr("Import files..."));
    m_reimportButton->setText(tr("Reimport tracked files..."));
    m_verifyButton->setText(tr("Verify import"));
    m_closeButton->setText(tr("Close"));
    m_preview->setPlaceholderText(tr("Select a verified file to view its source (read only)."));
    switch (m_status) {
    case Status::NotImported: m_statusLabel->setText(tr("No verified import.")); break;
    case Status::Verified: m_statusLabel->setText(tr("Verified import. Source bytes are read only and are never sent to AI.")); break;
    case Status::Failed: m_statusLabel->setText(tr("Import verification or operation failed: %1").arg(m_error)); break;
    case Status::WriteBlocked: m_statusLabel->setText(tr("Apply inspector changes first. Import writes also require generation, candidate review, and build to be idle.")); break;
    case Status::Invalid: m_statusLabel->setText(tr("The project context changed. Open external code management again.")); break;
    }
}

void ExternalCodeDialog::changeEvent(QEvent *event)
{
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslateUi();
}
