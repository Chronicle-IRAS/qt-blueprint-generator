#include "generation/project_scaffolder.h"
#include "workspace/external_code_importer.h"
#include "workspace/build_service.h"
#include "workspace/project_exporter.h"
#include "app/main_window.h"

#include <QCryptographicHash>
#include <QDirIterator>
#include <QFile>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
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
    void buildServiceStopsAfterFailedConfigure();
    void buildServiceConfiguresAndBuildsGeneratedProject();
    void buildServiceRejectsOwnedCmakeArguments_data();
    void buildServiceRejectsOwnedCmakeArguments();
    void mainWindowReportsBuildInputErrorsAndRestoresButton();
    void mainWindowStreamsFailedConfigureAndPreservesArguments();
    void mainWindowRestoresBuildButtonWhenCmakeCannotStart();
};

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
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(workspace);
    QVERIFY(build);
    QVERIFY(button);
    QVERIFY(log);
    QVERIFY(log->isReadOnly());
    QVERIFY(log->toPlainText().isEmpty());
    workspace->setText(QStringLiteral("relative-workspace"));
    build->setText(QStringLiteral("relative-build"));
    button->click();
    QTRY_VERIFY(log->toPlainText().contains(QStringLiteral("absolute"), Qt::CaseInsensitive));
    QVERIFY(button->isEnabled());
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
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(workspace);
    QVERIFY(build);
    QVERIFY(button);
    QVERIFY(log);
    workspace->setText(workspacePath);
    build->setText(buildPath);
    button->click();
    QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains(QStringLiteral("[configure] exit code")), 30000);
    QVERIFY(log->toPlainText().contains(QStringLiteral("[configure stderr]")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("expected UI failure")));
    QVERIFY(!log->toPlainText().contains(QStringLiteral("argument boundary lost")));
    QVERIFY(log->toPlainText().contains(QStringLiteral("Build failed:")));
    QVERIFY(button->isEnabled());
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
    auto *log = window.findChild<QPlainTextEdit *>(QStringLiteral("buildLog"));
    QVERIFY(workspace);
    QVERIFY(build);
    QVERIFY(button);
    QVERIFY(log);
    workspace->setText(workspacePath);
    build->setText(buildPath);

    button->click();
    QTRY_VERIFY_WITH_TIMEOUT(log->toPlainText().contains(QStringLiteral("Could not start CMake")), 10000);
    QVERIFY(log->toPlainText().contains(QStringLiteral("Build failed:")));
    QVERIFY(button->isEnabled());
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
