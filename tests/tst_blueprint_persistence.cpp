#include "app/main_window.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "ai/ai_client.h"
#include "app/candidate_review_dialog.h"

#include <QSettings>
#include <QMessageBox>
#include <QFile>
#include <QFileDialog>
#include <QAction>
#include <QGraphicsView>
#include <QMenu>
#include <QLineEdit>
#include <QPointer>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include "blueprint/blueprint_project_store.h"
#include <QtTest>
#include <memory>

namespace {
class DelayedClient final : public IAiClient
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

void populate(MainWindow &window)
{
    for (auto pair : {qMakePair("start", NodeType::Start),
                      qMakePair("logic", NodeType::LogicModule),
                      qMakePair("end", NodeType::End)}) {
        BlueprintNode node;
        node.id = QString::fromLatin1(pair.first);
        node.type = pair.second;
        node.name = QStringLiteral("Name ") + node.id;
        node.description = QStringLiteral("Description ") + node.id;
        if (node.type == NodeType::LogicModule) {
            node.inputs = {{QStringLiteral("request"), QStringLiteral("string"),
                            QStringLiteral("Input description")}};
            node.outputs = {{QStringLiteral("result"), QStringLiteral("bool"),
                             QStringLiteral("Output description")}};
            node.constraints = {QStringLiteral("No network access")};
            node.acceptanceCriteria = {QStringLiteral("Returns the result")};
        }
        const QPointF position(window.document().nodes.size() * 250.25, -61.5);
        QVERIFY(window.scene()->addNode(node, position));
    }
    QVERIFY(window.scene()->connectNodes(QStringLiteral("start"), QStringLiteral("logic"),
                                         QStringLiteral("request")));
    QVERIFY(window.scene()->connectNodes(QStringLiteral("logic"), QStringLiteral("end"),
                                         QStringLiteral("result")));
}

QAbstractButton *messageButton(QMessageBox *box, QMessageBox::StandardButton answer)
{
    if (auto *button = box->button(answer)) return button;
    QString name;
    switch (answer) {
    case QMessageBox::Save: name = QStringLiteral("saveBlueprintChangesButton"); break;
    case QMessageBox::Discard: name = QStringLiteral("discardBlueprintChangesButton"); break;
    case QMessageBox::Cancel: name = QStringLiteral("cancelBlueprintChangesButton"); break;
    case QMessageBox::Yes: name = QStringLiteral("replaceBlueprintProjectButton"); break;
    default: break;
    }
    return name.isEmpty() ? nullptr : box->findChild<QPushButton *>(name);
}

void answerNextMessage(QMessageBox::StandardButton answer, bool *seen = nullptr)
{
    QTimer::singleShot(0, [answer, seen] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) return;
        if (seen) *seen = true;
        if (auto *button = messageButton(box, answer)) button->click();
        else box->reject();
    });
}

QByteArray readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) return {};
    return file.readAll();
}

void chooseNextDirectory(const QString &directory, bool *seen = nullptr)
{
    QTimer::singleShot(0, [directory, seen] {
        auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
        if (!dialog) return;
        if (seen) *seen = true;
        dialog->setDirectory(directory);
        dialog->selectFile(directory);
        static_cast<QDialog *>(dialog)->accept();
    });
}
}

class BlueprintPersistenceTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName(QStringLiteral("BlueprintPersistenceTests"));
        QCoreApplication::setApplicationName(QStringLiteral("Persistence"));
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
        QCoreApplication::setAttribute(Qt::AA_DontUseNativeDialogs);
    }
    void init() { QSettings().clear(); }
    void roundTripRestoresGraphPropertiesAndLayout()
    {
        QTemporaryDir project;
        QVERIFY(project.isValid());
        MainWindow original;
        populate(original);
        const BlueprintDocument expected = original.document();
        const auto positions = original.scene()->layoutSnapshot();
        QVERIFY(original.saveProjectAs(project.path()));

        MainWindow reopened;
        QVERIFY(reopened.openProject(project.path()));
        QCOMPARE(reopened.document(), expected);
        QCOMPARE(reopened.scene()->layoutSnapshot(), positions);
        QCOMPARE(reopened.projectDirectory(), project.path());
        QVERIFY(!reopened.isModified());
        QCOMPARE(reopened.scene()->undoStack()->count(), 0);
        QVERIFY(reopened.scene()->edgeItem(expected.edges.first().id));
    }
    void dirtyTracksSavedContentAcrossUndoRedoAndDirectMoves()
    {
        QTemporaryDir project;
        MainWindow window;
        populate(window);
        QVERIFY(window.isModified());
        QVERIFY(window.windowTitle().endsWith(QStringLiteral(" *")));
        QVERIFY(window.saveProjectAs(project.path()));
        QVERIFY(!window.isModified());
        QVERIFY(window.scene()->undoStack()->isClean());
        const QPointF savedPosition = window.scene()->nodePosition(QStringLiteral("logic"));
        QVERIFY(window.scene()->moveNode(QStringLiteral("logic"), savedPosition + QPointF(40, 20)));
        QVERIFY(window.isModified());
        QVERIFY(window.windowTitle().endsWith(QStringLiteral(" *")));
        window.scene()->undoStack()->undo();
        QVERIFY(!window.isModified());
        window.scene()->undoStack()->redo();
        QVERIFY(window.isModified());
        // Returning to identical saved content is clean even at a different undo index.
        QVERIFY(window.scene()->moveNode(QStringLiteral("logic"), savedPosition));
        QVERIFY(!window.isModified());
        QVERIFY(!window.scene()->undoStack()->isClean());
        NodeItem *item = window.scene()->nodeItem(QStringLiteral("logic"));
        item->setPos(savedPosition + QPointF(100, 100));
        QVERIFY(window.isModified());
        QVERIFY(window.windowTitle().endsWith(QStringLiteral(" *")));
        item->setPos(savedPosition);
        QVERIFY(!window.isModified());
        QVERIFY(window.scene()->editNodeText(QStringLiteral("logic"), QStringLiteral("Changed"),
                                            QStringLiteral("Changed description")));
        QVERIFY(window.isModified());
        window.scene()->undoStack()->undo();
        QVERIFY(!window.isModified());
        item->setSelected(true);
        window.setTheme(EditorTheme::Theme::Dark);
        QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
        QVERIFY(!window.isModified());
        QVERIFY(!window.windowTitle().endsWith(QStringLiteral(" *")));
        QCOMPARE(window.scene()->nodePosition(QStringLiteral("logic")), savedPosition);
    }
    void newPromptsToCancelDiscardOrSaveChanges()
    {
        MainWindow window;
        populate(window);
        const auto original = window.document();
        const auto positions = window.scene()->layoutSnapshot();
        const int historyCount = window.scene()->undoStack()->count();
        bool sawPrompt = false;
        answerNextMessage(QMessageBox::Cancel, &sawPrompt);
        QVERIFY(!window.newProject());
        QVERIFY(sawPrompt);
        QCOMPARE(window.document(), original);
        QCOMPARE(window.scene()->layoutSnapshot(), positions);
        QCOMPARE(window.scene()->undoStack()->count(), historyCount);
        QVERIFY(window.isModified());

        answerNextMessage(QMessageBox::Discard);
        QVERIFY(window.newProject());
        QVERIFY(window.document().projectId != original.projectId);
        QCOMPARE(window.document().projectName, QStringLiteral("BlueprintProject"));
        QCOMPARE(window.document().target, QStringLiteral("qt6-widgets-cpp17-cmake"));
        QVERIFY(window.document().nodes.isEmpty());
        QVERIFY(window.document().edges.isEmpty());
        QVERIFY(window.scene()->layoutSnapshot().isEmpty());
        QVERIFY(window.projectDirectory().isEmpty());
        QCOMPARE(window.scene()->undoStack()->count(), 0);
        QVERIFY(!window.isModified());

        QTemporaryDir directory;
        populate(window);
        QVERIFY(window.saveProjectAs(directory.path()));
        QVERIFY(window.scene()->editNodeText(QStringLiteral("logic"), QStringLiteral("Saved changes"),
                                            QStringLiteral("Updated description")));
        const auto saved = window.document();
        answerNextMessage(QMessageBox::Save);
        QVERIFY(window.newProject());
        const auto project = BlueprintProjectStore::load(directory.path());
        QVERIFY(project);
        QCOMPARE(project->document, saved);
        QVERIFY(!window.isModified());
    }
    void saveAsConfirmsReplacingAnExistingProject()
    {
        QTemporaryDir first, second;
        MainWindow window;
        populate(window);
        QVERIFY(window.saveProjectAs(first.path()));
        {
            MainWindow existing;
            populate(existing);
            QVERIFY(existing.saveProjectAs(second.path()));
        }
        const QByteArray oldBlueprint = readFile(second.path() + "/blueprint.json");
        const QByteArray oldLayout = readFile(second.path() + "/layout.json");
        QVERIFY(window.scene()->editNodeText("logic", "Updated", "Updated description"));
        const auto original = window.document();
        const int historyIndex = window.scene()->undoStack()->index();
        const int cleanIndex = window.scene()->undoStack()->cleanIndex();
        bool sawPrompt = false;
        answerNextMessage(QMessageBox::Cancel, &sawPrompt);
        QVERIFY(!window.saveProjectAs(second.path()));
        QVERIFY(sawPrompt);
        QCOMPARE(readFile(second.path() + "/blueprint.json"), oldBlueprint);
        QCOMPARE(readFile(second.path() + "/layout.json"), oldLayout);
        QCOMPARE(window.document(), original);
        QCOMPARE(window.projectDirectory(), first.path());
        QCOMPARE(window.scene()->undoStack()->index(), historyIndex);
        QCOMPARE(window.scene()->undoStack()->cleanIndex(), cleanIndex);
        QVERIFY(window.isModified());
        answerNextMessage(QMessageBox::Yes);
        QVERIFY(window.saveProjectAs(second.path()));
        QCOMPARE(window.projectDirectory(), second.path());
        QCOMPARE(BlueprintProjectStore::load(second.path())->document, original);
        QVERIFY(!window.isModified());
    }
    void fileActionsAndShortcutsUseProjectDirectoryDialogs()
    {
        MainWindow window;
        auto *fileMenu = window.findChild<QMenu *>(QStringLiteral("fileMenu"));
        auto *newAction = window.findChild<QAction *>(QStringLiteral("newProjectAction"));
        auto *openAction = window.findChild<QAction *>(QStringLiteral("openProjectAction"));
        auto *saveAction = window.findChild<QAction *>(QStringLiteral("saveProjectAction"));
        auto *saveAsAction = window.findChild<QAction *>(QStringLiteral("saveProjectAsAction"));
        QVERIFY(fileMenu && newAction && openAction && saveAction && saveAsAction);
        QCOMPARE(newAction->shortcut(), QKeySequence(QKeySequence::New));
        QCOMPARE(openAction->shortcut(), QKeySequence(QKeySequence::Open));
        QCOMPARE(saveAction->shortcut(), QKeySequence(QKeySequence::Save));
        QCOMPARE(saveAsAction->shortcut(), QKeySequence(QKeySequence::SaveAs));
        populate(window);
        const auto document = window.document();
        QTemporaryDir first, second;
        window.show();
        const auto focusCanvas = [&window] {
            window.activateWindow();
            window.graphicsView()->setFocus();
            QApplication::processEvents();
        };
        focusCanvas();
        bool sawDirectoryDialog = false;
        chooseNextDirectory(first.path(), &sawDirectoryDialog);
        QTest::keyClick(window.graphicsView(), Qt::Key_S, Qt::ControlModifier);
        QVERIFY(sawDirectoryDialog);
        QCOMPARE(window.projectDirectory(), first.path());
        QVERIFY(!window.isModified());
        focusCanvas();
        chooseNextDirectory(second.path());
        QTest::keyClick(window.graphicsView(), Qt::Key_S, Qt::ControlModifier | Qt::ShiftModifier);
        QCOMPARE(window.projectDirectory(), second.path());
        focusCanvas();
        QTest::keyClick(window.graphicsView(), Qt::Key_N, Qt::ControlModifier);
        QVERIFY(window.document().nodes.isEmpty());
        QVERIFY(window.projectDirectory().isEmpty());
        focusCanvas();
        chooseNextDirectory(first.path());
        QTest::keyClick(window.graphicsView(), Qt::Key_O, Qt::ControlModifier);
        QCOMPARE(window.document(), document);
        QCOMPARE(window.projectDirectory(), first.path());
        QVERIFY(window.scene()->moveNode("logic", QPointF(100, 200)));
        focusCanvas();
        QTest::keyClick(window.graphicsView(), Qt::Key_S, Qt::ControlModifier);
        QVERIFY(!window.isModified());
        QCOMPARE(BlueprintProjectStore::load(first.path())->layout.value("logic"), QPointF(100, 200));
    }
    void projectSwitchResetsPendingGeneration_data()
    {
        QTest::addColumn<QString>("operation");
        QTest::newRow("new") << QStringLiteral("new");
        QTest::newRow("open-identical") << QStringLiteral("open");
        QTest::newRow("save-as") << QStringLiteral("save-as");
        QTest::newRow("close") << QStringLiteral("close");
    }
    void projectSwitchResetsPendingGeneration()
    {
        QFETCH(QString, operation);
        DelayedClient *client = nullptr;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            return client = new DelayedClient(parent);
        });
        QTemporaryDir first, second, workspace;
        populate(window);
        QVERIFY(window.saveProjectAs(first.path()));
        if (operation == "open")
            QVERIFY(BlueprintProjectStore::save(second.path(), window.document(), window.scene()->layoutSnapshot()));
        auto *workspaceEdit = window.findChild<QLineEdit *>(QStringLiteral("workspacePathEdit"));
        workspaceEdit->setText(workspace.path());
        window.scene()->nodeItem("logic")->setSelected(true);
        window.show();
        auto *generate = window.findChild<QAction *>(QStringLiteral("generateSelectedNodeAction"));
        auto *controller = window.findChild<GenerationController *>();
        QVERIFY(generate->isEnabled());
        generate->trigger();
        QCOMPARE(controller->state(), GenerationController::State::Generating);
        QVERIFY(client);
        const auto manifestBefore = readFile(workspace.path() + "/generation-manifest.json");
        // Layout-only changes retain generation; cancelled close retains it too.
        QVERIFY(window.scene()->moveNode("logic", QPointF(400, 120)));
        QCOMPARE(controller->state(), GenerationController::State::Generating);
        if (operation == "close") {
            answerNextMessage(QMessageBox::Cancel);
            QVERIFY(!window.close());
            QCOMPARE(controller->state(), GenerationController::State::Generating);
            QVERIFY(window.isVisible());
        }
        if (operation == "save-as") {
            QVERIFY(window.saveProjectAs(second.path()));
        } else {
            answerNextMessage(QMessageBox::Discard);
            if (operation == "new") QVERIFY(window.newProject());
            else if (operation == "open") QVERIFY(window.openProject(second.path()));
            else QVERIFY(window.close());
        }
        QCOMPARE(controller->state(), GenerationController::State::Idle);
        client->reply();
        QCOMPARE(controller->state(), GenerationController::State::Idle);
        QVERIFY(!controller->candidateBatch());
        QVERIFY(!window.findChild<CandidateReviewDialog *>());
        QCOMPARE(workspaceEdit->text(), workspace.path());
        QCOMPARE(readFile(workspace.path() + "/generation-manifest.json"), manifestBefore);
    }
    void projectSwitchDismissesCompletedReviewWithoutCandidateWrites_data()
    {
        projectSwitchResetsPendingGeneration_data();
    }
    void projectSwitchDismissesCompletedReviewWithoutCandidateWrites()
    {
        QFETCH(QString, operation);
        DelayedClient *client = nullptr;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            return client = new DelayedClient(parent);
        });
        QTemporaryDir first, second, workspace;
        populate(window);
        QVERIFY(window.saveProjectAs(first.path()));
        if (operation == "open")
            QVERIFY(BlueprintProjectStore::save(second.path(), window.document(), window.scene()->layoutSnapshot()));
        window.findChild<QLineEdit *>("workspacePathEdit")->setText(workspace.path());
        window.scene()->nodeItem("logic")->setSelected(true);
        window.show();
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        auto *controller = window.findChild<GenerationController *>();
        generate->trigger();
        QCOMPARE(controller->state(), GenerationController::State::Generating);
        client->reply();
        QCOMPARE(controller->state(), GenerationController::State::Success);
        QPointer<CandidateReviewDialog> review = window.findChild<CandidateReviewDialog *>();
        QVERIFY(review && review->isVisible());
        QVERIFY(controller->candidateBatch());
        const auto batch = *controller->candidateBatch();
        const QByteArray manifest = readFile(workspace.path() + "/generation-manifest.json");
        const QString candidatePath = workspace.path() + "/candidates/" + batch.generationId
                                      + "/logic/src/modules/logic/implementation/worker.cpp";
        const QByteArray candidate = readFile(candidatePath);
        QVERIFY(!candidate.isEmpty());

        // A failed load must preserve even an already open review and completed batch.
        QTemporaryDir invalid;
        answerNextMessage(QMessageBox::Ok);
        QVERIFY(!window.openProject(invalid.path()));
        QCOMPARE(controller->state(), GenerationController::State::Success);
        QVERIFY(review && review->isVisible());
        QVERIFY(review->findChild<QPushButton *>("acceptCandidateButton")->isEnabled());
        if (operation == "new") QVERIFY(window.newProject());
        else if (operation == "open") QVERIFY(window.openProject(second.path()));
        else if (operation == "save-as") QVERIFY(window.saveProjectAs(second.path()));
        else QVERIFY(window.close());
        QCOMPARE(controller->state(), GenerationController::State::Idle);
        QVERIFY(!controller->candidateBatch());
        QVERIFY(!review || !review->isVisible());
        QCOMPARE(readFile(workspace.path() + "/generation-manifest.json"), manifest);
        QCOMPARE(readFile(candidatePath), candidate);
        QVERIFY(!QFileInfo::exists(workspace.path() + "/generated-project/src/modules/logic/implementation/worker.cpp"));
        QTRY_VERIFY(!review);
    }
    void immediateWindowDestructionAfterDismissingReview_data()
    {
        projectSwitchResetsPendingGeneration_data();
    }
    void immediateWindowDestructionAfterDismissingReview()
    {
        QFETCH(QString, operation);
        DelayedClient *client = nullptr;
        auto window = std::make_unique<MainWindow>(
            [&](const AiProviderSettings &, QObject *parent) {
                return client = new DelayedClient(parent);
            });
        QTemporaryDir first, second, workspace;
        populate(*window);
        QVERIFY(window->saveProjectAs(first.path()));
        if (operation == "open")
            QVERIFY(BlueprintProjectStore::save(second.path(), window->document(), window->scene()->layoutSnapshot()));
        window->findChild<QLineEdit *>("workspacePathEdit")->setText(workspace.path());
        window->scene()->nodeItem("logic")->setSelected(true);
        window->show();
        window->findChild<QAction *>("generateSelectedNodeAction")->trigger();
        auto *controller = window->findChild<GenerationController *>();
        QCOMPARE(controller->state(), GenerationController::State::Generating);
        client->reply();
        QPointer<CandidateReviewDialog> review = window->findChild<CandidateReviewDialog *>();
        QVERIFY(review && review->isVisible());
        QVERIFY(controller->candidateBatch());
        const auto batch = *controller->candidateBatch();
        const QByteArray manifest = readFile(workspace.path() + "/generation-manifest.json");
        const QString candidatePath = workspace.path() + "/candidates/" + batch.generationId
                                      + "/logic/src/modules/logic/implementation/worker.cpp";
        const QByteArray candidate = readFile(candidatePath);
        QVERIFY(!candidate.isEmpty());
        if (operation == "new") QVERIFY(window->newProject());
        else if (operation == "open") QVERIFY(window->openProject(second.path()));
        else if (operation == "save-as") QVERIFY(window->saveProjectAs(second.path()));
        else QVERIFY(window->close());
        QCOMPARE(controller->state(), GenerationController::State::Idle);
        QVERIFY(review && !review->isVisible());
        // Parent teardown must be safe before WA_DeleteOnClose's deferred deletion runs.
        window.reset();
        QVERIFY(!review);
        QCOMPARE(readFile(workspace.path() + "/generation-manifest.json"), manifest);
        QCOMPARE(readFile(candidatePath), candidate);
        QVERIFY(!QFileInfo::exists(workspace.path() + "/generated-project/src/modules/logic/implementation/worker.cpp"));
    }
    void failedAndCancelledTransitionsPreserveDocumentLayoutAndHistory()
    {
        MainWindow window;
        QTemporaryDir current, other, invalid;
        populate(window);
        QVERIFY(window.saveProjectAs(current.path()));
        QVERIFY(BlueprintProjectStore::save(other.path(), window.document(), window.scene()->layoutSnapshot()));
        QVERIFY(window.scene()->moveNode("logic", QPointF(1234, -987)));
        window.scene()->nodeItem("logic")->setSelected(true);
        QVERIFY(window.scene()->beginConnection(QStringLiteral("draft")));
        QVERIFY(window.scene()->chooseConnectionNode("start"));
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        const int count = window.scene()->undoStack()->count();
        const int index = window.scene()->undoStack()->index();
        const int clean = window.scene()->undoStack()->cleanIndex();
        auto *item = window.scene()->nodeItem("logic");
        const auto verifyIntact = [&] {
            QCOMPARE(window.document(), document);
            QCOMPARE(window.scene()->layoutSnapshot(), layout);
            QCOMPARE(window.scene()->undoStack()->count(), count);
            QCOMPARE(window.scene()->undoStack()->index(), index);
            QCOMPARE(window.scene()->undoStack()->cleanIndex(), clean);
            QCOMPARE(window.scene()->nodeItem("logic"), item);
            QVERIFY(item->isSelected());
            QCOMPARE(window.scene()->connectionSource(), QStringLiteral("start"));
            QCOMPARE(window.projectDirectory(), current.path());
            QVERIFY(window.isModified());
        };
        QFile malformed(invalid.path() + "/blueprint.json");
        QVERIFY(malformed.open(QIODevice::WriteOnly));
        malformed.write("{invalid");
        malformed.close();
        answerNextMessage(QMessageBox::Ok);
        QVERIFY(!window.openProject(invalid.path()));
        verifyIntact();
        answerNextMessage(QMessageBox::Cancel);
        QVERIFY(!window.openProject(other.path()));
        verifyIntact();
        answerNextMessage(QMessageBox::Ok);
        QVERIFY(!window.saveProjectAs(invalid.path() + "/missing"));
        verifyIntact();

        // A real protected workspace marker makes saving fail without relying on OS permissions.
        QFile manifest(current.path() + "/generation-manifest.json");
        QVERIFY(manifest.open(QIODevice::WriteOnly));
        manifest.write("{}");
        manifest.close();
        const auto blueprintBytes = readFile(current.path() + "/blueprint.json");
        const auto layoutBytes = readFile(current.path() + "/layout.json");
        answerNextMessage(QMessageBox::Ok);
        QVERIFY(!window.saveProject());
        verifyIntact();
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            answerNextMessage(QMessageBox::Ok);
            messageButton(box, QMessageBox::Save)->click();
        });
        QVERIFY(!window.newProject());
        verifyIntact();
        QCOMPARE(readFile(current.path() + "/blueprint.json"), blueprintBytes);
        QCOMPARE(readFile(current.path() + "/layout.json"), layoutBytes);
    }
    void openAfterSavingTheSameDirectoryUsesTheNewlySavedContent()
    {
        MainWindow window;
        QTemporaryDir directory;
        populate(window);
        QVERIFY(window.saveProjectAs(directory.path()));
        QVERIFY(window.scene()->editNodeText("logic", "Latest", "Latest description"));
        QVERIFY(window.scene()->moveNode("logic", QPointF(500, 600)));
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        answerNextMessage(QMessageBox::Save);
        QVERIFY(window.openProject(directory.path()));
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        QCOMPARE(window.scene()->undoStack()->count(), 0);
        QVERIFY(!window.isModified());
    }
    void cancellingSaveDirectoryAbortsThePendingTransition()
    {
        MainWindow window;
        populate(window);
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        const int history = window.scene()->undoStack()->count();
        QTimer::singleShot(0, [] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            QTimer::singleShot(0, [] {
                if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                    dialog->reject();
            });
            messageButton(box, QMessageBox::Save)->click();
        });
        QVERIFY(!window.newProject());
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        QCOMPARE(window.scene()->undoStack()->count(), history);
        QVERIFY(window.projectDirectory().isEmpty());
        QVERIFY(window.isModified());
        QTimer::singleShot(0, [] {
            if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                dialog->reject();
        });
        QVERIFY(!window.saveProject());
        QCOMPARE(window.document(), document);
        QVERIFY(window.isModified());
    }
    void fileMenuAndDirtyPromptFollowLanguageAndTheme()
    {
        MainWindow window;
        QVERIFY(window.setLanguage(QStringLiteral("zh_CN")));
        window.setTheme(EditorTheme::Theme::Dark);
        QCOMPARE(window.findChild<QMenu *>("fileMenu")->title(), QStringLiteral("文件"));
        QCOMPARE(window.findChild<QAction *>("newProjectAction")->text(), QStringLiteral("新建"));
        QCOMPARE(window.findChild<QAction *>("openProjectAction")->text(), QStringLiteral("打开..."));
        QCOMPARE(window.findChild<QAction *>("saveProjectAction")->text(), QStringLiteral("保存"));
        QCOMPARE(window.findChild<QAction *>("saveProjectAsAction")->text(), QStringLiteral("另存为..."));
        populate(window);
        const auto document = window.document();
        const auto layout = window.scene()->layoutSnapshot();
        QString promptTitle;
        QStringList buttonTexts;
        QTimer::singleShot(0, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            promptTitle = box->windowTitle();
            for (auto choice : {QMessageBox::Save, QMessageBox::Discard, QMessageBox::Cancel})
                buttonTexts.append(messageButton(box, choice)->text());
            messageButton(box, QMessageBox::Cancel)->click();
        });
        QVERIFY(!window.newProject());
        QCOMPARE(promptTitle, QStringLiteral("蓝图有未保存的更改"));
        QCOMPARE(buttonTexts, QStringList({QStringLiteral("保存"), QStringLiteral("放弃"), QStringLiteral("取消")}));
        QCOMPARE(window.document(), document);
        QCOMPARE(window.scene()->layoutSnapshot(), layout);
        QCOMPARE(EditorTheme::theme(), EditorTheme::Theme::Dark);
        QVERIFY(window.isModified());
        QTemporaryDir directory;
        QVERIFY(window.saveProjectAs(directory.path()));
        QVERIFY(window.newProject());
        QVERIFY(window.openProject(directory.path()));
        QCOMPARE(window.currentLanguage(), QStringLiteral("zh_CN"));
        QCOMPARE(EditorTheme::theme(), EditorTheme::Theme::Dark);
        QVERIFY(!window.isModified());
        QVERIFY(window.setLanguage(QStringLiteral("en")));
        QCOMPARE(window.findChild<QMenu *>("fileMenu")->title(), QStringLiteral("File"));
        QCOMPARE(window.findChild<QAction *>("saveProjectAsAction")->text(), QStringLiteral("Save As..."));
    }
private:
    QTemporaryDir m_settings;
};

QTEST_MAIN(BlueprintPersistenceTest)
#include "tst_blueprint_persistence.moc"
