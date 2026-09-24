#include "app/workspace_browser_widget.h"
#include "app/main_window.h"
#include "generation/project_scaffolder.h"
#include "workspace/workspace_file_policy.h"
#include <QAction>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTreeView>
#include <QtTest>

namespace {
bool write(const QString &path, const QByteArray &bytes)
{
    if (!QDir().mkpath(QFileInfo(path).absolutePath())) return false;
    QFile file(path); return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}
QModelIndex find(QAbstractItemModel *model, const QString &path, const QModelIndex &parent = {})
{
    for (int row = 0; row < model->rowCount(parent); ++row) {
        const auto index = model->index(row, 0, parent);
        if (index.data(Qt::UserRole + 1).toString() == path) return index;
        const auto child = find(model, path, index); if (child.isValid()) return child;
    }
    return {};
}
QPlainTextEdit *preview(WorkspaceBrowserWidget &widget) { return widget.findChild<QPlainTextEdit *>("workspacePreview"); }
QString status(WorkspaceBrowserWidget &widget) { return widget.findChild<QLabel *>("workspaceBrowserStatus")->text(); }
}
class WorkspaceBrowserTest : public QObject
{
    Q_OBJECT
    QTemporaryDir m_settings;
private slots:
    void initTestCase()
    {
        QVERIFY(m_settings.isValid());
        QCoreApplication::setOrganizationName("WorkspaceBrowserTests");
        QCoreApplication::setApplicationName("WorkspaceBrowserTests");
        QSettings::setDefaultFormat(QSettings::IniFormat);
        QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
    }
    void nestedUnicodeDoubleClickAndSwitch()
    {
        QTemporaryDir a, b;
        const QString path = QString::fromUtf8("src/模块/实现.cpp");
        const QByteArray content = QString::fromUtf8("// 你好\nint answer = 42;\n").toUtf8();
        QVERIFY(write(a.path() + "/generated-project/" + path, content));
        QVERIFY(write(a.path() + "/candidates/draft.cpp", "private"));
        QVERIFY(write(a.path() + "/generated-project/candidates/draft.cpp", "private"));
        QVERIFY(write(a.path() + "/generated-project/generation-manifest.json", "private"));
        WorkspaceBrowserWidget widget; widget.resize(900, 500); widget.setWorkspacePath(a.path()); widget.show();
        auto *tree = widget.findChild<QTreeView *>(); tree->expandAll();
        auto index = find(tree->model(), path); QVERIFY(index.isValid());
        QVERIFY(!find(tree->model(), "candidates").isValid());
        QVERIFY(!find(tree->model(), "generation-manifest.json").isValid());
        QTest::qWait(30);
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(index).center());
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(index).center());
        QCOMPARE(preview(widget)->toPlainText(), QString::fromUtf8(content));
        QCOMPARE(widget.findChild<QLabel *>("workspaceCurrentPath")->text(), path);
        QVERIFY(preview(widget)->isReadOnly());
        widget.setWorkspacePath(b.path());
        QCOMPARE(tree->model()->rowCount(), 0); QVERIFY(preview(widget)->toPlainText().isEmpty());
        QCOMPARE(widget.findChild<QLabel *>("workspaceCurrentPath")->text(), QString("No file selected"));
        QVERIFY(status(widget).contains("No generated-project"));
        widget.setWorkspacePath(b.path() + "/missing"); QVERIFY(status(widget).contains("No generated-project"));
        QVERIFY(QDir().mkpath(b.path() + "/generated-project")); widget.setWorkspacePath(b.path());
        QVERIFY(status(widget).contains("empty"));
    }
    void previewFailures_data()
    {
        QTest::addColumn<QString>("name"); QTest::addColumn<QByteArray>("bytes"); QTest::addColumn<QString>("expected");
        QTest::newRow("binary") << "binary.cpp" << QByteArray("hello\0world", 11) << "not supported";
        QTest::newRow("unsupported") << "image.png" << QByteArray("hello") << "not supported";
        QTest::newRow("invalid UTF8") << "bad.cpp" << QByteArray::fromHex("ff") << "not supported";
        QTest::newRow("truncated UTF8") << "bad.cpp" << QByteArray::fromHex("e4b8") << "not supported";
        QTest::newRow("controls") << "bad.txt" << QByteArray(10, '\1') << "not supported";
        QTest::newRow("large") << "large.cpp" << QByteArray(WorkspaceFilePolicy::PreviewLimit + 1, 'a') << "too large";
        QTest::newRow("empty") << "empty.cpp" << QByteArray() << "Empty file";
    }
    void previewFailures()
    {
        QFETCH(QString, name); QFETCH(QByteArray, bytes); QFETCH(QString, expected);
        QTemporaryDir dir; QVERIFY(write(dir.path() + "/generated-project/good.cpp", "old content"));
        QVERIFY(write(dir.path() + "/generated-project/" + name, bytes));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.openFile("good.cpp");
        QVERIFY(!preview(widget)->toPlainText().isEmpty()); widget.openFile(name);
        QVERIFY(preview(widget)->toPlainText().isEmpty()); QVERIFY2(status(widget).contains(expected), qPrintable(status(widget)));
        QVERIFY(QFile::remove(dir.path() + "/generated-project/good.cpp")); widget.openFile("good.cpp");
        QVERIFY(status(widget).contains("missing file")); QVERIFY(preview(widget)->toPlainText().isEmpty());
    }
    void supportedSuffixAndReadError()
    {
        QTemporaryDir dir;
        QVERIFY(write(dir.path() + "/generated-project/source.CPP", "// uppercase extension"));
        QVERIFY(QDir().mkpath(dir.path() + "/generated-project/blocked.cpp"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.openFile("source.CPP");
        QCOMPARE(preview(widget)->toPlainText(), QString("// uppercase extension"));
        widget.openFile("blocked.cpp");
        QVERIFY(preview(widget)->toPlainText().isEmpty());
        QCOMPARE(status(widget), QString("Cannot read this file."));
    }
    void pathBoundaries()
    {
        QTemporaryDir dir; QVERIFY(write(dir.path() + "/generated-project/good.cpp", "inside"));
        QVERIFY(write(dir.path() + "/generated-project-other/secret.cpp", "outside"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path());
        for (const QString &path : {QString("../generated-project-other/secret.cpp"), QString("src/../../secret.cpp"),
                 dir.path() + "/generated-project-other/secret.cpp", QString("C:/secret.cpp"),
                 QString("//server/share/secret.cpp"), QString("good.cpp:stream"), QString("..\\secret.cpp")}) {
            widget.openFile("good.cpp"); widget.openFile(path);
            QVERIFY(preview(widget)->toPlainText().isEmpty()); QVERIFY(status(widget).contains("unsafe"));
        }
    }
    void rejectsReplacedRoot()
    {
        QTemporaryDir dir, outside;
        const QString root = dir.path() + "/generated-project";
        QVERIFY(write(root + "/good.cpp", "inside")); QVERIFY(write(outside.path() + "/good.cpp", "outside"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path());
        QVERIFY(QDir().rename(root, dir.path() + "/old-project"));
#ifdef Q_OS_WIN
        QProcess process; process.start("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(root), QDir::toNativeSeparators(outside.path())});
        QVERIFY(process.waitForFinished()); QVERIFY2(process.exitCode() == 0, process.readAllStandardError().constData());
        const auto cleanup = qScopeGuard([&] { QDir().rmdir(root); });
#else
        QVERIFY(QFile::link(outside.path(), root));
        const auto cleanup = qScopeGuard([&] { QFile::remove(root); });
#endif
        widget.openFile("good.cpp"); QVERIFY(preview(widget)->toPlainText().isEmpty());
        QVERIFY(status(widget).contains("unsafe")); widget.refresh(); QCOMPARE(widget.findChild<QTreeView *>()->model()->rowCount(), 0);
    }
    void rejectsChildAndWorkspaceAncestorLinks()
    {
        QTemporaryDir dir, outside;
        QVERIFY(write(dir.path() + "/generated-project/good.cpp", "inside"));
        QVERIFY(write(outside.path() + "/secret.cpp", "outside"));
        const QString link = dir.path() + "/generated-project/linked";
        const QString alias = dir.path() + "/workspace-alias";
#ifdef Q_OS_WIN
        auto makeLink = [](const QString &path, const QString &target) {
            QProcess process; process.start("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(path), QDir::toNativeSeparators(target)});
            return process.waitForFinished() && process.exitCode() == 0;
        };
        const auto cleanup = qScopeGuard([&] { QDir().rmdir(link); QDir().rmdir(alias); });
#else
        auto makeLink = [](const QString &path, const QString &target) { return QFile::link(target, path); };
        const auto cleanup = qScopeGuard([&] { QFile::remove(link); QFile::remove(alias); });
#endif
        QVERIFY(makeLink(link, outside.path()));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path());
        QVERIFY(!find(widget.findChild<QTreeView *>()->model(), "linked").isValid());
        widget.openFile("linked/secret.cpp"); QVERIFY(preview(widget)->toPlainText().isEmpty());
        QVERIFY(makeLink(alias, dir.path())); widget.setWorkspacePath(alias);
        QCOMPARE(widget.findChild<QTreeView *>()->model()->rowCount(), 0);
        widget.openFile("good.cpp"); QVERIFY(preview(widget)->toPlainText().isEmpty());
    }
    void policyUsesScaffoldManifest()
    {
        using namespace WorkspaceFilePolicy;
        QTemporaryDir dir; BlueprintDocument document;
        document.projectId = "demo"; document.projectName = "Demo"; document.target = "qt6-widgets-cpp17-cmake";
        BlueprintNode start; start.id = "start"; start.type = NodeType::Start; start.name = "Start";
        BlueprintNode end; end.id = "end"; end.type = NodeType::End; end.name = "End";
        document.nodes = {start, end}; document.edges = {{"edge", "start", "end", {}}};
        QString error; QVERIFY2(ProjectScaffolder::create(document, dir.path(), &error), qPrintable(error));
        const auto metadata = manifest(dir.path()); QVERIFY(!metadata.isEmpty());
        QCOMPARE(classify("src/custom.cpp", metadata), Kind::OrdinaryFutureEditable);
        for (const auto &path : {"CMakeLists.txt", "README.md", "src/main.cpp", "tests/scaffold_smoke.cpp", "src/contracts/types.h",
                "src/contracts/blueprint.json", "src/contracts/source-blueprint.json", "src/modules/logic/contract.h",
                "src/modules/logic/implementation/placeholder.h", "tests/logic/placeholder.h", "SRC/MAIN.CPP"})
            QCOMPARE(classify(path, metadata), Kind::ProtectedScaffold);
        QCOMPARE(classify("src/custom.cpp", {}), Kind::UnknownProtected);
        QCOMPARE(classify("src/external/module/source.cpp", metadata), Kind::ExternalProtected);
        QCOMPARE(classify("candidates/secret.cpp", metadata), Kind::Internal);
        auto extraProtected = metadata;
        auto protectedFiles = extraProtected.value("protectedFiles").toObject();
        protectedFiles.insert("src/custom.cpp", QString(64, 'a')); extraProtected.insert("protectedFiles", protectedFiles);
        QCOMPARE(classify("SRC/CUSTOM.CPP", extraProtected), Kind::ProtectedScaffold);
        QVERIFY(write(dir.path() + "/generation-manifest.json", "invalid")); QVERIFY(manifest(dir.path()).isEmpty());
    }
    void floatingDockPreservesPreview()
    {
        QTemporaryDir dir; QVERIFY(write(dir.path() + "/generated-project/src/good.cpp", "// preview"));
        MainWindow window; window.setLanguage("en"); window.show();
        window.findChild<QLineEdit *>("workspacePathEdit")->setText(dir.path());
        window.findChild<QAction *>("workspaceEditorAction")->trigger();
        auto *dock = window.findChild<QDockWidget *>("workspaceEditorDock");
        auto *widget = window.findChild<WorkspaceBrowserWidget *>();
        auto *tree = widget->findChild<QTreeView *>();
        tree->expandAll(); widget->openFile("src/good.cpp");
        for (bool floating : {true, false}) {
            dock->setFloating(floating);
            QCoreApplication::processEvents();
            QCOMPARE(preview(*widget)->toPlainText(), QString("// preview"));
            QCOMPARE(widget->findChild<QLabel *>("workspaceCurrentPath")->text(), QString("src/good.cpp"));
            QVERIFY(tree->isExpanded(find(tree->model(), "src")));
        }
    }
    void dockAndLanguage()
    {
        QTemporaryDir dir; QVERIFY(write(dir.path() + "/generated-project/good.cpp", "// preview"));
        MainWindow window; window.setLanguage("en"); window.show();
        auto *dock = window.findChild<QDockWidget *>("workspaceEditorDock");
        auto *action = window.findChild<QAction *>("workspaceEditorAction");
        auto *widget = window.findChild<WorkspaceBrowserWidget *>(); QVERIFY(dock && action && widget);
        QVERIFY(!dock->isVisible()); window.findChild<QLineEdit *>("workspacePathEdit")->setText(dir.path());
        action->trigger(); QVERIFY(dock->isVisible()); widget->openFile("good.cpp");
        QCOMPARE(preview(*widget)->toPlainText(), QString("// preview"));
        window.setLanguage("zh_CN"); QCOMPARE(dock->windowTitle(), QString::fromUtf8("工作区编辑器"));
        QCOMPARE(preview(*widget)->toPlainText(), QString("// preview"));
        QCOMPARE(widget->findChild<QLabel *>("workspaceCurrentPath")->text(), QString("good.cpp"));
        QTRY_VERIFY(status(*widget).contains(QString::fromUtf8("只读")));
        dock->close(); QVERIFY(!dock->isVisible());
        QVERIFY(write(dir.path() + "/generated-project/new.cpp", "new")); action->trigger();
        QVERIFY(dock->isVisible()); QVERIFY(find(widget->findChild<QTreeView *>()->model(), "new.cpp").isValid());
        window.setLanguage("en"); QCOMPARE(dock->windowTitle(), QString("Workspace Editor"));
        window.findChild<QAction *>("resetLayoutAction")->trigger(); QVERIFY(!dock->isVisible());
    }
};
QTEST_MAIN(WorkspaceBrowserTest)
#include "tst_workspace_browser.moc"
