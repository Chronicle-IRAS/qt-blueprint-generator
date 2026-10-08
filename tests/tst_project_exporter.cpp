#include "generation/project_scaffolder.h"
#include "workspace/external_code_importer.h"
#include "workspace/build_service.h"
#include "workspace/project_exporter.h"
#include "workspace/project_exporter_recovery_p.h"
#include "app/main_window.h"
#include "app/workspace_browser_widget.h"

#include <QAction>
#include <QCryptographicHash>
#include <QDirIterator>
#include <QDockWidget>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QtTest>
#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace {
BlueprintDocument blueprint()
{
    BlueprintDocument document;
    document.projectId = QStringLiteral("task9");
    document.projectName = QStringLiteral("Task 9");
    document.target = QStringLiteral("qt6-widgets-cpp17-cmake");
    BlueprintNode start;
    start.id = QStringLiteral("start");
    BlueprintNode page;
    page.id = QStringLiteral("page");
    page.type = NodeType::UiPage;
    page.name = QStringLiteral("Page");
    page.description = QStringLiteral("A page");
    BlueprintNode end;
    end.id = QStringLiteral("end");
    end.type = NodeType::End;
    document.nodes = {start, page, end};
    document.edges = {{QStringLiteral("a"), start.id, page.id, {}},
                      {QStringLiteral("b"), page.id, end.id, {}}};
    return document;
}

BlueprintNode externalNode()
{
    BlueprintNode node;
    node.id = QStringLiteral("vendor");
    node.type = NodeType::ExternalCode;
    node.name = QStringLiteral("Vendor API");
    node.description = QStringLiteral("Imported API contract");
    node.inputs = {{QStringLiteral("key"), QStringLiteral("QString"), QStringLiteral("Lookup key")}};
    node.outputs = {{QStringLiteral("value"), QStringLiteral("QString"), QStringLiteral("Lookup result")}};
    return node;
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QMap<QString, QByteArray> fileHashes(const QString &root)
{
    QMap<QString, QByteArray> hashes;
    QDirIterator iterator(root, QDir::Files | QDir::Hidden | QDir::System,
                          QDirIterator::Subdirectories);
    while (iterator.hasNext()) {
        const QString path = iterator.next();
        const QString relative = QDir(root).relativeFilePath(path).replace('\\', '/');
        hashes.insert(relative, QCryptographicHash::hash(readFile(path), QCryptographicHash::Sha256));
    }
    return hashes;
}


QStringList configureArguments()
{
    return {QStringLiteral("-G"), QString::fromUtf8(TASK9_CMAKE_GENERATOR),
            QStringLiteral("-DCMAKE_MAKE_PROGRAM=") + QString::fromUtf8(TASK9_MAKE_PROGRAM),
            QStringLiteral("-DCMAKE_CXX_COMPILER=") + QString::fromUtf8(TASK9_CXX_COMPILER),
            QStringLiteral("-DQt6_DIR=") + QString::fromUtf8(TASK9_QT_CMAKE_DIR),
            QStringLiteral("-DBUILD_TESTING=ON")};
}

void chooseMessageBox(QMessageBox::ButtonRole role,
                      QMessageBox::ButtonRole next = QMessageBox::InvalidRole)
{
    QTimer::singleShot(0, qApp, [role, next] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) qFatal("Expected a modal message box");
        for (auto *button : box->buttons()) {
            if (box->buttonRole(button) == role) {
                if (next != QMessageBox::InvalidRole) chooseMessageBox(next);
                QTest::mouseClick(button, Qt::LeftButton);
                return;
            }
        }
        qFatal("Expected message box choice was unavailable");
    });
}
}

class ProjectExporterTest final : public QObject
{
    Q_OBJECT

private slots:
    void rejectsNonEmptyTarget();
    void preservesEveryMappedFileHash();
    void exportsVerifiedExternalSources();
    void rejectsMalformedInputsAndMappingCollisions();
    void sourceLinkFailureLeavesTargetAndSiblingDirectoryUntouched();
    void commitFailureRemovesStagingAndLeavesTargetEmpty();
    void failedBackupRestorationRetainsBothRecoveryDirectories();
    void buildServiceStopsAfterFailedConfigure();
    void buildServiceConfiguresAndBuildsGeneratedProject();
    void buildServiceRejectsOwnedCmakeArguments_data();
    void buildServiceRejectsOwnedCmakeArguments();
    void mainWindowReportsBuildInputErrorsAndRestoresButton();
    void mainWindowStreamsFailedConfigureAndPreservesArguments();
    void mainWindowRestoresBuildButtonWhenCmakeCannotStart();
    void mainWindowBuildFailureCanBeEditedSavedAndRebuilt();
    void mainWindowBuildPreparesWorkspace_data();
    void mainWindowBuildPreparesWorkspace();
    void mainWindowBuildBlocksOnSaveFailure_data();
    void mainWindowBuildBlocksOnSaveFailure();
    void mainWindowClickBuildAfterCancelledWorkspaceEdit_data();
    void mainWindowClickBuildAfterCancelledWorkspaceEdit();
    void mainWindowAbortedBuildClickAppliesWorkspaceEdit();
    void mainWindowLostBuildMouseGrabAppliesWorkspaceEdit_data();
    void mainWindowLostBuildMouseGrabAppliesWorkspaceEdit();
    void mainWindowExportClickKeepsPathEditBehavior();
    void mainWindowExportPreparesWorkspace_data();
    void mainWindowExportPreparesWorkspace();
    void mainWindowExportBlocksOnSaveFailure_data();
    void mainWindowExportBlocksOnSaveFailure();
    void mainWindowExportFailurePreservesEditorAndAllowsRetry();
    void mainWindowClickExportAfterCancelledWorkspaceEdit_data();
    void mainWindowClickExportAfterCancelledWorkspaceEdit();
    void mainWindowCancelledExportDoesNotCreateAbsentTarget();
};

