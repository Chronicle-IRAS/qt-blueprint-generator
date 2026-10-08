#include "app/main_window.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "workspace/external_code_importer.h"
#include "workspace/build_service.h"
#include "ai/ai_client.h"
#include "app/candidate_review_dialog.h"
#include "ui/theme.h"
#include <QDialog>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QUndoStack>
#include <QtTest>

namespace {
class DeferredClient final : public IAiClient
{
public:
    using IAiClient::IAiClient;
    AiRequest request;
    void generate(const AiRequest &value) override { request = value; }
};
BlueprintNode externalNode()
{
    BlueprintNode node;
    node.id = "external";
    node.type = NodeType::ExternalCode;
    node.name = "External";
    node.description = "Manually supplied contract";
    node.constraints = {"Read only"};
    node.acceptanceCriteria = {"Provides the declared API"};
    return node;
}
void writeBytes(const QString &path, const QByteArray &bytes)
{
    QVERIFY(QDir().mkpath(QFileInfo(path).path()));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(bytes), bytes.size());
}
QByteArray readBytes(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}
void prepare(MainWindow &window, const QString &workspace)
{
    window.show();
    window.findChild<QLineEdit *>("workspacePathEdit")->setText(workspace);
    QVERIFY(window.scene()->addNode(externalNode(), {}));
    window.scene()->nodeItem("external")->setSelected(true);
}
QDialog *manage(MainWindow &window)
{
    auto *button = window.findChild<QPushButton *>("manageExternalCodeButton");
    if (!button) return nullptr;
    button->click();
    for (auto *dialog : window.findChildren<QDialog *>("externalCodeDialog"))
        if (dialog->isVisible()) return dialog;
    return nullptr;
}
// Drive actual Qt dialogs through their widgets, preserving nested event loops.
void modalActions(const QVector<std::function<void(QDialog *)>> &actions)
{
    auto *timer = new QTimer(qApp);
    timer->setInterval(10);
    auto next = std::make_shared<int>(0);
    QObject::connect(timer, &QTimer::timeout, timer, [timer, actions, next] {
        auto *modal = qobject_cast<QDialog *>(QApplication::activeModalWidget());
        if (!modal) return;
        actions.at((*next)++)(modal);
        if (*next == actions.size()) { timer->stop(); timer->deleteLater(); }
    });
    timer->start();
}
std::function<void(QDialog *)> chooseRoot(const QString &root)
{
    return [root](QDialog *modal) {
        auto *picker = qobject_cast<QFileDialog *>(modal);
        QVERIFY(picker);
        QCOMPARE(picker->fileMode(), QFileDialog::Directory);
        picker->setDirectory(root);
        QMetaObject::invokeMethod(picker, "accept");
    };
}
std::function<void(QDialog *)> chooseFiles(const QStringList &paths)
{
    return [paths](QDialog *modal) {
        auto *picker = qobject_cast<QFileDialog *>(modal);
        QVERIFY(picker);
        QCOMPARE(picker->fileMode(), QFileDialog::ExistingFiles);
        QStringList quoted;
        for (const auto &path : paths) quoted << QStringLiteral("\"%1\"").arg(path);
        picker->findChild<QLineEdit *>("fileNameEdit")->setText(quoted.join(' '));
        QMetaObject::invokeMethod(picker, "accept");
    };
}
void importSelection(QDialog *dialog, const QString &root, const QStringList &paths,
                     bool reimport = false, bool confirm = true)
{
    QVector<std::function<void(QDialog *)>> actions{chooseRoot(root), chooseFiles(paths)};
    if (reimport) actions << [confirm](QDialog *modal) {
        auto *box = qobject_cast<QMessageBox *>(modal);
        QVERIFY(box);
        auto *button = box->findChild<QPushButton *>(confirm ? "confirmExternalReimportButton"
                                                            : "cancelExternalReimportButton");
        QVERIFY(button);
        button->click();
    };
    modalActions(actions);
    dialog->findChild<QPushButton *>(reimport ? "reimportExternalCodeButton"
                                             : "importExternalCodeButton")->click();
}
}

