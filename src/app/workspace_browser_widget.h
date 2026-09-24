#pragma once
#include <QWidget>
#include <QJsonObject>
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
    void setWorkspacePath(const QString &workspace);
    void refresh();
    void openFile(const QString &relative);
protected:
    void changeEvent(QEvent *event) override;
private:
    enum class Status { Missing, EmptyProject, Ready, Limited, Selected, EmptyFile, Unsafe, ReadError, Unsupported, TooLarge };
    void populate(QStandardItem *parent, const QString &relative, int depth, int &remaining);
    void retranslateUi();
    QString m_workspace, m_root, m_currentPath;
    QJsonObject m_manifest;
    WorkspaceFilePolicy::Kind m_kind = WorkspaceFilePolicy::Kind::UnknownProtected;
    Status m_status = Status::Missing;
    QTreeView *m_tree;
    QStandardItemModel *m_model;
    QPlainTextEdit *m_preview;
    QLabel *m_path, *m_statusLabel;
    QPushButton *m_refresh;
};
