#pragma once

#include <QMainWindow>
#include <QHash>
#include <QPair>
#include <QPointF>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVector>

#include "blueprint/blueprint_document.h"
#include "app/generation_controller.h"
#include "ui/theme.h"
#include <QPointer>
#include <QJsonArray>
#include <QJsonObject>
#include <functional>
class BlueprintV2EditDialog;

class BlueprintScene;
class BlueprintV2Reader;
enum class BlueprintV2TransportOutcome;
class BlueprintV2View;
class QStackedWidget;
class BuildService;
class WorkspaceBrowserWidget;
class CandidateReviewDialog;
class ExternalCodeDialog;
class QLabel;
class NodePropertiesEditor;
class QAction;
class QDockWidget;
class QEvent;
class QCloseEvent;
class QFormLayout;
class QGraphicsItem;
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
    // Places a node with its top left corner at scenePosition; the toolbar entry keeps
    // using the view centre through addNodeOfType().
    bool addNodeOfTypeAt(NodeType type, const QPointF &scenePosition);
    const BlueprintDocument &document() const;
    BlueprintScene *scene() const;
    QGraphicsView *graphicsView() const;
    bool newProject();
    bool openProject(const QString &projectDirectory);
    void openV2Project(const QString &directory, const QString &trustedBundlePath = {});
    bool isV2Mode() const { return m_v2Mode; }
    void exitV2Mode();
    void setV2ReaderConfiguration(const QString &nodeExecutable, const QString &entryPoint, int timeoutMs = 15000);
    bool saveProject();
    bool saveProjectAs(const QString &projectDirectory);
    QString projectDirectory() const;
    bool isModified() const;
    QString currentLanguage() const;
    bool setLanguage(const QString &languageCode);
    void setTheme(EditorTheme::Theme theme);
    void setBuildToolConfiguration(const QString &cmakeExecutable,
                                   const QStringList &configureArguments,
                                   const QStringList &buildArguments = {});

signals:
    void v2LoadFinished(bool success);
    void v2PreviewFinished(bool success);
    void v2SaveFinished(bool success);

protected:
    void changeEvent(QEvent *event) override;
    void closeEvent(QCloseEvent *event) override;
    bool eventFilter(QObject *watched, QEvent *event) override;

