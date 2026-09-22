#pragma once

#include <QMainWindow>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVector>

#include "blueprint/blueprint_document.h"
#include "app/generation_controller.h"
#include <QPointer>

class BlueprintScene;
class BuildService;
class CandidateReviewDialog;
class QLabel;
class NodePropertiesEditor;
class QAction;
class QDockWidget;
class QEvent;
class QFormLayout;
class QGraphicsView;
class QLineEdit;
class QMenu;
class QPlainTextEdit;
class QPushButton;
class QToolBar;
class QToolButton;

class MainWindow final : public QMainWindow
{
    Q_OBJECT

public:
    explicit MainWindow(QWidget *parent = nullptr);
    explicit MainWindow(GenerationController::ClientFactory factory, QWidget *parent = nullptr);
    ~MainWindow() override;

    bool addNodeOfType(NodeType type);
    const BlueprintDocument &document() const;
    BlueprintScene *scene() const;
    QGraphicsView *graphicsView() const;
    QString currentLanguage() const;
    bool setLanguage(const QString &languageCode);
    void setBuildToolConfiguration(const QString &cmakeExecutable,
                                   const QStringList &configureArguments,
                                   const QStringList &buildArguments = {});

protected:
    void changeEvent(QEvent *event) override;

private:
    bool applyLanguage(const QString &languageCode, bool persist);
    void retranslateUi();
    void updateLanguageActions();
    void cancelConnection();
    void editNodeFromCanvas(const QString &nodeId);
    void applyProperties();
    void updatePropertyEditor();
    void startBuild();
    void exportProject();
    void resetWindowLayout();
    void appendBuildLog(const QString &text);
    QString selectedNodeId() const;
    void startGeneration();
    void updateGenerationUi();
    void showValidationDiagnostics();
    void invalidateGenerationContext();

    BlueprintDocument m_document;
    BlueprintDocument m_generationSnapshot;
    GenerationController *m_generationController = nullptr;
    QPointer<CandidateReviewDialog> m_reviewDialog;
    QAction *m_generateAction = nullptr;
    QAction *m_cancelGenerationAction = nullptr;
    QLabel *m_generationStatus = nullptr;
    QString m_generationNodeId;
    QTranslator m_translator;
    QString m_currentLanguage = QStringLiteral("en");
    BlueprintScene *m_scene = nullptr;
    QGraphicsView *m_view = nullptr;
    QToolBar *m_blueprintToolbar = nullptr;
    QToolButton *m_addNodeButton = nullptr;
    QMenu *m_addNodeMenu = nullptr;
    QVector<QPair<NodeType, QAction *>> m_addNodeActions;
    QLineEdit *m_connectionLabelEdit = nullptr;
    QAction *m_deleteAction = nullptr;
    QAction *m_connectAction = nullptr;
    QAction *m_cancelConnectionAction = nullptr;
    QAction *m_toolbarUndoAction = nullptr;
    QAction *m_toolbarRedoAction = nullptr;
    QAction *m_toolbarBuildAction = nullptr;
    QAction *m_toolbarExportAction = nullptr;
    QMenu *m_editMenu = nullptr;
    QAction *m_menuUndoAction = nullptr;
    QAction *m_menuRedoAction = nullptr;
    QMenu *m_viewMenu = nullptr;
    QMenu *m_aiMenu = nullptr;
    QAction *m_aiSettingsAction = nullptr;
    QAction *m_propertiesDockAction = nullptr;
    QAction *m_buildDockAction = nullptr;
    QAction *m_resetLayoutAction = nullptr;
    QMenu *m_languageMenu = nullptr;
    QAction *m_englishLanguageAction = nullptr;
    QAction *m_chineseLanguageAction = nullptr;
    QDockWidget *m_propertiesDock = nullptr;
    NodePropertiesEditor *m_nodePropertiesEditor = nullptr;
    QPushButton *m_applyPropertiesButton = nullptr;
    BuildService *m_buildService = nullptr;
    QDockWidget *m_buildDock = nullptr;
    QFormLayout *m_buildForm = nullptr;
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