void ProjectExporterTest::mainWindowBuildFailureCanBeEditedSavedAndRebuilt()
{
    QTemporaryDir root;
    QVERIFY(root.isValid());
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString build = root.filePath(QStringLiteral("build"));
    const QString source = QDir(workspace).filePath(
        QStringLiteral("generated-project/src/modules/page/implementation/custom.cpp"));
    const QByteArray broken = "#error phase3_intentional_build_failure\n";
    const QByteArray repaired = "int phase3_repaired_value() { return 42; }\n";
    QVERIFY(QDir().mkpath(workspace));
    QString scaffoldError;
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &scaffoldError),
             qPrintable(scaffoldError));
    QVERIFY(writeFile(source, broken));
    const QString toolchain = root.filePath(QStringLiteral("minimal toolchain.cmake"));
    QVERIFY(writeFile(toolchain, QByteArray{}));

    MainWindow window;
    window.setBuildToolConfiguration(QString::fromUtf8(TASK9_CMAKE_COMMAND),
                                     configureArguments() << QStringLiteral("--toolchain") << toolchain,
                                     {QStringLiteral("--parallel"), QStringLiteral("2")});
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *buildEdit = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *editor = window.findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
    auto *currentPath = window.findChild<QLabel *>(QStringLiteral("workspaceCurrentPath"));
    auto *save = window.findChild<QPushButton *>(QStringLiteral("workspaceSaveButton"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *action = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *service = window.findChild<BuildService *>();
    QVERIFY(pathEdit && buildEdit && browser && editor && currentPath && save);
    QVERIFY(button && action && log && service);
    QVERIFY(browser->setWorkspacePath(workspace));
    pathEdit->setText(workspace);
    buildEdit->setText(build);
    browser->openFile(QStringLiteral("src/modules/page/implementation/custom.cpp"));
    QCOMPARE(editor->toPlainText().toUtf8(), broken);
    QVERIFY(!editor->isReadOnly());
    const QString selectedFile = currentPath->text();
    QCOMPARE(selectedFile, QStringLiteral("src/modules/page/implementation/custom.cpp"));
    QSignalSpy finished(service, &BuildService::finished);
    QVERIFY(finished.isValid());

    button->click();
    QVERIFY(!button->isEnabled());
    QVERIFY(!action->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 120000);
    const BuildResult first = qvariant_cast<BuildResult>(finished.at(0).at(0));
    QVERIFY(first.configureStarted);
    QCOMPARE(first.configureExitCode, 0);
    QVERIFY(first.buildStarted);
    QVERIFY(first.buildExitCode != 0);
    QVERIFY(!first.success);
    QVERIFY2(log->toPlainText().contains(QStringLiteral("phase3_intentional_build_failure")),
             qPrintable(log->toPlainText()));
    QVERIFY(log->toPlainText().contains(QStringLiteral("Build failed:")));
    QVERIFY(button->isEnabled());
    QVERIFY(action->isEnabled());
    QCOMPARE(currentPath->text(), selectedFile);
    QCOMPARE(editor->toPlainText().toUtf8(), broken);
    QVERIFY(!editor->isReadOnly());
    QVERIFY(!browser->hasUnsavedChanges());

    editor->selectAll();
    editor->insertPlainText(QString::fromUtf8(repaired));
    QVERIFY(browser->hasUnsavedChanges());
    QVERIFY(save->isEnabled());
    save->click();
    QVERIFY(!browser->hasUnsavedChanges());
    QCOMPARE(readFile(source), repaired);
    QCOMPARE(currentPath->text(), selectedFile);

    action->trigger();
    QVERIFY(!button->isEnabled());
    QVERIFY(!action->isEnabled());
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 2, 120000);
    const BuildResult second = qvariant_cast<BuildResult>(finished.at(1).at(0));
    QVERIFY2(second.success, qPrintable(second.error + second.standardError + second.standardOutput));
    QVERIFY(second.configureStarted);
    QCOMPARE(second.configureExitCode, 0);
    QVERIFY(second.buildStarted);
    QCOMPARE(second.buildExitCode, 0);
    QVERIFY(log->toPlainText().contains(QStringLiteral("Build finished successfully.")));
    QVERIFY(button->isEnabled());
    QVERIFY(action->isEnabled());
    QCOMPARE(currentPath->text(), selectedFile);
    QCOMPARE(editor->toPlainText().toUtf8(), repaired);
    QVERIFY(!editor->isReadOnly());
    QVERIFY(!browser->hasUnsavedChanges());
}

void ProjectExporterTest::mainWindowBuildPreparesWorkspace_data()
{
    QTest::addColumn<bool>("switchWorkspace");
    QTest::addColumn<int>("decision");
    QTest::addColumn<bool>("toolbar");
    QTest::newRow("clean-toolbar") << false << -1 << true;
    QTest::newRow("clean-panel") << false << -1 << false;
    for (bool switchWorkspace : {false, true}) {
        for (int decision : {int(QMessageBox::AcceptRole), int(QMessageBox::DestructiveRole),
                             int(QMessageBox::RejectRole)}) {
            for (bool toolbar : {false, true}) {
                const QByteArray name = QByteArray(switchWorkspace ? "switch-" : "same-")
                    + (decision == QMessageBox::AcceptRole ? "save-" :
                       decision == QMessageBox::DestructiveRole ? "discard-" : "cancel-")
                    + (toolbar ? "toolbar" : "panel");
                QTest::newRow(name.constData()) << switchWorkspace << decision << toolbar;
            }
        }
    }
}

void ProjectExporterTest::mainWindowBuildPreparesWorkspace()
{
    QFETCH(bool, switchWorkspace);
    QFETCH(int, decision);
    QFETCH(bool, toolbar);
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    const QString source = QDir(first).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QVERIFY(QDir().mkpath(first));
    QString scaffoldError;
    QVERIFY2(ProjectScaffolder::create(blueprint(), first, &scaffoldError), qPrintable(scaffoldError));
    QVERIFY(writeFile(source, "original"));
    QVERIFY(QDir().mkpath(QDir(second).filePath(QStringLiteral("generated-project"))));
    MainWindow window;
    window.setBuildToolConfiguration(root.filePath(QStringLiteral("missing-cmake.exe")), {});
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *buildEdit = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *action = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    auto *dock = window.findChild<QDockWidget *>(QStringLiteral("workspaceEditorDock"));
    QVERIFY(pathEdit && buildEdit && browser && log && button && action && dock);
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    buildEdit->setText(root.filePath(QStringLiteral("build")));
    if (decision >= 0) {
        browser->openFile(QStringLiteral("src/custom.cpp"));
        auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
        QVERIFY(editor);
        editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor, QStringLiteral(" local"));
        QVERIFY(browser->hasUnsavedChanges());
        chooseMessageBox(QMessageBox::ButtonRole(decision));
    }
    if (switchWorkspace) pathEdit->setText(second); // No editingFinished signal.
    QVERIFY(dock->isHidden());
    if (toolbar) action->trigger();
    else button->click();

    const bool blocked = decision == QMessageBox::RejectRole;
    const QString expectedWorkspace = blocked || !switchWorkspace ? first : second;
    QCOMPARE(browser->workspacePath(), expectedWorkspace);
    QCOMPARE(pathEdit->text(), expectedWorkspace);
    QCOMPARE(log->toPlainText().contains(QStringLiteral("Starting configure for")), !blocked);
    if (!blocked) {
        QVERIFY(log->toPlainText().contains(QDir(expectedWorkspace).filePath(QStringLiteral("generated-project"))));
        QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains(QStringLiteral("Could not start CMake")), 10000);
    }
    QVERIFY(button->isEnabled());
    QVERIFY(action->isEnabled());
    QCOMPARE(readFile(source), decision == QMessageBox::AcceptRole ? QByteArray("original local")
                                                          : QByteArray("original"));
    QCOMPARE(browser->hasUnsavedChanges(), blocked);
    if (blocked) { chooseMessageBox(QMessageBox::DestructiveRole); window.close(); }
}

