#include "app/main_window.h"
#include "app/candidate_review_dialog.h"
#include "ai/fake_ai_client.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include <QAction>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
class DeferredClient final : public IAiClient
{
public:
    using IAiClient::IAiClient;
    AiRequest request;
    void generate(const AiRequest &value) override { request = value; }
};
QByteArray response()
{
    return "{\"nodeId\":\"logic\",\"summary\":\"done\",\"files\":[{\"path\":\"src/modules/logic/implementation/worker.cpp\",\"content\":\"// candidate\"}]}";
}
class SynchronousClient final : public IAiClient
{
public:
    using IAiClient::IAiClient;
    void generate(const AiRequest &value) override { emit responseReady(value.requestId, response()); }
};
void prepare(MainWindow &window, const QString &workspace)
{
    window.findChild<QLineEdit *>("workspacePathEdit")->setText(workspace);
    for (auto p : {qMakePair("start", NodeType::Start), qMakePair("logic", NodeType::LogicModule), qMakePair("end", NodeType::End)}) {
        BlueprintNode n; n.id = p.first; n.type = p.second; n.name = p.first;
        n.description = "Complete module contract";
        QVERIFY(window.scene()->addNode(n, {}));
    }
    QVERIFY(window.scene()->connectNodes("start", "logic"));
    QVERIFY(window.scene()->connectNodes("logic", "end"));
    window.scene()->nodeItem("logic")->setSelected(true);
    window.show();
}
QByteArray readFile(const QString &path)
{
    QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll();
}
void confirmNextDialog()
{
    QTimer::singleShot(0, [] {
        if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
            box->button(QMessageBox::Yes)->click();
    });
}
}

