#include "app/main_window.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "app/blueprint_v2_reader.h"
#include "app/workspace_browser_widget.h"
#include "blueprint/blueprint_project_store.h"
#include "ai/ai_client.h"
#include "app/candidate_review_dialog.h"
#include "generation/project_scaffolder.h"
#include "workspace/build_service.h"
#include <QtTest>
#include <QJsonArray>
#include <QJsonDocument>
#include <QFile>
#include <QTemporaryDir>
#include <QDir>
#include <QAction>
#include <QPlainTextEdit>
#include <QGraphicsView>
#include <QGraphicsItem>
#include <QTimer>
#include <QMessageBox>
#include <QPushButton>
#include <QLineEdit>
#include <QLabel>
#include <QSettings>
#include <QPointer>
#include <QDirIterator>
#include <QDockWidget>

namespace {
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
void write(const QString &path, const QByteArray &bytes) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); }
QString fixture(const char *name) { return QDir(QStringLiteral(V2_FIXTURE_DIR)).filePath(QString::fromUtf8(name)); }
void configure(MainWindow &w) { w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE), QStringLiteral(V2_READER_ENTRY)); }
QJsonObject blueprint() { return QJsonDocument::fromJson(read(fixture("blueprint.json"))).object(); }
QString bundle(QTemporaryDir &temporary) {
    const QString path = temporary.filePath("信任.json");
    // Independent fixture pins, deliberately not derived from blueprint pluginLocks.
    QJsonArray pins{
      QJsonObject{{"pluginId","org.example.pipeline"},{"package","pipeline-plugin"},{"version","1.2.0"},{"sha256",QString(64,'a')},{"pluginApiVersion","1.0-draft"}},
      QJsonObject{{"pluginId","org.example.math"},{"package","math-plugin"},{"version","2.0.0"},{"sha256",QString(64,'b')},{"pluginApiVersion","1.0-draft"}}};
    QFile file(path); if (!file.open(QIODevice::WriteOnly)) return {};
    file.write(QJsonDocument(QJsonObject{{"descriptors", QJsonDocument::fromJson(read(fixture("descriptors.json"))).array()},{"trustedLocks",pins}}).toJson()); return path;
}
QMap<QString,QByteArray> files(const QString &path) {
    QMap<QString,QByteArray> result; QDirIterator it(path, QDir::Files | QDir::Hidden, QDirIterator::Subdirectories);
    while(it.hasNext()) { auto p=it.next(); result[QDir(path).relativeFilePath(p)]=read(p); } return result;
}
class Answers {
public:
    explicit Answers(QVector<QMessageBox::ButtonRole> values) : answers(std::move(values)) {
        timer.setInterval(10); QObject::connect(&timer,&QTimer::timeout,[this] {
          auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if (!box || last==box) return;
          last=box; ++count; auto role=next<answers.size()?answers[next++]:QMessageBox::RejectRole;
          for(auto *button:box->buttons()) if(box->buttonRole(button)==role) {button->click();return;} box->reject();
        }); timer.start();
    }
    int count=0;
private:
    QVector<QMessageBox::ButtonRole> answers; qsizetype next=0; QPointer<QMessageBox> last; QTimer timer;
};
class DeferredClient : public IAiClient {
public:
    using IAiClient::IAiClient;
    AiRequest request;
    void generate(const AiRequest &r) override {request=r;}
    void reply() {emit responseReady(request.requestId,R"({"nodeId":"logic","summary":"done","files":[{"path":"src/modules/logic/implementation/worker.cpp","content":"// candidate"}]})");}
};
void prepareGeneration(MainWindow &w,const QString &workspace) {
    w.findChild<QLineEdit *>("workspacePathEdit")->setText(workspace);
    for(auto pair:{qMakePair("start",NodeType::Start),qMakePair("logic",NodeType::LogicModule),qMakePair("end",NodeType::End)}) {
      BlueprintNode node; node.id=pair.first;node.name=pair.first;node.type=pair.second;node.description="Complete module contract";QVERIFY(w.scene()->addNode(node,{}));
    }
    QVERIFY(w.scene()->connectNodes("start","logic"));QVERIFY(w.scene()->connectNodes("logic","end"));w.scene()->nodeItem("logic")->setSelected(true);
}
}
class BlueprintV2GuiTest : public QObject {
 Q_OBJECT
private slots:
 void initTestCase() {
   if (!QFileInfo::exists(QStringLiteral(V2_NODE_EXECUTABLE)) || !QFileInfo::exists(QStringLiteral(V2_READER_ENTRY))) QSKIP("Optional Node reader runtime is not built; Schema 1 remains available.");
   QCoreApplication::setOrganizationName("BlueprintV2Tests"); QCoreApplication::setApplicationName("BlueprintV2Tests"); QSettings().clear();
 }
 void init() {QSettings().clear();}
 void opensRealReaderWithoutPlugins() {
   MainWindow window; configure(window);
   QSignalSpy done(&window, &MainWindow::v2LoadFinished);
   window.openV2Project(QStringLiteral(V2_FIXTURE_DIR));
   QTRY_COMPARE_WITH_TIMEOUT(done.size(), 1, 20000);
   QVERIFY(done[0][0].toBool());
   QVERIFY(window.isV2Mode());
   QCOMPARE(window.document().nodes.size(), 0);
   auto *view = window.findChild<QGraphicsView *>("v2GraphicsView"); QVERIFY(view);
   int nodes = 0;
   for (auto *item : view->scene()->items()) if (item->data(0).isValid()) ++nodes;
   QCOMPARE(nodes, 3);
 }
 void knownNodesSelectionAndNoWrites() {
   QTemporaryDir temp; QVERIFY(temp.isValid()); auto trust=bundle(temp); const auto before=files(temp.path());const auto beforeInput=files(QStringLiteral(V2_FIXTURE_DIR));
   MainWindow w; configure(w); QSignalSpy done(&w,&MainWindow::v2LoadFinished); w.openV2Project(QStringLiteral(V2_FIXTURE_DIR),trust);
   QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000); QVERIFY(done[0][0].toBool());
   auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");
   w.show();QCoreApplication::processEvents();
   QVERIFY(!w.findChild<QDockWidget *>("propertiesDock")->isVisible());
   QVERIFY(!w.findChild<QDockWidget *>("buildExportDock")->isVisible());
   for(auto *item:view->scene()->items()) if(item->data(0).isValid()) QVERIFY(view->viewport()->rect().contains(view->mapFromScene(item->sceneBoundingRect().center())));
   for(auto *item:view->scene()->items()) if(item->data(0).isValid() && item->data(0).toInt()==0) { view->ensureVisible(item);QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene(item->sceneBoundingRect().center()));break; }
   auto *properties=w.findChild<QPlainTextEdit *>("v2Properties"); QVERIFY(properties->isReadOnly());
   QVERIFY(properties->toPlainText().contains("\"kind\": \"known\"")); QVERIFY(properties->toPlainText().contains("温度"));
   QVERIFY(properties->toPlainText().contains("org.example.pipeline/Reading@1"));
   auto *diagnostics=w.findChild<QPlainTextEdit *>("v2Diagnostics"); QVERIFY(diagnostics->isReadOnly());
   QVERIFY(diagnostics->toPlainText().contains("archive-value")); QVERIFY(diagnostics->toPlainText().contains("metadata only"));
   for(auto name:{"saveProjectAction","saveProjectAsAction","generateSelectedNodeAction","generateLegacyModuleAction","toolbarBuildAction","toolbarExportAction","deleteSelectionAction","beginConnectionAction"}) {
     auto *action=w.findChild<QAction *>(name); QVERIFY(action); QVERIFY(!action->isEnabled()); action->trigger();
   }
   QVERIFY(!w.addNodeOfType(NodeType::Start)); QVERIFY(!w.saveProject()); QVERIFY(!w.saveProjectAs(temp.filePath("unexpected")));
   auto *workspace=w.findChild<WorkspaceBrowserWidget *>(); QVERIFY(!workspace->saveCurrentFile());
   QVERIFY(w.setLanguage("zh_CN")); w.setTheme(EditorTheme::Theme::Dark);
   QVERIFY(w.findChild<QLabel *>("v2ReadOnlyBanner")->text().contains("只读"));
   for(auto name:{"saveProjectAction","generateSelectedNodeAction","toolbarBuildAction"}) QVERIFY(!w.findChild<QAction *>(name)->isEnabled());
   QCOMPARE(files(temp.path()),before);QCOMPARE(files(QStringLiteral(V2_FIXTURE_DIR)),beforeInput); w.exitV2Mode(); QVERIFY(!w.isV2Mode()); QVERIFY(w.findChild<QDockWidget *>("propertiesDock")->isVisible()); QVERIFY(w.addNodeOfType(NodeType::Start));
 }
 void duplicateRecordsAreDistinctAndRelationshipsUnresolved() {
   QTemporaryDir temp; auto object=blueprint(); auto nodes=object["nodes"].toArray(); auto n=nodes[1].toObject(); n["id"]="source";n["properties"]=QJsonObject{{"identity","second duplicate"}}; nodes[1]=n;object["nodes"]=nodes;
   write(temp.filePath("blueprint.json"),QJsonDocument(object).toJson());
   MainWindow w;configure(w);QSignalSpy done(&w,&MainWindow::v2LoadFinished); w.openV2Project(temp.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][0].toBool());
   auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");int count=0;
   for(auto *item:view->scene()->items()) if(item->data(0).isValid()) {++count;if(item->data(0).toInt()==1)item->setSelected(true);}
   QCOMPARE(count,3);QVERIFY(w.findChild<QPlainTextEdit *>("v2Properties")->toPlainText().contains("second duplicate"));
   auto text=w.findChild<QPlainTextEdit *>("v2Diagnostics")->toPlainText();QVERIFY(text.contains("DUPLICATE_NODE_ID"));QVERIFY(text.contains("unresolved"));QVERIFY(text.contains("archive-value"));
   int lines=0;for(auto *item:view->scene()->items()) if(dynamic_cast<QGraphicsLineItem *>(item))++lines;QCOMPARE(lines,0);
 }
 void failedLoadsPreserveProjectAndDirtyHistory() {
   QTemporaryDir temp; MainWindow w; configure(w);QVERIFY(w.addNodeOfType(NodeType::Start));const auto prior=w.document();const auto title=w.windowTitle();const auto undo=w.scene()->undoStack()->count();
   write(temp.filePath("blueprint.json"),"{\"schemaVersion\":1}");QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(temp.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(!done[0][0].toBool());
   QCOMPARE(w.document(),prior);QCOMPARE(w.windowTitle(),title);QCOMPARE(w.scene()->undoStack()->count(),undo);QVERIFY(w.isModified());
   w.openV2Project(QStringLiteral(V2_FIXTURE_DIR),temp.filePath("missing.json"));QCOMPARE(done.size(),2);QCOMPARE(w.document(),prior);
   write(temp.filePath("bad.json"),"{\"descriptors\":{},\"trustedLocks\":[]}");w.openV2Project(QStringLiteral(V2_FIXTURE_DIR),temp.filePath("bad.json"));QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,20000);QVERIFY(!done[2][0].toBool());QCOMPARE(w.document(),prior);
 }
 void cancelsDirtyTransitionThenDiscardsToActualSavedLegacy() {
   QTemporaryDir legacy; MainWindow w; configure(w);QVERIFY(w.addNodeOfType(NodeType::Start));QVERIFY(w.saveProjectAs(legacy.path()));const auto saved=w.document();QVERIFY(w.addNodeOfType(NodeType::End));const auto dirty=w.document();
   QSignalSpy done(&w,&MainWindow::v2LoadFinished);
   { Answers answer({QMessageBox::RejectRole}); w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(!done[0][0].toBool());QCOMPARE(w.document(),dirty); }
   { Answers answer({QMessageBox::DestructiveRole}); w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,20000);QVERIFY(done[1][0].toBool()); }
   w.exitV2Mode();QCOMPARE(w.document(),saved);QVERIFY(!w.isModified());QCOMPARE(w.scene()->undoStack()->count(),0);
 }
 void processFailuresPreserveCurrentV2() {
   QTemporaryDir temp;MainWindow w;configure(w);QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][0].toBool());
   const auto title=w.windowTitle();const auto text=w.findChild<QPlainTextEdit *>("v2Diagnostics")->toPlainText();
   write(temp.filePath("wrong.cjs"),"process.stdin.resume();process.stdin.on('end',()=>process.stdout.write(JSON.stringify({protocolVersion:2,requestId:'wrong',status:'read',canGenerate:false,diagnostics:[]})));" );
   w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),temp.filePath("wrong.cjs"));w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,20000);QVERIFY(!done[1][0].toBool());QCOMPARE(w.windowTitle(),title);QCOMPARE(w.findChild<QPlainTextEdit *>("v2Diagnostics")->toPlainText(),text);
   write(temp.filePath("slow.cjs"),"setTimeout(()=>{},30000);"); w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),temp.filePath("slow.cjs"),40);w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,2000);QVERIFY(!done[2][0].toBool());QVERIFY(w.isV2Mode());
   w.setV2ReaderConfiguration(temp.filePath("missing-node.exe"),QStringLiteral(V2_READER_ENTRY));w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),4,2000);QVERIFY(!done[3][0].toBool());QCOMPARE(w.windowTitle(),title);
 }
 void supersededReplyCannotReplaceNewerProject() {
   QTemporaryDir temp;write(temp.filePath("delayed.cjs"),"const fs=require('fs');let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',()=>setTimeout(()=>{let r=JSON.parse(s);process.stdout.write(JSON.stringify({protocolVersion:1,requestId:r.requestId,status:'error',canGenerate:false,diagnostics:[]}))},300));");
   MainWindow w; w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),temp.filePath("delayed.cjs"));QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));configure(w);w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][0].toBool());QTest::qWait(500);QCOMPARE(done.size(),1);QVERIFY(w.isV2Mode());
 }
 void queuedMissingRuntimeFailureCannotCancelNewerRead() {
   MainWindow w;w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),"missing-entry.cjs");QSignalSpy done(&w,&MainWindow::v2LoadFinished);
   w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));configure(w);w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));
   QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][0].toBool());QVERIFY(w.isV2Mode());
 }
 void duplicatePortDoesNotGuessEndpoint() {
   QTemporaryDir temp;auto object=blueprint();auto nodes=object["nodes"].toArray();auto n=nodes[0].toObject();auto ports=n["ports"].toArray();ports.append(ports[0]);n["ports"]=ports;nodes[0]=n;object["nodes"]=nodes;write(temp.filePath("blueprint.json"),QJsonDocument(object).toJson());
   MainWindow w;configure(w);QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(temp.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][0].toBool());auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");int lines=0;for(auto *item:view->scene()->items())if(dynamic_cast<QGraphicsLineItem *>(item))++lines;QCOMPARE(lines,0);QVERIFY(w.findChild<QPlainTextEdit *>("v2Diagnostics")->toPlainText().contains("DUPLICATE_PORT_ID"));
 }
 void workspaceSourceCancelPreservesBlueprintDiscardChoice() {
   QTemporaryDir temp;BlueprintDocument document;document.projectId="source";document.projectName="Source";document.target="qt6-widgets-cpp17-cmake";BlueprintNode start;start.id="start";start.name="Start";start.type=NodeType::Start;BlueprintNode end;end.id="end";end.name="End";end.type=NodeType::End;document.nodes={start,end};document.edges={{"edge","start","end",{}}};QString error;QVERIFY2(ProjectScaffolder::create(document,temp.path(),&error),qPrintable(error));write(temp.filePath("generated-project/note.txt"),"saved");
   MainWindow w;configure(w);auto *workspace=w.findChild<WorkspaceBrowserWidget *>();QVERIFY(workspace->setWorkspacePath(temp.path()));workspace->openFile("note.txt");auto *preview=workspace->findChild<QPlainTextEdit *>("workspacePreview");QVERIFY(!preview->isReadOnly());preview->insertPlainText("dirty");QVERIFY(workspace->hasUnsavedChanges());QVERIFY(w.addNodeOfType(NodeType::Start));auto prior=w.document();
   QSignalSpy done(&w,&MainWindow::v2LoadFinished);{Answers answer({QMessageBox::DestructiveRole,QMessageBox::RejectRole});w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(!done[0][0].toBool());}
   QCOMPARE(w.document(),prior);QVERIFY(w.isModified());QVERIFY(workspace->hasUnsavedChanges());QCOMPARE(read(temp.filePath("generated-project/note.txt")),QByteArray("saved"));
   {Answers answer({QMessageBox::DestructiveRole,QMessageBox::DestructiveRole});w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,20000);QVERIFY(done[1][0].toBool());}
   QVERIFY(preview->isReadOnly());QVERIFY(!workspace->saveCurrentFile());QVERIFY(workspace->findChild<QLabel *>("workspaceBrowserStatus")->text().contains("Read-only"));
   preview->insertPlainText("programmatic stale edit");QVERIFY(workspace->hasUnsavedChanges());QVERIFY(!workspace->saveCurrentFile());QCOMPARE(read(temp.filePath("generated-project/note.txt")),QByteArray("saved"));preview->setPlainText("saved");preview->document()->setModified(false);
   w.exitV2Mode();QVERIFY(!workspace->hasUnsavedChanges());QVERIFY(!preview->isReadOnly());QCOMPARE(preview->toPlainText(),QString("saved"));
 }
 void invalidUtf8DoesNotReplaceLegacy() {
   QTemporaryDir temp;write(temp.filePath("blueprint.json"),QByteArray::fromHex("7bffc0807d"));MainWindow w;configure(w);QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(temp.path());QCOMPARE(done.size(),1);QVERIFY(!done[0][0].toBool());QVERIFY(!w.isV2Mode());
 }
 void saveTransitionPreservesSavedHistoryAndLegacyReturn() {
   QTemporaryDir legacy;MainWindow w;configure(w);QVERIFY(w.addNodeOfType(NodeType::Start));QVERIFY(w.saveProjectAs(legacy.path()));QVERIFY(w.addNodeOfType(NodeType::End));const auto current=w.document();const int undo=w.scene()->undoStack()->count();QSignalSpy done(&w,&MainWindow::v2LoadFinished);
   {Answers answer({QMessageBox::AcceptRole});w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][0].toBool());}
   QString error;const auto loaded=BlueprintProjectStore::load(legacy.path(),&error);QVERIFY2(loaded.has_value(),qPrintable(error));QCOMPARE(loaded->document,current);w.exitV2Mode();QCOMPARE(w.document(),current);QCOMPARE(w.scene()->undoStack()->count(),undo);QVERIFY(!w.isModified());
   w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,20000);QVERIFY(done[1][0].toBool());QVERIFY(w.newProject());QVERIFY(!w.isV2Mode());QCOMPARE(w.document().nodes.size(),0);
   w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),3,20000);QVERIFY(done[2][0].toBool());QVERIFY(w.openProject(legacy.path()));QVERIFY(!w.isV2Mode());QCOMPARE(w.document(),current);
 }
 void transportFailures_data() {
   QTest::addColumn<QByteArray>("script");
   const QByteArray base="let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',()=>{const r=JSON.parse(s);";
   QTest::newRow("wrong-id")<<base+"process.stdout.write(JSON.stringify({protocolVersion:1,requestId:'other',status:'error',canGenerate:false,diagnostics:[]}))});";
   QTest::newRow("generation-enabled")<<base+"process.stdout.write(JSON.stringify({protocolVersion:1,requestId:r.requestId,status:'error',canGenerate:true,diagnostics:[]}))});";
   QTest::newRow("malformed-display-dto")<<base+"process.stdout.write(JSON.stringify({protocolVersion:1,requestId:r.requestId,status:'read',sourceText:r.sourceText,semanticValid:true,document:{nodes:[],edges:[{}],channels:[]},nodes:[],canGenerate:false,diagnostics:[]}))});";
   QTest::newRow("multiple-replies")<<base+"process.stdout.write('{}{}')});";
   QTest::newRow("nonzero-exit")<<QByteArray("process.exit(9);");
   QTest::newRow("crash")<<QByteArray("process.kill(process.pid,'SIGKILL');");
   QTest::newRow("malformed-json")<<QByteArray("process.stdout.write('{');");
   QTest::newRow("invalid-utf8")<<QByteArray("process.stdout.write(Buffer.from([0xff]));");
   QTest::newRow("oversize-stdout")<<QByteArray("process.stdout.write('x'.repeat(32*1024*1024+1));");
   QTest::newRow("oversize-stderr")<<QByteArray("process.stderr.write('x'.repeat(64*1024+1));setTimeout(()=>{},1000);");
 }
 void transportFailures() {
   QFETCH(QByteArray,script);QTemporaryDir temporary;write(temporary.filePath("boundary.cjs"),script);BlueprintV2Reader reader;reader.setConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),temporary.filePath("boundary.cjs"),2000);QSignalSpy done(&reader,&BlueprintV2Reader::finished);
   reader.read(QString::fromUtf8(read(fixture("blueprint.json"))),QJsonObject{{"descriptors",QJsonArray()},{"trustedLocks",QJsonArray()}});QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,4000);QVERIFY(done[0][0].toJsonObject().isEmpty());QVERIFY(!done[0][1].toString().isEmpty());
 }
 void childDoesNotInheritUnrelatedCredentials() {
   QTemporaryDir temp;write(temp.filePath("env.cjs"),"let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',()=>{const r=JSON.parse(s);process.stdout.write(JSON.stringify({protocolVersion:1,requestId:r.requestId,status:'error',canGenerate:false,diagnostics:[{code:process.env.BLUEPRINT_TEST_CREDENTIAL?'LEAK':'CLEAN',message:'boundary',severity:'error',path:'/'}]}))});");
   qputenv("BLUEPRINT_TEST_CREDENTIAL","synthetic-test-secret");BlueprintV2Reader reader;reader.setConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),temp.filePath("env.cjs"));QSignalSpy done(&reader,&BlueprintV2Reader::finished);reader.read("{}",{});qunsetenv("BLUEPRINT_TEST_CREDENTIAL");QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,4000);QCOMPARE(done[0][0].toJsonObject()["diagnostics"].toArray()[0].toObject()["code"].toString(),QString("CLEAN"));
 }
 void activeGenerationIsCancelledAndLateCompletionIgnored() {
   QTemporaryDir workspace,legacy;DeferredClient *client=nullptr;MainWindow w([&](const AiProviderSettings &,QObject *p){client=new DeferredClient(p);return client;});configure(w);prepareGeneration(w,workspace.path());QVERIFY(w.saveProjectAs(legacy.path()));w.findChild<QAction *>("generateSelectedNodeAction")->trigger();QVERIFY(client);QPointer<DeferredClient> retained=client;const auto before=files(workspace.path());QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][0].toBool());if(retained)retained->reply();QCoreApplication::processEvents();QVERIFY(!w.findChild<CandidateReviewDialog *>());QCOMPARE(files(workspace.path()),before);QVERIFY(!w.findChild<QAction *>("generateSelectedNodeAction")->isEnabled());
 }
 void activeBuildPreventsTransition() {
   QTemporaryDir temp;QVERIFY(QDir().mkpath(temp.filePath("generated-project")));write(temp.filePath("slow.cjs"),"setTimeout(()=>process.exit(0),600);");MainWindow w;configure(w);w.setBuildToolConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),{temp.filePath("slow.cjs")});w.findChild<QLineEdit *>("workspacePathEdit")->setText(temp.path());w.findChild<QLineEdit *>("buildDirectoryEdit")->setText(temp.filePath("build"));w.findChild<QAction *>("toolbarBuildAction")->trigger();auto *build=w.findChild<BuildService *>();QVERIFY(build->isRunning());QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QCOMPARE(done.size(),1);QVERIFY(!done[0][0].toBool());QVERIFY(!w.isV2Mode());QTRY_VERIFY_WITH_TIMEOUT(!build->isRunning(),5000);
 }
 void staleNodeEditorCannotMutateLegacyAfterV2Entry() {
   QTemporaryDir legacy;MainWindow w;configure(w);QVERIFY(w.addNodeOfType(NodeType::LogicModule));QVERIFY(w.saveProjectAs(legacy.path()));const auto before=w.document();w.show();QCoreApplication::processEvents();
   QPointer<QDialog> editor;QSignalSpy done(&w,&MainWindow::v2LoadFinished);
   connect(&w,&MainWindow::v2LoadFinished,&w,[&](bool success){if(success && editor)editor->findChild<QPushButton *>("saveNodeEditButton")->click();});
   QTimer::singleShot(0,&w,[&]{editor=qobject_cast<QDialog *>(QApplication::activeModalWidget());if(!editor)return;editor->findChild<QLineEdit *>("directNodeNameEdit")->setText("Stale accepted edit");w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));});
   const QPoint center=w.graphicsView()->mapFromScene(w.scene()->nodeItem(before.nodes[0].id)->sceneBoundingRect().center());QTest::mouseDClick(w.graphicsView()->viewport(),Qt::LeftButton,Qt::NoModifier,center);
   QCOMPARE(done.size(),1);QVERIFY(done[0][0].toBool());QVERIFY(w.isV2Mode());QCOMPARE(w.document(),before);
 }
 void staleReplaceConfirmationCannotWriteAfterV2Entry() {
   QTemporaryDir legacy,target;MainWindow w;configure(w);QVERIFY(w.addNodeOfType(NodeType::Start));QVERIFY(w.saveProjectAs(legacy.path()));auto targetDocument=w.document();targetDocument.projectName="Existing target";QString error;QVERIFY(BlueprintProjectStore::save(target.path(),targetDocument,{},&error));const auto before=files(target.path());QPointer<QMessageBox> prompt;QSignalSpy done(&w,&MainWindow::v2LoadFinished);
   connect(&w,&MainWindow::v2LoadFinished,&w,[&](bool success){if(success && prompt)prompt->findChild<QPushButton *>("replaceBlueprintProjectButton")->click();});
   QTimer::singleShot(0,&w,[&]{prompt=qobject_cast<QMessageBox *>(QApplication::activeModalWidget());if(prompt)w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));});
   const bool saved=w.saveProjectAs(target.path());QCOMPARE(done.size(),1);QVERIFY(done[0][0].toBool());QVERIFY(w.isV2Mode());QVERIFY(!saved);QCOMPARE(files(target.path()),before);
 }
 void failedLoadKeepsCandidateSuccessDismissesWithoutDiskWrites() {
   QTemporaryDir workspace,legacy,invalid;DeferredClient *client=nullptr;MainWindow w([&](const AiProviderSettings &,QObject *p){client=new DeferredClient(p);return client;});configure(w);prepareGeneration(w,workspace.path());QVERIFY(w.saveProjectAs(legacy.path()));
   auto *generate=w.findChild<QAction *>("generateSelectedNodeAction");QVERIFY(generate->isEnabled());generate->trigger();QVERIFY(client);client->reply();QTRY_VERIFY(w.findChild<CandidateReviewDialog *>());const auto before=files(workspace.path());
   QPointer<CandidateReviewDialog> review=w.findChild<CandidateReviewDialog *>();write(invalid.filePath("blueprint.json"),"{");QSignalSpy done(&w,&MainWindow::v2LoadFinished);w.openV2Project(invalid.path());QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(!done[0][0].toBool());QVERIFY(review);QVERIFY(review->isVisible());
   w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,20000);QVERIFY(done[1][0].toBool());QTRY_VERIFY(!review || !review->isVisible());QCOMPARE(files(workspace.path()),before);QVERIFY(!generate->isEnabled());
 }
};
QTEST_MAIN(BlueprintV2GuiTest)
#include "tst_blueprint_v2_gui.moc"