void ProjectExporterTest::mainWindowBuildBlocksOnSaveFailure_data()
{
    QTest::addColumn<bool>("switchWorkspace");
    QTest::addColumn<bool>("externalConflict");
    QTest::newRow("deleted-same") << false << false;
    QTest::newRow("deleted-switch") << true << false;
    QTest::newRow("disk-conflict-same") << false << true;
    QTest::newRow("disk-conflict-switch") << true << true;
}

void ProjectExporterTest::mainWindowBuildBlocksOnSaveFailure()
{
    QFETCH(bool, switchWorkspace);
    QFETCH(bool, externalConflict);
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    const QString source = QDir(first).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QVERIFY(QDir().mkpath(first));
    QString scaffoldError;
    QVERIFY2(ProjectScaffolder::create(blueprint(), first, &scaffoldError), qPrintable(scaffoldError));
    QVERIFY(writeFile(source, "original"));
    QVERIFY(QDir().mkpath(QDir(second).filePath(QStringLiteral("generated-project"))));
    MainWindow window;
    window.setBuildToolConfiguration(root.filePath(QStringLiteral("missing-cmake.exe")), {});
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *buildEdit = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *action = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    QVERIFY(pathEdit && buildEdit && browser && log && action);
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    buildEdit->setText(root.filePath(QStringLiteral("build")));
    browser->openFile(QStringLiteral("src/custom.cpp"));
    auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
    QVERIFY(editor);
    editor->moveCursor(QTextCursor::End);
    QTest::keyClicks(editor, QStringLiteral(" local"));
    QVERIFY(browser->hasUnsavedChanges());
    if (externalConflict) QVERIFY(writeFile(source, "external"));
    else QVERIFY(QFile::remove(source));
    if (switchWorkspace) pathEdit->setText(second); // No editingFinished signal.
    chooseMessageBox(QMessageBox::AcceptRole,
                     externalConflict ? QMessageBox::RejectRole : QMessageBox::InvalidRole);
    action->trigger();
    QCOMPARE(browser->workspacePath(), first);
    QCOMPARE(pathEdit->text(), first);
    QVERIFY(browser->hasUnsavedChanges());
    QCOMPARE(editor->toPlainText(), QStringLiteral("original local"));
    QCOMPARE(readFile(source), externalConflict ? QByteArray("external") : QByteArray());
    QVERIFY(!log->toPlainText().contains(QStringLiteral("Starting configure for")));
    chooseMessageBox(QMessageBox::DestructiveRole);
    window.close();
}

void ProjectExporterTest::mainWindowClickBuildAfterCancelledWorkspaceEdit_data()
{
    QTest::addColumn<bool>("processBetweenPressAndRelease");
    QTest::addColumn<bool>("toolbar");
    QTest::newRow("panel-single-click") << false << false;
    QTest::newRow("panel-event-loop-between-press-and-release") << true << false;
    QTest::newRow("toolbar-single-click") << false << true;
    QTest::newRow("toolbar-event-loop-between-press-and-release") << true << true;
}

void ProjectExporterTest::mainWindowClickBuildAfterCancelledWorkspaceEdit()
{
    QFETCH(bool, processBetweenPressAndRelease);
    QFETCH(bool, toolbar);
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    const QString source = QDir(first).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QVERIFY(QDir().mkpath(first));
    QString error;
    QVERIFY2(ProjectScaffolder::create(blueprint(), first, &error), qPrintable(error));
    QVERIFY(writeFile(source, "original"));
    QVERIFY(QDir().mkpath(QDir(second).filePath(QStringLiteral("generated-project"))));

    MainWindow window;
    window.setBuildToolConfiguration(root.filePath(QStringLiteral("missing-cmake.exe")), {});
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *buildEdit = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *panelButton = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *toolbarAction = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    auto *toolbarWidget = window.findChild<QToolBar *>();
    QVERIFY(pathEdit && buildEdit && browser && log && panelButton && toolbarAction && toolbarWidget);
    auto *toolbarButton = qobject_cast<QToolButton *>(toolbarWidget->widgetForAction(toolbarAction));
    QVERIFY(toolbarButton);
    QWidget *button = toolbar ? static_cast<QWidget *>(toolbarButton) : panelButton;
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    buildEdit->setText(root.filePath(QStringLiteral("build")));
    browser->openFile(QStringLiteral("src/custom.cpp"));
    auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
    QVERIFY(editor);
    editor->moveCursor(QTextCursor::End);
    QTest::keyClicks(editor, QStringLiteral(" local"));
    QVERIFY(browser->hasUnsavedChanges());
    window.resize(1800, 900);
    window.show();
    QVERIFY(window.isVisible());
    QVERIFY(button->isVisible());

    pathEdit->setFocus();
    QTRY_VERIFY(pathEdit->hasFocus());
    pathEdit->selectAll();
    QTest::keyClicks(pathEdit, second);
    QCOMPARE(pathEdit->text(), second);

    int prompts = 0;
    QTimer cancelPrompts;
    cancelPrompts.setInterval(1);
    QObject::connect(&cancelPrompts, &QTimer::timeout, &window, [&] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) return;
        for (auto *choice : box->buttons()) {
            if (box->buttonRole(choice) == QMessageBox::RejectRole) {
                ++prompts;
                QTest::mouseClick(choice, Qt::LeftButton);
                return;
            }
        }
    });
    cancelPrompts.start();
    if (processBetweenPressAndRelease) {
        QTest::mousePress(button, Qt::LeftButton);
        QCoreApplication::processEvents();
        QTest::mouseRelease(button, Qt::LeftButton);
    } else {
        QTest::mouseClick(button, Qt::LeftButton);
    }
    cancelPrompts.stop();

    QCOMPARE(prompts, 1);
    QCOMPARE(pathEdit->text(), first);
    QCOMPARE(browser->workspacePath(), first);
    QCOMPARE(editor->toPlainText(), QStringLiteral("original local"));
    QVERIFY(browser->hasUnsavedChanges());
    QCOMPARE(readFile(source), QByteArray("original"));
    QVERIFY(!log->toPlainText().contains(QStringLiteral("Starting configure for")));
    chooseMessageBox(QMessageBox::DestructiveRole);
    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(log->toPlainText().contains(QStringLiteral("Starting configure for")));
    QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains(QStringLiteral("Could not start CMake")), 10000);
    QVERIFY(!browser->hasUnsavedChanges());
    window.close();
}