class GenerationWorkflowTest : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase()
    {
        QCoreApplication::setOrganizationName("BlueprintWorkflowTests");
        QCoreApplication::setApplicationName("OfflineWorkflow");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void init() { QSettings().clear(); }
    void synchronousCompletionUsesCapturedBlueprint()
    {
        QTemporaryDir dir;
        MainWindow window([](const AiProviderSettings &, QObject *parent) {
            return new SynchronousClient(parent);
        });
        prepare(window, dir.path());
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        generate->trigger();
        auto *dialog = window.findChild<CandidateReviewDialog *>();
        QVERIFY(dialog);
        QVERIFY(!generate->isEnabled());
        dialog->findChild<QPushButton *>("acceptCandidateButton")->click();
        QCOMPARE(readFile(dir.path()+"/generated-project/src/modules/logic/implementation/worker.cpp"), QByteArray("// candidate"));
        QVERIFY(readFile(dir.path()+"/generation-manifest.json").contains("accepted"));
    }
    void staleResponseCannotReplaceNewRequest()
    {
        QTemporaryDir dir;
        DeferredClient *client = nullptr;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client;
        });
        prepare(window, dir.path());
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        auto *status = window.findChild<QLabel *>("generationStatus");
        generate->trigger();
        QPointer<DeferredClient> old = client;
        const QUuid oldId = client->request.requestId;
        window.findChild<QAction *>("cancelGenerationAction")->trigger();
        generate->trigger();
        QVERIFY(client != old.data());
        if (old) emit old->responseReady(oldId, response());
        emit client->responseReady(oldId, response());
        QVERIFY(status->text().contains("Generating"));
        QVERIFY(!window.findChild<CandidateReviewDialog *>());
        emit client->responseReady(client->request.requestId, response());
        auto *dialog = window.findChild<CandidateReviewDialog *>();
        QVERIFY(dialog);
        dialog->findChild<QPushButton *>("rejectCandidateButton")->click();
        QVERIFY(readFile(dir.path()+"/generation-manifest.json").contains("rejected"));
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/src/modules/logic/implementation/worker.cpp"));
    }
    void unsafeResponseFailsWithoutReview()
    {
        QTemporaryDir dir;
        MainWindow window([](const AiProviderSettings &, QObject *parent) {
            auto *fake = new FakeAiClient(parent);
            fake->setSuccessfulResponse("invalid response");
            return fake;
        });
        prepare(window, dir.path());
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        generate->trigger();
        QTRY_VERIFY(window.findChild<QLabel *>("generationStatus")->text().contains("Failed"));
        QVERIFY(!window.findChild<CandidateReviewDialog *>());
        QVERIFY(generate->isEnabled());
        QVERIFY(!readFile(dir.path()+"/generation-manifest.json").contains("pending"));
    }
    void selectionEligibilityAndCancellation()
    {
        QTemporaryDir dir;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        auto *cancel = window.findChild<QAction *>("cancelGenerationAction");
        auto *status = window.findChild<QLabel *>("generationStatus");
        QVERIFY(!generate->isEnabled());
        prepare(window, dir.path());
        QVERIFY(generate->isEnabled());
        auto decision = window.document().nodes.at(1);
        decision.type = NodeType::Decision;
        QVERIFY(window.scene()->editNode("logic", decision));
        QVERIFY(generate->isEnabled());
        decision.type = NodeType::UiPage;
        QVERIFY(window.scene()->editNode("logic", decision));
        QVERIFY(generate->isEnabled());
        decision.type = NodeType::LogicModule;
        QVERIFY(window.scene()->editNode("logic", decision));
        window.scene()->nodeItem("start")->setSelected(true);
        QVERIFY(!generate->isEnabled());
        window.scene()->clearSelection();
        window.scene()->nodeItem("start")->setSelected(true);
        QVERIFY(!generate->isEnabled());
        window.scene()->clearSelection();
        window.scene()->nodeItem("logic")->setSelected(true);
        generate->trigger();
        QVERIFY(client); QVERIFY(cancel->isEnabled()); QVERIFY(!generate->isEnabled());
        QVERIFY(status->text().contains("Generating"));
        cancel->trigger();
        QVERIFY(status->text().contains("Cancelled"));
        QVERIFY(!cancel->isEnabled()); QVERIFY(generate->isEnabled());
        if (client) emit client->responseReady(client->request.requestId, response());
        QCoreApplication::processEvents();
        QVERIFY(!window.findChild<CandidateReviewDialog *>());
        QVERIFY(!readFile(dir.path()+"/generation-manifest.json").contains("worker.cpp"));
    }
    void contextChangesCancelAndErrorsStaySafe()
    {
        QTemporaryDir dir;
        QPointer<DeferredClient> client;
        MainWindow window([&](const AiProviderSettings &, QObject *parent) {
            client = new DeferredClient(parent); return client.data();
        });
        prepare(window, dir.path());
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        auto *status = window.findChild<QLabel *>("generationStatus");
        generate->trigger();
        auto changed = window.document().nodes.at(1);
        changed.description = "Changed contract";
        QVERIFY(window.scene()->editNode("logic", changed));
        QVERIFY(status->text().contains("Cancelled"));
        if (client) emit client->responseReady(client->request.requestId, response());
        QVERIFY(!window.findChild<CandidateReviewDialog *>());
        QTemporaryDir retryDir;
        window.findChild<QLineEdit *>("workspacePathEdit")->setText(retryDir.path());
        generate->trigger();
        QVERIFY(client);
        QVERIFY(status->text().contains("Generating"));
        emit client->requestFailed(client->request.requestId,
                                  {AiErrorKind::Authentication, 401, "secret-provider-detail"});
        QVERIFY(status->text().contains("Failed"));
        QVERIFY(!status->text().contains("secret-provider-detail"));
        QVERIFY(window.setLanguage("zh_CN"));
        QVERIFY(!status->text().contains("secret-provider-detail"));
        QVERIFY(window.setLanguage("en"));
        QVERIFY(status->text().contains("Failed"));
        generate->trigger();
        QVERIFY(status->text().contains("Generating"));
        window.findChild<QLineEdit *>("workspacePathEdit")->setText(dir.path()+"/other");
        QVERIFY(status->text().contains("Cancelled"));
    }
    void selectionGenerateReviewAccept()
    {
        QTemporaryDir dir;
        QString model;
        MainWindow window([&](const AiProviderSettings &settings, QObject *parent) {
            model = settings.model;
            auto *fake = new FakeAiClient(parent); fake->setSuccessfulResponse(response()); return fake;
        });
        AiProviderSettings settings; settings.model = "offline-model";
        QSettings store; QVERIFY(settings.save(store));
        prepare(window, dir.path());
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        QVERIFY(generate); QVERIFY(generate->isEnabled()); generate->trigger();
        QTRY_VERIFY(window.findChild<CandidateReviewDialog *>());
        QCOMPARE(model, QString("offline-model"));
        auto *dialog = window.findChild<CandidateReviewDialog *>();
        QVERIFY(dialog->findChild<QListWidget *>("candidateFiles"));
        QCOMPARE(dialog->findChild<QListWidget *>("candidateFiles")->count(), 1);
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/src/modules/logic/implementation/worker.cpp"));
        QVERIFY(readFile(dir.path()+"/generation-manifest.json").contains("pending"));
        confirmNextDialog();
        dialog->findChild<QPushButton *>("acceptCandidateButton")->click();
        QCOMPARE(readFile(dir.path()+"/generated-project/src/modules/logic/implementation/worker.cpp"), QByteArray("// candidate"));
        QVERIFY(readFile(dir.path()+"/generation-manifest.json").contains("accepted"));
    }
    void editRejectLanguageAndContextReview()
    {
        QTemporaryDir dir;
        MainWindow window([](const AiProviderSettings &, QObject *parent) {
            auto *fake = new FakeAiClient(parent);
            fake->setSuccessfulResponse("{\"nodeId\":\"logic\",\"summary\":\"done\",\"files\":[{\"path\":\"src/modules/logic/implementation/a.cpp\",\"content\":\"// a\"},{\"path\":\"src/modules/logic/implementation/b.cpp\",\"content\":\"// b\"},{\"path\":\"src/modules/logic/implementation/c.cpp\",\"content\":\"// c\"}]}");
            return fake;
        });
        prepare(window, dir.path());
        auto *generate = window.findChild<QAction *>("generateSelectedNodeAction");
        generate->trigger();
        QTRY_VERIFY(window.findChild<CandidateReviewDialog *>());
        auto *dialog = window.findChild<CandidateReviewDialog *>();
        QCOMPARE(dialog->windowModality(), Qt::ApplicationModal);
        QVERIFY(!generate->isEnabled());
        auto *files = dialog->findChild<QListWidget *>("candidateFiles");
        auto *editor = dialog->findChild<QPlainTextEdit *>("candidateEditor");
        QCOMPARE(files->count(), 3);
        editor->setPlainText("// edited draft");
        const QString englishReject = dialog->findChild<QPushButton *>("rejectCandidateButton")->text();
        QVERIFY(window.setLanguage("zh_CN"));
        QCOMPARE(editor->toPlainText(), QString("// edited draft"));
        QTRY_VERIFY(dialog->findChild<QPushButton *>("rejectCandidateButton")->text() != englishReject);
        QVERIFY(window.setLanguage("en"));
        QTRY_COMPARE(dialog->findChild<QPushButton *>("rejectCandidateButton")->text(), englishReject);
        dialog->findChild<QPushButton *>("editAcceptCandidateButton")->click();
        QCOMPARE(readFile(dir.path()+"/generated-project/src/modules/logic/implementation/a.cpp"), QByteArray("// edited draft"));
        files->setCurrentRow(1);
        dialog->findChild<QPushButton *>("rejectCandidateButton")->click();
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/src/modules/logic/implementation/b.cpp"));
        files->setCurrentRow(2);
        window.findChild<QLineEdit *>("workspacePathEdit")->setText(dir.path()+"/other");
        QVERIFY(!dialog->findChild<QPushButton *>("acceptCandidateButton")->isEnabled());
        QVERIFY(!dialog->findChild<QPushButton *>("editAcceptCandidateButton")->isEnabled());
        confirmNextDialog();
        dialog->findChild<QPushButton *>("cancelRemainingButton")->click();
        QTRY_VERIFY(!window.findChild<CandidateReviewDialog *>());
        QVERIFY(generate->isEnabled());
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/src/modules/logic/implementation/c.cpp"));
        QVERIFY(readFile(dir.path()+"/generation-manifest.json").contains("rejected"));
    }
private:
    QTemporaryDir m_settings;
};
QTEST_MAIN(GenerationWorkflowTest)
#include "tst_generation_workflow.moc"
