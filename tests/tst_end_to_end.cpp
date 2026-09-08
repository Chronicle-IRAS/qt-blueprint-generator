#include "ai/fake_ai_client.h"
#include "blueprint/blueprint_serializer.h"
#include "blueprint/blueprint_validator.h"
#include "generation/generation_service.h"
#include "generation/ir_compiler.h"
#include "generation/project_scaffolder.h"
#include "generation/prompt_compiler.h"
#include "workspace/build_service.h"
#include "workspace/project_exporter.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSet>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

namespace {
QByteArray readFile(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

bool writeFile(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath()))
        return false;
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

QString sha256(const QByteArray &bytes)
{
    return QString::fromLatin1(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256).toHex());
}

QMap<QString, QByteArray> snapshot(const QString &root)
{
    QMap<QString, QByteArray> files;
    QDirIterator it(root, QDir::Files | QDir::Hidden | QDir::System, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        files.insert(QDir(root).relativeFilePath(path).replace('\\', '/'), readFile(path));
    }
    return files;
}

QJsonObject manifest(const QString &workspace)
{
    return QJsonDocument::fromJson(readFile(workspace + "/generation-manifest.json")).object();
}

std::optional<QString> modulePrompt(const QString &workspace, const QString &nodeId, QString *error)
{
    const QString project = workspace + "/generated-project";
    const QJsonObject ir = QJsonDocument::fromJson(readFile(project + "/src/contracts/blueprint.json")).object();
    auto prompt = PromptCompiler::compileModulePrompt(ir, nodeId, error);
    if (prompt) {
        *prompt += "\n\nPublic types:\n" + QString::fromUtf8(readFile(project + "/src/contracts/types.h"));
        *prompt += "\nNode contract:\n" + QString::fromUtf8(readFile(project + "/src/modules/" + nodeId + "/contract.h"));
    }
    return prompt;
}

struct ProcessResult {
    bool started = false;
    bool finished = false;
    QProcess::ExitStatus exitStatus = QProcess::CrashExit;
    int exitCode = -1;
    QByteArray output;
    QByteArray error;
};

ProcessResult runCtest(const QString &build, const QStringList &arguments)
{
    QProcess process;
    process.setProgram(QString::fromUtf8(TASK10_CTEST_COMMAND));
    process.setArguments(QStringList{"--test-dir", build, "-C", "Debug"} + arguments);
    process.start();
    ProcessResult result;
    result.started = process.waitForStarted(10000);
    if (result.started)
        result.finished = process.waitForFinished(60000);
    if (!result.finished) {
        result.error = process.errorString().toUtf8();
        process.kill();
        process.waitForFinished(5000);
    }
    result.exitStatus = process.exitStatus();
    result.exitCode = process.exitCode();
    result.output = process.readAllStandardOutput();
    result.error += process.readAllStandardError();
    return result;
}
}

class EndToEndTest final : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void loginBlueprintGeneratesExportsBuildsAndPassesQtTests();
    void fakeFailureLeavesWorkspaceUnchanged();
    void manualEditRequiresExplicitOverwrite();

private:
    BlueprintDocument m_document;
    QJsonArray m_responses;
};

