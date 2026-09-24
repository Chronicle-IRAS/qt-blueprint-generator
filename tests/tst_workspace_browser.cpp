#include "app/workspace_browser_widget.h"
#include "app/main_window.h"
#include "generation/project_scaffolder.h"
#include "workspace/workspace_file_policy.h"
#include <QAction>
#include <QDockWidget>
#include <QDir>
#include <QFile>
#include <QInputMethodEvent>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QProcess>
#include <QScopeGuard>
#include <QSettings>
#include <QTemporaryDir>
#include <QTimer>
#include <QTreeView>
#include <QtTest>
#include <algorithm>
#include <utility>

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
bool scaffold(const QString &workspace)
{
    BlueprintDocument document;
    document.projectId = "demo"; document.projectName = "Demo"; document.target = "qt6-widgets-cpp17-cmake";
    BlueprintNode start; start.id = "start"; start.type = NodeType::Start; start.name = "Start";
    BlueprintNode end; end.id = "end"; end.type = NodeType::End; end.name = "End";
    document.nodes = {start, end}; document.edges = {{"edge", "start", "end", {}}};
    QString error;
    return ProjectScaffolder::create(document, workspace, &error);
}
void choose(QMessageBox::ButtonRole role)
{
    QTimer::singleShot(0, qApp, [role] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (!box) return;
        for (auto *button : box->buttons()) {
            if (box->buttonRole(button) == role) { QTest::mouseClick(button, Qt::LeftButton); return; }
        }
    });
}
void typeAtEnd(QPlainTextEdit *editor, const QString &text)
{
    editor->moveCursor(QTextCursor::End);
    if (std::all_of(text.begin(), text.end(), [](QChar c) { return c.unicode() < 128; }))
        QTest::keyClicks(editor, text);
    else {
        QInputMethodEvent event;
        event.setCommitString(text);
        QApplication::sendEvent(editor, &event);
    }
}
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
        QCOMPARE(classify("src/contracts/extra.h", metadata), Kind::ProtectedScaffold);
        QCOMPARE(classify("candidates/secret.cpp", metadata), Kind::Internal);
        auto extraProtected = metadata;
        auto protectedFiles = extraProtected.value("protectedFiles").toObject();
        protectedFiles.insert("src/custom.cpp", QString(64, 'a')); extraProtected.insert("protectedFiles", protectedFiles);
        QCOMPARE(classify("SRC/CUSTOM.CPP", extraProtected), Kind::ProtectedScaffold);
        QVERIFY(write(dir.path() + "/generation-manifest.json", "invalid")); QVERIFY(manifest(dir.path()).isEmpty());
    }
    void editableSaveAndProtection()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString ordinary = dir.path() + "/generated-project/src/custom.cpp";
        const QByteArray original = QString::fromUtf8("// 你好\nint value = 1;\n").toUtf8();
        QVERIFY(write(ordinary, original));
        QVERIFY(write(dir.path() + "/generated-project/src/external/lib/source.cpp", "// external"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        auto *editor = preview(widget);
        auto *save = widget.findChild<QPushButton *>("workspaceSaveButton"); QVERIFY(save);
        widget.openFile("src/custom.cpp");
        QVERIFY(!editor->isReadOnly()); QVERIFY(!save->isEnabled());
        editor->moveCursor(QTextCursor::End); QTest::keyClicks(editor, "// edited");
        QVERIFY(save->isEnabled());
        QCOMPARE(widget.findChild<QLabel *>("workspaceCurrentPath")->text(), QString("src/custom.cpp *"));
        QTest::mouseClick(save, Qt::LeftButton);
        QVERIFY2(!save->isEnabled(), qPrintable(status(widget)));
        QFile file(ordinary); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original + "// edited");
        file.close();
        typeAtEnd(editor, " second");
        QTest::mouseClick(save, Qt::LeftButton);
        QVERIFY(!save->isEnabled());
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), original + "// edited second");
        file.close();
        widget.openFile("src/main.cpp"); QVERIFY(editor->isReadOnly()); QVERIFY(!save->isEnabled());
        widget.openFile("src/external/lib/source.cpp"); QVERIFY(editor->isReadOnly()); QVERIFY(!save->isEnabled());
        QVERIFY(write(dir.path() + "/generation-manifest.json", "invalid"));
        widget.refresh(); widget.openFile("src/custom.cpp");
        QVERIFY(editor->isReadOnly()); QVERIFY(!save->isEnabled());
    }
    void utf8CrLfAndEmptyFile()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, QString::fromUtf8("// 中文\r\nint x = 1;\r\n").toUtf8()));
        QVERIFY(write(dir.path() + "/generated-project/src/empty.cpp", {}));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), QString::fromUtf8("// 新增"));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QString::fromUtf8("// 中文\r\nint x = 1;\r\n// 新增").toUtf8());
        file.close(); widget.refresh(); widget.openFile("src/custom.cpp");
        QCOMPARE(preview(widget)->toPlainText(), QString::fromUtf8("// 中文\nint x = 1;\n// 新增"));
        widget.openFile("src/empty.cpp"); QVERIFY(!preview(widget)->isReadOnly());
        typeAtEnd(preview(widget), "content"); QVERIFY(widget.findChild<QPushButton *>("workspaceSaveButton")->isEnabled());
    }
    void mixedLineEndingsRetainExistingSeparators()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, "first\r\nsecond\nthird\r\n"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), "added");
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("first\r\nsecond\nthird\r\nadded"));
    }
    void insertedLinePreservesExistingMixedLineEndings()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, "first\r\nsecond\nthird\r\n"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp");
        auto *editor = preview(widget);
        editor->setFocus(); editor->moveCursor(QTextCursor::Start);
        QTest::keyClicks(editor, "new"); QTest::keyClick(editor, Qt::Key_Return);
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("new\r\nfirst\r\nsecond\nthird\r\n"));
        file.close();
        editor->moveCursor(QTextCursor::Start);
        QTest::keyClicks(editor, "top"); QTest::keyClick(editor, Qt::Key_Return);
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("top\r\nnew\r\nfirst\r\nsecond\nthird\r\n"));
    }
    void disjointEditsPreserveExistingMixedLineEndings()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, "first\r\nsecond\nthird\r\nfourth\n"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp");
        auto *editor = preview(widget);
        editor->setFocus(); editor->moveCursor(QTextCursor::Start);
        QTest::keyClicks(editor, "new"); QTest::keyClick(editor, Qt::Key_Return);
        editor->moveCursor(QTextCursor::End);
        QTest::keyClicks(editor, "tail"); QTest::keyClick(editor, Qt::Key_Return);
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("new\r\nfirst\r\nsecond\nthird\r\nfourth\ntail\n"));
    }
    void undoRestoresOriginalMixedLineEnding()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, "first\r\nsecond\nthird\r\n"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp");
        auto *editor = preview(widget);
        editor->setFocus(); editor->moveCursor(QTextCursor::Start);
        for (int i = 0; i < 5; ++i) editor->moveCursor(QTextCursor::NextCharacter);
        QTest::keyClick(editor, Qt::Key_Delete);
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), QString("first\nsecond\nthird\n"));
        typeAtEnd(editor, "tail");
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("first\r\nsecond\nthird\r\ntail"));
    }
    void undoRestoresMixedLineEndingWhileStillDirty()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, "first\r\nsecond\nthird\r\n"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp");
        auto *editor = preview(widget);
        typeAtEnd(editor, "tail");
        editor->setFocus(); editor->moveCursor(QTextCursor::Start);
        for (int i = 0; i < 5; ++i) editor->moveCursor(QTextCursor::NextCharacter);
        QTest::keyClick(editor, Qt::Key_Delete);
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), QString("first\nsecond\nthird\ntail"));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("first\r\nsecond\nthird\r\ntail"));
    }
    void multilevelUndoRestoresMixedLineEnding()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, "first\r\nsecond\nthird\r\n"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp");
        auto *editor = preview(widget);
        typeAtEnd(editor, "tail");
        editor->setFocus(); editor->moveCursor(QTextCursor::Start);
        for (int i = 0; i < 5; ++i) editor->moveCursor(QTextCursor::NextCharacter);
        QTest::keyClick(editor, Qt::Key_Delete);
        QTest::keyClicks(editor, "x");
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QTest::keyClick(editor, Qt::Key_Z, Qt::ControlModifier);
        QCOMPARE(editor->toPlainText(), QString("first\nsecond\nthird\ntail"));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(source); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), QByteArray("first\r\nsecond\nthird\r\ntail"));
    }
    void dirtyFileSwitch_data()
    {
        QTest::addColumn<int>("decision");
        QTest::newRow("save") << int(QMessageBox::AcceptRole);
        QTest::newRow("discard") << int(QMessageBox::DestructiveRole);
        QTest::newRow("cancel") << int(QMessageBox::RejectRole);
    }
    void dirtyFileSwitch()
    {
        QFETCH(int, decision);
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString path = dir.path() + "/generated-project/src/one.cpp";
        QVERIFY(write(path, "one")); QVERIFY(write(dir.path() + "/generated-project/src/two.cpp", "two"));
        WorkspaceBrowserWidget widget; widget.resize(900, 500); widget.setWorkspacePath(dir.path()); widget.show();
        auto *tree = widget.findChild<QTreeView *>(); tree->expandAll();
        widget.openFile("src/one.cpp"); typeAtEnd(preview(widget), " changed");
        const auto index = find(tree->model(), "src/two.cpp"); QVERIFY(index.isValid());
        QCoreApplication::processEvents();
        QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(index).center());
        choose(QMessageBox::ButtonRole(decision));
        QTest::mouseDClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier, tree->visualRect(index).center());
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), decision == QMessageBox::AcceptRole ? QByteArray("one changed") : QByteArray("one"));
        if (decision == QMessageBox::RejectRole) {
            QCOMPARE(preview(widget)->toPlainText(), QString("one changed"));
            QVERIFY(widget.findChild<QLabel *>("workspaceCurrentPath")->text().endsWith(" *"));
        } else {
            QCOMPARE(preview(widget)->toPlainText(), QString("two"));
            QCOMPARE(widget.findChild<QLabel *>("workspaceCurrentPath")->text(), QString("src/two.cpp"));
        }
    }
    void dirtyRefresh_data()
    {
        QTest::addColumn<int>("decision");
        QTest::newRow("save") << int(QMessageBox::AcceptRole);
        QTest::newRow("discard") << int(QMessageBox::DestructiveRole);
        QTest::newRow("cancel") << int(QMessageBox::RejectRole);
    }
    void dirtyRefresh()
    {
        QFETCH(int, decision);
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString path = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(path, "original"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), " changed");
        choose(QMessageBox::ButtonRole(decision));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceRefreshButton"), Qt::LeftButton);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), decision == QMessageBox::AcceptRole ? QByteArray("original changed") : QByteArray("original"));
        if (decision == QMessageBox::RejectRole) {
            QCOMPARE(preview(widget)->toPlainText(), QString("original changed"));
            QVERIFY(widget.findChild<QLabel *>("workspaceCurrentPath")->text().endsWith(" *"));
        } else {
            QVERIFY(preview(widget)->toPlainText().isEmpty());
            QVERIFY(!widget.findChild<QPushButton *>("workspaceSaveButton")->isEnabled());
        }
    }
    void dirtyWorkspaceSwitch_data()
    {
        QTest::addColumn<int>("decision");
        QTest::newRow("save") << int(QMessageBox::AcceptRole);
        QTest::newRow("discard") << int(QMessageBox::DestructiveRole);
        QTest::newRow("cancel") << int(QMessageBox::RejectRole);
    }
    void dirtyWorkspaceSwitch()
    {
        QFETCH(int, decision);
        QTemporaryDir first, second; QVERIFY(scaffold(first.path())); QVERIFY(scaffold(second.path()));
        const QString original = first.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(original, "first"));
        MainWindow window; window.setLanguage("en"); window.show();
        auto *pathEdit = window.findChild<QLineEdit *>("workspacePathEdit");
        pathEdit->setText(first.path()); window.findChild<QAction *>("workspaceEditorAction")->trigger();
        auto *widget = window.findChild<WorkspaceBrowserWidget *>();
        widget->openFile("src/custom.cpp"); typeAtEnd(preview(*widget), " changed");
        choose(QMessageBox::ButtonRole(decision)); pathEdit->setText(second.path());
        QTest::keyClick(pathEdit, Qt::Key_Return);
        QFile file(original); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), decision == QMessageBox::AcceptRole ? QByteArray("first changed") : QByteArray("first"));
        if (decision == QMessageBox::RejectRole) {
            QCOMPARE(pathEdit->text(), first.path());
            QCOMPARE(widget->workspacePath(), first.path());
            QCOMPARE(preview(*widget)->toPlainText(), QString("first changed"));
            QVERIFY(widget->hasUnsavedChanges());
            choose(QMessageBox::DestructiveRole); window.close();
        } else {
            QCOMPARE(pathEdit->text(), second.path());
            QCOMPARE(widget->workspacePath(), second.path());
            QVERIFY(preview(*widget)->toPlainText().isEmpty());
        }
    }
    void dirtyDockAndMainClose_data()
    {
        QTest::addColumn<QString>("operation"); QTest::addColumn<int>("decision");
        for (const auto &operation : {QString("dockX"), QString("viewAction"), QString("mainClose"), QString("reset")})
            for (const auto &row : {std::pair<const char *, int>{"save", int(QMessageBox::AcceptRole)},
                                     {"discard", int(QMessageBox::DestructiveRole)}, {"cancel", int(QMessageBox::RejectRole)}})
                QTest::newRow(qPrintable(operation + '-' + row.first)) << operation << row.second;
    }
    void dirtyDockAndMainClose()
    {
        QFETCH(QString, operation); QFETCH(int, decision);
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString path = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(path, "original"));
        MainWindow window; window.setLanguage("en"); window.show();
        window.findChild<QLineEdit *>("workspacePathEdit")->setText(dir.path());
        auto *action = window.findChild<QAction *>("workspaceEditorAction"); action->trigger();
        auto *dock = window.findChild<QDockWidget *>("workspaceEditorDock");
        auto *widget = window.findChild<WorkspaceBrowserWidget *>();
        widget->openFile("src/custom.cpp"); typeAtEnd(preview(*widget), " changed");
        choose(QMessageBox::ButtonRole(decision));
        if (operation == "dockX") dock->close();
        else if (operation == "viewAction") action->trigger();
        else if (operation == "mainClose") window.close();
        else window.findChild<QAction *>("resetLayoutAction")->trigger();
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly));
        QCOMPARE(file.readAll(), decision == QMessageBox::AcceptRole ? QByteArray("original changed") : QByteArray("original"));
        if (decision == QMessageBox::RejectRole) {
            QVERIFY(window.isVisible()); QVERIFY(dock->isVisible());
            QCOMPARE(preview(*widget)->toPlainText(), QString("original changed"));
            QVERIFY(widget->hasUnsavedChanges());
            choose(QMessageBox::DestructiveRole); window.close();
        } else if (operation == "mainClose") QVERIFY(!window.isVisible());
        else QVERIFY(!dock->isVisible());
    }
    void externalDiskChangeRequiresDecision()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString path = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(path, "original"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), " local");
        QVERIFY(write(path, "external"));
        choose(QMessageBox::RejectRole);
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("external")); file.close();
        QVERIFY(widget.hasUnsavedChanges()); QCOMPARE(preview(widget)->toPlainText(), QString("original local"));
        choose(QMessageBox::AcceptRole);
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QCOMPARE(preview(widget)->toPlainText(), QString("external"));
        QVERIFY(!widget.hasUnsavedChanges());
    }
    void failedConflictReloadKeepsLocalChanges()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString path = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(path, "original"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), " local");
        QVERIFY(write(path, "external"));
        QTimer::singleShot(0, qApp, [&] {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box) return;
            QFile::remove(path);
            for (auto *button : box->buttons())
                if (box->buttonRole(button) == QMessageBox::AcceptRole) {
                    QTest::mouseClick(button, Qt::LeftButton); return;
                }
        });
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(!QFileInfo::exists(path));
        QCOMPARE(preview(widget)->toPlainText(), QString("original local"));
        QVERIFY(widget.hasUnsavedChanges());
        QVERIFY(widget.findChild<QPushButton *>("workspaceSaveButton")->isEnabled());
    }
    void saveRejectsDeletedFileAndChangedManifest()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString path = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(path, "original"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), " local");
        QVERIFY(write(dir.path() + "/generation-manifest.json", "invalid"));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(widget.hasUnsavedChanges()); QVERIFY(status(widget).contains("Save failed"));
        QFile file(path); QVERIFY(file.open(QIODevice::ReadOnly)); QCOMPARE(file.readAll(), QByteArray("original")); file.close();
        QVERIFY(QFile::remove(path));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(widget.hasUnsavedChanges()); QVERIFY(!QFileInfo::exists(path));
    }
    void saveRejectsReplacedRootAndParentJunction()
    {
        QTemporaryDir dir, outside; QVERIFY(scaffold(dir.path()));
        const QString root = dir.path() + "/generated-project";
        QVERIFY(write(root + "/src/custom.cpp", "original"));
        QVERIFY(write(outside.path() + "/src/custom.cpp", "outside"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), " local");
        QVERIFY(QDir().rename(root + "/src", root + "/saved-src"));
#ifdef Q_OS_WIN
        auto makeLink = [](const QString &path, const QString &target) {
            QProcess process; process.start("cmd", {"/c", "mklink", "/J", QDir::toNativeSeparators(path), QDir::toNativeSeparators(target)});
            return process.waitForFinished() && process.exitCode() == 0;
        };
        const auto cleanup = qScopeGuard([&] { QDir().rmdir(root + "/src"); QDir().rmdir(root); });
#else
        auto makeLink = [](const QString &path, const QString &target) { return QFile::link(target, path); };
        const auto cleanup = qScopeGuard([&] { QFile::remove(root + "/src"); QFile::remove(root); });
#endif
        QVERIFY(makeLink(root + "/src", outside.path() + "/src"));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(widget.hasUnsavedChanges()); QVERIFY(status(widget).contains("Save failed"));
        QFile outsideFile(outside.path() + "/src/custom.cpp"); QVERIFY(outsideFile.open(QIODevice::ReadOnly));
        QCOMPARE(outsideFile.readAll(), QByteArray("outside")); outsideFile.close();
        QVERIFY(QDir().rmdir(root + "/src"));
        QVERIFY(QDir().rename(root, dir.path() + "/old-project"));
        QVERIFY(makeLink(root, outside.path()));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(widget.hasUnsavedChanges()); QVERIFY(status(widget).contains("Save failed"));
        QVERIFY(outsideFile.open(QIODevice::ReadOnly)); QCOMPARE(outsideFile.readAll(), QByteArray("outside"));
    }
    void saveRejectsOrdinaryRootReplacementWithIdenticalBytes()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString root = dir.path() + "/generated-project";
        const QString relative = "src/custom.cpp";
        QVERIFY(write(root + "/" + relative, "original"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile(relative); typeAtEnd(preview(widget), " local");
        QVERIFY(QDir().rename(root, dir.path() + "/old-project"));
        QVERIFY(write(root + "/" + relative, "original"));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(widget.hasUnsavedChanges());
        QVERIFY(status(widget).contains("Save failed"));
        QFile replacement(root + "/" + relative); QVERIFY(replacement.open(QIODevice::ReadOnly));
        QCOMPARE(replacement.readAll(), QByteArray("original"));
    }
    void saveRejectsFileReplacementWithIdenticalBytes()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        const QString source = dir.path() + "/generated-project/src/custom.cpp";
        QVERIFY(write(source, "original"));
        WorkspaceBrowserWidget widget; widget.setWorkspacePath(dir.path()); widget.show();
        widget.openFile("src/custom.cpp"); typeAtEnd(preview(widget), " local");
        QVERIFY(QFile::rename(source, dir.path() + "/generated-project/src/old-custom.cpp"));
        QVERIFY(write(source, "original"));
        QTest::mouseClick(widget.findChild<QPushButton *>("workspaceSaveButton"), Qt::LeftButton);
        QVERIFY(widget.hasUnsavedChanges());
        QFile replacement(source); QVERIFY(replacement.open(QIODevice::ReadOnly));
        QCOMPARE(replacement.readAll(), QByteArray("original"));
    }
    void languageAndThemeKeepDirtyEditor()
    {
        QTemporaryDir dir; QVERIFY(scaffold(dir.path()));
        QVERIFY(write(dir.path() + "/generated-project/src/custom.cpp", "original"));
        MainWindow window; window.setLanguage("en"); window.show();
        window.findChild<QLineEdit *>("workspacePathEdit")->setText(dir.path());
        window.findChild<QAction *>("workspaceEditorAction")->trigger();
        auto *widget = window.findChild<WorkspaceBrowserWidget *>();
        widget->openFile("src/custom.cpp"); typeAtEnd(preview(*widget), " local");
        QVERIFY(window.setLanguage("zh_CN")); window.setTheme(EditorTheme::Theme::Dark);
        QCOMPARE(preview(*widget)->toPlainText(), QString("original local"));
        QVERIFY(!preview(*widget)->isReadOnly()); QVERIFY(widget->hasUnsavedChanges());
        QVERIFY(widget->findChild<QPushButton *>("workspaceSaveButton")->isEnabled());
        QVERIFY(widget->findChild<QLabel *>("workspaceCurrentPath")->text().endsWith(" *"));
        window.setTheme(EditorTheme::Theme::Light); QVERIFY(window.setLanguage("en"));
        QCOMPARE(preview(*widget)->toPlainText(), QString("original local"));
        choose(QMessageBox::DestructiveRole); window.close();
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
