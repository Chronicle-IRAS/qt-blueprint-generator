#pragma once

#include <QMainWindow>
#include <QPair>
#include <QString>
#include <QStringList>
#include <QTranslator>
#include <QVector>

#include "blueprint/blueprint_document.h"

class BlueprintScene;
class BuildService;
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
    void retranslateUi();
    void updateLanguageActions();
    void deleteSelection();
    void cancelConnection();
    void applyProperties();
    void updatePropertyEditor();
    void startBuild();
    void exportProject();
    void appendBuildLog(const QString &text);
    QString selectedNodeId() const;

    BlueprintDocument m_document;
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
    QMenu *m_editMenu = nullptr;
    QAction *m_menuUndoAction = nullptr;
    QAction *m_menuRedoAction = nullptr;
    QMenu *m_languageMenu = nullptr;
    QAction *m_englishLanguageAction = nullptr;
    QAction *m_chineseLanguageAction = nullptr;
    QDockWidget *m_propertiesDock = nullptr;
    QFormLayout *m_propertyForm = nullptr;
    QLineEdit *m_nameEdit = nullptr;
    QPlainTextEdit *m_descriptionEdit = nullptr;
    QPlainTextEdit *m_inputsEdit = nullptr;
    QPlainTextEdit *m_outputsEdit = nullptr;
    QPlainTextEdit *m_constraintsEdit = nullptr;
    QPlainTextEdit *m_acceptanceCriteriaEdit = nullptr;
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