void EndToEndTest::initTestCase()
{
    QString error;
    const QString fixture = QString::fromUtf8(TASK10_FIXTURE_DIR);
    const auto document = BlueprintSerializer::fromJson(readFile(fixture + "/blueprint.json"), &error);
    QVERIFY2(document.has_value(), qPrintable(error));
    m_document = *document;
    QCOMPARE(m_document.nodes.size(), 6);
    QCOMPARE(m_document.edges.size(), 5);
    QVERIFY(BlueprintValidator::validate(m_document).isEmpty());
    const auto roundTrip = BlueprintSerializer::fromJson(BlueprintSerializer::toJson(m_document), &error);
    QVERIFY2(roundTrip.has_value(), qPrintable(error));
    QVERIFY(*roundTrip == m_document);
    QJsonParseError parseError;
    const auto responses = QJsonDocument::fromJson(readFile(fixture + "/fake-ai-response.json"), &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    QVERIFY(responses.isArray());
    m_responses = responses.array();
    QCOMPARE(m_responses.size(), 3);
    QSet<QString> expected;
    for (const auto &node : m_document.nodes)
        if (node.type == NodeType::UiPage || node.type == NodeType::LogicModule || node.type == NodeType::Decision)
            expected.insert(node.id);
    QSet<QString> actual;
    for (const auto &response : m_responses) {
        const auto object = response.toObject();
        actual.insert(object.value("nodeId").toString());
        QCOMPARE(object.value("files").toArray().size(), 2);
    }
    QCOMPARE(actual, expected);
    QCOMPARE(IrCompiler::compile(m_document).value("modules").toArray().size(), expected.size());
}

void EndToEndTest::loginBlueprintGeneratesExportsBuildsAndPassesQtTests()
{
    QTemporaryDir root(QDir::tempPath() + "/bp10-XXXXXX");
    QVERIFY(root.isValid());
    const QString workspace = root.filePath("w");
    const QString project = workspace + "/generated-project";
    const QString exported = root.filePath("e");
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(exported));
    QString error;
    QVERIFY2(ProjectScaffolder::create(m_document, workspace, &error), qPrintable(error));
    const auto originalManifest = manifest(workspace);
    const auto originalProject = snapshot(project);
    QVERIFY(!originalProject.isEmpty());
    const auto storedIr = QJsonDocument::fromJson(originalProject.value("src/contracts/blueprint.json")).object();
    QCOMPARE(storedIr.value("project").toObject().value("namespace").toString(), QString("Blueprint_Login_Demo"));
    QCOMPARE(storedIr.value("modules"), IrCompiler::compile(m_document).value("modules"));
    const auto protectedFiles = originalManifest.value("protectedFiles").toObject();
    for (auto it = protectedFiles.begin(); it != protectedFiles.end(); ++it) {
        QVERIFY(originalProject.contains(it.key()));
        QCOMPARE(sha256(originalProject.value(it.key())), it.value().toString());
    }
    QCOMPARE(originalManifest.value("blueprintSha256").toString(),
             sha256(originalProject.value("src/contracts/source-blueprint.json")));

    FakeAiClient client;
    GenerationService service(&client);
    QSignalSpy success(&service, &GenerationService::generationSucceeded);
    QSignalSpy failure(&service, &GenerationService::generationFailed);
    QMap<QString, QByteArray> accepted;
    QSet<QString> expectedTests{"scaffold_smoke"};
    for (const auto &response : m_responses) {
        const QJsonObject object = response.toObject();
        const QString nodeId = object.value("nodeId").toString();
        const auto prompt = modulePrompt(workspace, nodeId, &error);
        QVERIFY2(prompt.has_value(), qPrintable(error));
        QVERIFY(prompt->contains("Namespace: Blueprint_Login_Demo"));
        QVERIFY(prompt->contains(QString::fromUtf8(originalProject.value("src/contracts/types.h"))));
        QVERIFY(prompt->contains(QString::fromUtf8(originalProject.value("src/modules/" + nodeId + "/contract.h"))));
        const auto before = snapshot(workspace);
        client.setSuccessfulResponse(QJsonDocument(object).toJson(QJsonDocument::Compact));
        success.clear();
        const QString generation = "login-" + nodeId;
        const QUuid request = service.generate(*prompt, nodeId, workspace + "/candidates/" + generation + '/' + nodeId);
        QVERIFY(!request.isNull());
        QCOMPARE(success.size(), 0);
        QCOMPARE(failure.size(), 0);
        QCOMPARE(client.requests().last().requestId, request);
        QCOMPARE(client.requests().last().prompt, *prompt);
        QTRY_COMPARE_WITH_TIMEOUT(success.size(), 1, 3000);
        QCOMPARE(failure.size(), 0);
        QCOMPARE(success.first().at(0).toUuid(), request);
        const auto result = qvariant_cast<GenerationResult>(success.first().at(1));
        QCOMPARE(result.nodeId, nodeId);
        QCOMPARE(result.files.size(), 2);
        QCOMPARE(snapshot(workspace), before);
        const auto projectBefore = snapshot(project);
        QVERIFY2(GenerationService::persistCandidate(workspace, generation, result, "fake-login-demo", *prompt, &error), qPrintable(error));
        QCOMPARE(snapshot(project), projectBefore);
        const auto batch = manifest(workspace).value("batches").toObject().value(generation).toObject();
        QCOMPARE(batch.value("nodeId").toString(), nodeId);
        QCOMPARE(batch.value("model").toString(), QString("fake-login-demo"));
        QCOMPARE(batch.value("promptSha256").toString(), sha256(prompt->toUtf8()));
        for (const auto &file : result.files) {
            const auto beforePreview = snapshot(workspace);
            const auto preview = GenerationService::previewCandidate(workspace, generation, nodeId, file.relativePath, &error);
            QVERIFY2(preview.has_value(), qPrintable(error));
            QCOMPARE(snapshot(workspace), beforePreview);
            QCOMPARE(preview->generationId, generation);
            QCOMPARE(preview->nodeId, nodeId);
            QCOMPARE(preview->relativePath, file.relativePath);
            QVERIFY(!preview->currentContent.has_value());
            QVERIFY(preview->currentSha256.isEmpty());
            QVERIFY(preview->baselineSha256.isEmpty());
            QVERIFY(preview->lastGeneratedSha256.isEmpty());
            QVERIFY(!preview->conflict);
            QCOMPARE(preview->candidateContent, file.content.toUtf8());
            QCOMPARE(preview->candidateSha256, sha256(file.content.toUtf8()));
            QCOMPARE(readFile(file.absoluteCandidatePath), file.content.toUtf8());
            QVERIFY2(GenerationService::acceptCandidate(workspace, *preview, false, std::nullopt, &error), qPrintable(error));
            QCOMPARE(readFile(project + '/' + file.relativePath), preview->candidateContent);
            accepted.insert(file.relativePath, preview->candidateContent);
            if (file.relativePath.startsWith("tests/") && file.relativePath.endsWith(".cpp"))
                expectedTests.insert("test_" + sha256(file.relativePath.toUtf8()));
            const auto updated = manifest(workspace);
            const auto record = updated.value("files").toObject().value(file.relativePath).toObject();
            QCOMPARE(record.value("sha256").toString(), preview->candidateSha256);
            QCOMPARE(record.value("nodeId").toString(), nodeId);
            QCOMPARE(record.value("generationId").toString(), generation);
            QCOMPARE(updated.value("batches").toObject().value(generation).toObject()
                         .value("files").toObject().value(file.relativePath).toObject().value("state").toString(), QString("accepted"));
        }
    }
    QCOMPARE(client.requests().size(), 3);
    QCOMPARE(accepted.size(), 6);
    QCOMPARE(expectedTests.size(), 4);
    QCOMPARE(manifest(workspace).value("protectedFiles"), originalManifest.value("protectedFiles"));
    QCOMPARE(manifest(workspace).value("blueprintSha256"), originalManifest.value("blueprintSha256"));
    QCOMPARE(manifest(workspace).value("modules"), originalManifest.value("modules"));
    for (auto it = originalProject.begin(); it != originalProject.end(); ++it)
        QCOMPARE(readFile(project + '/' + it.key()), it.value());
    QVERIFY(writeFile(workspace + "/unrelated.txt", "not part of export"));
    const auto beforeExport = snapshot(workspace);
    QVERIFY2(ProjectExporter::exportProject(workspace, exported, &error), qPrintable(error));
    QCOMPARE(snapshot(workspace), beforeExport);
    auto expectedExport = snapshot(project);
    expectedExport.insert("blueprint.json", originalProject.value("src/contracts/source-blueprint.json"));
    expectedExport.insert("generation-manifest.json", readFile(workspace + "/generation-manifest.json"));
    QCOMPARE(snapshot(exported), expectedExport);
    QVERIFY(!QFileInfo::exists(exported + "/candidates"));
    QVERIFY(!QFileInfo::exists(exported + "/unrelated.txt"));
    const auto reloaded = BlueprintSerializer::fromJson(readFile(exported + "/blueprint.json"), &error);
    QVERIFY2(reloaded.has_value(), qPrintable(error));
    QVERIFY(BlueprintValidator::validate(*reloaded).isEmpty());
    QCOMPARE(IrCompiler::toCanonicalJson(IrCompiler::compile(*reloaded)),
             IrCompiler::toCanonicalJson(IrCompiler::compile(m_document)));

    BuildService buildService;
    QSignalSpy stages(&buildService, &BuildService::stageFinished);
    QSignalSpy output(&buildService, &BuildService::standardOutput);
    QSignalSpy finished(&buildService, &BuildService::finished);
    BuildRequest build;
    build.sourceDirectory = exported;
    build.buildDirectory = root.filePath("b");
    build.cmakeExecutable = QString::fromUtf8(TASK10_CMAKE_COMMAND);
    build.configureArguments = {"-G", QString::fromUtf8(TASK10_CMAKE_GENERATOR),
        "-DCMAKE_MAKE_PROGRAM=" + QString::fromUtf8(TASK10_MAKE_PROGRAM),
        "-DCMAKE_CXX_COMPILER=" + QString::fromUtf8(TASK10_CXX_COMPILER),
        "-DQt6_DIR=" + QString::fromUtf8(TASK10_QT_CMAKE_DIR), "-DBUILD_TESTING=ON"};
    build.buildArguments = {"--config", "Debug", "--parallel", "2"};
    QVERIFY2(buildService.start(build, &error), qPrintable(error));
    QCOMPARE(finished.size(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(finished.size(), 1, 180000);
    const auto result = qvariant_cast<BuildResult>(finished.first().first());
    QVERIFY2(result.success, qPrintable(result.error + result.standardError + result.standardOutput));
    QVERIFY(result.configureStarted);
    QVERIFY(result.buildStarted);
    QCOMPARE(result.configureExitCode, 0);
    QCOMPARE(result.buildExitCode, 0);
    QCOMPARE(stages.size(), 2);
    QCOMPARE(qvariant_cast<BuildStage>(stages.at(0).at(0)), BuildStage::Configure);
    QCOMPARE(qvariant_cast<BuildStage>(stages.at(1).at(0)), BuildStage::Build);
    QCOMPARE(stages.at(0).at(1).toInt(), 0);
    QCOMPARE(stages.at(1).at(1).toInt(), 0);
    QVERIFY(!output.isEmpty());
    QVERIFY(!buildService.isRunning());

    const auto discovery = runCtest(build.buildDirectory, {"--show-only=json-v1"});
    const QByteArray discoveryLog = discovery.error + discovery.output;
    QVERIFY2(discovery.started && discovery.finished, discoveryLog.constData());
    QCOMPARE(discovery.exitStatus, QProcess::NormalExit);
    QVERIFY2(discovery.exitCode == 0, discoveryLog.constData());
    QJsonParseError parseError;
    const auto discovered = QJsonDocument::fromJson(discovery.output, &parseError);
    QCOMPARE(parseError.error, QJsonParseError::NoError);
    const auto tests = discovered.object().value("tests").toArray();
    QCOMPARE(tests.size(), 4);
    QSet<QString> actualTests;
    for (const auto &test : tests) {
        const auto item = test.toObject();
        actualTests.insert(item.value("name").toString());
        QVERIFY(!item.value("command").toArray().isEmpty());
        QVERIFY(QFileInfo::exists(item.value("command").toArray().first().toString()));
    }
    QCOMPARE(actualTests, expectedTests);
    const auto ctest = runCtest(build.buildDirectory, {"--output-on-failure", "--no-tests=error"});
    const QByteArray testLog = ctest.error + ctest.output;
    QVERIFY2(ctest.started && ctest.finished, testLog.constData());
    QCOMPARE(ctest.exitStatus, QProcess::NormalExit);
    QVERIFY2(ctest.exitCode == 0, testLog.constData());
    QVERIFY2(ctest.output.contains("100% tests passed, 0 tests failed out of 4"), testLog.constData());
    qInfo().noquote() << QString::fromUtf8(ctest.output);
    QCOMPARE(snapshot(exported), expectedExport);

    // Retaining an artifact is opt-in. The exporter enforces absolute, existing,
    // empty destinations and leaves a rejected destination untouched.
    if (qEnvironmentVariableIsSet("BLUEPRINT_DEMO_OUTPUT_DIR")) {
        const QString destination = qEnvironmentVariable("BLUEPRINT_DEMO_OUTPUT_DIR");
        const auto before = snapshot(destination);
        const bool exportedDemo = ProjectExporter::exportProject(workspace, destination, &error);
        if (!exportedDemo)
            QCOMPARE(snapshot(destination), before);
        QVERIFY2(exportedDemo, qPrintable(error));
        QCOMPARE(snapshot(destination), expectedExport);
        qInfo().noquote() << "Verified login demo exported to" << destination;
    }
}

void EndToEndTest::fakeFailureLeavesWorkspaceUnchanged()
{
    QTemporaryDir root(QDir::tempPath() + "/bp10-XXXXXX");
    QVERIFY(root.isValid());
    QString error;
    QVERIFY2(ProjectScaffolder::create(m_document, root.path(), &error), qPrintable(error));
    const auto prompt = modulePrompt(root.path(), "login_page", &error);
    QVERIFY2(prompt.has_value(), qPrintable(error));
    const auto before = snapshot(root.path());
    FakeAiClient client;
    client.setFailure("offline demo failure");
    GenerationService service(&client);
    QSignalSpy success(&service, &GenerationService::generationSucceeded);
    QSignalSpy failure(&service, &GenerationService::generationFailed);
    const auto request = service.generate(*prompt, "login_page", root.filePath("candidates/failed/login_page"));
    QVERIFY(!request.isNull());
    QCOMPARE(failure.size(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(failure.size(), 1, 3000);
    QCOMPARE(success.size(), 0);
    QCOMPARE(failure.first().at(0).toUuid(), request);
    QCOMPARE(failure.first().at(1).toString(), QString("offline demo failure"));
    QCOMPARE(client.requests().size(), 1);
    QCOMPARE(client.requests().first().requestId, request);
    QCOMPARE(client.requests().first().prompt, *prompt);
    QCOMPARE(snapshot(root.path()), before);
    QVERIFY(!QFileInfo::exists(root.filePath("candidates")));
}

void EndToEndTest::manualEditRequiresExplicitOverwrite()
{
    QTemporaryDir root(QDir::tempPath() + "/bp10-XXXXXX");
    QVERIFY(root.isValid());
    QString error;
    QVERIFY2(ProjectScaffolder::create(m_document, root.path(), &error), qPrintable(error));
    const auto object = m_responses.first().toObject();
    const auto nodeId = object.value("nodeId").toString();
    const auto prompt = modulePrompt(root.path(), nodeId, &error);
    QVERIFY2(prompt.has_value(), qPrintable(error));
    FakeAiClient client;
    client.setSuccessfulResponse(QJsonDocument(object).toJson(QJsonDocument::Compact));
    GenerationService service(&client);
    QSignalSpy success(&service, &GenerationService::generationSucceeded);
    QSignalSpy failure(&service, &GenerationService::generationFailed);
    const auto request = service.generate(*prompt, nodeId, root.filePath("candidates/manual/" + nodeId));
    QCOMPARE(success.size(), 0);
    QTRY_COMPARE_WITH_TIMEOUT(success.size(), 1, 3000);
    QCOMPARE(success.first().at(0).toUuid(), request);
    QCOMPARE(failure.size(), 0);
    const auto result = qvariant_cast<GenerationResult>(success.first().at(1));
    QVERIFY2(GenerationService::persistCandidate(root.path(), "initial", result, "fake-login-demo", *prompt, &error), qPrintable(error));
    const auto file = result.files.first();
    const auto initial = GenerationService::previewCandidate(root.path(), "initial", nodeId, file.relativePath, &error);
    QVERIFY2(initial.has_value(), qPrintable(error));
    QVERIFY2(GenerationService::acceptCandidate(root.path(), *initial, false, std::nullopt, &error), qPrintable(error));
    const QByteArray manualBytes = file.content.toUtf8() + "\n// User's retained edit.\n";
    QVERIFY(writeFile(root.filePath("generated-project/" + file.relativePath), manualBytes));
    QVERIFY2(GenerationService::persistCandidate(root.path(), "regenerate", result, "fake-login-demo", *prompt, &error), qPrintable(error));
    const auto preview = GenerationService::previewCandidate(root.path(), "regenerate", nodeId, file.relativePath, &error);
    QVERIFY2(preview.has_value(), qPrintable(error));
    QVERIFY(preview->conflict);
    QVERIFY(preview->currentContent.has_value());
    QCOMPARE(*preview->currentContent, manualBytes);
    QCOMPARE(preview->currentSha256, sha256(manualBytes));
    QCOMPARE(preview->baselineSha256, sha256(manualBytes));
    QCOMPARE(preview->lastGeneratedSha256, sha256(file.content.toUtf8()));
    const auto before = snapshot(root.path());
    QVERIFY(!GenerationService::acceptCandidate(root.path(), *preview));
    QCOMPARE(snapshot(root.path()), before);
    QVERIFY2(GenerationService::rejectCandidate(root.path(), "regenerate", nodeId, file.relativePath, &error), qPrintable(error));
    QCOMPARE(readFile(root.filePath("generated-project/" + file.relativePath)), manualBytes);
}

QTEST_GUILESS_MAIN(EndToEndTest)
#include "tst_end_to_end.moc"
