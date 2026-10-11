#include "ai/fake_ai_client.h"
#include "app/candidate_review_dialog.h"
#include "app/generation_controller.h"
#include "blueprint/blueprint_serializer.h"
#include "generation/project_scaffolder.h"
#include "workspace/build_service.h"
#include "workspace/project_exporter.h"

#include <QDirIterator>
#include <QEventLoop>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QListWidget>
#include <QProcess>
#include <QPushButton>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>
#include <algorithm>

namespace {
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{}; }
bool write(const QString &path, const QByteArray &bytes) {
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
BlueprintDocument blueprint() { return *BlueprintSerializer::fromJson(read(QString::fromUtf8(RUNTIME_FIXTURE_DIR) + "/blueprint.json")); }
QJsonArray responses() { return QJsonDocument::fromJson(read(QString::fromUtf8(RUNTIME_FIXTURE_DIR) + "/fake-ai-response.json")).array(); }
BuildResult buildProject(const QString &source, const QString &directory, const QStringList &targets = {}) {
    BuildService service;
    BuildRequest request;
    request.sourceDirectory = source; request.buildDirectory = directory;
    request.cmakeExecutable = QString::fromUtf8(RUNTIME_CMAKE_COMMAND);
    request.configureArguments = {"-G", QString::fromUtf8(RUNTIME_CMAKE_GENERATOR),
        "-DCMAKE_MAKE_PROGRAM=" + QString::fromUtf8(RUNTIME_MAKE_PROGRAM),
        "-DCMAKE_CXX_COMPILER=" + QString::fromUtf8(RUNTIME_CXX_COMPILER),
        "-DQt6_DIR=" + QString::fromUtf8(RUNTIME_QT_CMAKE_DIR), "-DBUILD_TESTING=ON"};
    request.buildArguments = {"--parallel", "2"};
    if (!targets.isEmpty()) request.buildArguments += QStringList{"--target"} + targets;
    QEventLoop loop; QTimer timer; timer.setSingleShot(true);
    BuildResult result;
    QObject::connect(&service, &BuildService::finished, &loop, [&](const BuildResult &value) { result = value; loop.quit(); });
    QObject::connect(&timer, &QTimer::timeout, &loop, &QEventLoop::quit);
    if (!service.start(request, &result.error)) return result;
    timer.start(180000); loop.exec();
    if (service.isRunning()) result.error = "Generated project build timed out";
    return result;
}
struct ProcessResult {
    bool started = false, finished = false;
    QProcess::ExitStatus status = QProcess::CrashExit;
    int code = -1;
    QByteArray output;
};
ProcessResult run(const QString &program, const QString &cwd, const QStringList &args = {}, int timeout = 20000) {
    QProcess process; process.setWorkingDirectory(cwd); process.setProgram(program); process.setArguments(args);
    auto env = QProcessEnvironment::systemEnvironment(); env.insert("QT_QPA_PLATFORM", "offscreen");
    env.insert("QT_FORCE_STDERR_LOGGING", "1"); process.setProcessEnvironment(env);
    process.setProcessChannelMode(QProcess::MergedChannels); process.start();
    ProcessResult result; result.started = process.waitForStarted(10000);
    if (result.started) result.finished = process.waitForFinished(timeout);
    if (!result.finished) { process.kill(); process.waitForFinished(5000); }
    result.status = process.exitStatus(); result.code = process.exitCode(); result.output = process.readAll();
    return result;
}
QString executable(const QString &build) {
#ifdef Q_OS_WIN
    return build + "/generated_app.exe";
#else
    return build + "/generated_app";
#endif
}
QString buildLog(const BuildResult &result) { return result.error + result.standardError + result.standardOutput; }
bool runtimeSuccess(const ProcessResult &result) {
    return result.started && result.finished && result.status == QProcess::NormalExit && result.code == 0
        && result.output.contains("RUNTIME_CHAIN_OK");
}
}