void ProjectExporterTest::mainWindowAbortedBuildClickAppliesWorkspaceEdit()
{
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(QDir(first).filePath(QStringLiteral("generated-project"))));
    QVERIFY(QDir().mkpath(QDir(second).filePath(QStringLiteral("generated-project"))));
    MainWindow window;
    window.setBuildToolConfiguration(root.filePath(QStringLiteral("missing-cmake.exe")), {});
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *buildEdit = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *button = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(pathEdit && buildEdit && browser && button && log);
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    buildEdit->setText(root.filePath(QStringLiteral("build")));
    window.show();
    pathEdit->setFocus();
    QTRY_VERIFY(pathEdit->hasFocus());
    pathEdit->selectAll();
    QTest::keyClicks(pathEdit, second);
    QTest::mousePress(button, Qt::LeftButton);
    QTest::mouseRelease(button, Qt::LeftButton, Qt::NoModifier, QPoint(-10, -10));
    QCoreApplication::processEvents();
    QCOMPARE(browser->workspacePath(), second);
    QCOMPARE(pathEdit->text(), second);
    QVERIFY(!log->toPlainText().contains(QStringLiteral("Starting configure for")));

    QTest::mouseClick(button, Qt::LeftButton);
    QVERIFY(log->toPlainText().contains(QStringLiteral("Starting configure for")));
    QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains(QStringLiteral("Could not start CMake")), 10000);
}

void ProjectExporterTest::mainWindowLostBuildMouseGrabAppliesWorkspaceEdit_data()
{
    QTest::addColumn<bool>("deactivateWindow");
    QTest::newRow("mouse-ungrab") << false;
    QTest::newRow("window-deactivate") << true;
}

void ProjectExporterTest::mainWindowLostBuildMouseGrabAppliesWorkspaceEdit()
{
    QFETCH(bool, deactivateWindow);
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(QDir(first).filePath(QStringLiteral("generated-project"))));
    QVERIFY(QDir().mkpath(QDir(second).filePath(QStringLiteral("generated-project"))));
    MainWindow window;
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *button = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(pathEdit && browser && button && log);
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    window.show();
    pathEdit->setFocus();
    QTRY_VERIFY(pathEdit->hasFocus());
    pathEdit->selectAll();
    QTest::keyClicks(pathEdit, second);
    QTest::mousePress(button, Qt::LeftButton);
    QEvent interrupted(deactivateWindow ? QEvent::WindowDeactivate : QEvent::UngrabMouse);
    QCoreApplication::sendEvent(deactivateWindow ? static_cast<QObject *>(&window)
                                               : static_cast<QObject *>(button), &interrupted);
    QCoreApplication::processEvents();
    QCOMPARE(browser->workspacePath(), second);
    QCOMPARE(pathEdit->text(), second);
    QVERIFY(!log->toPlainText().contains(QStringLiteral("Starting configure for")));
    QTest::mouseRelease(button, Qt::LeftButton, Qt::NoModifier, QPoint(-10, -10));
}

void ProjectExporterTest::mainWindowExportClickKeepsPathEditBehavior()
{
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    QVERIFY(QDir().mkpath(QDir(first).filePath(QStringLiteral("generated-project"))));
    QVERIFY(QDir().mkpath(QDir(second).filePath(QStringLiteral("generated-project"))));
    MainWindow window;
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *exportButton = window.findChild<QPushButton *>(QStringLiteral("exportProjectButton"));
    QVERIFY(pathEdit && browser && exportButton);
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    window.show();
    QVERIFY(exportButton->isVisible());
    pathEdit->setFocus();
    QTRY_VERIFY(pathEdit->hasFocus());
    pathEdit->selectAll();
    QTest::keyClicks(pathEdit, second);
    QString workspaceAtClick;
    QObject::connect(exportButton, &QPushButton::clicked, &window,
                     [&] { workspaceAtClick = browser->workspacePath(); });
    QTest::mouseClick(exportButton, Qt::LeftButton);
    QCOMPARE(workspaceAtClick, second);
    QCOMPARE(pathEdit->text(), second);
}

void ProjectExporterTest::mainWindowExportPreparesWorkspace_data()
{
    QTest::addColumn<bool>("switchWorkspace");
    QTest::addColumn<int>("decision");
    QTest::addColumn<bool>("toolbar");
    QTest::newRow("clean-panel") << false << -1 << false;
    QTest::newRow("clean-toolbar") << false << -1 << true;
    QTest::newRow("clean-switch-panel") << true << -1 << false;
    QTest::newRow("clean-switch-toolbar") << true << -1 << true;
    for (bool switchWorkspace : {false, true}) {
        for (int decision : {int(QMessageBox::AcceptRole), int(QMessageBox::DestructiveRole),
                             int(QMessageBox::RejectRole)}) {
            for (bool toolbar : {false, true}) {
                const QByteArray name = QByteArray(switchWorkspace ? "switch-" : "same-")
                    + (decision == QMessageBox::AcceptRole ? "save-" :
                       decision == QMessageBox::DestructiveRole ? "discard-" : "cancel-")
                    + (toolbar ? "toolbar" : "panel");
                QTest::newRow(name.constData()) << switchWorkspace << decision << toolbar;
            }
        }
    }
}

void ProjectExporterTest::mainWindowExportPreparesWorkspace()
{
    QFETCH(bool, switchWorkspace);
    QFETCH(int, decision);
    QFETCH(bool, toolbar);
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    const QString target = root.filePath(QStringLiteral("export"));
    const QString firstSource = QDir(first).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    const QString secondSource = QDir(second).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QString error;
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));
    QVERIFY(QDir().mkpath(target));
    QVERIFY2(ProjectScaffolder::create(blueprint(), first, &error), qPrintable(error));
    error.clear();
    QVERIFY2(ProjectScaffolder::create(blueprint(), second, &error), qPrintable(error));
    QVERIFY(writeFile(firstSource, "int value = 1;\n"));
    QVERIFY(writeFile(secondSource, "int value = 3;\n"));

    MainWindow window;
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *targetEdit = window.findChild<QLineEdit *>(QStringLiteral("exportTargetEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("exportProjectButton"));
    auto *action = window.findChild<QAction *>(QStringLiteral("toolbarExportAction"));
    QVERIFY(pathEdit && targetEdit && browser && log && button && action);
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    targetEdit->setText(target);
    if (decision >= 0) {
        browser->openFile(QStringLiteral("src/custom.cpp"));
        auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
        QVERIFY(editor);
        editor->selectAll();
        editor->insertPlainText(QStringLiteral("int value = 2;\n"));
        QVERIFY(browser->hasUnsavedChanges());
        chooseMessageBox(QMessageBox::ButtonRole(decision));
    }
    if (switchWorkspace) pathEdit->setText(second); // No editingFinished signal.
    if (toolbar) action->trigger();
    else button->click();

    const bool cancelled = decision == QMessageBox::RejectRole;
    const QString expectedWorkspace = cancelled || !switchWorkspace ? first : second;
    QCOMPARE(browser->workspacePath(), expectedWorkspace);
    QCOMPARE(pathEdit->text(), expectedWorkspace);
    QCOMPARE(browser->hasUnsavedChanges(), cancelled);
    QCOMPARE(readFile(firstSource), decision == QMessageBox::AcceptRole
                                       ? QByteArray("int value = 2;\n") : QByteArray("int value = 1;\n"));
    if (cancelled) {
        QVERIFY(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
        QVERIFY(!log->toPlainText().contains(QStringLiteral("Export finished")));
        chooseMessageBox(QMessageBox::DestructiveRole);
        window.close();
    } else {
        QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("src/custom.cpp"))),
                 switchWorkspace ? QByteArray("int value = 3;\n")
                                 : decision == QMessageBox::AcceptRole
                                     ? QByteArray("int value = 2;\n") : QByteArray("int value = 1;\n"));
        QVERIFY(log->toPlainText().contains(QStringLiteral("Export finished")));
    }
}

