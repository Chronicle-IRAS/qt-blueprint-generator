#pragma once
#include <QWidget>
#include <QJsonObject>
#include <QHash>
#include <QStringList>
#include "workspace/workspace_file_policy.h"
class QLabel;
class QPushButton;
class QTreeView;
class QPlainTextEdit;
class QStandardItemModel;
class QStandardItem;

class WorkspaceBrowserWidget final : public QWidget
{
    Q_OBJECT
public:
    explicit WorkspaceBrowserWidget(QWidget *parent = nullptr);
    bool setWorkspacePath(const QString &workspace);
    bool refresh();
    void openFile(const QString &relative);
    bool saveCurrentFile();
    bool requestCanDiscardChanges();
    bool hasUnsavedChanges() const;
    QString workspacePath() const;
protected:
    void changeEvent(QEvent *event) override;
private:
    enum class Status { Missing, EmptyProject, Ready, Limited, Selected, EmptyFile, Unsafe, ReadError, Unsupported, TooLarge, SaveFailed, ReloadFailed, DiskChanged };
    void populate(QStandardItem *parent, const QString &relative, int depth, int &remaining);
    void retranslateUi();
    void trackTextChange();
    void discardChanges();
    bool reloadCurrentFile();
    QString m_workspace, m_root, m_currentPath;
    QString m_cleanText;
    QString m_saveError;
    QByteArray m_diskHash;
    QString m_rootIdentity, m_fileIdentity;
    bool m_hadBom = false;
    QStringList m_lineEndings;
    QStringList m_cleanLineEndings;
    QString m_observedText;
    struct RemovedLineEndings { qsizetype index = 0; QStringList styles; };
    QHash<QByteArray, RemovedLineEndings> m_removedLineEndings;
    bool m_trackingEdits = false;
    QJsonObject m_manifest;
    WorkspaceFilePolicy::Kind m_kind = WorkspaceFilePolicy::Kind::UnknownProtected;
    Status m_status = Status::Missing;
    QTreeView *m_tree;
    QStandardItemModel *m_model;
    QPlainTextEdit *m_preview;
    QLabel *m_path, *m_statusLabel;
    QPushButton *m_refresh, *m_save;
};
