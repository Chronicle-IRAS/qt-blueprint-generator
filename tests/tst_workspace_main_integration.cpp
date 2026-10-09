#include "app/main_window.h"
#include "app/candidate_review_dialog.h"
#include "app/external_code_dialog.h"
#include "app/workspace_browser_widget.h"
#include "ai/ai_client.h"
#include "blueprint/blueprint_project_store.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "generation/project_scaffolder.h"

#include <QAction>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPointer>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTimer>
#include <QtTest>
#include <utility>

namespace {
class DeferredClient final : public IAiClient
{
public:
    using IAiClient::IAiClient;
    AiRequest request;
    void generate(const AiRequest &value) override { request = value; }
    void reply()
    {
        emit responseReady(request.requestId,
            R"({"nodeId":"logic","summary":"done","files":[{"path":"src/modules/logic/implementation/worker.cpp","content":"// candidate"}]})");
    }
};

// Exercise the real modal dialogs, including a second unsaved-owner prompt.
class PromptAnswers final
{
public:
    explicit PromptAnswers(QVector<QMessageBox::ButtonRole> answers) : m_answers(std::move(answers))
    {
        m_timer.setInterval(10);
        QObject::connect(&m_timer, &QTimer::timeout, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box || box == m_last) return;
            m_last = box;
            prompts.append(box->objectName().isEmpty()
                ? qobject_cast<WorkspaceBrowserWidget *>(box->parentWidget())
                    ? QStringLiteral("source") : QStringLiteral("message")
                : box->objectName());
            const auto role = m_next < m_answers.size() ? m_answers.at(m_next++) : QMessageBox::RejectRole;
            for (auto *button : box->buttons()) {
                if (box->buttonRole(button) == role) { button->click(); return; }
            }
            box->reject();
        });
        m_timer.start();
    }
    QStringList prompts;
private:
    QTimer m_timer;
    QPointer<QMessageBox> m_last;
    QVector<QMessageBox::ButtonRole> m_answers;
    qsizetype m_next = 0;
};

QByteArray readBytes(const QString &path)
{
    if (path.isEmpty()) return {};
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}
bool writeBytes(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
void populate(MainWindow &window, bool includeExternal)
{
    for (auto pair : {qMakePair("start", NodeType::Start), qMakePair("logic", NodeType::LogicModule),
                      qMakePair("external", NodeType::ExternalCode), qMakePair("end", NodeType::End)}) {
        if (pair.second == NodeType::ExternalCode && !includeExternal) continue;
        BlueprintNode node;
        node.id = QString::fromLatin1(pair.first);
        node.type = pair.second;
        node.name = node.id;
        node.description = QStringLiteral("Complete contract for ") + node.id;
        QVERIFY(window.scene()->addNode(node, QPointF(window.document().nodes.size() * 220, 0)));
    }
    QVERIFY(window.scene()->connectNodes("start", "logic"));
    if (includeExternal) {
        QVERIFY(window.scene()->connectNodes("logic", "external"));
        QVERIFY(window.scene()->connectNodes("external", "end"));
    } else {
        QVERIFY(window.scene()->connectNodes("logic", "end"));
    }
}
WorkspaceBrowserWidget *browser(MainWindow &window)
{
    return window.findChild<WorkspaceBrowserWidget *>();
}
QPlainTextEdit *sourceEditor(MainWindow &window)
{
    return window.findChild<QPlainTextEdit *>("workspacePreview");
}
void prepare(MainWindow &window, const QString &project, const QString &workspace, bool includeExternal = false)
{
    populate(window, includeExternal);
    if (!project.isEmpty()) QVERIFY(window.saveProjectAs(project));
    QVERIFY(ProjectScaffolder::create(window.document(), workspace));
    QVERIFY(writeBytes(workspace + "/generated-project/src/user.cpp", "// original\n"));
    window.findChild<QLineEdit *>("workspacePathEdit")->setText(workspace);
    QVERIFY(browser(window)->setWorkspacePath(workspace));
    browser(window)->openFile("src/user.cpp");
    QVERIFY(!sourceEditor(window)->isReadOnly());
    window.findChild<QDockWidget *>("workspaceEditorDock")->show();
    window.show();
    QApplication::processEvents();
    sourceEditor(window)->moveCursor(QTextCursor::End);
    sourceEditor(window)->insertPlainText("// unsaved source\n");
    QVERIFY(browser(window)->hasUnsavedChanges());
}
void startGeneration(MainWindow &window)
{
    window.scene()->clearSelection();
    window.scene()->nodeItem("logic")->setSelected(true);
    auto *action = window.findChild<QAction *>("generateSelectedNodeAction");
    QVERIFY(action && action->isEnabled());
    action->trigger();
    QCOMPARE(window.findChild<GenerationController *>()->state(), GenerationController::State::Generating);
}
void verifySource(MainWindow &window, const QString &workspace, const QString &text)
{
    QCOMPARE(browser(window)->workspacePath(), workspace);
    QCOMPARE(window.findChild<QLineEdit *>("workspacePathEdit")->text(), workspace);
    QCOMPARE(sourceEditor(window)->toPlainText(), text);
    QVERIFY(browser(window)->hasUnsavedChanges());
    QVERIFY(window.findChild<QLabel *>("workspaceCurrentPath")->text().contains("src/user.cpp"));
}
}

class WorkspaceMainIntegrationTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("WorkspaceMainIntegrationTests");
        QCoreApplication::setApplicationName("WorkspaceMainIntegrationTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    void init() { QSettings().clear(); }

    void blueprintCancelDoesNotOfferOrDiscardSource()
    {
        QTemporaryDir project, workspace;
        MainWindow window;
        prepare(window, project.path(), workspace.path());
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        const QString source = sourceEditor(window)->toPlainText();
        PromptAnswers answers({QMessageBox::RejectRole, QMessageBox::DestructiveRole});
        QVERIFY(!window.close());
        QCOMPARE(answers.prompts, QStringList{"unsavedBlueprintChangesDialog"});
        QVERIFY(window.isVisible());
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        QVERIFY(window.isModified());
        verifySource(window, workspace.path(), source);
        QCOMPARE(readBytes(workspace.filePath("generated-project/src/user.cpp")), QByteArray("// original\n"));
    }

    void sourceCancelAfterBlueprintDiscardPreservesGenerationAndBothOwners()
    {
        QTemporaryDir project, workspace;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        prepare(window, project.path(), workspace.path());
        startGeneration(window);
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        const int history = window.scene()->undoStack()->index();
        const QString source = sourceEditor(window)->toPlainText();
        PromptAnswers answers({QMessageBox::DestructiveRole, QMessageBox::RejectRole});
        QVERIFY(!window.close());
        QCOMPARE(answers.prompts, (QStringList{"unsavedBlueprintChangesDialog", "source"}));
        QVERIFY(window.isVisible());
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        QCOMPARE(window.scene()->undoStack()->index(), history);
        QVERIFY(window.isModified());
        verifySource(window, workspace.path(), source);
        QCOMPARE(window.findChild<GenerationController *>()->state(), GenerationController::State::Generating);
        QVERIFY(client);
    }

    void acceptedCloseDismissesReviewWithoutWritingCandidates()
    {
        QTemporaryDir project, workspace;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        prepare(window, project.path(), workspace.path());
        startGeneration(window);
        QVERIFY(client);
        client->reply();
        auto *controller = window.findChild<GenerationController *>();
        QCOMPARE(controller->state(), GenerationController::State::Success);
        QPointer<CandidateReviewDialog> review = window.findChild<CandidateReviewDialog *>();
        QVERIFY(review && review->isVisible());
        const auto batch = *controller->candidateBatch();
        const QString candidatePath = workspace.path() + "/candidates/" + batch.generationId
            + "/logic/src/modules/logic/implementation/worker.cpp";
        const auto candidate = readBytes(candidatePath);
        const auto manifest = readBytes(workspace.filePath("generation-manifest.json"));
        QVERIFY(!candidate.isEmpty());
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        PromptAnswers answers({QMessageBox::DestructiveRole, QMessageBox::DestructiveRole});
        QVERIFY(window.close());
        QCOMPARE(answers.prompts, (QStringList{"unsavedBlueprintChangesDialog", "source"}));
        QCOMPARE(controller->state(), GenerationController::State::Idle);
        QVERIFY(!controller->candidateBatch());
        QVERIFY(!review || !review->isVisible());
        QVERIFY(!browser(window)->hasUnsavedChanges());
        QCOMPARE(sourceEditor(window)->toPlainText(), QString("// original\n"));
        QCOMPARE(readBytes(candidatePath), candidate);
        QCOMPARE(readBytes(workspace.filePath("generation-manifest.json")), manifest);
        QTRY_VERIFY(!review);
    }

    void failedExplicitSourceSaveBlocksClose()
    {
        QTemporaryDir project, workspace;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        prepare(window, project.path(), workspace.path());
        startGeneration(window);
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        const QString source = sourceEditor(window)->toPlainText();
        // Protection changed after opening: saving must fail without relying on OS permissions.
        QVERIFY(writeBytes(workspace.filePath("generation-manifest.json"), "{}"));
        PromptAnswers answers({QMessageBox::DestructiveRole, QMessageBox::AcceptRole});
        QVERIFY(!window.close());
        QCOMPARE(answers.prompts, (QStringList{"unsavedBlueprintChangesDialog", "source"}));
        QVERIFY(window.isVisible());
        QVERIFY(window.isModified());
        verifySource(window, workspace.path(), source);
        QCOMPARE(window.findChild<GenerationController *>()->state(), GenerationController::State::Generating);
        QCOMPARE(readBytes(workspace.filePath("generated-project/src/user.cpp")), QByteArray("// original\n"));
    }

    void explicitBlueprintSaveBeforeSourceCancel_data()
    {
        QTest::addColumn<QString>("saveKind");
        QTest::newRow("first-save") << QString("first");
        QTest::newRow("same-directory") << QString("same");
        QTest::newRow("failed-first-save") << QString("failed");
    }
    void explicitBlueprintSaveBeforeSourceCancel()
    {
        QFETCH(QString, saveKind);
        QTemporaryDir project, workspace;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        const bool firstSave = saveKind != "same";
        const bool saveSucceeds = saveKind != "failed";
        if (!saveSucceeds)
            QVERIFY(writeBytes(project.filePath("generation-manifest.json"), "{}"));
        prepare(window, firstSave ? QString() : project.path(), workspace.path());
        startGeneration(window);
        QVERIFY(client);
        client->reply();
        auto *controller = window.findChild<GenerationController *>();
        QCOMPARE(controller->state(), GenerationController::State::Success);
        QPointer<CandidateReviewDialog> review = window.findChild<CandidateReviewDialog *>();
        QVERIFY(review && review->isVisible());
        const auto batch = *controller->candidateBatch();
        const QString candidatePath = workspace.path() + "/candidates/" + batch.generationId
            + "/logic/src/modules/logic/implementation/worker.cpp";
        const auto candidate = readBytes(candidatePath);
        const auto manifest = readBytes(workspace.filePath("generation-manifest.json"));
        QVERIFY(!candidate.isEmpty());
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        const QString source = sourceEditor(window)->toPlainText();
        bool sawDirectory = false;
        QTimer directoryAnswer;
        directoryAnswer.setInterval(10);
        QObject::connect(&directoryAnswer, &QTimer::timeout, [&] {
            auto *picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!picker) return;
            directoryAnswer.stop();
            sawDirectory = true;
            picker->setDirectory(project.path());
            picker->selectFile(project.path());
            static_cast<QDialog *>(picker)->accept();
        });
        directoryAnswer.start();
        PromptAnswers answers({QMessageBox::AcceptRole,
                               saveSucceeds ? QMessageBox::RejectRole : QMessageBox::AcceptRole});
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QCOMPARE(sawDirectory, firstSave);
        QCOMPARE(answers.prompts, (QStringList{"unsavedBlueprintChangesDialog",
                                              saveSucceeds ? "source" : "message"}));
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        verifySource(window, workspace.path(), source);
        QCOMPARE(readBytes(workspace.filePath("generated-project/src/user.cpp")), QByteArray("// original\n"));
        if (saveSucceeds) {
            QCOMPARE(window.projectDirectory(), project.path());
            QVERIFY(!window.isModified());
            const auto saved = BlueprintProjectStore::load(project.path());
            QVERIFY(saved);
            QCOMPARE(saved->document, document);
            QCOMPARE(saved->layout, layout);
        } else {
            QVERIFY(window.projectDirectory().isEmpty());
            QVERIFY(window.isModified());
            QVERIFY(!QFileInfo::exists(project.filePath("blueprint.json")));
            QVERIFY(!QFileInfo::exists(project.filePath("layout.json")));
        }
        // A successful first Save already changed identity, even though closing was cancelled.
        if (firstSave && saveSucceeds) {
            QCOMPARE(controller->state(), GenerationController::State::Idle);
            QVERIFY(!controller->candidateBatch());
            QVERIFY(!review || !review->isVisible());
            QTRY_VERIFY(!review);
        } else {
            QCOMPARE(controller->state(), GenerationController::State::Success);
            QVERIFY(controller->candidateBatch());
            QCOMPARE(controller->candidateBatch()->generationId, batch.generationId);
            QVERIFY(review && review->isVisible());
            QVERIFY(review->findChild<QPushButton *>("acceptCandidateButton")->isEnabled());
        }
        QCOMPARE(readBytes(candidatePath), candidate);
        QCOMPARE(readBytes(workspace.filePath("generation-manifest.json")), manifest);
        QVERIFY(!QFileInfo::exists(workspace.filePath("generated-project/src/modules/logic/implementation/worker.cpp")));
    }

    void successfulProjectTransitionPreservesSourceAndResetsSession_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::addColumn<bool>("completed");
        for (const auto &operation : {"new", "open", "save-as"}) {
            QTest::newRow(qPrintable(QString(operation) + "-generating")) << QString(operation) << false;
            QTest::newRow(qPrintable(QString(operation) + "-review")) << QString(operation) << true;
        }
    }
    void successfulProjectTransitionPreservesSourceAndResetsSession()
    {
        QFETCH(QString, operation);
        QFETCH(bool, completed);
        QTemporaryDir project, workspace, other;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        prepare(window, project.path(), workspace.path());
        QVERIFY(BlueprintProjectStore::save(other.path(), window.document(), window.scene()->layoutSnapshot()));
        startGeneration(window);
        auto *controller = window.findChild<GenerationController *>();
        QVERIFY(client);
        if (completed) client->reply();
        QPointer<CandidateReviewDialog> review = window.findChild<CandidateReviewDialog *>();
        QCOMPARE(bool(review), completed);
        QString candidatePath;
        if (completed) candidatePath = workspace.path() + "/candidates/" + controller->candidateBatch()->generationId
            + "/logic/src/modules/logic/implementation/worker.cpp";
        const auto candidate = readBytes(candidatePath);
        const auto manifest = readBytes(workspace.filePath("generation-manifest.json"));
        const QString source = sourceEditor(window)->toPlainText();
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        PromptAnswers answers({operation == "save-as" ? QMessageBox::AcceptRole : QMessageBox::DestructiveRole});
        if (operation == "new") QVERIFY(window.newProject());
        else if (operation == "open") QVERIFY(window.openProject(other.path()));
        else QVERIFY(window.saveProjectAs(other.path()));
        QCOMPARE(answers.prompts.size(), 1);
        verifySource(window, workspace.path(), source);
        QVERIFY(!window.isModified());
        QCOMPARE(controller->state(), GenerationController::State::Idle);
        QVERIFY(!controller->candidateBatch());
        if (client) client->reply();
        QCOMPARE(controller->state(), GenerationController::State::Idle);
        QVERIFY(!review || !review->isVisible());
        QCOMPARE(readBytes(workspace.filePath("generation-manifest.json")), manifest);
        if (completed) { QCOMPARE(readBytes(candidatePath), candidate); QTRY_VERIFY(!review); }
    }

    void failedOrCancelledProjectTransitionRetainsSourceAndSession()
    {
        QTemporaryDir project, workspace, other, invalid;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        prepare(window, project.path(), workspace.path());
        QVERIFY(BlueprintProjectStore::save(other.path(), window.document(), window.scene()->layoutSnapshot()));
        startGeneration(window);
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        const QString source = sourceEditor(window)->toPlainText();
        {
            PromptAnswers answers({QMessageBox::AcceptRole});
            QVERIFY(!window.openProject(invalid.path()));
            QCOMPARE(answers.prompts.size(), 1);
        }
        {
            PromptAnswers answers({QMessageBox::RejectRole});
            QVERIFY(!window.openProject(other.path()));
            QCOMPARE(answers.prompts, QStringList{"unsavedBlueprintChangesDialog"});
        }
        {
            PromptAnswers answers({QMessageBox::RejectRole});
            QVERIFY(!window.saveProjectAs(other.path()));
            QCOMPARE(answers.prompts, QStringList{"replaceBlueprintProjectDialog"});
        }
        {
            PromptAnswers answers({QMessageBox::AcceptRole});
            QVERIFY(!window.saveProjectAs(invalid.filePath("missing")));
            QCOMPARE(answers.prompts.size(), 1);
        }
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        QCOMPARE(window.projectDirectory(), project.path());
        verifySource(window, workspace.path(), source);
        QCOMPARE(window.findChild<GenerationController *>()->state(), GenerationController::State::Generating);
        QVERIFY(client);
    }

    void fileSaveAndAppearanceRespectDirtyOwners()
    {
        QTemporaryDir project, workspace;
        MainWindow window;
        prepare(window, project.path(), workspace.path());
        QVERIFY(window.findChild<QMenu *>("fileMenu"));
        auto *save = window.findChild<QAction *>("saveProjectAction");
        QVERIFY(save);
        QCOMPARE(save->shortcut(), QKeySequence(QKeySequence::Save));
        QVERIFY(window.findChild<QAction *>("newProjectAction"));
        QVERIFY(window.findChild<QAction *>("openProjectAction"));
        QVERIFY(window.findChild<QAction *>("saveProjectAsAction"));
        QVERIFY(window.findChild<QDockWidget *>("workspaceEditorDock"));
        QVERIFY(window.findChild<QAction *>("workspaceEditorAction"));
        QVERIFY(window.findChild<QPushButton *>("browseWorkspaceButton"));
        const QString source = sourceEditor(window)->toPlainText();
        QVERIFY(window.scene()->moveNode("logic", QPointF(50, 80)));
        const auto layout = window.scene()->layoutSnapshot();
        QVERIFY(window.setLanguage("zh_CN"));
        window.setTheme(EditorTheme::Theme::Dark);
        QVERIFY(window.isModified());
        verifySource(window, workspace.path(), source);
        QVERIFY(window.setLanguage("en"));
        window.setTheme(EditorTheme::Theme::Light);
        QVERIFY(window.isModified());
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        verifySource(window, workspace.path(), source);
        // File Save, including its Ctrl+S binding, owns the blueprint only.
        window.activateWindow();
        sourceEditor(window)->setFocus();
        QApplication::processEvents();
        QTest::keyClick(sourceEditor(window), Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!window.isModified());
        verifySource(window, workspace.path(), source);
        QCOMPARE(readBytes(workspace.filePath("generated-project/src/user.cpp")), QByteArray("// original\n"));
        QVERIFY(BlueprintProjectStore::load(project.path())->layout == layout);
        window.findChild<QPushButton *>("workspaceSaveButton")->click();
        QVERIFY(!browser(window)->hasUnsavedChanges());
        QCOMPARE(readBytes(workspace.filePath("generated-project/src/user.cpp")), source.toUtf8());
        QVERIFY(!window.isModified());
    }

    void nestedExternalPickerProjectTransitionRespectsSource_data()
    {
        QTest::addColumn<bool>("validProject");
        QTest::newRow("successful-open") << true;
        QTest::newRow("failed-open") << false;
    }
    void nestedExternalPickerProjectTransitionRespectsSource()
    {
        QFETCH(bool, validProject);
        QTemporaryDir project, workspace, other;
        MainWindow window;
        prepare(window, project.path(), workspace.path(), true);
        if (validProject)
            QVERIFY(BlueprintProjectStore::save(other.path(), window.document(), window.scene()->layoutSnapshot()));
        window.scene()->nodeItem("external")->setSelected(true);
        window.findChild<QPushButton *>("manageExternalCodeButton")->click();
        QPointer<ExternalCodeDialog> external = window.findChild<ExternalCodeDialog *>();
        QVERIFY(external && external->isVisible());
        const QString source = sourceEditor(window)->toPlainText();
        bool sawPicker = false;
        bool operationResult = true;
        QTimer::singleShot(0, [&] {
            QPointer<QFileDialog> picker = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
            if (!picker) return;
            sawPicker = true;
            PromptAnswers answers({QMessageBox::AcceptRole});
            operationResult = window.openProject(other.path());
            if (picker) picker->reject();
        });
        external->findChild<QPushButton *>("chooseExternalSourceRootButton")->click();
        QVERIFY(sawPicker);
        QCOMPARE(operationResult, validProject);
        verifySource(window, workspace.path(), source);
        if (validProject) {
            QVERIFY(!external || !external->isVisible());
            QTRY_VERIFY(!external);
        } else {
            QVERIFY(external && external->isVisible());
            QVERIFY(external->findChild<QPushButton *>("chooseExternalSourceRootButton")->isEnabled());
        }
        QVERIFY(!QFileInfo::exists(workspace.filePath("external/external/import-manifest.json")));
    }
private:
    QTemporaryDir m_settings;
};

QTEST_MAIN(WorkspaceMainIntegrationTest)
#include "tst_workspace_main_integration.moc"