void ProjectExporterTest::mainWindowExportBlocksOnSaveFailure_data()
{
    QTest::addColumn<bool>("switchWorkspace");
    QTest::addColumn<bool>("externalConflict");
    QTest::newRow("deleted-same") << false << false;
    QTest::newRow("deleted-switch") << true << false;
    QTest::newRow("disk-conflict-same") << false << true;
    QTest::newRow("disk-conflict-switch") << true << true;
}

void ProjectExporterTest::mainWindowExportBlocksOnSaveFailure()
{
    QFETCH(bool, switchWorkspace);
    QFETCH(bool, externalConflict);
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    const QString target = root.filePath(QStringLiteral("export"));
    const QString source = QDir(first).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QString error;
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));
    QVERIFY(QDir().mkpath(target));
    QVERIFY2(ProjectScaffolder::create(blueprint(), first, &error), qPrintable(error));
    error.clear();
    QVERIFY2(ProjectScaffolder::create(blueprint(), second, &error), qPrintable(error));
    QVERIFY(writeFile(source, "int value = 1;\n"));
    MainWindow window;
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *targetEdit = window.findChild<QLineEdit *>(QStringLiteral("exportTargetEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *action = window.findChild<QAction *>(QStringLiteral("toolbarExportAction"));
    QVERIFY(pathEdit && targetEdit && browser && log && action);
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    targetEdit->setText(target);
    browser->openFile(QStringLiteral("src/custom.cpp"));
    auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
    QVERIFY(editor);
    editor->selectAll();
    editor->insertPlainText(QStringLiteral("int value = 2;\n"));
    QVERIFY(browser->hasUnsavedChanges());
    if (externalConflict) QVERIFY(writeFile(source, "int value = 9;\n"));
    else QVERIFY(QFile::remove(source));
    if (switchWorkspace) pathEdit->setText(second);
    chooseMessageBox(QMessageBox::AcceptRole,
                     externalConflict ? QMessageBox::RejectRole : QMessageBox::InvalidRole);
    action->trigger();
    QCOMPARE(browser->workspacePath(), first);
    QCOMPARE(pathEdit->text(), first);
    QVERIFY(browser->hasUnsavedChanges());
    QCOMPARE(editor->toPlainText(), QStringLiteral("int value = 2;\n"));
    QCOMPARE(readFile(source), externalConflict ? QByteArray("int value = 9;\n") : QByteArray());
    QVERIFY(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    QVERIFY(!log->toPlainText().contains(QStringLiteral("Export finished")));
    chooseMessageBox(QMessageBox::DestructiveRole);
    window.close();
}

void ProjectExporterTest::mainWindowExportFailurePreservesEditorAndAllowsRetry()
{
    QTemporaryDir root;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString failedTarget = root.filePath(QStringLiteral("occupied"));
    const QString retryTarget = root.filePath(QStringLiteral("retry"));
    const QString source = QDir(workspace).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QString error;
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(failedTarget));
    QVERIFY(QDir().mkpath(retryTarget));
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));
    QVERIFY(writeFile(source, "int value = 1;\n"));
    QVERIFY(writeFile(QDir(failedTarget).filePath(QStringLiteral("keep.txt")), "keep"));
    MainWindow window;
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *targetEdit = window.findChild<QLineEdit *>(QStringLiteral("exportTargetEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("exportProjectButton"));
    QVERIFY(pathEdit && targetEdit && browser && log && button);
    QVERIFY(browser->setWorkspacePath(workspace));
    pathEdit->setText(workspace);
    browser->openFile(QStringLiteral("src/custom.cpp"));
    auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
    QVERIFY(editor);
    editor->selectAll();
    editor->insertPlainText(QStringLiteral("int value = 2;\n"));
    targetEdit->setText(failedTarget);
    chooseMessageBox(QMessageBox::AcceptRole);
    button->click();
    QCOMPARE(browser->workspacePath(), workspace);
    QCOMPARE(editor->toPlainText(), QStringLiteral("int value = 2;\n"));
    QVERIFY(!browser->hasUnsavedChanges());
    QCOMPARE(readFile(source), QByteArray("int value = 2;\n"));
    QCOMPARE(readFile(QDir(failedTarget).filePath(QStringLiteral("keep.txt"))), QByteArray("keep"));
    QVERIFY(log->toPlainText().contains(QStringLiteral("Export failed")));
    targetEdit->setText(retryTarget);
    button->click();
    QCOMPARE(readFile(QDir(retryTarget).filePath(QStringLiteral("src/custom.cpp"))),
             QByteArray("int value = 2;\n"));
    QVERIFY(!browser->hasUnsavedChanges());
}

void ProjectExporterTest::mainWindowClickExportAfterCancelledWorkspaceEdit_data()
{
    QTest::addColumn<bool>("toolbar");
    QTest::newRow("panel") << false;
    QTest::newRow("toolbar") << true;
}

void ProjectExporterTest::mainWindowClickExportAfterCancelledWorkspaceEdit()
{
    QFETCH(bool, toolbar);
    QTemporaryDir root;
    const QString first = root.filePath(QStringLiteral("first"));
    const QString second = root.filePath(QStringLiteral("second"));
    const QString target = root.filePath(QStringLiteral("export"));
    const QString firstSource = QDir(first).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QString error;
    QVERIFY(QDir().mkpath(first));
    QVERIFY(QDir().mkpath(second));
    QVERIFY(QDir().mkpath(target));
    QVERIFY2(ProjectScaffolder::create(blueprint(), first, &error), qPrintable(error));
    error.clear();
    QVERIFY2(ProjectScaffolder::create(blueprint(), second, &error), qPrintable(error));
    QVERIFY(writeFile(firstSource, "int value = 1;\n"));
    MainWindow window;
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *targetEdit = window.findChild<QLineEdit *>(QStringLiteral("exportTargetEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *panelButton = window.findChild<QPushButton *>(QStringLiteral("exportProjectButton"));
    auto *action = window.findChild<QAction *>(QStringLiteral("toolbarExportAction"));
    auto *toolBar = window.findChild<QToolBar *>();
    QVERIFY(pathEdit && targetEdit && browser && panelButton && action && toolBar);
    auto *toolbarButton = qobject_cast<QToolButton *>(toolBar->widgetForAction(action));
    QVERIFY(toolbarButton);
    QWidget *button = toolbar ? static_cast<QWidget *>(toolbarButton) : panelButton;
    QVERIFY(browser->setWorkspacePath(first));
    pathEdit->setText(first);
    targetEdit->setText(target);
    browser->openFile(QStringLiteral("src/custom.cpp"));
    auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
    QVERIFY(editor);
    editor->selectAll();
    editor->insertPlainText(QStringLiteral("int value = 2;\n"));
    window.resize(1800, 900);
    window.show();
    QVERIFY(button->isVisible());
    pathEdit->setFocus();
    QTRY_VERIFY(pathEdit->hasFocus());
    pathEdit->selectAll();
    QTest::keyClicks(pathEdit, second);
    QCOMPARE(pathEdit->text(), second);
    chooseMessageBox(QMessageBox::RejectRole);
    QTest::mouseClick(button, Qt::LeftButton);
    QCOMPARE(browser->workspacePath(), first);
    QCOMPARE(pathEdit->text(), first);
    QVERIFY(browser->hasUnsavedChanges());
    QVERIFY(QDir(target).entryList(QDir::AllEntries | QDir::NoDotAndDotDot).isEmpty());
    chooseMessageBox(QMessageBox::DestructiveRole);
    window.close();
}

void ProjectExporterTest::mainWindowCancelledExportDoesNotCreateAbsentTarget()
{
    QTemporaryDir root;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString target = root.filePath(QStringLiteral("absent-export"));
    const QString source = QDir(workspace).filePath(QStringLiteral("generated-project/src/custom.cpp"));
    QString error;
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));
    QVERIFY(writeFile(source, "int value = 1;\n"));
    QVERIFY(!QFileInfo::exists(target));

    MainWindow window;
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *targetEdit = window.findChild<QLineEdit *>(QStringLiteral("exportTargetEdit"));
    auto *browser = window.findChild<WorkspaceBrowserWidget *>();
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("exportProjectButton"));
    QVERIFY(pathEdit && targetEdit && browser && log && button);
    QVERIFY(browser->setWorkspacePath(workspace));
    pathEdit->setText(workspace);
    targetEdit->setText(target);
    browser->openFile(QStringLiteral("src/custom.cpp"));
    auto *editor = browser->findChild<QPlainTextEdit *>(QStringLiteral("workspacePreview"));
    QVERIFY(editor);
    editor->selectAll();
    editor->insertPlainText(QStringLiteral("int value = 2;\n"));
    QVERIFY(browser->hasUnsavedChanges());

    chooseMessageBox(QMessageBox::RejectRole);
    button->click();
    QVERIFY(!QFileInfo::exists(target));
    QCOMPARE(browser->workspacePath(), workspace);
    QCOMPARE(pathEdit->text(), workspace);
    QCOMPARE(editor->toPlainText(), QStringLiteral("int value = 2;\n"));
    QVERIFY(browser->hasUnsavedChanges());
    QCOMPARE(readFile(source), QByteArray("int value = 1;\n"));
    QVERIFY(log->toPlainText().isEmpty());
    chooseMessageBox(QMessageBox::DestructiveRole);
    window.close();
}