private:
    void chooseOpenV2Project();
    void updateModePermissions();
    void cancelV2Load();
    void finishV2Operation(const QJsonObject &reply, const QString &error, BlueprintV2TransportOutcome outcome);
    void acceptV2Load(const QJsonObject &reply, const QString &directory, const QJsonObject &registry);
    void editV2Values();
    bool confirmV2Transition(std::function<void()> resume);
    void leaveV2Mode();
    QJsonObject m_v2Registry, m_pendingV2Registry, m_v2Baseline, m_v2Accepted;
    QJsonArray m_v2Edits, m_v2CandidateEdits;
    QString m_v2Operation;
    bool m_v2Uncertain = false;
    QPointer<BlueprintV2EditDialog> m_v2Dialog;
    std::function<void()> m_v2Resume;
    quint64 m_v2InteractionEpoch = 0;
    BlueprintV2Reader *m_v2Reader = nullptr;
    BlueprintV2View *m_v2View = nullptr;
    QStackedWidget *m_canvasStack = nullptr;
    QAction *m_openV2Action = nullptr, *m_exitV2Action = nullptr;
    bool m_v2Mode = false;
    bool m_legacyPropertiesVisible = true, m_legacyBuildVisible = true;
    QString m_v2Directory, m_pendingV2Directory;
    quint64 m_v2LoadSerial = 0;
    bool applyLanguage(const QString &languageCode, bool persist);
    void applyTheme(EditorTheme::Theme theme, bool persist);
    void retranslateUi();
    void updateLanguageActions();
    void updateThemeActions();
    void showCanvasContextMenu(QGraphicsItem *target, const QPoint &screenPosition,
                               const QPointF &scenePosition);
    void cancelConnection();
    void editNodeFromCanvas(const QString &nodeId);
    void applyProperties();
    void updatePropertyEditor();
    void applyWorkspacePathEdit();
    void browseWorkspace();
    void finishAbortedWorkspaceOperationClick();
    bool prepareWorkspaceOperation();
    void startBuild();
    void exportProject();
    void resetWindowLayout();
    void appendBuildLog(const QString &text);
    QString selectedNodeId() const;
    void startGeneration(ProjectScaffolder::Mode mode);
    void updateGenerationUi();
    void showValidationDiagnostics();
    void invalidateGenerationContext();
    void markProjectSaved();
    void updateProjectTitle();
    bool confirmProjectTransition();
    bool chooseSaveProjectDirectory();
    void chooseOpenProjectDirectory();
    void resetProjectGenerationContext();
    void showExternalCodeManagement();
    void invalidateExternalCodeContext();

    BlueprintDocument m_document;
    BlueprintDocument m_savedDocument;
    QHash<QString, QPointF> m_savedLayout;
    QString m_projectDirectory;
    bool m_replacingProject = false;
    QMenu *m_fileMenu = nullptr;
    QAction *m_newProjectAction = nullptr;
    QAction *m_openProjectAction = nullptr;
    QAction *m_saveProjectAction = nullptr;
    QAction *m_saveProjectAsAction = nullptr;
    BlueprintDocument m_generationSnapshot;
    GenerationController *m_generationController = nullptr;
    QPointer<CandidateReviewDialog> m_reviewDialog;
    QPointer<ExternalCodeDialog> m_externalCodeDialog;
    quint64 m_projectEpoch = 0;
    QAction *m_generateAction = nullptr;
    QAction *m_generateLegacyAction = nullptr;
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
    QAction *m_selectAllAction = nullptr;
    QAction *m_fitViewAction = nullptr;
    QAction *m_resetViewAction = nullptr;
    QMenu *m_aiMenu = nullptr;
    QAction *m_aiSettingsAction = nullptr;
    QAction *m_propertiesDockAction = nullptr;
    QAction *m_buildDockAction = nullptr;
    QAction *m_workspaceDockAction = nullptr;
    QAction *m_resetLayoutAction = nullptr;
    QMenu *m_languageMenu = nullptr;
    QAction *m_englishLanguageAction = nullptr;
    QAction *m_chineseLanguageAction = nullptr;
    QMenu *m_themeMenu = nullptr;
    QAction *m_lightThemeAction = nullptr;
    QAction *m_darkThemeAction = nullptr;
    EditorTheme::Theme m_theme = EditorTheme::Theme::Light;
    QDockWidget *m_propertiesDock = nullptr;
    NodePropertiesEditor *m_nodePropertiesEditor = nullptr;
    QPushButton *m_applyPropertiesButton = nullptr;
    QPushButton *m_manageExternalCodeButton = nullptr;
    BuildService *m_buildService = nullptr;
    QDockWidget *m_buildDock = nullptr;
    QDockWidget *m_workspaceDock = nullptr;
    WorkspaceBrowserWidget *m_workspaceBrowser = nullptr;
    QFormLayout *m_buildForm = nullptr;
    QLineEdit *m_workspacePathEdit = nullptr;
    QWidget *m_workspacePathRow = nullptr;
    QPushButton *m_browseWorkspaceButton = nullptr;
    bool m_workspaceOperationMousePressPending = false;
    bool m_workspaceOperationPreflightActive = false;
    bool m_deferredWorkspaceEdit = false;
    QLineEdit *m_buildDirectoryEdit = nullptr;
    QLineEdit *m_exportTargetEdit = nullptr;
    QLineEdit *m_cmakeExecutableEdit = nullptr;
    QLineEdit *m_configureArgumentsEdit = nullptr;
    QPushButton *m_buildProjectButton = nullptr;
    QPushButton *m_exportProjectButton = nullptr;
    QPlainTextEdit *m_buildLog = nullptr;
    QStringList m_buildArguments;
};
