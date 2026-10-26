#include "app/main_window.h"
#include "editor/blueprint_scene.h"
#include "editor/node_item.h"
#include "app/blueprint_v2_reader.h"
#include "app/blueprint_v2_edit_dialog.h"
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
#include <QUrl>
#include <QGraphicsTextItem>
#include <QClipboard>
#include <QWheelEvent>
#include <QDialogButtonBox>
#include <QProcess>

namespace {
QByteArray read(const QString &path) { QFile file(path); return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray(); }
void write(const QString &path, const QByteArray &bytes) { QFile file(path); QVERIFY(file.open(QIODevice::WriteOnly)); QCOMPARE(file.write(bytes), bytes.size()); }
QString fixture(const char *name) { return QDir(QStringLiteral(V2_FIXTURE_DIR)).filePath(QString::fromUtf8(name)); }
void configure(MainWindow &w) { w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE), QStringLiteral(V2_READER_ENTRY)); }
void editProjectName(MainWindow &w,const QString &name) {
    QTimer::singleShot(0,&w,[&w,name]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QVERIFY(dialog);dialog->findChild<QLineEdit *>("v2ProjectNameEdit")->setText(name);dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->click();});
    auto *button=w.findChild<QPushButton *>("v2EditValuesButton");QVERIFY(button);QVERIFY(button->isEnabled());button->click();
}
QByteArray malformedSuccessScript(const QJsonObject &base,const QByteArray &mutation) {
    return "const base="+QJsonDocument(base).toJson(QJsonDocument::Compact)+
      ";let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',()=>{const r=JSON.parse(s);const read=base.readResult;const status={load:'loaded',preview:'prepared',save:'saved'}[r.operation];let reply={protocolVersion:2,requestId:r.requestId,operation:r.operation,canGenerate:false,writable:true,readResult:structuredClone(read),editTargets:base.editTargets,result:{status,canGenerate:false,readResult:structuredClone(read),sha256:base.result.sha256,writeState:'verified',sourceText:read.sourceText,diagnostics:read.diagnostics}};"+
      mutation+"process.stdout.write(JSON.stringify(reply));});";
}
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
 void malformedOperationSuccessIsRejected_data() {
   QTest::addColumn<QString>("operation");QTest::addColumn<QByteArray>("mutation");
   const QList<QPair<QByteArray,QByteArray>> variants{
     {"rejected-snapshot","reply.readResult={status:'rejected',sourceText:'',diagnostics:[],canGenerate:false};reply.result.readResult=reply.readResult;delete reply.editTargets;"},
     {"missing-wrapped-snapshot","delete reply.result.readResult;"},
     {"inconsistent-wrapped-snapshot","reply.result.readResult.document.projectName='different';"},
     {"missing-metadata","delete reply.editTargets;"}};
   for(const QString operation:{"load","preview","save"})for(const auto &variant:variants)QTest::newRow(qPrintable(operation+"-"+variant.first))<<operation<<variant.second;
   QTest::newRow("prepared-source-mismatch")<<QString("preview")<<QByteArray("reply.result.sourceText='different';");
 }
 void malformedOperationSuccessIsRejected() {
   QFETCH(QString,operation);QFETCH(QByteArray,mutation);QTemporaryDir project,runtime;write(project.filePath("blueprint.json"),read(fixture("blueprint.json")));
   BlueprintV2Reader reader;reader.setConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),QStringLiteral(V2_READER_ENTRY));QSignalSpy done(&reader,&BlueprintV2Reader::finished);const QJsonObject registry{{"descriptors",QJsonArray()},{"trustedLocks",QJsonArray()}};reader.load(project.path(),registry);QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000);QVERIFY(done[0][1].toString().isEmpty());const auto base=done[0][0].toJsonObject();
   write(runtime.filePath("malformed.cjs"),malformedSuccessScript(base,mutation));reader.setConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),runtime.filePath("malformed.cjs"));
   const QJsonArray edits{QJsonObject{{"kind","project-name"},{"value","draft"}}};
   if(operation=="load")reader.load(project.path(),registry);else if(operation=="preview")reader.preview(QString::fromUtf8(read(fixture("blueprint.json"))),edits,registry);else reader.save(project.path(),QString::fromUtf8(read(fixture("blueprint.json"))),base["result"].toObject()["sha256"].toString(),edits,registry);
   QTRY_COMPARE_WITH_TIMEOUT(done.size(),2,4000);QVERIFY2(done[1][0].toJsonObject().isEmpty(),"malformed successful DTO must not be emitted as transport success");QVERIFY(!done[1][1].toString().isEmpty());
 }
 void malformedSavedRejectedSnapshotRetainsDraftAndBaseline() {
   QTemporaryDir project,runtime;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);const QJsonObject registry{{"descriptors",QJsonArray()},{"trustedLocks",QJsonArray()}};
   BlueprintV2Reader reader;reader.setConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),QStringLiteral(V2_READER_ENTRY));QSignalSpy baseLoaded(&reader,&BlueprintV2Reader::finished);reader.load(project.path(),registry);QTRY_COMPARE_WITH_TIMEOUT(baseLoaded.size(),1,20000);const auto base=baseLoaded[0][0].toJsonObject();
   MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"retained draft");
   write(runtime.filePath("malformed.cjs"),malformedSuccessScript(base,"reply.readResult={status:'rejected',sourceText:'',diagnostics:[],canGenerate:false};delete reply.result.readResult;delete reply.editTargets;"));w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),runtime.filePath("malformed.cjs"));QSignalSpy saved(&w,&MainWindow::v2SaveFinished);QVERIFY(w.saveProject());QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,4000);QVERIFY2(!saved[0][0].toBool(),"rejected snapshot cannot certify a verified save");QVERIFY(w.isModified());QVERIFY(!w.saveProject());QCOMPARE(read(project.filePath("blueprint.json")),original);QVERIFY(w.windowTitle().contains("retained draft"));
 }
 void provenNotDispatchedFailureCanSaveSameDraftAfterRuntimeRepair_data() {
   QTest::addColumn<bool>("missingEntry");QTest::newRow("missing-entry")<<true;QTest::newRow("failed-to-start")<<false;
 }
 void provenNotDispatchedFailureCanSaveSameDraftAfterRuntimeRepair() {
   QFETCH(bool,missingEntry);QTemporaryDir project;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"retry same draft");
   w.setV2ReaderConfiguration(missingEntry?QStringLiteral(V2_NODE_EXECUTABLE):project.filePath("missing-node.exe"),missingEntry?project.filePath("missing-entry.cjs"):QStringLiteral(V2_READER_ENTRY));QSignalSpy saved(&w,&MainWindow::v2SaveFinished);QVERIFY(w.saveProject());QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,4000);QVERIFY(!saved[0][0].toBool());QVERIFY(w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);QVERIFY2(w.findChild<QPushButton *>("v2EditValuesButton")->isEnabled(),"a proven not-dispatched failure must preserve retryable draft state");
   configure(w);QVERIFY(w.saveProject());QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,30000);QVERIFY(saved[1][0].toBool());QVERIFY(!w.isModified());QCOMPARE(loaded.size(),1);QCOMPARE(read(project.filePath("blueprint.json")),QByteArray(original).replace("Reading pipeline","retry same draft"));
 }
 void staleSaveContinuationCannotOverrideReturnOrNestedInteraction_data() {
   QTest::addColumn<QString>("interaction");for(const QString interaction:{"return","nested-cancel","nested-preview"})QTest::newRow(qPrintable(interaction))<<interaction;
 }
 void staleSaveContinuationCannotOverrideReturnOrNestedInteraction() {
   QFETCH(QString,interaction);QTemporaryDir project;write(project.filePath("blueprint.json"),read(fixture("blueprint.json")));MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"first saved draft");
   connect(&w,&MainWindow::v2SaveFinished,&w,[&](bool success){if(!success)return;if(interaction=="return")w.exitV2Mode();else if(interaction=="nested-preview")editProjectName(w,"new interaction draft");else {QTimer::singleShot(0,&w,[&]{w.findChild<QDialog *>("v2EditValuesDialog")->reject();});w.findChild<QPushButton *>("v2EditValuesButton")->click();}});
   QSignalSpy saved(&w,&MainWindow::v2SaveFinished);{Answers answers({QMessageBox::AcceptRole,QMessageBox::RejectRole});w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,30000);QTest::qWait(2000);}
   if(interaction=="return")QVERIFY2(!w.isV2Mode(),"Return must invalidate the former Save-to-Open intent");else {QCOMPARE(w.projectDirectory(),project.path());QCOMPARE(w.isModified(),interaction=="nested-preview");if(interaction=="nested-preview")QVERIFY(w.windowTitle().contains("new interaction draft"));}QCOMPARE(loaded.size(),2);
 }
 void longExistingScalarsAreNotSilentlyTruncatedIntoPendingEdits() {
   const QString literal="\""+QString(40000,'x')+"\"";QJsonObject targets{{"projectName","old"},{"nodes",QJsonArray{QJsonObject{{"sourceIndex",0},{"properties",QJsonArray{QJsonObject{{"property","long"},{"valueLiteral",literal}}}},{"ports",QJsonArray()}}}}};
   BlueprintV2EditDialog dialog(targets,0,targets,{},nullptr);QCOMPARE(dialog.findChild<QLineEdit *>("v2Property_0")->text(),literal);dialog.findChild<QLineEdit *>("v2ProjectNameEdit")->setText("new");QCOMPARE(dialog.edits(),QJsonArray({QJsonObject{{"kind","project-name"},{"value","new"}}}));
 }
 void editingPreviewsWithoutWritingAndSavesTwice() {
   QTemporaryDir project, trust; const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);
   MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path(),bundle(trust));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);QVERIFY(loaded[0][0].toBool());
   auto *button=w.findChild<QPushButton *>("v2EditValuesButton");QVERIFY2(button,"v2 restricted editor is available");
   auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");for(auto *item:view->scene()->items())if(item->data(0).isValid()&&item->data(0).toInt()==0)item->setSelected(true);
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QVERIFY(dialog);dialog->findChild<QLineEdit *>("v2ProjectNameEdit")->setText("first edit");dialog->findChild<QLineEdit *>("v2Property_0")->setText("\"new label\"");dialog->findChild<QLineEdit *>("v2Port_0")->setText("output");dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->click();});
   button->click();QVERIFY(w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);QVERIFY(w.windowTitle().contains('*'));QVERIFY(w.findChild<QPlainTextEdit *>("v2Properties")->toPlainText().contains("new label"));
   bool portVisible=false;for(auto *item:view->scene()->items())if(auto *text=dynamic_cast<QGraphicsTextItem *>(item))portVisible|=text->toPlainText().contains("output");QVERIFY(portVisible);QCOMPARE(w.findChild<QAction *>("saveProjectAction")->shortcut(),QKeySequence(QKeySequence::Save));
   QVERIFY(w.saveProject());QTRY_VERIFY_WITH_TIMEOUT(!w.isModified(),30000);const auto first=read(project.filePath("blueprint.json"));QCOMPARE(first,QByteArray(original).replace("Reading pipeline","first edit").replace("温度","new label").replace("可编辑显示名","output"));
   QVERIFY2(button->isEnabled(),"verified save retains editable document authority");
   w.openV2Project(project.path(),bundle(trust));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),2,20000);QVERIFY(loaded[1][0].toBool());
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");dialog->findChild<QLineEdit *>("v2ProjectNameEdit")->setText("second edit");dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->click();});button->click();QVERIFY(w.isModified());QVERIFY(w.saveProject());QTRY_VERIFY_WITH_TIMEOUT(!w.isModified(),30000);QCOMPARE(read(project.filePath("blueprint.json")),QByteArray(first).replace("first edit","second edit"));
 }
 void canonicalRepeatedEditsAndResetUseOriginalBaseline() {
   QTemporaryDir project;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);
   editProjectName(w,"one");QVERIFY(w.isModified());editProjectName(w,"two");QVERIFY(w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);
   editProjectName(w,"Reading pipeline");QVERIFY(!w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);
   editProjectName(w,"three");QVERIFY(w.saveProject());QTRY_VERIFY_WITH_TIMEOUT(!w.isModified(),30000);editProjectName(w,"four");QVERIFY(w.saveProject());QTRY_VERIFY_WITH_TIMEOUT(!w.isModified(),30000);QCOMPARE(read(project.filePath("blueprint.json")),QByteArray(original).replace("Reading pipeline","four"));
 }
 void invalidPreviewRetainsInputAndAcceptedViewAndUnknownHasNoInternalEditor() {
   QTemporaryDir project,trust;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path(),bundle(trust));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);
   auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");for(auto *item:view->scene()->items())if(item->data(0).isValid()&&item->data(0).toInt()==0)item->setSelected(true);
   const auto snapshot=w.findChild<QPlainTextEdit *>("v2Properties")->toPlainText();QSignalSpy preview(&w,&MainWindow::v2PreviewFinished);
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");auto *input=dialog->findChild<QLineEdit *>("v2Property_0");QVERIFY(input);input->setText("17");dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->click();});
   QTimer timer;timer.setInterval(10);connect(&timer,&QTimer::timeout,&w,[&]{if(preview.size()!=1)return;auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QVERIFY(dialog);QCOMPARE(dialog->findChild<QLineEdit *>("v2Property_0")->text(),QString("17"));QVERIFY(dialog->findChild<QLabel *>("v2EditError")->text().contains("PROPERTY_CONTRACT"));QCOMPARE(w.findChild<QPlainTextEdit *>("v2Properties")->toPlainText(),snapshot);QVERIFY(!w.isModified());dialog->reject();timer.stop();});timer.start();w.findChild<QPushButton *>("v2EditValuesButton")->click();QCOMPARE(read(project.filePath("blueprint.json")),original);
   w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),2,20000);for(auto *item:view->scene()->items())if(item->data(0).isValid()&&item->data(0).toInt()==0)item->setSelected(true);
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QVERIFY(!dialog->findChild<QLineEdit *>("v2Property_0"));QVERIFY(!dialog->findChild<QLineEdit *>("v2Port_0"));dialog->reject();});w.findChild<QPushButton *>("v2EditValuesButton")->click();
 }
 void actualConflictAndDirectoryFailureKeepDraftAndBytes() {
   QTemporaryDir project;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"draft");
   QSignalSpy saved(&w,&MainWindow::v2SaveFinished);write(project.filePath("blueprint.json"),original+"\n");QVERIFY(w.saveProject());QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,30000);QVERIFY(!saved[0][0].toBool());QVERIFY(w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original+"\n");QVERIFY(w.findChild<QLabel *>("v2OperationStatus")->text().contains("CONFLICT"));
   write(project.filePath("blueprint.json"),original);write(project.filePath("README.md"),"protected");QVERIFY(w.saveProject());QTRY_COMPARE_WITH_TIMEOUT(saved.size(),2,30000);QVERIFY(!saved[1][0].toBool());QVERIFY(w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);QCOMPARE(read(project.filePath("README.md")),QByteArray("protected"));QVERIFY(w.findChild<QLabel *>("v2OperationStatus")->text().contains("DIRECTORY_UNSUITABLE"));
 }
 void saveReplyLossBlocksRetryAndCompetingTransitions_data() {
   QTest::addColumn<QByteArray>("script");
   QTest::newRow("timeout")<<QByteArray("setTimeout(()=>{},30000);");
   QTest::newRow("crash")<<QByteArray("setTimeout(()=>process.exit(8),100);");
   QTest::newRow("malformed")<<QByteArray("setTimeout(()=>process.stdout.write('{'),100);");
   QTest::newRow("committed-unverified")<<QByteArray("let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',()=>setTimeout(()=>{let r=JSON.parse(s);process.stdout.write(JSON.stringify({protocolVersion:2,requestId:r.requestId,operation:'save',canGenerate:false,result:{status:'failed',code:'VERIFY_FAILED',writeState:'committed-unverified',canGenerate:false,message:'inspect disk'}}))},100));");
 }
 void saveReplyLossBlocksRetryAndCompetingTransitions() {
   QFETCH(QByteArray,script);QTemporaryDir project,runtime;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);write(runtime.filePath("failure.cjs"),script);MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"draft");w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),runtime.filePath("failure.cjs"),600);
   QSignalSpy saved(&w,&MainWindow::v2SaveFinished);QVERIFY(w.saveProject());QVERIFY(!w.saveProject());QVERIFY(!w.newProject());w.exitV2Mode();QVERIFY(w.isV2Mode());QVERIFY(!w.close());w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QCOMPARE(loaded.size(),1);configure(w);
   QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,3000);QVERIFY(!saved[0][0].toBool());QVERIFY(w.isModified());QVERIFY(!w.saveProject());QVERIFY(!w.findChild<QPushButton *>("v2EditValuesButton")->isEnabled());QVERIFY(w.findChild<QLabel *>("v2OperationStatus")->text().contains("unknown"));QCOMPARE(read(project.filePath("blueprint.json")),original);
   configure(w);{Answers answers({QMessageBox::DestructiveRole});w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),2,20000);}QVERIFY(!w.isModified());QVERIFY(w.findChild<QPushButton *>("v2EditValuesButton")->isEnabled());
 }
 void dirtyV2Transitions_data() {
   QTest::addColumn<QString>("operation");QTest::addColumn<int>("role");
   for(const QString operation:{"new","legacy","return","close","v2"})for(auto role:{QMessageBox::AcceptRole,QMessageBox::DestructiveRole,QMessageBox::RejectRole})QTest::newRow(qPrintable(operation+QString::number(role)))<<operation<<int(role);
 }
 void dirtyV2Transitions() {
   QFETCH(QString,operation);QFETCH(int,role);QTemporaryDir project,legacy;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);MainWindow w;configure(w);QVERIFY(w.saveProjectAs(legacy.path()));QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"transition draft");QSignalSpy saved(&w,&MainWindow::v2SaveFinished);
   Answers answer({static_cast<QMessageBox::ButtonRole>(role)});
   if(operation=="new")w.newProject();else if(operation=="legacy")w.openProject(legacy.path());else if(operation=="return")w.exitV2Mode();else if(operation=="close")w.close();else w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));
   if(operation=="v2")QTRY_VERIFY_WITH_TIMEOUT(loaded.size()>=2,20000);
   if(role==QMessageBox::AcceptRole){QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,30000);QVERIFY(saved[0][0].toBool());if(operation=="v2")QTRY_VERIFY_WITH_TIMEOUT(w.projectDirectory()!=project.path(),20000);else if(operation!="close")QTRY_VERIFY(!w.isV2Mode());QCOMPARE(read(project.filePath("blueprint.json")),QByteArray(original).replace("Reading pipeline","transition draft"));}
   else if(role==QMessageBox::RejectRole){QVERIFY(w.isV2Mode());QVERIFY(w.isModified());QCOMPARE(w.projectDirectory(),project.path());QCOMPARE(read(project.filePath("blueprint.json")),original);}
   else {if(operation!="close")QVERIFY(!w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);}
 }
 void actualStoreCommitWithLostReplyRequiresReloadRatherThanRetry() {
   QTemporaryDir project,runtime;const auto original=read(fixture("blueprint.json"));write(project.filePath("blueprint.json"),original);MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"committed without reply");
   const auto protocol=QUrl::fromLocalFile(QFileInfo(QStringLiteral(V2_READER_ENTRY)).dir().filePath("blueprint-document-protocol.js")).toString();
   write(runtime.filePath("lost.cjs"),("let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',async()=>{const api=await import('"+protocol+"');const r=await api.handleBlueprintDocumentRequest(JSON.parse(s));if(r.result.status!=='saved'){process.stdout.write(JSON.stringify(r));return;}setTimeout(()=>{},30000);});").toUtf8());
   w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),runtime.filePath("lost.cjs"),8000);QSignalSpy saved(&w,&MainWindow::v2SaveFinished);QVERIFY(w.saveProject());QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,12000);QVERIFY(!saved[0][0].toBool());QVERIFY(w.isModified());QVERIFY(!w.saveProject());QCOMPARE(read(project.filePath("blueprint.json")),QByteArray(original).replace("Reading pipeline","committed without reply"));QVERIFY(w.findChild<QLabel *>("v2OperationStatus")->text().contains("unknown"));
   configure(w);{Answers answers({QMessageBox::DestructiveRole});w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),2,20000);}QVERIFY(!w.isModified());QVERIFY(w.windowTitle().contains("committed without reply"));editProjectName(w,"after inspection");QVERIFY(w.saveProject());QTRY_VERIFY_WITH_TIMEOUT(!w.isModified(),30000);
 }
 void selectedNodeIdentityAndDiagnosticsFollowAcceptedPreview() {
   QTemporaryDir project,trust;auto doc=blueprint();auto nodes=doc["nodes"].toArray();auto first=nodes[0].toObject();first["properties"]=QJsonObject{{"label",17}};nodes[0]=first;nodes.append(first);doc["nodes"]=nodes;const auto original=QJsonDocument(doc).toJson();write(project.filePath("blueprint.json"),original);MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path(),bundle(trust));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);QVERIFY(w.findChild<QPlainTextEdit *>("v2Diagnostics")->toPlainText().contains("NODE_PROPERTIES"));
   auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");for(auto *item:view->scene()->items())if(item->data(0).isValid())item->setSelected(item->data(0).toInt()==0);
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QVERIFY(dialog->findChild<QLineEdit *>("v2Property_0"));for(auto *item:view->scene()->items())if(item->data(0).isValid())item->setSelected(item->data(0).toInt()==3);dialog->findChild<QLineEdit *>("v2Property_0")->setText("\"repaired first\"");dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->click();});w.findChild<QPushButton *>("v2EditValuesButton")->click();QVERIFY(w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);
   auto snapshot=w.findChild<QPlainTextEdit *>("v2Properties")->toPlainText();QVERIFY(snapshot.contains("17"));QVERIFY(snapshot.contains("sourceIndex\": 3"));QVERIFY(w.saveProject());QTRY_VERIFY_WITH_TIMEOUT(!w.isModified(),30000);auto after=QJsonDocument::fromJson(read(project.filePath("blueprint.json"))).object()["nodes"].toArray();QCOMPARE(after[0].toObject()["properties"].toObject()["label"].toString(),QString("repaired first"));QCOMPARE(after[3].toObject()["properties"].toObject()["label"].toInt(),17);
 }
 void chineseEditFormAndPolicyPermissions() {
   QTemporaryDir project;write(project.filePath("blueprint.json"),read(fixture("blueprint.json")));MainWindow w;configure(w);QVERIFY(w.setLanguage("zh_CN"));QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);QCOMPARE(w.findChild<QAction *>("openV2ProjectAction")->text(),QString::fromUtf8("打开蓝图 v2..."));auto *button=w.findChild<QPushButton *>("v2EditValuesButton");QCOMPARE(button->text(),QString::fromUtf8("编辑 v2 值…"));
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QCOMPARE(dialog->windowTitle(),QString::fromUtf8("编辑 v2 值"));QCOMPARE(dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->text(),QString::fromUtf8("应用预览（未保存）"));dialog->reject();});button->click();
   w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),2,20000);QVERIFY(!button->isEnabled());QVERIFY(w.findChild<QLabel *>("v2ReadOnlyBanner")->text().contains(QString::fromUtf8("目录保护")));QVERIFY(!w.saveProject());
 }
 void scalarNegativeZeroAndOpaqueCrLfBytesSurviveGuiSave() {
   QTemporaryDir project,trust;const auto trustPath=bundle(trust);auto declarations=QJsonDocument::fromJson(read(trustPath)).object();auto descriptors=declarations["descriptors"].toArray();auto descriptor=descriptors[0].toObject();auto definitions=descriptor["nodeDefinitions"].toArray();auto definition=definitions[0].toObject();auto schema=definition["propertiesSchema"].toObject();auto properties=schema["properties"].toObject();properties["amount"]=QJsonObject{{"type","number"}};schema["properties"]=properties;definition["propertiesSchema"]=schema;definitions[0]=definition;descriptor["nodeDefinitions"]=definitions;descriptors[0]=descriptor;declarations["descriptors"]=descriptors;write(trustPath,QJsonDocument(declarations).toJson());
   auto doc=blueprint();auto nodes=doc["nodes"].toArray();auto node=nodes[0].toObject();auto values=node["properties"].toObject();values["amount"]=0;node["properties"]=values;nodes[0]=node;doc["nodes"]=nodes;doc["opaque"]=QJsonObject{{"raw","escaped text"}};auto original=QJsonDocument(doc).toJson().replace("\n","\r\n").replace("escaped text","escap\\u0065d text").replace("\"label\"","\"la\\u0062el\"");write(project.filePath("blueprint.json"),original);
   MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path(),trustPath);QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");for(auto *item:view->scene()->items())if(item->data(0).isValid())item->setSelected(item->data(0).toInt()==0);
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QCOMPARE(dialog->findChild<QLineEdit *>("v2Property_0")->text(),QString("0"));dialog->findChild<QLineEdit *>("v2Property_0")->setText("-0");dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->click();});w.findChild<QPushButton *>("v2EditValuesButton")->click();QVERIFY(w.isModified());QCOMPARE(read(project.filePath("blueprint.json")),original);QVERIFY(w.saveProject());QTRY_VERIFY_WITH_TIMEOUT(!w.isModified(),30000);QCOMPARE(read(project.filePath("blueprint.json")),QByteArray(original).replace("\"amount\": 0","\"amount\": -0"));
   w.openV2Project(project.path(),trustPath);QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),2,20000);for(auto *item:view->scene()->items())if(item->data(0).isValid())item->setSelected(item->data(0).toInt()==0);
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");QCOMPARE(dialog->findChild<QLineEdit *>("v2Property_0")->text(),QString("-0"));dialog->reject();});w.findChild<QPushButton *>("v2EditValuesButton")->click();
 }
 void delayedPreviewKeepsCapturedNodeAndBlocksStaleTransitions() {
   QTemporaryDir project,trust,runtime;write(project.filePath("blueprint.json"),read(fixture("blueprint.json")));MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path(),bundle(trust));QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);
   const auto protocol=QUrl::fromLocalFile(QFileInfo(QStringLiteral(V2_READER_ENTRY)).dir().filePath("blueprint-document-protocol.js")).toString();write(runtime.filePath("delayed.cjs"),("let s='';process.stdin.on('data',d=>s+=d);process.stdin.on('end',()=>setTimeout(async()=>{let api=await import('"+protocol+"');process.stdout.write(JSON.stringify(await api.handleBlueprintDocumentRequest(JSON.parse(s))))},300));").toUtf8());w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),runtime.filePath("delayed.cjs"));
   QTimer::singleShot(0,&w,[&]{auto *dialog=w.findChild<QDialog *>("v2EditValuesDialog");dialog->findChild<QLineEdit *>("v2ProjectNameEdit")->setText("delayed accepted");dialog->findChild<QPushButton *>("v2ApplyPreviewButton")->click();QVERIFY(!w.newProject());QVERIFY(!w.saveProject());w.exitV2Mode();w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QCOMPARE(loaded.size(),1);QVERIFY(w.isV2Mode());});w.findChild<QPushButton *>("v2EditValuesButton")->click();QVERIFY(w.isModified());QCOMPARE(w.projectDirectory(),project.path());QVERIFY(w.windowTitle().contains("delayed accepted"));
 }
 void staleSaveContinuationCannotOverrideACompletionListenerTransition() {
   QTemporaryDir project;write(project.filePath("blueprint.json"),read(fixture("blueprint.json")));MainWindow w;configure(w);QSignalSpy loaded(&w,&MainWindow::v2LoadFinished);w.openV2Project(project.path());QTRY_COMPARE_WITH_TIMEOUT(loaded.size(),1,20000);editProjectName(w,"saved before transition");connect(&w,&MainWindow::v2SaveFinished,&w,[&](bool success){if(success)QVERIFY(w.newProject());});
   QSignalSpy saved(&w,&MainWindow::v2SaveFinished);{Answers answers({QMessageBox::AcceptRole});w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));QTRY_COMPARE_WITH_TIMEOUT(saved.size(),1,30000);}QVERIFY(!w.isV2Mode());QCOMPARE(w.document().nodes.size(),0);QTest::qWait(300);QVERIFY(!w.isV2Mode());
 }
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
 void chineseV2OpenDialogTranslatesButtons() {
   MainWindow w; QVERIFY(w.setLanguage("zh_CN")); QString openText,cancelText;
   QTimer::singleShot(0,&w,[&] {
     auto *dialog=qobject_cast<QDialog *>(QApplication::activeModalWidget()); if(!dialog)return;
     auto *buttons=dialog->findChild<QDialogButtonBox *>();
     if(buttons) for(auto *button:buttons->buttons()) {
       if(buttons->buttonRole(button)==QDialogButtonBox::AcceptRole)openText=button->text();
       if(buttons->buttonRole(button)==QDialogButtonBox::RejectRole)cancelText=button->text();
     }
     dialog->reject();
   });
   w.findChild<QAction *>("openV2ProjectAction")->trigger();
   QCOMPARE(openText,QString("打开")); QCOMPARE(cancelText,QString("取消")); QVERIFY(!w.isV2Mode());
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
 void defaultV2LayoutIsReadableAndResetPreservesMode_data() {
   QTest::addColumn<QString>("language"); QTest::addColumn<bool>("dark");
   QTest::newRow("english-light")<<QString("en")<<false;
   QTest::newRow("english-dark")<<QString("en")<<true;
   QTest::newRow("chinese-light")<<QString("zh_CN")<<false;
   QTest::newRow("chinese-dark")<<QString("zh_CN")<<true;
 }
 void defaultV2LayoutIsReadableAndResetPreservesMode() {
   QFETCH(QString,language); QFETCH(bool,dark);
   MainWindow w; configure(w); QVERIFY(w.setLanguage(language)); w.setTheme(dark ? EditorTheme::Theme::Dark : EditorTheme::Theme::Light);
   w.resize(1100,700); w.show(); QCoreApplication::processEvents();
   auto *legacyProperties=w.findChild<QDockWidget *>("propertiesDock"); auto *legacyBuild=w.findChild<QDockWidget *>("buildExportDock");
   if(dark)legacyBuild->hide();
   const bool propertiesVisible=legacyProperties->isVisible(), buildVisible=legacyBuild->isVisible();
   QSignalSpy done(&w,&MainWindow::v2LoadFinished); w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));
   QTRY_COMPARE_WITH_TIMEOUT(done.size(),1,20000); QVERIFY(done[0][0].toBool()); QCoreApplication::processEvents();
   auto *banner=w.findChild<QLabel *>("v2ReadOnlyBanner"); auto *view=w.findChild<QGraphicsView *>("v2GraphicsView");
   qInfo()<<"V2 layout: window"<<w.size()<<"banner"<<banner->size()<<"viewport"<<view->viewport()->size()<<"scene"<<view->sceneRect()<<"scale"<<view->transform().m11();
   QVERIFY2(banner->height()<=banner->fontMetrics().height()*3,qPrintable(QString("banner %1px; font %2px").arg(banner->height()).arg(banner->fontMetrics().height())));
   QVERIFY(view->viewport()->height()>w.centralWidget()->height()*0.8);
   QVERIFY(view->viewport()->width()>w.centralWidget()->width()*0.55);
   int nodes=0; QGraphicsItem *first=nullptr;
   for(auto *item:view->scene()->items()) if(item->data(0).isValid()) {
     ++nodes; if(item->data(0).toInt()==0)first=item;
     QVERIFY(view->viewport()->rect().contains(view->mapFromScene(item->sceneBoundingRect().center())));
     auto *label=dynamic_cast<QGraphicsTextItem *>(item->childItems().first()); QVERIFY(label);
     const qreal screenTextHeight=QFontMetricsF(label->font()).height()*view->transform().m22();
     QVERIFY2(screenTextHeight>=10.0,qPrintable(QString("node text height %1px").arg(screenTextHeight)));
   }
   QCOMPARE(nodes,3); QVERIFY(first);
   QTest::mouseClick(view->viewport(),Qt::LeftButton,Qt::NoModifier,view->mapFromScene(first->sceneBoundingRect().center()));
   auto *properties=w.findChild<QPlainTextEdit *>("v2Properties"); QVERIFY(properties->toPlainText().contains("source"));
   properties->selectAll(); properties->copy(); QCOMPARE(QApplication::clipboard()->text(),properties->toPlainText());
   const qreal scale=view->transform().m11(); const QPoint center=view->viewport()->rect().center();
   QWheelEvent zoom(center,view->viewport()->mapToGlobal(center),QPoint(),QPoint(0,120),Qt::NoButton,Qt::ControlModifier,Qt::NoScrollPhase,false);
   QApplication::sendEvent(view->viewport(),&zoom); QVERIFY(view->transform().m11()>scale);
   w.findChild<QAction *>("resetLayoutAction")->trigger(); QCoreApplication::processEvents();
   QVERIFY(!legacyProperties->isVisible()); QVERIFY(!legacyBuild->isVisible());
   w.exitV2Mode(); QCOMPARE(legacyProperties->isVisible(),propertiesVisible); QCOMPARE(legacyBuild->isVisible(),buildVisible);
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
 void sourcePreflightCannotBuildOrExportAfterV2Entry_data() {
   QTest::addColumn<bool>("exporting"); QTest::addColumn<bool>("switchWorkspace"); QTest::addColumn<bool>("rejectBuild");
   for (bool switching : {false, true}) {
     const QByteArray suffix = switching ? "-switch-workspace" : "-same-workspace";
     QTest::newRow(("build" + suffix).constData()) << false << switching << false;
     QTest::newRow(("rejected-build" + suffix).constData()) << false << switching << true;
     QTest::newRow(("export" + suffix).constData()) << true << switching << false;
   }
 }
 void sourcePreflightCannotBuildOrExportAfterV2Entry() {
   QFETCH(bool, exporting); QFETCH(bool, switchWorkspace); QFETCH(bool, rejectBuild);
   QTemporaryDir source, alternate, legacy, output, runtime;
   BlueprintDocument document; document.projectId="preflight"; document.projectName="Preflight"; document.target="qt6-widgets-cpp17-cmake";
   BlueprintNode start; start.id="start"; start.type=NodeType::Start; BlueprintNode end; end.id="end"; end.type=NodeType::End;
   document.nodes={start,end}; document.edges={{"edge","start","end",{}}}; QString error;
   QVERIFY2(ProjectScaffolder::create(document,source.path(),&error),qPrintable(error));
   QVERIFY2(ProjectScaffolder::create(document,alternate.path(),&error),qPrintable(error));
   write(source.filePath("generated-project/note.txt"),"saved");
   // Delay loading the deployed production CLI, not a synthetic Reader response.
   const QByteArray entry = QJsonDocument(QJsonArray{QUrl::fromLocalFile(QStringLiteral(V2_READER_ENTRY)).toString()}).toJson(QJsonDocument::Compact);
   write(runtime.filePath("delayed.cjs"),"setTimeout(()=>import(" + entry.mid(1,entry.size()-2) + "),250);");
   write(runtime.filePath("build.cjs"),"setTimeout(()=>process.exit(0),1000);");
   MainWindow w; QVERIFY(w.addNodeOfType(NodeType::Start)); QVERIFY(w.saveProjectAs(legacy.path()));
   w.setV2ReaderConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),runtime.filePath("delayed.cjs"));
   w.setBuildToolConfiguration(QStringLiteral(V2_NODE_EXECUTABLE),{runtime.filePath("build.cjs")});
   auto *workspace=w.findChild<WorkspaceBrowserWidget *>(); QVERIFY(workspace->setWorkspacePath(source.path())); workspace->openFile("note.txt");
   auto *preview=workspace->findChild<QPlainTextEdit *>("workspacePreview"); preview->insertPlainText("dirty"); QVERIFY(workspace->hasUnsavedChanges());
   w.findChild<QLineEdit *>("workspacePathEdit")->setText(switchWorkspace ? alternate.path() : source.path());
   w.findChild<QLineEdit *>("buildDirectoryEdit")->setText(rejectBuild ? (switchWorkspace ? alternate : source).filePath("generated-project") : output.filePath("build"));
   w.findChild<QLineEdit *>("exportTargetEdit")->setText(output.path());
   QSignalSpy done(&w,&MainWindow::v2LoadFinished); QPointer<QMessageBox> original; int nestedPrompts=0; bool completedInsideOriginal=false;
   const auto discard=[](QMessageBox *box) { for(auto *button:box->buttons()) if(box->buttonRole(button)==QMessageBox::DestructiveRole) { button->click(); return; } };
   QTimer prompts; prompts.setInterval(10);
   connect(&prompts,&QTimer::timeout,&w,[&] {
     auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()); if(!box)return;
     if(!original) original=box;
     else if(box!=original) { ++nestedPrompts; discard(box); }
   }); prompts.start();
   connect(&w,&MainWindow::v2LoadFinished,&w,[&](bool success) {
     completedInsideOriginal=success && original && QApplication::activeModalWidget()==original;
     if(original) discard(original);
   });
   // Avoid hanging the suite if the expected modal ordering stops occurring.
   QTimer deadline; deadline.setSingleShot(true); connect(&deadline,&QTimer::timeout,&w,[&] { if(auto *box=qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))box->reject(); }); deadline.start(15000);
   w.openV2Project(QStringLiteral(V2_FIXTURE_DIR));
   w.findChild<QAction *>(exporting ? "toolbarExportAction" : "toolbarBuildAction")->trigger();
   QCOMPARE(done.size(),1); QVERIFY(done[0][0].toBool()); QVERIFY(completedInsideOriginal); QCOMPARE(nestedPrompts,1); QVERIFY(w.isV2Mode());
   QVERIFY(!w.findChild<BuildService *>()->isRunning());
   QVERIFY(files(output.path()).isEmpty());
   QVERIFY(!w.findChild<QAction *>("toolbarBuildAction")->isEnabled());
   QVERIFY(!w.findChild<QPushButton *>("buildProjectButton")->isEnabled());
   const auto log=w.findChild<QPlainTextEdit *>("buildLog")->toPlainText();
   QVERIFY(!log.contains("Starting configure")); QVERIFY(!log.contains("Export finished"));
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