class RuntimeEndToEndTest final : public QObject {
    Q_OBJECT
private slots:
    void productionGenerationAcceptanceBuildLaunchAndExport() {
        QTemporaryDir root(QDir::tempPath() + "/bprt-XXXXXX"); QVERIFY(root.isValid());
        const QString workspace = root.filePath("w"), project = workspace + "/generated-project", build = root.filePath("b");
        QVERIFY(QDir().mkpath(workspace)); QString error;
        QVERIFY2(ProjectScaffolder::createRuntime(blueprint(), workspace, &error), qPrintable(error));
        // Configuring and compiling only the static library/smoke cannot prove the runtime can link.
        auto result = buildProject(project, build, {"blueprint_modules", "scaffold_smoke"});
        QVERIFY2(result.success, qPrintable(buildLog(result)));
        result = buildProject(project, build);
        QVERIFY(!result.success); QVERIFY(result.buildStarted);
        QVERIFY2(buildLog(result).contains("undefined reference") && buildLog(result).contains("createModule_"), qPrintable(buildLog(result)));
        auto accept = [&](const BlueprintDocument &document, const QJsonObject &response) {
            GenerationController controller([&](const AiProviderSettings &, QObject *owner) {
                auto *fake = new FakeAiClient(owner); fake->setSuccessfulResponse(QJsonDocument(response).toJson(QJsonDocument::Compact)); return fake;
            });
            QVERIFY(controller.startRuntime(document, response.value("nodeId").toString(), workspace, {}));
            QTRY_COMPARE_WITH_TIMEOUT(controller.state(), GenerationController::State::Success, 5000);
            QVERIFY(controller.candidateBatch()); const auto batch = *controller.candidateBatch();
            QCOMPARE(batch.mode, ProjectScaffolder::Mode::LinearRuntime);
            CandidateReviewDialog dialog(document, workspace, batch.generationId, batch.result, nullptr, batch.mode);
            auto *list = dialog.findChild<QListWidget *>("candidateFiles");
            auto *button = dialog.findChild<QPushButton *>("acceptCandidateButton"); QVERIFY(list && button);
            for (int index = 0; index < list->count(); ++index) {
                list->setCurrentRow(index); QVERIFY(button->isEnabled()); button->click();
                const auto &file = batch.result.files[index];
                QCOMPARE(read(project + '/' + file.relativePath), file.content.toUtf8());
            }
        };
        const auto fixture = responses(); QCOMPARE(fixture.size(), 3);
        accept(blueprint(), fixture[0].toObject());
        result = buildProject(project, build); QVERIFY(!result.success);
        QVERIFY2(buildLog(result).contains("createModule_fe7bfcc196ec0711"), qPrintable(buildLog(result)));
        accept(blueprint(), fixture[1].toObject()); accept(blueprint(), fixture[2].toObject());
        const QString producer = project + "/src/modules/z_first/implementation/producer.cpp";
        const auto originalProducer = read(producer);
        auto noFactory = originalProducer; noFactory.replace("createModule_073fde743a5b3153(QObject", "oldFactory(QObject");
        QVERIFY(write(producer, noFactory)); result = buildProject(project, build);
        QVERIFY(!result.success); QVERIFY2(buildLog(result).contains("createModule_073fde743a5b3153"), qPrintable(buildLog(result)));
        QVERIFY(write(producer, originalProducer)); result = buildProject(project, build);
        QVERIFY2(result.success, qPrintable(buildLog(result)));
        auto launched = run(executable(build), build);
        QVERIFY2(runtimeSuccess(launched), launched.output.constData());

        const auto ctest = run(QString::fromUtf8(RUNTIME_CTEST_COMMAND), build,
                               {"--test-dir", build, "--output-on-failure", "--no-tests=error"}, 60000);
        QVERIFY(ctest.started && ctest.finished); QCOMPARE(ctest.status, QProcess::NormalExit);
        QVERIFY2(ctest.code == 0, ctest.output.constData());
        QVERIFY2(ctest.output.contains("0 tests failed out of 4"), ctest.output.constData());

        // A semantically identical order change still uses the exact same protected sources,
        // and its newly generated candidates produce the same running chain.
        auto reordered = blueprint(); std::reverse(reordered.nodes.begin(), reordered.nodes.end()); std::reverse(reordered.edges.begin(), reordered.edges.end());
        QVERIFY2(ProjectScaffolder::createRuntime(reordered, workspace, &error), qPrintable(error));
        for (const auto &item : fixture) accept(reordered, item.toObject());
        result = buildProject(project, build); QVERIFY2(result.success, qPrintable(buildLog(result)));
        launched = run(executable(build), build); QVERIFY2(runtimeSuccess(launched), launched.output.constData());

        const QString exported = root.filePath("e"), exportBuild = root.filePath("eb"); QVERIFY(QDir().mkpath(exported));
        QVERIFY2(ProjectExporter::exportProject(workspace, exported, &error), qPrintable(error));
        QVERIFY(QFileInfo::exists(exported + "/src/runtime/workflow_controller.cpp"));
        result = buildProject(exported, exportBuild); QVERIFY2(result.success, qPrintable(buildLog(result)));
        launched = run(executable(exportBuild), exportBuild); QVERIFY2(runtimeSuccess(launched), launched.output.constData());
        const auto exportTests = run(QString::fromUtf8(RUNTIME_CTEST_COMMAND), exportBuild,
                                    {"--test-dir", exportBuild, "--output-on-failure", "--no-tests=error"}, 60000);
        QVERIFY(exportTests.started && exportTests.finished); QCOMPARE(exportTests.status, QProcess::NormalExit);
        QVERIFY2(exportTests.code == 0 && exportTests.output.contains("0 tests failed out of 4"), exportTests.output.constData());

        // Mutants affect only this temporary generated tree. Each must defeat the actual
        // process oracle; restoring the source must restore a working executable.
        const QString controller = project + "/src/runtime/workflow_controller.cpp";
        const auto originalController = read(controller);
        QVector<QPair<QString, QByteArray>> mutations;
        auto noExecute = originalController; QVERIFY(noExecute.contains("module0->execute(inputs0)")); noExecute.replace("module0->execute(inputs0)", "Outputs{}"); mutations.append({controller, noExecute});
        auto wrongBinding = originalController; QVERIFY(wrongBinding.contains("outputs0.value(")); wrongBinding.replace("outputs0.value(", "Outputs{}.value("); mutations.append({controller, wrongBinding});
        auto noShow = originalController; QVERIFY(noShow.contains("m_page->show();")); noShow.replace("m_page->show();", "// removed show"); mutations.append({controller, noShow});
        for (int index = 0; index < mutations.size(); ++index) {
            QVERIFY(write(mutations[index].first, mutations[index].second));
            result = buildProject(project, build); QVERIFY2(result.success, qPrintable(buildLog(result)));
            launched = run(executable(build), build, {}, index == 2 ? 1500 : 20000);
            QVERIFY2(!runtimeSuccess(launched), "Runtime oracle failed to detect a controller mutant");
            QVERIFY(write(controller, originalController));
        }
        const QByteArray outputLine = "return {{QStringLiteral(\"message\"), QStringLiteral(\"hello\")}};";
        QVERIFY(originalProducer.contains(outputLine));
        const QVector<QByteArray> badOutputs{
            "return {};", "return {{QStringLiteral(\"message\"), 123}};",
            "return {{QStringLiteral(\"message\"), QVariant{}}};",
            "return {{QStringLiteral(\"message\"), QStringLiteral(\"hello\")}, {QStringLiteral(\"extra\"), 1}};",
            "throw std::runtime_error(\"execute failed\");"};
        for (const auto &replacement : badOutputs) {
            auto mutant = originalProducer; mutant.replace(outputLine, replacement); QVERIFY(write(producer, mutant));
            result = buildProject(project, build); QVERIFY2(result.success, qPrintable(buildLog(result)));
            launched = run(executable(build), build); QVERIFY(launched.started && launched.finished);
            QCOMPARE(launched.status, QProcess::NormalExit); QVERIFY(launched.code != 0);
            QVERIFY2(launched.output.contains("Runtime startup failed"), launched.output.constData());
        }
        auto nullFactory = originalProducer; nullFactory.replace("return std::make_unique<ArbitraryProducer>(parent);", "return {};");
        QVERIFY(write(producer, nullFactory)); result = buildProject(project, build); QVERIFY2(result.success, qPrintable(buildLog(result)));
        launched = run(executable(build), build); QVERIFY(launched.started && launched.finished);
        QCOMPARE(launched.status, QProcess::NormalExit); QVERIFY(launched.code != 0);
        QVERIFY2(launched.output.contains("factory returned null"), launched.output.constData());
        QVERIFY(write(producer, originalProducer)); result = buildProject(project, build); QVERIFY2(result.success, qPrintable(buildLog(result)));
        launched = run(executable(build), build); QVERIFY2(runtimeSuccess(launched), launched.output.constData());
    }
    void logicOnlyExitsAfterExecutingAndDirectStartEndExits() {
        for (bool hasLogic : {false, true}) {
            QTemporaryDir root(QDir::tempPath() + "/bprt-XXXXXX"); QVERIFY(root.isValid());
            auto doc = blueprint(); doc.nodes.remove(2, 2);
            doc.edges = {{"one", "start", "z_first", {}}, {"two", "z_first", "end", {}}};
            if (!hasLogic) { doc.nodes.remove(1); doc.edges = {{"one", "start", "end", {}}}; }
            QString error; QVERIFY2(ProjectScaffolder::createRuntime(doc, root.path(), &error), qPrintable(error));
            const QString project = root.path() + "/generated-project";
            if (hasLogic) {
                auto source = responses()[0].toObject().value("files").toArray()[0].toObject().value("content").toString().toUtf8();
                source.replace("qApp->setProperty(\"runtimeOrder\", QStringLiteral(\"first\"));", "std::puts(\"LOGIC_EXECUTED\"); std::fflush(stdout);");
                source.prepend("#include <cstdio>\n");
                QVERIFY(write(project + "/src/modules/z_first/implementation/producer.cpp", source));
            }
            const QString build = root.filePath("b"); const auto built = buildProject(project, build); QVERIFY2(built.success, qPrintable(buildLog(built)));
            const auto launched = run(executable(build), build); QVERIFY(launched.started && launched.finished);
            QCOMPARE(launched.status, QProcess::NormalExit); QCOMPARE(launched.code, 0);
            if (hasLogic) QVERIFY2(launched.output.contains("LOGIC_EXECUTED"), launched.output.constData());
        }
    }
    void exactTypesSafePortTextAndInputProjectionExecute() {
        QTemporaryDir root(QDir::tempPath() + "/bprt-XXXXXX"); QVERIFY(root.isValid());
        auto doc = blueprint(); doc.nodes.remove(3);
        doc.edges = {{"one", "start", "z_first", {}}, {"two", "z_first", "a_second", {}}, {"three", "a_second", "end", {}}};
        doc.nodes[1].outputs.clear(); doc.nodes[2].inputs.clear(); doc.nodes[2].outputs.clear();
        const QString unusual = QString::fromUtf8("中文\"\\\x01" "F\xc2\xa0\xe2\x80\xa8");
        doc.nodes[1].name = unusual;
        // Build the candidate's expected key independently as UTF-16 code units,
        // so the runtime source's UTF-8 hex encoding must preserve every character.
        QString keyFunction = "QString key(const QString &suffix) { QString text; ";
        for (QChar character : unusual) keyFunction += "text.append(QChar(0x" + QString::number(character.unicode(), 16) + ")); ";
        keyFunction += "return text + suffix; }\n";
        const QVector<QPair<QString, QString>> values{
            {"bool", "true"}, {"int", "7"}, {"double", "3.5"}, {"QString", "QStringLiteral(\"value\")"},
            {"QByteArray", "QByteArray(\"bytes\")"}, {"QStringList", "QStringList{QStringLiteral(\"item\")}"},
            {"QVariantList", "QVariantList{1, QStringLiteral(\"item\")}"},
            {"QVariantMap", "QVariantMap{{QStringLiteral(\"nested\"), 42}}"},
            {"QVariant", "QVariant::fromValue(QSize(2, 3))"}};
        QString outputPairs, inputChecks;
        for (const auto &value : values) {
            const auto port = PortSpec{unusual + value.first, value.first, {}};
            doc.nodes[1].outputs.append(port); doc.nodes[2].inputs.append(port);
            if (!outputPairs.isEmpty()) outputPairs += ", ";
            outputPairs += "{key(QStringLiteral(\"" + value.first + "\")), QVariant::fromValue(" + value.second + ")}";
            const QString expected = value.first == "QVariant" ? "QMetaType::QSize" : "QMetaType::" + (value.first == "bool" ? "Bool" : value.first == "int" ? "Int" : value.first == "double" ? "Double" : value.first);
            inputChecks += "if (inputs.value(key(QStringLiteral(\"" + value.first + "\"))).metaType().id() != " + expected + ") throw std::runtime_error(\"type or key mismatch\");\n";
        }
        doc.nodes[1].outputs.append({"private", "bool", {}});
        outputPairs += ", {QStringLiteral(\"private\"), true}";
        QString error; QVERIFY2(ProjectScaffolder::createRuntime(doc, root.path(), &error), qPrintable(error));
        const QString project = root.path() + "/generated-project";
        const QString includes = "#include <QSize>\n#include <QStringList>\n#include <cstdio>\n#include <stdexcept>\n";
        const QString first = "#include \"modules/z_first/contract.h\"\n" + includes + "namespace Blueprint_Runtime_Linear {\n" + keyFunction
            + "class Producer : public Module_073fde743a5b3153 { public: Outputs execute(const Inputs &) override { return {" + outputPairs + "}; } };\n"
            + "std::unique_ptr<Module_073fde743a5b3153> createModule_073fde743a5b3153(QObject *) { return std::make_unique<Producer>(); }\n}\n";
        // The helper is TU-local; no candidate registry or global symbol convention is needed.
        const QString second = "#include \"modules/a_second/contract.h\"\n" + includes + "namespace Blueprint_Runtime_Linear { namespace {\n" + keyFunction + "}\n"
            + "class Consumer : public Module_fe7bfcc196ec0711 { public: Outputs execute(const Inputs &inputs) override {\n"
            + "if (inputs.size() != 9 || inputs.contains(\"private\")) throw std::runtime_error(\"unexpected input keys\");\n" + inputChecks
            + "std::puts(\"RUNTIME_CHAIN_OK\"); std::fflush(stdout); return {}; } };\n"
            + "std::unique_ptr<Module_fe7bfcc196ec0711> createModule_fe7bfcc196ec0711(QObject *) { return std::make_unique<Consumer>(); }\n}\n";
        QVERIFY(write(project + "/src/modules/z_first/implementation/producer.cpp", first.toUtf8()));
        QVERIFY(write(project + "/src/modules/a_second/implementation/consumer.cpp", second.toUtf8()));
        const QString build = root.filePath("b"); const auto built = buildProject(project, build);
        QVERIFY2(built.success, qPrintable(buildLog(built)));
        const auto launched = run(executable(build), build); QVERIFY2(runtimeSuccess(launched), launched.output.constData());
    }
};
QTEST_MAIN(RuntimeEndToEndTest)
#include "tst_runtime_end_to_end.moc"