void ProjectExporterTest::rejectsNonEmptyTarget()
{
    QTemporaryDir root;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString target = root.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(target));
    QString error;
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));
    QVERIFY(writeFile(QDir(target).filePath(QStringLiteral("keep.txt")), "keep"));

    QVERIFY(!ProjectExporter::exportProject(workspace, target, &error));
    QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("keep.txt"))), QByteArray("keep"));
    QCOMPARE(fileHashes(target).size(), 1);
}

void ProjectExporterTest::exportsVerifiedExternalSources()
{
    QTemporaryDir root;
    const QString source = root.filePath(QStringLiteral("source"));
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString target = root.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(source));
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(target));
    const QByteArray externalBytes = QByteArray::fromHex("0065787465726e616c0d0aff");
    QVERIFY(writeFile(QDir(source).filePath(QStringLiteral("include/api.hpp")), externalBytes));
    QString error;
    const BlueprintNode external = externalNode();
    QVERIFY2(ExternalCodeImporter::importFiles(external, source, {QStringLiteral("include/api.hpp")},
                                               workspace, &error), qPrintable(error));
    BlueprintDocument document = blueprint();
    document.nodes.insert(2, external);
    document.edges = {{QStringLiteral("a"), QStringLiteral("start"), QStringLiteral("page"), {}},
                      {QStringLiteral("b"), QStringLiteral("page"), external.id, {}},
                      {QStringLiteral("c"), external.id, QStringLiteral("end"), {}}};
    QVERIFY2(ProjectScaffolder::create(document, workspace, &error), qPrintable(error));

    const QString colliding = QDir(workspace).filePath(QStringLiteral("generated-project/src/external/manual.h"));
    QVERIFY(writeFile(colliding, "must not merge"));
    QVERIFY(!ProjectExporter::exportProject(workspace, target, &error));
    QVERIFY(fileHashes(target).isEmpty());
    QVERIFY(QFile::remove(colliding));
    QVERIFY(QDir().rmdir(QFileInfo(colliding).absolutePath()));

    QVERIFY2(ProjectExporter::exportProject(workspace, target, &error), qPrintable(error));
    QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("src/external/vendor/include/api.hpp"))),
             externalBytes);
    QCOMPARE(readFile(QDir(target).filePath(QStringLiteral("src/external/vendor/import-manifest.json"))),
             readFile(QDir(workspace).filePath(QStringLiteral("external/vendor/import-manifest.json"))));
}

void ProjectExporterTest::rejectsMalformedInputsAndMappingCollisions()
{
    QTemporaryDir root;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString target = root.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(target));
    QString error;
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));
    QVERIFY(writeFile(QDir(workspace).filePath(QStringLiteral("generated-project/blueprint.json")), "collision"));
    QVERIFY(!ProjectExporter::exportProject(workspace, target, &error));
    QVERIFY(fileHashes(target).isEmpty());
    QVERIFY(QFile::remove(QDir(workspace).filePath(QStringLiteral("generated-project/blueprint.json"))));
    QVERIFY(writeFile(QDir(workspace).filePath(QStringLiteral("generation-manifest.json")), "{}"));
    QVERIFY(!ProjectExporter::exportProject(workspace, target, &error));
    QVERIFY(!ProjectExporter::exportProject(QStringLiteral("relative-workspace"), target, &error));
    QVERIFY(!ProjectExporter::exportProject(workspace, QStringLiteral("relative-target"), &error));
}

void ProjectExporterTest::sourceLinkFailureLeavesTargetAndSiblingDirectoryUntouched()
{
    QTemporaryDir root;
    QTemporaryDir outside;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString target = root.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(target));
    QString error;
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));
    const QString link = QDir(workspace).filePath(QStringLiteral("generated-project/src/linked"));
#ifdef Q_OS_WIN
    if (QProcess::execute(QStringLiteral("cmd.exe"),
                          {QStringLiteral("/c"), QStringLiteral("mklink"), QStringLiteral("/J"),
                           QDir::toNativeSeparators(link), QDir::toNativeSeparators(outside.path())}) != 0)
        QSKIP("Directory junction creation unavailable");
    const auto cleanup = qScopeGuard([&] { QDir().rmdir(link); });
