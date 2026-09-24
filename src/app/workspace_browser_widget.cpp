#include "app/workspace_browser_widget.h"
#include "ui/theme.h"
#include <QDir>
#include <QDirIterator>
#include <QEvent>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSplitter>
#include <QStandardItemModel>
#include <QStringDecoder>
#include <QTreeView>
#include <QVBoxLayout>

WorkspaceBrowserWidget::WorkspaceBrowserWidget(QWidget *parent) : QWidget(parent)
{
    setObjectName("workspaceBrowser");
    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(EditorTheme::SpaceMedium, EditorTheme::SpaceMedium, EditorTheme::SpaceMedium, EditorTheme::SpaceMedium);
    layout->setSpacing(EditorTheme::SpaceMedium);
    m_refresh = new QPushButton(this); m_refresh->setObjectName("workspaceRefreshButton");
    layout->addWidget(m_refresh, 0, Qt::AlignLeft);
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
    connect(m_tree, &QTreeView::doubleClicked, this, [this](const QModelIndex &index) {
        if (!index.data(Qt::UserRole + 2).toBool()) openFile(index.data(Qt::UserRole + 1).toString());
    });
    retranslateUi();
}
void WorkspaceBrowserWidget::setWorkspacePath(const QString &workspace)
{
    if (workspace == m_workspace) return;
    m_workspace = workspace; refresh();
}
void WorkspaceBrowserWidget::refresh()
{
    m_model->clear(); m_preview->clear(); m_currentPath.clear(); m_root.clear(); m_manifest = {};
    m_status = Status::Missing;
    if (WorkspaceFilePolicy::rootPath(m_workspace, m_root)) {
        m_manifest = WorkspaceFilePolicy::manifest(m_workspace);
        int remaining = 2000;
        populate(m_model->invisibleRootItem(), {}, 0, remaining);
        m_status = remaining == 0 ? Status::Limited : m_model->rowCount() ? Status::Ready : Status::EmptyProject;
    }
    retranslateUi();
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
    m_preview->clear(); m_currentPath = relative; m_status = Status::Unsafe;
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
                            m_preview->setPlainText(text); m_status = text.isEmpty() ? Status::EmptyFile : Status::Selected;
                        }
                    }
                }
            }
        }
    }
    retranslateUi();
}
void WorkspaceBrowserWidget::changeEvent(QEvent *event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) retranslateUi();
}
void WorkspaceBrowserWidget::retranslateUi()
{
    m_refresh->setText(tr("Refresh"));
    m_path->setText(m_currentPath.isEmpty() ? tr("No file selected") : m_currentPath);
    QString status;
    switch (m_status) {
    case Status::Missing: status = tr("No generated-project directory. Set an existing workspace root in Build and export."); break;
    case Status::EmptyProject: status = tr("The generated-project directory is empty."); break;
    case Status::Ready: status = tr("Double-click a file to preview it. All files are read-only."); break;
    case Status::Limited: status = tr("The file tree limit was reached (2,000 entries / 32 levels)."); break;
    case Status::Unsafe: status = tr("Cannot open file: missing file or unsafe workspace path. Refresh the file tree."); break;
    case Status::ReadError: status = tr("Cannot read this file."); break;
    case Status::Unsupported: status = tr("Preview is not supported for this file type or encoding. UTF-8 text is required."); break;
    case Status::TooLarge: status = tr("File is too large to preview (limit: 1 MiB)."); break;
    case Status::EmptyFile: status = tr("Empty file. Read-only."); break;
    case Status::Selected:
        switch (m_kind) {
        case WorkspaceFilePolicy::Kind::OrdinaryFutureEditable: status = tr("Read-only preview — ordinary project file."); break;
        case WorkspaceFilePolicy::Kind::ProtectedScaffold: status = tr("Read-only preview — protected scaffold / contract."); break;
        case WorkspaceFilePolicy::Kind::ExternalProtected: status = tr("Read-only preview — protected external code."); break;
        default: status = tr("Read-only preview — protection metadata is unavailable."); break;
        }
        break;
    }
    m_statusLabel->setText(status);
}
