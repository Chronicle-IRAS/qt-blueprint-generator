#include "app/workspace_browser_widget.h"
#include "ui/theme.h"
#include <QDir>
#include <QDirIterator>
#include <QCryptographicHash>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSaveFile>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStringDecoder>
#include <QTextDocument>
#include <QTreeView>
#include <QVBoxLayout>

WorkspaceBrowserWidget::WorkspaceBrowserWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName("workspaceBrowser");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(EditorTheme::SpaceMedium, EditorTheme::SpaceMedium, EditorTheme::SpaceMedium, EditorTheme::SpaceMedium);
    layout->setSpacing(EditorTheme::SpaceMedium);
    auto *actions = new QHBoxLayout;
    m_refresh = new QPushButton(this); m_refresh->setObjectName("workspaceRefreshButton");
    m_save = new QPushButton(this); m_save->setObjectName("workspaceSaveButton");
    actions->addWidget(m_refresh); actions->addWidget(m_save); actions->addStretch();
    layout->addLayout(actions);
    auto *splitter = new QSplitter(this);
    m_tree = new QTreeView(splitter); m_tree->setObjectName("workspaceFileTree");
    m_model = new QStandardItemModel(this); m_tree->setModel(m_model); m_tree->setHeaderHidden(true);
    m_tree->setEditTriggers(QAbstractItemView::NoEditTriggers);
    auto *previewArea = new QWidget(splitter);
    auto *previewLayout = new QVBoxLayout(previewArea); previewLayout->setContentsMargins(0, 0, 0, 0);
    m_path = new QLabel(previewArea); m_path->setObjectName("workspaceCurrentPath"); m_path->setTextFormat(Qt::PlainText);
    m_path->setTextInteractionFlags(Qt::TextSelectableByMouse); m_path->setWordWrap(true);
    m_preview = new QPlainTextEdit(previewArea); m_preview->setObjectName("workspacePreview");
    m_preview->setReadOnly(true); m_preview->setFont(EditorTheme::codeFont());
    previewLayout->addWidget(m_path); previewLayout->addWidget(m_preview);
    splitter->setStretchFactor(1, 1); layout->addWidget(splitter, 1);
    m_statusLabel = new QLabel(this); m_statusLabel->setObjectName("workspaceBrowserStatus");
    m_statusLabel->setWordWrap(true); m_statusLabel->setTextFormat(Qt::PlainText); layout->addWidget(m_statusLabel);
    connect(m_refresh, &QPushButton::clicked, this, &WorkspaceBrowserWidget::refresh);
    connect(m_save, &QPushButton::clicked, this, &WorkspaceBrowserWidget::saveCurrentFile);
    connect(m_preview->document(), &QTextDocument::modificationChanged, this, [this] { retranslateUi(); });
    connect(m_preview, &QPlainTextEdit::textChanged, this, &WorkspaceBrowserWidget::trackTextChange);
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.data(Qt::UserRole + 2).toBool()) openFile(index.data(Qt::UserRole + 1).toString());
    });
    retranslateUi();
}
bool WorkspaceBrowserWidget::setWorkspacePath(const QString &workspace)
{
    if (workspace == m_workspace) return true;
    if (!requestCanDiscardChanges()) return false;
    m_workspace = workspace;
    return refresh();
}
bool WorkspaceBrowserWidget::refresh()
{
    if (!requestCanDiscardChanges()) return false;
    m_trackingEdits = false;
    m_model->clear(); m_preview->setReadOnly(true); m_preview->clear(); m_preview->document()->setModified(false);
    m_currentPath.clear(); m_root.clear(); m_manifest = {}; m_diskHash.clear(); m_cleanText.clear();
    m_rootIdentity.clear(); m_fileIdentity.clear();
    m_lineEndings.clear(); m_cleanLineEndings.clear(); m_observedText.clear(); m_saveError.clear();
    m_removedLineEndings.clear();
    m_status = Status::Missing;
    if (WorkspaceFilePolicy::rootPath(m_workspace, m_root)) {
        m_rootIdentity = WorkspaceFilePolicy::entryIdentity(m_root);
        m_manifest = WorkspaceFilePolicy::manifest(m_workspace);
        int remaining = 2000;
        populate(m_model->invisibleRootItem(), {}, 0, remaining);
        m_status = remaining == 0 ? Status::Limited : m_model->rowCount() ? Status::Ready : Status::EmptyProject;
    }
    retranslateUi();
    return true;
}
void WorkspaceBrowserWidget::populate(QStandardItem *parent, const QString &relative, int depth, int &remaining)
{
    if (depth >= 32) { remaining = 0; return; }
    QDirIterator entries(QDir(m_root).filePath(relative), QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot);
    while (remaining > 0 && entries.hasNext()) {
        entries.next(); --remaining;
        const QString path = relative.isEmpty() ? entries.fileName() : relative + '/' + entries.fileName();
        QString absolute;
        if (WorkspaceFilePolicy::internal(path) || !WorkspaceFilePolicy::resolve(m_workspace, m_root, path, absolute)) continue;
        const QFileInfo info(absolute);
        if (!info.isDir() && !info.isFile()) continue;
        auto *item = new QStandardItem(info.fileName()); item->setEditable(false);
        item->setData(path, Qt::UserRole + 1); item->setData(info.isDir(), Qt::UserRole + 2); parent->appendRow(item);
        if (info.isDir()) populate(item, path, depth + 1, remaining);
    }
    parent->sortChildren(0);
}
void WorkspaceBrowserWidget::openFile(const QString &relative)
{
    if (relative == m_currentPath && (m_status == Status::Selected || m_status == Status::EmptyFile)) return;
    if (!requestCanDiscardChanges()) return;
    m_trackingEdits = false;
    m_preview->setReadOnly(true); m_preview->clear(); m_preview->document()->setModified(false);
    m_currentPath = relative; m_cleanText.clear(); m_diskHash.clear(); m_hadBom = false;
    m_fileIdentity.clear();
    m_lineEndings.clear(); m_cleanLineEndings.clear(); m_observedText.clear();
    m_removedLineEndings.clear();
    m_saveError.clear(); m_status = Status::Unsafe; m_kind = WorkspaceFilePolicy::Kind::UnknownProtected;
    QString absolute;
    if (!WorkspaceFilePolicy::internal(relative) && WorkspaceFilePolicy::resolve(m_workspace, m_root, relative, absolute)) {
        m_kind = WorkspaceFilePolicy::classify(relative, m_manifest);
        const QFileInfo info(absolute);
        const QString suffix = info.suffix().toLower();
        const QStringList supported = {"cpp", "cc", "c", "h", "hpp", "cmake", "txt", "json", "md"};
        m_status = Status::Unsupported;
        if (supported.contains(suffix)) {
            QFile file(absolute); m_status = Status::ReadError;
            if (info.isFile() && file.open(QIODevice::ReadOnly)) {
                const QByteArray bytes = file.read(WorkspaceFilePolicy::PreviewLimit + 1);
                if (file.error() == QFileDevice::NoError) {
                    m_status = Status::TooLarge;
                    if (bytes.size() <= WorkspaceFilePolicy::PreviewLimit) {
                        QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
                        const QString text = decoder.decode(bytes);
                        int controls = 0;
                        for (auto c : text) if ((c.unicode() < 32 && c != '\n' && c != '\r' && c != '\t') || c.unicode() == 127) ++controls;
                        m_status = Status::Unsupported;
                        if (!bytes.contains('\0') && !decoder.hasError() && controls == 0) {
                            m_preview->setPlainText(text);
                            m_preview->document()->setModified(false);
                            m_cleanText = m_preview->toPlainText();
                            m_diskHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
                            m_fileIdentity = WorkspaceFilePolicy::entryIdentity(absolute);
                            m_hadBom = bytes.startsWith(QByteArray::fromHex("efbbbf"));
                            for (qsizetype i = 0; i < text.size(); ++i) {
                                if (text.at(i) == '\r') {
                                    if (i + 1 < text.size() && text.at(i + 1) == '\n') {
                                        m_lineEndings.append("\r\n"); ++i;
                                    } else m_lineEndings.append("\r");
                                } else if (text.at(i) == '\n') m_lineEndings.append("\n");
                            }
                            m_cleanLineEndings = m_lineEndings;
                            m_observedText = m_preview->toPlainText();
                            m_trackingEdits = true;
                            m_preview->setReadOnly(m_kind != WorkspaceFilePolicy::Kind::OrdinaryFutureEditable);
                            m_status = text.isEmpty() ? Status::EmptyFile : Status::Selected;
                        }
                    }
                }
            }
        }
    }
    retranslateUi();
}
bool WorkspaceBrowserWidget::hasUnsavedChanges() const
{
    return !m_currentPath.isEmpty() && m_preview->document()->isModified();
}
QString WorkspaceBrowserWidget::workspacePath() const { return m_workspace; }
void WorkspaceBrowserWidget::trackTextChange()
{
    if (!m_trackingEdits) return;
    const QString current = m_preview->toPlainText();
    qsizetype prefix = 0;
    while (prefix < qMin(current.size(), m_observedText.size())
           && current.at(prefix) == m_observedText.at(prefix)) ++prefix;
    qsizetype suffix = 0;
    while (suffix < qMin(current.size(), m_observedText.size()) - prefix
           && current.at(current.size() - 1 - suffix)
                  == m_observedText.at(m_observedText.size() - 1 - suffix)) ++suffix;
    const qsizetype index = m_observedText.left(prefix).count('\n');
    const qsizetype removed = m_observedText.mid(prefix, m_observedText.size() - prefix - suffix).count('\n');
    const qsizetype added = current.mid(prefix, current.size() - prefix - suffix).count('\n');
    QStringList removedStyles;
    for (qsizetype i = 0; i < removed; ++i) removedStyles.append(m_lineEndings.at(index + i));
    QString style = QStringLiteral("\n");
    if (!m_lineEndings.isEmpty())
        style = m_lineEndings.at(qMin(index, m_lineEndings.size() - 1));
    for (qsizetype i = 0; i < removed; ++i) m_lineEndings.removeAt(index);
    for (qsizetype i = 0; i < added; ++i) m_lineEndings.insert(index + i, style);
    if (added) {
        const auto it = m_removedLineEndings.constFind(QCryptographicHash::hash(m_observedText.toUtf8(), QCryptographicHash::Sha256));
        if (it != m_removedLineEndings.cend() && it->index == index && it->styles.size() == added)
            for (qsizetype i = 0; i < added; ++i) m_lineEndings[index + i] = it->styles.at(i);
    }
    if (current == m_cleanText) m_lineEndings = m_cleanLineEndings;
    if (removed) m_removedLineEndings.insert(QCryptographicHash::hash(current.toUtf8(), QCryptographicHash::Sha256), {index, removedStyles});
    m_observedText = current;
}
void WorkspaceBrowserWidget::discardChanges()
{
    m_trackingEdits = false;
    m_preview->setPlainText(m_cleanText);
    m_lineEndings = m_cleanLineEndings;
    m_observedText = m_cleanText;
    m_removedLineEndings.clear();
    m_trackingEdits = true;
    m_preview->document()->setModified(false);
    m_status = m_cleanText.isEmpty() ? Status::EmptyFile : Status::Selected;
    retranslateUi();
}
bool WorkspaceBrowserWidget::requestCanDiscardChanges()
{
    if (!hasUnsavedChanges()) return true;
    QMessageBox box(QMessageBox::Question, tr("Unsaved changes"),
                    tr("Save changes to %1?").arg(m_currentPath), QMessageBox::NoButton, this);
    auto *save = box.addButton(tr("Save"), QMessageBox::AcceptRole);
    auto *discard = box.addButton(tr("Discard"), QMessageBox::DestructiveRole);
    box.addButton(tr("Cancel"), QMessageBox::RejectRole);
    box.exec();
    if (box.clickedButton() == save) return saveCurrentFile();
    if (box.clickedButton() == discard) { discardChanges(); return true; }
    return false;
}
bool WorkspaceBrowserWidget::reloadCurrentFile()
{
    const QString relative = m_currentPath;
    const QString localText = m_preview->toPlainText();
    const QString cleanText = m_cleanText;
    const QStringList lineEndings = m_lineEndings;
    const QStringList cleanLineEndings = m_cleanLineEndings;
    const auto removedLineEndings = m_removedLineEndings;
    const QByteArray diskHash = m_diskHash;
    const QString fileIdentity = m_fileIdentity;
    const bool hadBom = m_hadBom;
    const auto kind = m_kind;
    m_preview->document()->setModified(false);
    m_currentPath.clear();
    openFile(relative);
    if (m_status == Status::Selected || m_status == Status::EmptyFile) return true;
    m_trackingEdits = false;
    m_preview->setPlainText(localText);
    m_currentPath = relative;
    m_cleanText = cleanText;
    m_lineEndings = lineEndings;
    m_cleanLineEndings = cleanLineEndings;
    m_observedText = localText;
    m_removedLineEndings = removedLineEndings;
    m_diskHash = diskHash;
    m_fileIdentity = fileIdentity;
    m_hadBom = hadBom;
    m_kind = kind;
    m_preview->setReadOnly(false);
    m_preview->document()->setModified(true);
    m_trackingEdits = true;
    m_status = Status::ReloadFailed;
    retranslateUi();
    return false;
}
bool WorkspaceBrowserWidget::saveCurrentFile()
{
    if (m_kind != WorkspaceFilePolicy::Kind::OrdinaryFutureEditable || !hasUnsavedChanges()) return false;
    QString absolute;
    const auto currentManifest = WorkspaceFilePolicy::manifest(m_workspace);
    if (!WorkspaceFilePolicy::resolve(m_workspace, m_root, m_currentPath, absolute)
        || WorkspaceFilePolicy::classify(m_currentPath, currentManifest) != WorkspaceFilePolicy::Kind::OrdinaryFutureEditable
        || !QFileInfo(absolute).isFile()
        || m_rootIdentity.isEmpty() || WorkspaceFilePolicy::entryIdentity(m_root) != m_rootIdentity
        || m_fileIdentity.isEmpty() || WorkspaceFilePolicy::entryIdentity(absolute) != m_fileIdentity) {
        m_status = Status::SaveFailed; m_saveError = tr("The file or its protection status changed."); retranslateUi(); return false;
    }
    QFile source(absolute);
    if (!source.open(QIODevice::ReadOnly)) {
        m_status = Status::SaveFailed; m_saveError = source.errorString(); retranslateUi(); return false;
    }
    const QByteArray current = source.read(WorkspaceFilePolicy::PreviewLimit + 1);
    if (source.error() != QFileDevice::NoError || current.size() > WorkspaceFilePolicy::PreviewLimit) {
        m_status = Status::SaveFailed; m_saveError = tr("Cannot verify the current disk file."); retranslateUi(); return false;
    }
    source.close();
    if (QCryptographicHash::hash(current, QCryptographicHash::Sha256) != m_diskHash) {
        m_status = Status::DiskChanged; retranslateUi();
        QMessageBox box(QMessageBox::Warning, tr("File changed on disk"),
                        tr("File changed on disk. Reload and discard your changes?"), QMessageBox::NoButton, this);
        auto *reload = box.addButton(tr("Reload"), QMessageBox::AcceptRole);
        box.addButton(tr("Cancel"), QMessageBox::RejectRole);
        box.exec();
        if (box.clickedButton() == reload) reloadCurrentFile();
        return false;
    }
    const QString currentText = m_preview->toPlainText();
    QString text;
    text.reserve(currentText.size() + m_lineEndings.size());
    qsizetype newlineOrdinal = 0;
    for (qsizetype i = 0; i < currentText.size(); ++i) {
        if (currentText.at(i) != '\n') { text += currentText.at(i); continue; }
        text += m_lineEndings.value(newlineOrdinal, QStringLiteral("\n"));
        ++newlineOrdinal;
    }
    QByteArray bytes = text.toUtf8();
    if (m_hadBom) bytes.prepend(QByteArray::fromHex("efbbbf"));
    QSaveFile target(absolute);
    if (!target.open(QIODevice::WriteOnly) || target.write(bytes) != bytes.size() || !target.commit()) {
        m_status = Status::SaveFailed; m_saveError = target.errorString(); retranslateUi(); return false;
    }
    m_diskHash = QCryptographicHash::hash(bytes, QCryptographicHash::Sha256);
    m_fileIdentity = WorkspaceFilePolicy::entryIdentity(absolute);
    m_cleanText = m_preview->toPlainText();
    m_cleanLineEndings = m_lineEndings;
    m_preview->document()->setModified(false);
    m_status = m_cleanText.isEmpty() ? Status::EmptyFile : Status::Selected;
    retranslateUi();
    return true;
}
void WorkspaceBrowserWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslateUi();
}
void WorkspaceBrowserWidget::retranslateUi()
{
    m_refresh->setText(tr("Refresh"));
    m_save->setText(tr("Save"));
    m_save->setEnabled(m_kind == WorkspaceFilePolicy::Kind::OrdinaryFutureEditable && hasUnsavedChanges());
    m_path->setText(m_currentPath.isEmpty() ? tr("No file selected") : m_currentPath + (hasUnsavedChanges() ? " *" : ""));
    QString status;
    switch (m_status) {
    case Status::Missing: status = tr("No generated-project directory. Set an existing workspace root in Build and export."); break;
    case Status::EmptyProject: status = tr("The generated-project directory is empty."); break;
    case Status::Ready: status = tr("Double-click a file to open it."); break;
    case Status::Limited: status = tr("The file tree limit was reached (2,000 entries / 32 levels)."); break;
    case Status::Unsafe: status = tr("Cannot open file: missing file or unsafe workspace path. Refresh the file tree."); break;
    case Status::ReadError: status = tr("Cannot read this file."); break;
    case Status::Unsupported: status = tr("Preview is not supported for this file type or encoding. UTF-8 text is required."); break;
    case Status::TooLarge: status = tr("File is too large to preview (limit: 1 MiB)."); break;
    case Status::EmptyFile: status = m_kind == WorkspaceFilePolicy::Kind::OrdinaryFutureEditable ? tr("Empty file. Editable.") : tr("Empty file. Read-only."); break;
    case Status::SaveFailed: status = tr("Save failed: %1").arg(m_saveError); break;
    case Status::ReloadFailed: status = tr("Reload failed; local edits were kept."); break;
    case Status::DiskChanged: status = tr("File changed on disk."); break;
    case Status::Selected:
        switch (m_kind) {
        case WorkspaceFilePolicy::Kind::OrdinaryFutureEditable: status = tr("Editable project file."); break;
        case WorkspaceFilePolicy::Kind::ProtectedScaffold: status = tr("Read-only preview — protected scaffold / contract."); break;
        case WorkspaceFilePolicy::Kind::ExternalProtected: status = tr("Read-only preview — protected external code."); break;
        default: status = tr("Read-only preview — protection metadata is unavailable."); break;
        }
        break;
    }
    if (hasUnsavedChanges() && m_status != Status::SaveFailed && m_status != Status::DiskChanged)
        status += tr(" Modified.");
    m_statusLabel->setText(status);
}