#else
    if (!QFile::link(outside.path(), link))
        QSKIP("Directory symlink creation unavailable");
    const auto cleanup = qScopeGuard([&] { QFile::remove(link); });
#endif
    const QStringList before = QDir(root.path()).entryList(QDir::AllEntries | QDir::Hidden
                                                           | QDir::System | QDir::NoDotAndDotDot);
    QVERIFY(!ProjectExporter::exportProject(workspace, target, &error));
    QVERIFY(fileHashes(target).isEmpty());
    QCOMPARE(QDir(root.path()).entryList(QDir::AllEntries | QDir::Hidden
                                         | QDir::System | QDir::NoDotAndDotDot), before);
}

void ProjectExporterTest::commitFailureRemovesStagingAndLeavesTargetEmpty()
{
#ifdef Q_OS_WIN
    QTemporaryDir root;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString target = root.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(target));
    QString error;
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));
    const QString lockedPath = QDir::toNativeSeparators(target);
    const HANDLE handle = CreateFileW(reinterpret_cast<LPCWSTR>(lockedPath.utf16()), GENERIC_READ,
                                      FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                                      FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    QVERIFY(handle != INVALID_HANDLE_VALUE);
    const auto closeHandle = qScopeGuard([&] { CloseHandle(handle); });
    const bool exported = ProjectExporter::exportProject(workspace, target, &error);
    QVERIFY(!exported);
    QVERIFY(fileHashes(target).isEmpty());
    QCOMPARE(QDir(root.path()).entryList({QStringLiteral(".export.task9-export-*")},
                                         QDir::AllEntries | QDir::Hidden | QDir::System), QStringList{});
#else
    QSKIP("Windows handle sharing regression");
#endif
}

void ProjectExporterTest::failedBackupRestorationRetainsBothRecoveryDirectories()
{
    const QString target = QStringLiteral("C:/exports/project");
    const QString backup = QStringLiteral("C:/exports/.project.backup-id");
    const QString rollback = QStringLiteral("C:/exports/.project.stage-id-rollback");

    const auto decision = ProjectExporterRecovery::decideBackupRemovalFailure(
        true, false, target, backup, rollback);

    QVERIFY(!decision.removeRollback);
    QCOMPARE(decision.error,
             QStringLiteral("Could not remove export backup and target restoration failed; "
                            "original empty target retained at C:/exports/.project.backup-id; "
                            "completed export retained at C:/exports/.project.stage-id-rollback"));

    const auto restored = ProjectExporterRecovery::decideBackupRemovalFailure(
        true, true, target, backup, rollback);
    QVERIFY(restored.removeRollback);
    QCOMPARE(restored.error, QStringLiteral("Could not remove export backup; target restored"));

    const auto targetNotMoved = ProjectExporterRecovery::decideBackupRemovalFailure(
        false, false, target, backup, rollback);
    QVERIFY(!targetNotMoved.removeRollback);
    QVERIFY(targetNotMoved.error.contains(backup));
    QVERIFY(targetNotMoved.error.contains(target));
}

void ProjectExporterTest::buildServiceStopsAfterFailedConfigure()
{
    QTemporaryDir root;
    const QString source = root.filePath(QStringLiteral("source"));
    const QString build = root.filePath(QStringLiteral("build"));
    QVERIFY(QDir().mkpath(source));
    BuildService service;
    QSignalSpy finished(&service, &BuildService::finished);
    BuildRequest request;
    request.sourceDirectory = source;
    request.buildDirectory = build;
    request.cmakeExecutable = QString::fromUtf8(TASK9_CMAKE_COMMAND);
    request.configureArguments = configureArguments();
    QString error;
    QVERIFY2(service.start(request, &error), qPrintable(error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 30000);
    const BuildResult result = qvariant_cast<BuildResult>(finished.first().first());
    QVERIFY(!result.success);
    QVERIFY(result.configureStarted);
    QVERIFY(!result.buildStarted);
    QVERIFY(result.configureExitCode != 0);
    QVERIFY(result.standardOutput.contains(QStringLiteral("CMakeLists"))
            || result.standardError.contains(QStringLiteral("CMakeLists")));
}

void ProjectExporterTest::buildServiceConfiguresAndBuildsGeneratedProject()
{
    QTemporaryDir root;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString build = root.filePath(QStringLiteral("build"));
    QVERIFY(QDir().mkpath(workspace));
    QString error;
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));
    const QString toolchain = root.filePath(QStringLiteral("minimal toolchain.cmake"));
    QVERIFY(writeFile(toolchain, QByteArray{}));
    BuildService service;
    QSignalSpy output(&service, &BuildService::standardOutput);
    QSignalSpy stages(&service, &BuildService::stageFinished);
    QSignalSpy finished(&service, &BuildService::finished);
    BuildRequest request;
    request.sourceDirectory = QDir(workspace).filePath(QStringLiteral("generated-project"));
    request.buildDirectory = build;
    request.cmakeExecutable = QString::fromUtf8(TASK9_CMAKE_COMMAND);
    request.configureArguments = configureArguments()
        << QStringLiteral("--toolchain") << toolchain;
    request.buildArguments = {QStringLiteral("--config"), QStringLiteral("Debug"),
                              QStringLiteral("--parallel"), QStringLiteral("2")};
    QVERIFY2(service.start(request, &error), qPrintable(error));
    QVERIFY(!service.start(request, &error));
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 120000);
    const BuildResult result = qvariant_cast<BuildResult>(finished.first().first());
    QVERIFY2(result.success, qPrintable(result.error + result.standardError + result.standardOutput));
    QCOMPARE(result.configureExitCode, 0);
    QCOMPARE(result.buildExitCode, 0);
    QVERIFY(result.configureStarted);
    QVERIFY(result.buildStarted);
    QCOMPARE(stages.size(), 2);
    QVERIFY(!output.isEmpty());
}

void ProjectExporterTest::buildServiceRejectsOwnedCmakeArguments_data()
{
    QTest::addColumn<QStringList>("arguments");
    QTest::newRow("source separated") << QStringList{QStringLiteral("-S"), QStringLiteral("other")};
    QTest::newRow("source joined") << QStringList{QStringLiteral("-Sother")};
    QTest::newRow("source long separated") << QStringList{QStringLiteral("--source"), QStringLiteral("other")};
    QTest::newRow("source long equals") << QStringList{QStringLiteral("--source=other")};
    QTest::newRow("binary separated") << QStringList{QStringLiteral("-B"), QStringLiteral("other")};
    QTest::newRow("binary joined") << QStringList{QStringLiteral("-Bother")};
    QTest::newRow("build long separated") << QStringList{QStringLiteral("--build"), QStringLiteral("other")};
    QTest::newRow("build long equals") << QStringList{QStringLiteral("--build=other")};
    QTest::newRow("script separated") << QStringList{QStringLiteral("-P"), QStringLiteral("script.cmake")};
    QTest::newRow("script joined") << QStringList{QStringLiteral("-Pscript.cmake")};
    QTest::newRow("command mode") << QStringList{QStringLiteral("-E"), QStringLiteral("echo"), QStringLiteral("hello")};
    QTest::newRow("install mode") << QStringList{QStringLiteral("--install"), QStringLiteral("other")};
    QTest::newRow("open mode") << QStringList{QStringLiteral("--open"), QStringLiteral("other")};
    QTest::newRow("workflow mode") << QStringList{QStringLiteral("--workflow"), QStringLiteral("--preset"), QStringLiteral("ci")};
}