class ExternalCodeGuiTest final : public QObject
{
    Q_OBJECT
private slots:
    void inspectorEntryIsExternalOnly()
    {
        MainWindow window;
        window.show();
        auto *button = window.findChild<QPushButton *>("manageExternalCodeButton");
        QVERIFY2(button, "ExternalCode inspector must offer the import management entry");
        QVERIFY(!button->isVisible());
        BlueprintNode node;
        node.id = "external";
        node.type = NodeType::ExternalCode;
        node.name = "External";
        node.description = "Manually supplied contract";
        QVERIFY(window.scene()->addNode(node, {}));
        window.scene()->nodeItem(node.id)->setSelected(true);
        QVERIFY(button->isVisible());
        window.scene()->clearSelection();
        QVERIFY(!button->isVisible());
    }
    void importNestedFilesAndReadOnlyView()
    {
        QTemporaryDir workspace, source;
        const QByteArray bytes("// UTF8 source\r\n\0\xff", 19);
        writeBytes(source.filePath("include/api.h"), "// header\r\n");
        writeBytes(source.filePath("src/api.cpp"), bytes);
        MainWindow window;
        prepare(window, workspace.path());
        QVERIFY(QDir().mkpath(workspace.filePath("project")));
        QVERIFY(window.saveProjectAs(workspace.filePath("project")));
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        const auto undo = window.scene()->undoStack()->index();
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source.path(), {source.filePath("include/api.h"), source.filePath("src/api.cpp")});
        QString error;
        QVERIFY2(ExternalCodeImporter::verifyImport(externalNode(), workspace.path(), &error), qPrintable(error));
        auto *list = dialog->findChild<QListWidget *>("externalImportedFiles");
        QCOMPARE(list->count(), 2);
        QVERIFY(list->item(0)->text().contains("include/api.h"));
        QVERIFY(list->item(0)->text().contains("SHA-256"));
        list->setCurrentRow(1);
        auto *preview = dialog->findChild<QPlainTextEdit *>("externalSourcePreview");
        QVERIFY(preview->isReadOnly());
        QCOMPARE(preview->font(), EditorTheme::codeFont());
        QCOMPARE(preview->toPlainText(), QString::fromUtf8(bytes).replace("\r\n", "\n"));
        QByteArray imported;
        QVERIFY(ExternalCodeImporter::readImportedFile(externalNode(), workspace.path(), "src/api.cpp", imported));
        QCOMPARE(imported, bytes);
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        QCOMPARE(window.scene()->undoStack()->index(), undo);
        QVERIFY(!window.isModified());
        QVERIFY(dialog->findChild<QLabel *>("externalCodeBinding")->text().contains(workspace.path()));
    }
    void cancelPickersAndReimportHaveNoWrites()
    {
        QTemporaryDir workspace, source;
        writeBytes(source.filePath("a.cpp"), "old");
        MainWindow window;
        prepare(window, workspace.path());
        auto *dialog = manage(window);
        QVERIFY(dialog);
        modalActions({[](QDialog *picker) { picker->reject(); }});
        dialog->findChild<QPushButton *>("importExternalCodeButton")->click();
        QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/import-manifest.json")));
        QVERIFY(!QFileInfo::exists(workspace.filePath("external")));
        modalActions({chooseRoot(source.path()), [](QDialog *picker) { picker->reject(); }});
        dialog->findChild<QPushButton *>("importExternalCodeButton")->click();
        QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/import-manifest.json")));
        QVERIFY(!QFileInfo::exists(workspace.filePath("external")));
        importSelection(dialog, source.path(), {source.filePath("a.cpp")});
        QJsonObject manifest;
        QVERIFY(ExternalCodeImporter::importManifest(externalNode(), workspace.path(), manifest));
        writeBytes(source.filePath("a.cpp"), "new");
        importSelection(dialog, source.path(), {source.filePath("a.cpp")}, true, false);
        QJsonObject after;
        QVERIFY(ExternalCodeImporter::importManifest(externalNode(), workspace.path(), after));
        QCOMPARE(after, manifest);
        QByteArray bytes;
        QVERIFY(ExternalCodeImporter::readImportedFile(externalNode(), workspace.path(), "a.cpp", bytes));
        QCOMPARE(bytes, QByteArray("old"));
        importSelection(dialog, source.path(), {source.filePath("a.cpp")}, true);
        QVERIFY(ExternalCodeImporter::readImportedFile(externalNode(), workspace.path(), "a.cpp", bytes));
        QCOMPARE(bytes, QByteArray("new"));
    }
    void verificationFailureClearsAllPreview()
    {
        QTemporaryDir workspace, source;
        writeBytes(source.filePath("a.cpp"), "old");
        MainWindow window;
        prepare(window, workspace.path());
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source.path(), {source.filePath("a.cpp")});
        auto *list = dialog->findChild<QListWidget *>("externalImportedFiles");
        list->setCurrentRow(0);
        QVERIFY(!dialog->findChild<QPlainTextEdit *>("externalSourcePreview")->toPlainText().isEmpty());
        writeBytes(workspace.filePath("external/external/a.cpp"), "tampered");
        dialog->findChild<QPushButton *>("verifyExternalCodeButton")->click();
        QCOMPARE(list->count(), 0);
        QVERIFY(dialog->findChild<QPlainTextEdit *>("externalSourcePreview")->toPlainText().isEmpty());
    }
    void inspectorDraftRequiresApply()
    {
        QTemporaryDir workspace;
        MainWindow window;
        prepare(window, workspace.path());
        window.findChild<QLineEdit *>("nodeNameEdit")->setText("Draft");
        modalActions({[](QDialog *box) { box->reject(); }});
        QVERIFY(!manage(window));
        QCOMPARE(window.document().nodes.first(), externalNode());
        QVERIFY(!QFileInfo::exists(workspace.filePath("generated-project")));
    }
    void contextChangesDismissDialog_data()
    {
        QTest::addColumn<QString>("change");
        for (const auto &change : {"selection", "workspace", "contract", "delete", "new", "open", "saveAs"})
            QTest::newRow(change) << QString::fromLatin1(change);
    }
    void contextChangesDismissDialog()
    {
        QFETCH(QString, change);
        QTemporaryDir workspace;
        MainWindow window;
        prepare(window, workspace.path());
        QVERIFY(QDir().mkpath(workspace.filePath("project")));
        QVERIFY(QDir().mkpath(workspace.filePath("equal-project")));
        QVERIFY(window.saveProjectAs(workspace.filePath("project")));
        QPointer<QDialog> dialog = manage(window);
        QVERIFY(dialog);
        if (change == "selection") window.scene()->clearSelection();
        if (change == "workspace") window.findChild<QLineEdit *>("workspacePathEdit")->setText(workspace.filePath("other"));
        if (change == "contract") { auto node = externalNode(); node.description = "Changed"; QVERIFY(window.scene()->editNode(node.id, node)); }
        if (change == "delete") window.scene()->deleteSelectedItems();
        if (change == "new") QVERIFY(window.newProject());
        if (change == "open") QVERIFY(window.openProject(workspace.filePath("project")));
        if (change == "saveAs") QVERIFY(window.saveProjectAs(workspace.filePath("equal-project")));
        QVERIFY(!dialog || !dialog->isVisible());
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QVERIFY(!dialog);
    }
    void unsafeOutsideAndBackendFailures_data()
    {
        QTest::addColumn<QString>("kind");
        QTest::newRow("outside-root") << QString("outside");
        QTest::newRow("unsupported-extension") << QString("extension");
        QTest::newRow("existing-unowned-destination") << QString("unowned");
    }
    void unsafeOutsideAndBackendFailures()
    {
        QFETCH(QString, kind);
        QTemporaryDir workspace, source, outside;
        const QString path = kind == "outside" ? outside.filePath("a.cpp")
                            : source.filePath(kind == "extension" ? "a.txt" : "a.cpp");
        writeBytes(path, "source");
        if (kind == "unowned") writeBytes(workspace.filePath("external/external/a.cpp"), "unowned");
        MainWindow window;
        prepare(window, workspace.path());
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source.path(), {path});
        QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/import-manifest.json")));
        QCOMPARE(dialog->findChild<QListWidget *>("externalImportedFiles")->count(), 0);
        if (kind == "unowned") QCOMPARE(readBytes(workspace.filePath("external/external/a.cpp")), QByteArray("unowned"));
    }
    void contractEditUndoAndReopenFindManifest()
    {
        QTemporaryDir workspace, source, project;
        writeBytes(source.filePath("a.cpp"), "original");
        MainWindow window;
        prepare(window, workspace.path());
        QVERIFY(window.saveProjectAs(project.path()));
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source.path(), {source.filePath("a.cpp")});
        auto changed = externalNode();
        changed.description = "New applied contract";
        QVERIFY(window.scene()->editNode(changed.id, changed));
        QVERIFY(!ExternalCodeImporter::verifyImport(changed, workspace.path()));
        dialog = manage(window);
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QListWidget *>("externalImportedFiles")->count(), 0);
        window.scene()->undoStack()->undo();
        QVERIFY(ExternalCodeImporter::verifyImport(externalNode(), workspace.path()));
        QVERIFY(window.openProject(project.path()));
        window.scene()->nodeItem("external")->setSelected(true);
        dialog = manage(window);
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QListWidget *>("externalImportedFiles")->count(), 1);
        QVERIFY(!window.isModified());
    }
    void languageAndThemePreserveVerifiedSelection()
    {
        QTemporaryDir workspace, source;
        writeBytes(source.filePath("a.cpp"), "// source <tag>");
        MainWindow window;
        prepare(window, workspace.path());
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source.path(), {source.filePath("a.cpp")});
        auto *list = dialog->findChild<QListWidget *>("externalImportedFiles");
        list->setCurrentRow(0);
        auto *preview = dialog->findChild<QPlainTextEdit *>("externalSourcePreview");
        const auto sourceText = preview->toPlainText();
        QVERIFY(window.setLanguage("en"));
        const auto english = dialog->windowTitle();
        QVERIFY(window.setLanguage("zh_CN"));
        QTRY_VERIFY(dialog->windowTitle() != english);
        window.setTheme(EditorTheme::Theme::Dark);
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(preview->toPlainText(), sourceText);
        QVERIFY(preview->isReadOnly());
        QVERIFY(window.setLanguage("en"));
        window.setTheme(EditorTheme::Theme::Light);
        QCOMPARE(list->currentRow(), 0);
        QCOMPARE(preview->toPlainText(), sourceText);
    }
    void staleDuringNestedDialogs_data()
    {
        QTest::addColumn<QString>("stage");
        QTest::addColumn<QString>("change");
        for (const auto &stage : {"root", "files", "confirmation"})
            for (const auto &change : {"selection", "workspace", "contract", "delete", "new", "open", "saveAs", "destroy"})
                QTest::newRow(qPrintable(QString("%1-%2").arg(stage, change))) << QString(stage) << QString(change);
    }
    void staleDuringNestedDialogs()
    {
        QFETCH(QString, stage);
        QFETCH(QString, change);
        QTemporaryDir workspace, source, project, otherProject;
        writeBytes(source.filePath("a.cpp"), "new");
        auto *window = new MainWindow;
        prepare(*window, workspace.path());
        QVERIFY(window->saveProjectAs(project.path()));
        if (stage == "confirmation") {
            writeBytes(source.filePath("a.cpp"), "old");
            QVERIFY(ExternalCodeImporter::importFiles(externalNode(), source.path(), {"a.cpp"}, workspace.path()));
            writeBytes(source.filePath("a.cpp"), "new");
        }
        QPointer<MainWindow> owner = window;
        QPointer<QDialog> dialog = manage(*window);
        QVERIFY(dialog);
        QVector<std::function<void(QDialog *)>> actions;
        if (stage != "root") actions << chooseRoot(source.path());
        if (stage == "confirmation") actions << chooseFiles({source.filePath("a.cpp")});
        bool changed = false;
        actions << [&](QDialog *) {
            changed = true;
            if (change == "selection") window->scene()->clearSelection();
            if (change == "workspace") window->findChild<QLineEdit *>("workspacePathEdit")->setText(source.path());
            if (change == "contract") { auto node = externalNode(); node.description = "Changed"; QVERIFY(window->scene()->editNode(node.id, node)); }
            if (change == "delete") window->scene()->deleteSelectedItems();
            if (change == "new") QVERIFY(window->newProject());
            if (change == "open") QVERIFY(window->openProject(project.path()));
            if (change == "saveAs") QVERIFY(window->saveProjectAs(otherProject.path()));
            if (change == "destroy") delete window;
        };
        modalActions(actions);
        dialog->findChild<QPushButton *>(stage == "confirmation" ? "reimportExternalCodeButton"
                                                                 : "importExternalCodeButton")->click();
        QVERIFY(changed);
        QVERIFY(!dialog || !dialog->isVisible());
        if (stage == "confirmation") {
            QCOMPARE(readBytes(workspace.filePath("external/external/a.cpp")), QByteArray("old"));
        } else QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/import-manifest.json")));
        delete owner.data();
    }
    void failedAndCancelledProjectSwitchKeepContext()
    {
        QTemporaryDir workspace, project;
        MainWindow window;
        prepare(window, workspace.path());
        QPointer<QDialog> dialog = manage(window);
        QVERIFY(dialog);
        modalActions({[](QDialog *box) { box->reject(); }});
        QVERIFY(!window.openProject(project.path()));
        QVERIFY(dialog && dialog->isVisible());
        modalActions({[](QDialog *box) { box->findChild<QPushButton *>("cancelBlueprintChangesButton")->click(); }});
        QVERIFY(!window.newProject());
        QVERIFY(dialog && dialog->isVisible());
    }
    void draftChangedDuringPickerBlocksWrite()
    {
        QTemporaryDir workspace, source;
        writeBytes(source.filePath("a.cpp"), "source");
        MainWindow window;
        prepare(window, workspace.path());
        auto *dialog = manage(window);
        QVERIFY(dialog);
        modalActions({chooseRoot(source.path()), [&](QDialog *picker) {
            window.findChild<QLineEdit *>("nodeNameEdit")->setText("draft");
            chooseFiles({source.filePath("a.cpp")})(picker);
        }});
        dialog->findChild<QPushButton *>("importExternalCodeButton")->click();
        QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/import-manifest.json")));
        QCOMPARE(window.document().nodes.first(), externalNode());
    }
    void buildStartedDuringPickerBlocksWrite()
    {
        QTemporaryDir workspace, source;
        writeBytes(source.filePath("a.cpp"), "source");
        MainWindow window;
        prepare(window, workspace.path());
        auto *dialog = manage(window);
        QVERIFY(dialog);
        modalActions({chooseRoot(source.path()), [&](QDialog *picker) {
            BuildRequest request;
            request.sourceDirectory = source.path();
            request.buildDirectory = workspace.path();
            request.cmakeExecutable = "cmd.exe";
            request.configureArguments = {"/c", "ping", "-n", "3", "127.0.0.1"};
            QVERIFY(window.findChild<BuildService *>()->start(request));
            QVERIFY(window.findChild<BuildService *>()->isRunning());
            chooseFiles({source.filePath("a.cpp")})(picker);
        }});
        dialog->findChild<QPushButton *>("importExternalCodeButton")->click();
        QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/import-manifest.json")));
    }
    void relativeWorkspaceRejected()
    {
        QTemporaryDir working, source;
        QVERIFY(QDir(working.path()).mkdir("relative"));
        writeBytes(source.filePath("a.cpp"), "source");
        const QString previous = QDir::currentPath();
        struct RestoreDirectory { QString path; ~RestoreDirectory() { QDir::setCurrent(path); } } restore{previous};
        QVERIFY(QDir::setCurrent(working.path()));
        MainWindow window;
        prepare(window, "relative");
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source.path(), {source.filePath("a.cpp")});
        QVERIFY(!QFileInfo::exists(working.filePath("relative/external/external/import-manifest.json")));
    }
    void generationAndCandidateReviewBlockReimport_data()
    {
        QTest::addColumn<bool>("review");
        QTest::newRow("generating") << false;
        QTest::newRow("candidate-review") << true;
    }
    void generationAndCandidateReviewBlockReimport()
    {
        QFETCH(bool, review);
        QTemporaryDir workspace, source;
        writeBytes(source.filePath("a.cpp"), "old");
        QVERIFY(ExternalCodeImporter::importFiles(externalNode(), source.path(), {"a.cpp"}, workspace.path()));
        DeferredClient *client = nullptr;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client;
        });
        prepare(window, workspace.path());
        for (auto pair : {qMakePair("start", NodeType::Start), qMakePair("logic", NodeType::LogicModule), qMakePair("end", NodeType::End)}) {
            BlueprintNode node;
            node.id = pair.first; node.type = pair.second; node.name = pair.first;
            node.description = "Complete contract";
            QVERIFY(window.scene()->addNode(node, {}));
        }
        QVERIFY(window.scene()->connectNodes("start", "external"));
        QVERIFY(window.scene()->connectNodes("external", "logic"));
        QVERIFY(window.scene()->connectNodes("logic", "end"));
        window.scene()->clearSelection();
        window.scene()->nodeItem("external")->setSelected(true);
        auto *dialog = manage(window);
        QVERIFY(dialog);
        auto *controller = window.findChild<GenerationController *>();
        modalActions({chooseRoot(source.path()), [&](QDialog *picker) {
            QVERIFY(controller->start(window.document(), "logic", workspace.path(), AiProviderSettings{}));
            QCOMPARE(controller->state(), GenerationController::State::Generating);
            if (review) {
                emit client->responseReady(client->request.requestId,
                    "{\"nodeId\":\"logic\",\"summary\":\"done\",\"files\":[{\"path\":\"src/modules/logic/implementation/a.cpp\",\"content\":\"// candidate\"}]}");
                QCOMPARE(controller->state(), GenerationController::State::Success);
                QVERIFY(window.findChild<CandidateReviewDialog *>());
            }
            chooseFiles({source.filePath("a.cpp")})(picker);
        }});
        // First import uses the same paths; its write must also be blocked after the picker.
        dialog->findChild<QPushButton *>("importExternalCodeButton")->click();
        writeBytes(source.filePath("a.cpp"), "new");
        const auto generationState = readBytes(workspace.filePath("generation-manifest.json"));
        dialog->findChild<QPushButton *>("reimportExternalCodeButton")->click();
        QCOMPARE(readBytes(workspace.filePath("external/external/a.cpp")), QByteArray("old"));
        QCOMPARE(readBytes(workspace.filePath("generation-manifest.json")), generationState);
        QVERIFY(!dialog->findChild<QFileDialog *>("externalSourceRootDialog")
                || !dialog->findChild<QFileDialog *>("externalSourceRootDialog")->isVisible());
    }
    void reimportRebindsAppliedContractAndRejectsPathChanges()
    {
        QTemporaryDir workspace, source;
        writeBytes(source.filePath("a.cpp"), "old");
        MainWindow window;
        prepare(window, workspace.path());
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source.path(), {source.filePath("a.cpp")});
        auto changed = externalNode();
        changed.description = "New contract";
        QVERIFY(window.scene()->editNode(changed.id, changed));
        dialog = manage(window);
        QVERIFY(dialog);
        QCOMPARE(dialog->findChild<QListWidget *>("externalImportedFiles")->count(), 0);
        writeBytes(source.filePath("a.cpp"), "new");
        importSelection(dialog, source.path(), {source.filePath("a.cpp")}, true);
        QVERIFY(ExternalCodeImporter::verifyImport(changed, workspace.path()));
        QVERIFY(!ExternalCodeImporter::verifyImport(externalNode(), workspace.path()));
        const QByteArray manifest = readBytes(workspace.filePath("external/external/import-manifest.json"));
        writeBytes(source.filePath("b.cpp"), "untracked");
        importSelection(dialog, source.path(), {source.filePath("b.cpp")}, true);
        QCOMPARE(readBytes(workspace.filePath("external/external/import-manifest.json")), manifest);
        QCOMPARE(readBytes(workspace.filePath("external/external/a.cpp")), QByteArray("new"));
        QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/b.cpp")));
    }
    void parentDestroyedDuringApplyFirstWarning()
    {
        QTemporaryDir workspace;
        auto *window = new MainWindow;
        prepare(*window, workspace.path());
        window->findChild<QLineEdit *>("nodeNameEdit")->setText("draft");
        QPointer<MainWindow> owner(window);
        bool seen = false;
        modalActions({[&](QDialog *box) {
            QVERIFY(qobject_cast<QMessageBox *>(box));
            seen = true;
            delete window;
        }});
        window->findChild<QPushButton *>("manageExternalCodeButton")->click();
        QVERIFY(seen);
        QVERIFY(!owner);
    }
    void chineseSourceAndWorkspaceRootsAllowPortableFilePaths()
    {
        QTemporaryDir fixture;
        const QString source = fixture.filePath(QStringLiteral("源码"));
        const QString workspace = fixture.filePath(QStringLiteral("工作区"));
        QVERIFY(QDir().mkpath(workspace));
        writeBytes(QDir(source).filePath("src/a.cpp"), "old");
        MainWindow window;
        prepare(window, workspace);
        auto *dialog = manage(window);
        QVERIFY(dialog);
        importSelection(dialog, source, {QDir(source).filePath("src/a.cpp")});
        QVERIFY(ExternalCodeImporter::verifyImport(externalNode(), workspace));
        QCOMPARE(readBytes(QDir(workspace).filePath("external/external/src/a.cpp")), QByteArray("old"));
        writeBytes(QDir(source).filePath("src/a.cpp"), "new");
        importSelection(dialog, source, {QDir(source).filePath("src/a.cpp")}, true);
        QVERIFY(ExternalCodeImporter::verifyImport(externalNode(), workspace));
        QCOMPARE(readBytes(QDir(workspace).filePath("external/external/src/a.cpp")), QByteArray("new"));
    }
};

int main(int argc, char **argv)
{
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("ExternalCodeGuiTests");
    QCoreApplication::setApplicationName("ExternalCodeGuiTests");
    QSettings().clear();
    ExternalCodeGuiTest test;
    return QTest::qExec(&test, argc, argv);
}
#include "tst_external_code_gui.moc"
