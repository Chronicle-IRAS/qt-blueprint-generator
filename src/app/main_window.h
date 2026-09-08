#pragma once

#include <QMainWindow>
#include <QString>
#include <QStringList>

#include "blueprint/blueprint_document.h"

class BlueprintScene;
class BuildService;
class QGraphicsView;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class MainWindow final : public QMainWindow
{
public:
    explicit MainWindow(QWidget *parent = nullptr);
    ~MainWindow() override;

    bool addNodeOfType(NodeType type);
    const BlueprintDocument &document() const;
    BlueprintScene *scene() const;
    QGraphicsView *graphicsView() const;
    void setBuildToolConfiguration(const QString &cmakeExecutable,
                                   const QStringList &configureArguments,
                                   const QStringList &buildArguments = {});

private:
    void deleteSelection();
    void cancelConnection();
    void applyProperties();
    void updatePropertyEditor();
    void startBuild();
    void exportProject();
    void appendBuildLog(const QString &text);
    QString selectedNodeId() const;

    BlueprintDocument m_document;
    BlueprintScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QLineEdit *m_connectionLabelEdit = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QPlainTextEdit *m_inputsEdit = nullptr;
    QPlainTextEdit *m_outputsEdit = nullptr;
    QPlainTextEdit *m_constraintsEdit = nullptr;
    QPlainTextEdit *m_acceptanceCriteriaEdit = nullptr;
    QPushButton *m_applyPropertiesButton = nullptr;
    BuildService *m_buildService = nullptr;
    QLineEdit *m_workspacePathEdit = nullptr;
    QLineEdit *m_buildDirectoryEdit = nullptr;
    QLineEdit *m_exportTargetEdit = nullptr;
    QLineEdit *m_cmakeExecutableEdit = nullptr;
    QLineEdit *m_configureArgumentsEdit = nullptr;
    QPushButton *m_buildProjectButton = nullptr;
    QPushButton *m_exportProjectButton = nullptr;
    QPlainTextEdit *m_buildLog = nullptr;
    QStringList m_buildArguments;
};