void ProjectExporterTest::buildServiceRejectsOwnedCmakeArguments()
{
    QFETCH(QStringList, arguments);
    QTemporaryDir root;
    const QString source = root.filePath(QStringLiteral("source"));
    const QString build = root.filePath(QStringLiteral("build"));
    QVERIFY(QDir().mkpath(source));
    BuildService service;
    QSignalSpy finished(&service, &BuildService::finished);
    BuildRequest request;
    request.sourceDirectory = source;
    request.buildDirectory = build;
    request.cmakeExecutable = QString::fromUtf8(TASK9_CMAKE_COMMAND);
    request.configureArguments = arguments;
    QString error;

    QVERIFY(service.start(request, &error) == false);
    QVERIFY2(error.contains(QStringLiteral("reserved"), Qt::CaseInsensitive), qPrintable(error));
    QVERIFY(!service.isRunning());
    QVERIFY(finished.isEmpty());
    QVERIFY(!QFileInfo::exists(build));
}

void ProjectExporterTest::mainWindowReportsBuildInputErrorsAndRestoresButton()
{
    QTest::ignoreMessage(QtWarningMsg,
                         QRegularExpression(QStringLiteral("QFontDatabase: Cannot find font directory.*")));
    MainWindow window;
    auto *workspace = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *build = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *toolbarAction = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(workspace);
    QVERIFY(build);
    QVERIFY(button);
    QVERIFY(toolbarAction);
    QVERIFY(log);
    QVERIFY(log->isReadOnly());
    QVERIFY(log->toPlainText().isEmpty());
    workspace->setText(QStringLiteral("relative-workspace"));
    build->setText(QStringLiteral("relative-build"));
    toolbarAction->trigger();
    QTRY_VERIFY(log->toPlainText().contains(QStringLiteral("absolute"), Qt::CaseInsensitive));
    QVERIFY(button->isEnabled());
    QVERIFY(toolbarAction->isEnabled());
}

void ProjectExporterTest::mainWindowStreamsFailedConfigureAndPreservesArguments()
{
    QTemporaryDir root(QDir::tempPath() + QStringLiteral("/task9 ui XXXXXX"));
    QVERIFY(root.isValid());
    const QString workspacePath = root.filePath(QStringLiteral("workspace"));
    const QString sourcePath = QDir(workspacePath).filePath(QStringLiteral("generated-project"));
    const QString buildPath = root.filePath(QStringLiteral("build"));
    QVERIFY(QDir().mkpath(sourcePath));
    QVERIFY(writeFile(QDir(sourcePath).filePath(QStringLiteral("CMakeLists.txt")),
                      "cmake_minimum_required(VERSION 3.22)\n"
                      "if(NOT TASK9_ARGUMENT STREQUAL \"space value\")\n"
                      "  message(FATAL_ERROR \"argument boundary lost\")\n"
                      "endif()\n"
                      "message(FATAL_ERROR \"expected UI failure\")\n"));
    MainWindow window;
    window.setBuildToolConfiguration(
        QString::fromUtf8(TASK9_CMAKE_COMMAND),
        configureArguments() << QStringLiteral("-DTASK9_ARGUMENT=space value"));
    auto *workspace = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *build = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *toolbarAction = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(workspace);
    QVERIFY(build);
    QVERIFY(button);
    QVERIFY(toolbarAction);
    QVERIFY(log);
    workspace->setText(workspacePath);
    build->setText(buildPath);
    toolbarAction->trigger();
    QVERIFY(!button->isEnabled());
    QVERIFY(!toolbarAction->isEnabled());
    QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains(QStringLiteral("[configure] exit code")), 30000);
    QVERIFY(log->toPlainText().contains(QStringLiteral("[configure stderr]")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("expected UI failure")));
    QVERIFY(!log->toPlainText().contains(QStringLiteral("argument boundary lost")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("Build failed:")));
    QVERIFY(button->isEnabled());
    QVERIFY(toolbarAction->isEnabled());
}

void ProjectExporterTest::mainWindowRestoresBuildButtonWhenCmakeCannotStart()
{
    QTemporaryDir root;
    const QString workspacePath = root.filePath(QStringLiteral("workspace"));
    const QString sourcePath = QDir(workspacePath).filePath(QStringLiteral("generated-project"));
    const QString buildPath = root.filePath(QStringLiteral("build"));
    QVERIFY(QDir().mkpath(sourcePath));
    MainWindow window;
    window.setBuildToolConfiguration(root.filePath(QStringLiteral("task9_missing_tool.exe")), {});
    auto *workspace = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
    auto *build = window.findChild<QLineEdit *>(QStringLiteral("buildDirectoryEdit"));
    auto *button = window.findChild<QPushButton *>(QStringLiteral("buildProjectButton"));
    auto *toolbarAction = window.findChild<QAction *>(QStringLiteral("toolbarBuildAction"));
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(workspace);
    QVERIFY(build);
    QVERIFY(button);
    QVERIFY(toolbarAction);
    QVERIFY(log);
    workspace->setText(workspacePath);
    build->setText(buildPath);

    toolbarAction->trigger();
    QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains(QStringLiteral("Could not start CMake")), 10000);
    QVERIFY(log->toPlainText().contains(QStringLiteral("Build failed:")));
    QVERIFY(button->isEnabled());
    QVERIFY(toolbarAction->isEnabled());
}

void ProjectExporterTest::preservesEveryMappedFileHash()
{
    QTemporaryDir root;
    const QString workspace = root.filePath(QStringLiteral("workspace"));
    const QString project = QDir(workspace).filePath(QStringLiteral("generated-project"));
    const QString target = root.filePath(QStringLiteral("export"));
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(target));
    QString error;
    QVERIFY2(ProjectScaffolder::create(blueprint(), workspace, &error), qPrintable(error));

    QVERIFY2(ProjectExporter::exportProject(workspace, target, &error), qPrintable(error));
    QMap<QString, QByteArray> expected = fileHashes(project);
    expected.insert(QStringLiteral("generation-manifest.json"),
                    QCryptographicHash::hash(readFile(QDir(workspace).filePath(QStringLiteral("generation-manifest.json"))),
                                             QCryptographicHash::Sha256));
    expected.insert(QStringLiteral("blueprint.json"),
                    QCryptographicHash::hash(readFile(QDir(project).filePath(QStringLiteral("src/contracts/source-blueprint.json"))),
                                             QCryptographicHash::Sha256));
    QCOMPARE(fileHashes(target), expected);
}

QTEST_MAIN(ProjectExporterTest)
#include "tst_project_exporter.moc"
