#include "app/generation_controller.h"
#include "ai/fake_ai_client.h"
#include "generation/project_scaffolder.h"
#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonDocument>
#include <QTemporaryDir>
#include <QTranslator>
#include <QtTest>

namespace {
BlueprintDocument document()
{
    BlueprintDocument d;
    d.projectId = "project"; d.projectName = "Demo";
    d.target = "qt6-widgets-cpp17-cmake";
    for (auto p : {qMakePair("start", NodeType::Start), qMakePair("logic", NodeType::LogicModule), qMakePair("end", NodeType::End)}) {
        BlueprintNode n; n.id = p.first; n.type = p.second; n.name = p.first;
        n.description = "Complete contract"; d.nodes.append(n);
    }
    d.edges = {{"a", "start", "logic", {}}, {"b", "logic", "end", {}}};
    return d;
}
QByteArray response()
{
    return R"({"nodeId":"logic","summary":"done","files":[{"path":"src/modules/logic/implementation/worker.cpp","content":"// candidate"}]})";
}
QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
QMap<QString, QByteArray> fileHashes(const QString &root)
{
    QMap<QString, QByteArray> hashes;
    QDirIterator files(root, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while (files.hasNext()) {
        const QString path = files.next();
        hashes.insert(QDir(root).relativeFilePath(path), QCryptographicHash::hash(read(path), QCryptographicHash::Sha256));
    }
    return hashes;
}
class ManualClient : public IAiClient {
public:
    using IAiClient::IAiClient;
    AiRequest last;
    bool synchronous = false;
    void generate(const AiRequest &r) override { last = r; if (synchronous) reply(); }
    void reply() { emit responseReady(last.requestId, response()); }
};
class TestTranslator : public QTranslator {
public:
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *, const char *, int) const override
    {
        return QByteArray(context) == "GenerationController" ? QStringLiteral("translated error") : QString();
    }
};
}
class GenerationControllerTest : public QObject {
    Q_OBJECT
private slots:
    void invalidInputs()
    {
        int calls = 0;
        GenerationController c([&](const AiProviderSettings &, QObject *p) { ++calls; return new FakeAiClient(p); });
        QTemporaryDir dir;
        QVERIFY(!c.start(document(), "start", dir.path(), {}));
        QVERIFY(!c.start(document(), "missing", dir.path(), {}));
        QVERIFY(!c.start(document(), "logic", "relative", {}));
        QVERIFY(!c.start(document(), "logic", dir.path()+"/absent", {}));
        auto settings = AiProviderSettings{}; settings.model.clear();
        QVERIFY(!c.start(document(), "logic", dir.path(), settings));
        auto invalid = document(); invalid.edges.clear();
        QVERIFY(!c.start(invalid, "logic", dir.path(), {}));
        QCOMPARE(calls, 0);
        QCOMPARE(c.state(), GenerationController::State::Failed);
    }
    void scaffoldInitializationKeepsSpecificReasonAndFiles_data()
    {
        QTest::addColumn<QString>("failure");
        QTest::addColumn<QString>("reason");
        QTest::newRow("blueprint-mismatch") << "mismatch" << "Blueprint or scaffold version changed; use a new workspace";
        QTest::newRow("protected-changed") << "changed" << "Scaffold file was manually changed or removed: src/main.cpp";
        QTest::newRow("protected-removed") << "removed" << "Scaffold file was manually changed or removed: src/main.cpp";
        QTest::newRow("untracked-project") << "untracked" << "Refusing to overwrite an untracked project file: src/main.cpp";
    }
    void scaffoldInitializationKeepsSpecificReasonAndFiles()
    {
        QFETCH(QString, failure);
        QFETCH(QString, reason);
        QTemporaryDir dir;
        auto requested = document();
        const QString main = dir.path() + "/generated-project/src/main.cpp";
        if (failure != "untracked") QVERIFY(ProjectScaffolder::create(requested, dir.path()));
        if (failure == "mismatch") requested.nodes[1].description += " revised";
        if (failure == "changed" || failure == "untracked") {
            QVERIFY(QDir().mkpath(QFileInfo(main).absolutePath()));
            QFile file(main); QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write("// keep manual code"), qint64(19)); file.close();
        }
        if (failure == "removed") QVERIFY(QFile::remove(main));
        const auto before = fileHashes(dir.path());
        int calls = 0;
        GenerationController controller([&](const AiProviderSettings &, QObject *parent) {
            ++calls; return new ManualClient(parent);
        });
        QVERIFY(!controller.start(requested, "logic", dir.path(), {}));
        QCOMPARE(controller.state(), GenerationController::State::Failed);
        QCOMPARE(calls, 0);
        QVERIFY(!controller.candidateBatch());
        QVERIFY(controller.diagnostics().isEmpty());
        QCOMPARE(fileHashes(dir.path()), before);
        QVERIFY2(controller.errorMessage().contains(reason), qPrintable(controller.errorMessage()));
        QTemporaryDir empty;
        QVERIFY(controller.start(requested, "logic", empty.path(), {}));
        QCOMPARE(calls, 1);
        QVERIFY(controller.errorMessage().isEmpty());
        controller.cancel();
        QVERIFY(controller.errorMessage().isEmpty());
        QCOMPARE(fileHashes(dir.path()), before);
    }
    void validationFailureExposesDiagnostics()
    {
        int calls = 0;
        GenerationController c([&](const AiProviderSettings &, QObject *p) { ++calls; return new FakeAiClient(p); });
        QTemporaryDir dir;

        // An edge pointing at a missing node leaves both node and edge level problems.
        auto invalid = document();
        invalid.edges = {{"broken", "ghost", "logic", {}}, {"b", "logic", "end", {}}};
        QVERIFY(!c.start(invalid, "logic", dir.path(), {}));
        QCOMPARE(calls, 0);
        QCOMPARE(c.state(), GenerationController::State::Failed);

        const QVector<BlueprintDiagnostic> diagnostics = c.diagnostics();
        QVERIFY(!diagnostics.isEmpty());
        QVERIFY(diagnostics.size() > 1);
        bool sawEdgeDiagnostic = false;
        bool sawNodeDiagnostic = false;
        for (const BlueprintDiagnostic &diagnostic : diagnostics) {
            QVERIFY(!diagnostic.message.isEmpty());
            sawEdgeDiagnostic = sawEdgeDiagnostic || !diagnostic.edgeId.isEmpty();
            sawNodeDiagnostic = sawNodeDiagnostic || !diagnostic.nodeId.isEmpty();
        }
        QVERIFY(sawEdgeDiagnostic);
        QVERIFY(sawNodeDiagnostic);

        // A provider side failure must stay distinguishable from a blueprint failure.
        GenerationController providerController([](const AiProviderSettings &, QObject *p) {
            auto *fake = new FakeAiClient(p);
            fake->setFailure({AiErrorKind::Authentication, 401, "provider detail"});
            return fake;
        });
        QVERIFY(providerController.start(document(), "logic", dir.path(), {}));
        QTRY_COMPARE(providerController.state(), GenerationController::State::Failed);
        QVERIFY(providerController.diagnostics().isEmpty());

        // Stale diagnostics never leak into a later session.
        GenerationController recovered([&](const AiProviderSettings &, QObject *p) {
            auto *fake = new FakeAiClient(p);
            fake->setSuccessfulResponse(response());
            return fake;
        });
        QVERIFY(!recovered.start(invalid, "logic", dir.path(), {}));
        QVERIFY(!recovered.diagnostics().isEmpty());
        QVERIFY(recovered.start(document(), "logic", dir.path(), {}));
        QVERIFY(recovered.diagnostics().isEmpty());
        QTRY_COMPARE(recovered.state(), GenerationController::State::Success);
        QVERIFY(recovered.diagnostics().isEmpty());
    }
    void fakeSuccessPendingOnly()
    {
        FakeAiClient *client = nullptr;
        QString capturedModel;
        GenerationController c([&](const AiProviderSettings &s, QObject *p) {
            capturedModel = s.model;
            client = new FakeAiClient(p); client->setSuccessfulResponse(response()); return client;
        });
        QTemporaryDir dir; auto s = AiProviderSettings{}; s.model = "saved-model";
        QVERIFY(c.start(document(), "logic", dir.path(), s));
        QCOMPARE(capturedModel, QString("saved-model"));
        QCOMPARE(c.state(), GenerationController::State::Generating);
        const auto prompt = client->requests().first().prompt;
        QVERIFY(prompt.contains("Blueprint_"));
        QVERIFY(prompt.contains("using Inputs = QVariantMap"));
        QVERIFY(prompt.contains("virtual Outputs execute"));
        QVERIFY(prompt.contains("src/modules/logic/implementation/"));
        QVERIFY(prompt.contains("\"modules/logic/contract.h\""));
        QVERIFY(prompt.contains("tests/logic/"));
        QVERIFY(prompt.contains(".h, .hpp, .cpp, .cc"));
        QVERIFY(prompt.contains("derive"));
        QVERIFY(prompt.contains("own main()"));
        QVERIFY(!c.start(document(), "logic", dir.path(), s));
        QTRY_COMPARE(c.state(), GenerationController::State::Success);
        QVERIFY(c.candidateBatch().has_value());
        auto batch = *c.candidateBatch();
        QVERIFY(GenerationService::previewCandidate(dir.path(), batch.generationId, "logic", batch.result.files.first().relativePath).has_value());
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/src/modules/logic/implementation/worker.cpp"));
        QVERIFY(read(dir.path()+"/generation-manifest.json").contains("pending"));
    }
    void sanitizedFailure_data()
    {
        QTest::addColumn<AiErrorKind>("kind");
        QTest::addColumn<QString>("expected");
        QTest::newRow("network") << AiErrorKind::Network << "network";
        QTest::newRow("timeout") << AiErrorKind::Timeout << "timed out";
        QTest::newRow("authentication") << AiErrorKind::Authentication << "authentication";
        QTest::newRow("payment") << AiErrorKind::PaymentRequired << "payment";
        QTest::newRow("model") << AiErrorKind::ModelNotFound << "model";
        QTest::newRow("endpoint") << AiErrorKind::EndpointNotFound << "endpoint";
        QTest::newRow("rate") << AiErrorKind::RateLimited << "rate limit";
        QTest::newRow("invalid") << AiErrorKind::InvalidResponse << "invalid";
        QTest::newRow("unavailable") << AiErrorKind::ProviderUnavailable << "unavailable";
        QTest::newRow("credentials") << AiErrorKind::CredentialUnavailable << "credentials";
        QTest::newRow("configuration") << AiErrorKind::InvalidConfiguration << "settings";
        QTest::newRow("unknown") << AiErrorKind::Unknown << "failed";
    }
    void sanitizedFailure()
    {
        QFETCH(AiErrorKind, kind);
        QFETCH(QString, expected);
        GenerationController c([kind](const AiProviderSettings &, QObject *p) {
            auto f = new FakeAiClient(p);
            f->setFailure({kind, 500, "secret-token provider body"}); return f;
        });
        QTemporaryDir dir; QVERIFY(c.start(document(), "logic", dir.path(), {}));
        QTRY_COMPARE(c.state(), GenerationController::State::Failed);
        QVERIFY(!c.errorMessage().contains("secret"));
        QVERIFY(c.errorMessage().contains(expected, Qt::CaseInsensitive));
        QVERIFY(!c.candidateBatch());
        TestTranslator translator;
        QVERIFY(QCoreApplication::installTranslator(&translator));
        QCOMPARE(c.errorMessage(), QStringLiteral("translated error"));
        QCoreApplication::removeTranslator(&translator);
    }
    void invalidResponse()
    {
        GenerationController c([](const AiProviderSettings &, QObject *p) {
            auto f = new FakeAiClient(p); f->setSuccessfulResponse("secret invalid JSON"); return f;
        });
        QTemporaryDir dir; QVERIFY(c.start(document(), "logic", dir.path(), {}));
        QTRY_COMPARE(c.state(), GenerationController::State::Failed);
        QVERIFY(c.errorMessage().contains("invalid", Qt::CaseInsensitive));
        QVERIFY(!c.errorMessage().contains("secret"));
    }
    void cancellationAndStaleDocument()
    {
        ManualClient *client = nullptr;
        GenerationController c([&](const AiProviderSettings &, QObject *p) { return client = new ManualClient(p); });
        QTemporaryDir dir;
        QVERIFY(c.start(document(), "logic", dir.path(), {}));
        c.cancel(); client->reply();
        QCOMPARE(c.state(), GenerationController::State::Cancelled);
        QVERIFY(!c.candidateBatch());
        QVERIFY(c.start(document(), "logic", dir.path(), {}));
        c.invalidateContext(document(), dir.path());
        QCOMPARE(c.state(), GenerationController::State::Generating);
        auto changed = document(); changed.nodes[1].description = "changed";
        c.invalidateContext(changed, dir.path()); client->reply();
        QCOMPARE(c.state(), GenerationController::State::Cancelled);
        QVERIFY(!read(dir.path()+"/generation-manifest.json").contains("pending"));
    }
    void unconditionalResetCancelsAndClearsAllSessionState()
    {
        ManualClient *client = nullptr;
        GenerationController controller([&](const AiProviderSettings &, QObject *parent) {
            return client = new ManualClient(parent);
        });
        QTemporaryDir directory;
        QVERIFY(controller.start(document(), "logic", directory.path(), {}));
        controller.invalidateContext(document(), directory.path());
        QCOMPARE(controller.state(), GenerationController::State::Generating);
        auto *old = client;
        controller.resetContext();
        QCOMPARE(controller.state(), GenerationController::State::Idle);
        old->reply();
        QCOMPARE(controller.state(), GenerationController::State::Idle);
        QVERIFY(!controller.candidateBatch());
        QVERIFY(controller.errorMessage().isEmpty());
        QVERIFY(controller.diagnostics().isEmpty());

        QVERIFY(controller.start(document(), "logic", directory.path(), {}));
        client->reply();
        QCOMPARE(controller.state(), GenerationController::State::Success);
        QVERIFY(controller.candidateBatch());
        const auto manifest = read(directory.path() + "/generation-manifest.json");
        controller.resetContext();
        QCOMPARE(controller.state(), GenerationController::State::Idle);
        QVERIFY(!controller.candidateBatch());
        QCOMPARE(read(directory.path() + "/generation-manifest.json"), manifest);

        auto invalid = document();
        invalid.edges.clear();
        QVERIFY(!controller.start(invalid, "logic", directory.path(), {}));
        QVERIFY(!controller.diagnostics().isEmpty());
        QVERIFY(!controller.errorMessage().isEmpty());
        controller.resetContext();
        QCOMPARE(controller.state(), GenerationController::State::Idle);
        QVERIFY(controller.errorMessage().isEmpty());
        QVERIFY(controller.diagnostics().isEmpty());
        QVERIFY(!controller.candidateBatch());
    }
    void synchronousSuccess()
    {
        GenerationController c([](const AiProviderSettings &, QObject *p) {
            auto f = new ManualClient(p); f->synchronous = true; return f;
        });
        QTemporaryDir dir; QVERIFY(c.start(document(), "logic", dir.path(), {}));
        QCOMPARE(c.state(), GenerationController::State::Success);
        QVERIFY(c.candidateBatch());
    }
    void selectionChangedDuringStartKeepsSnapshot()
    {
        ManualClient *client = nullptr;
        GenerationController c([&](const AiProviderSettings &, QObject *p) { return client = new ManualClient(p); });
        QString selectedId = "logic";
        connect(&c, &GenerationController::stateChanged, &c, [&](GenerationController::State state) {
            if (state == GenerationController::State::Generating) selectedId = "other";
        });
        QTemporaryDir dir;
        QVERIFY(c.start(document(), selectedId, dir.path(), {}));
        QCOMPARE(selectedId, QString("other"));
        client->reply();
        QCOMPARE(c.state(), GenerationController::State::Success);
        QVERIFY(c.candidateBatch());
        QCOMPARE(c.candidateBatch()->result.nodeId, QString("logic"));
    }
    void workspaceChangeAndOldSession()
    {
        ManualClient *client = nullptr;
        GenerationController c([&](const AiProviderSettings &, QObject *p) { return client = new ManualClient(p); });
        QTemporaryDir first, second;
        QVERIFY(c.start(document(), "logic", first.path(), {}));
        auto *old = client;
        c.invalidateContext(document(), second.path());
        QCOMPARE(c.state(), GenerationController::State::Cancelled);
        QVERIFY(c.start(document(), "logic", second.path(), {}));
        old->reply();
        QCOMPARE(c.state(), GenerationController::State::Generating);
        QVERIFY(!c.candidateBatch());
        client->reply();
        QCOMPARE(c.state(), GenerationController::State::Success);
        QCOMPARE(c.candidateBatch()->workspace, second.path());
        QVERIFY(!read(first.path()+"/generation-manifest.json").contains("pending"));
    }
    void destructionDuringStateSignal()
    {
        int requests = 0;
        QPointer<GenerationController> c = new GenerationController(
            [&](const AiProviderSettings &, QObject *p) { ++requests; return new ManualClient(p); });
        connect(c, &GenerationController::stateChanged, this, [&] { delete c.data(); });
        QTemporaryDir dir;
        QVERIFY(c->start(document(), "logic", dir.path(), {}));
        QVERIFY(!c);
        QCOMPARE(requests, 1);
    }
    void scaffoldChangedDuringRequest()
    {
        ManualClient *client = nullptr;
        GenerationController c([&](const AiProviderSettings &, QObject *p) { return client = new ManualClient(p); });
        QTemporaryDir dir; QVERIFY(c.start(document(), "logic", dir.path(), {}));
        QFile f(dir.path()+"/generated-project/src/main.cpp"); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("tampered"); f.close();
        client->reply();
        QCOMPARE(c.state(), GenerationController::State::Failed);
        QVERIFY(!c.candidateBatch());
    }
};
QTEST_GUILESS_MAIN(GenerationControllerTest)
#include "tst_generation_controller.moc"
