#include "generation/project_scaffolder.h"
#include "generation/generation_service.h"
#include "blueprint/blueprint_validator.h"
#include "generation/workspace_io.h"
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonArray>
#include <QProcess>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QtTest>
#include <algorithm>

namespace {
BlueprintDocument blueprint()
{
    BlueprintDocument d;
    d.projectId = "project"; d.projectName = "class";
    d.target = "qt6-widgets-cpp17-cmake";
    for (auto pair : {qMakePair("start", NodeType::Start), qMakePair("page", NodeType::UiPage),
                      qMakePair("logic", NodeType::LogicModule), qMakePair("decision", NodeType::Decision),
                      qMakePair("end", NodeType::End), qMakePair("end2", NodeType::End)}) {
        BlueprintNode n; n.id = pair.first; n.type = pair.second; n.name = "name\";#include <evil>";
        n.description = "A complete node contract";
        n.inputs = {{"some port", "}; invalid arbitrary type\n#error injected", "description"}};
        d.nodes.append(n);
    }
    d.edges = {{"a", "start", "page", {}}, {"b", "page", "logic", {}},
               {"c", "logic", "decision", {}}, {"d", "decision", "end", "true"}, {"e", "decision", "end2", "false"}};
    return d;
}
QByteArray read(const QString &path) { QFile f(path); if (!f.open(QIODevice::ReadOnly)) return {}; return f.readAll(); }
bool write(const QString &path, const QByteArray &bytes) { QFile f(path); return f.open(QIODevice::WriteOnly) && f.write(bytes) == bytes.size(); }
const QString candidatePath = "src/modules/logic/implementation/worker.cpp";
GenerationResult result(const QString &content = "// first version\n")
{
    return {"logic", "summary", {{candidatePath, "C:/outside/forged.cpp", content}}};
}
}

class ProjectScaffolderTest : public QObject
{
    Q_OBJECT
private slots:
    void createsDeterministicCompleteSkeleton()
    {
        QTemporaryDir a, b; QString error;
        QVERIFY(BlueprintValidator::validate(blueprint()).isEmpty());
        QVERIFY2(ProjectScaffolder::create(blueprint(), a.path(), &error), qPrintable(error));
        QVERIFY2(ProjectScaffolder::create(blueprint(), b.path(), &error), qPrintable(error));
        for (const QString &p : {QString("CMakeLists.txt"), QString("src/main.cpp"), QString("README.md"),
                 QString("src/contracts/types.h"), QString("src/contracts/blueprint.json"),
                 QString("src/modules/page/contract.h"), QString("src/modules/logic/contract.h"),
                 QString("src/modules/decision/contract.h"), QString("tests/scaffold_smoke.cpp")}) {
            const QByteArray bytes = read(a.path()+"/generated-project/"+p);
            QVERIFY2(!bytes.isEmpty(), qPrintable(p));
            QCOMPARE(bytes, read(b.path()+"/generated-project/"+p));
        }
        QVERIFY(!QFileInfo::exists(a.path()+"/generated-project/src/modules/start"));
        QVERIFY(!QFileInfo::exists(a.path()+"/generated-project/src/modules/end"));
        QVERIFY(QFileInfo::exists(a.path()+"/generation-manifest.json"));
        QVERIFY(ProjectScaffolder::create(blueprint(), a.path(), &error));
    }
    void neverOverwritesManualEdits()
    {
        QTemporaryDir dir; QString error;
        QVERIFY2(ProjectScaffolder::create(blueprint(), dir.path(), &error), qPrintable(error));
        const QString main = dir.path()+"/generated-project/src/main.cpp";
        QVERIFY(write(main, "manual edit"));
        QVERIFY(!ProjectScaffolder::create(blueprint(), dir.path(), &error));
        QCOMPARE(read(main), QByteArray("manual edit"));
    }
    void storesVersionsAndAcceptsOneFile()
    {
        QTemporaryDir dir; QString error;
        QVERIFY(ProjectScaffolder::create(blueprint(), dir.path(), &error));
        auto r = result(); r.files.append({"tests/logic/worker.cpp", {}, "int main() { return 0; }"});
        QVERIFY2(GenerationService::persistCandidate(dir.path(), "batch1", r, "model", "prompt", &error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/"+candidatePath));
        QCOMPARE(read(dir.path()+"/candidates/batch1/logic/"+candidatePath), r.files[0].content.toUtf8());
        auto preview = GenerationService::previewCandidate(dir.path(), "batch1", "logic", candidatePath, &error);
        QVERIFY2(preview.has_value(), qPrintable(error)); QVERIFY(!preview->conflict);
        QVERIFY2(GenerationService::acceptCandidate(dir.path(), *preview, false, std::nullopt, &error), qPrintable(error));
        QCOMPARE(read(dir.path()+"/generated-project/"+candidatePath), r.files[0].content.toUtf8());
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/tests/logic/worker.cpp"));
        QVERIFY(!GenerationService::acceptCandidate(dir.path(), *preview, false, std::nullopt, &error));
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "batch1", r, "model", "prompt", &error));
        QVERIFY(GenerationService::rejectCandidate(dir.path(), "batch1", "logic", "tests/logic/worker.cpp", &error));
        QVERIFY(!GenerationService::previewCandidate(dir.path(), "batch1", "logic", "tests/logic/worker.cpp", &error));
    }
    void manualConflictRequiresExactObservedConfirmation()
    {
        QTemporaryDir dir; QString error;
        QVERIFY(ProjectScaffolder::create(blueprint(), dir.path(), &error));
        QVERIFY(GenerationService::persistCandidate(dir.path(), "v1", result(), "model", "prompt", &error));
        auto p = GenerationService::previewCandidate(dir.path(), "v1", "logic", candidatePath, &error); QVERIFY(p);
        QVERIFY(GenerationService::acceptCandidate(dir.path(), *p, false, std::nullopt, &error));
        QVERIFY(write(dir.path()+"/generated-project/"+candidatePath, "manual edit"));
        QVERIFY(GenerationService::persistCandidate(dir.path(), "v2", result("// v2"), "model", "prompt", &error));
        p = GenerationService::previewCandidate(dir.path(), "v2", "logic", candidatePath, &error); QVERIFY(p); QVERIFY(p->conflict);
        QVERIFY(!GenerationService::acceptCandidate(dir.path(), *p, false, std::nullopt, &error));
        QVERIFY(write(dir.path()+"/generated-project/"+candidatePath, "later manual edit"));
        QVERIFY(!GenerationService::acceptCandidate(dir.path(), *p, true, std::nullopt, &error));
        QCOMPARE(read(dir.path()+"/generated-project/"+candidatePath), QByteArray("later manual edit"));
        p = GenerationService::previewCandidate(dir.path(), "v2", "logic", candidatePath, &error); QVERIFY(p);
        QVERIFY(GenerationService::acceptCandidate(dir.path(), *p, true, QByteArray("// edited then accepted"), &error));
        QCOMPARE(read(dir.path()+"/generated-project/"+candidatePath), QByteArray("// edited then accepted"));
        QCOMPARE(read(dir.path()+"/candidates/v2/logic/"+candidatePath), QByteArray("// v2"));
    }
    void cancellationAndStaleCandidates()
    {
        QTemporaryDir dir; QString error;
        QVERIFY(ProjectScaffolder::create(blueprint(), dir.path(), &error));
        QVERIFY(GenerationService::persistCandidate(dir.path(), "old", result(), "model", "prompt", &error));
        QVERIFY(GenerationService::persistCandidate(dir.path(), "new", result("// newer"), "model", "prompt", &error));
        auto newer = GenerationService::previewCandidate(dir.path(), "new", "logic", candidatePath, &error); QVERIFY(newer);
        QVERIFY(GenerationService::acceptCandidate(dir.path(), *newer, false, std::nullopt, &error));
        auto older = GenerationService::previewCandidate(dir.path(), "old", "logic", candidatePath, &error); QVERIFY(older); QVERIFY(older->conflict);
        QVERIFY(!GenerationService::acceptCandidate(dir.path(), *older, false, std::nullopt, &error));
        QVERIFY(GenerationService::cancelCandidate(dir.path(), "old", "logic", &error));
        QVERIFY(!GenerationService::previewCandidate(dir.path(), "old", "logic", candidatePath, &error));
    }
    void rejectsForgedOwnershipPathsAndPrefixCollisions()
    {
        QTemporaryDir dir; QString error;
        QVERIFY(ProjectScaffolder::create(blueprint(), dir.path(), &error));
        const QByteArray before = read(dir.path()+"/generation-manifest.json");
        for (const QString &path : {QString("../escape.cpp"), QString("C:/escape.cpp"), QString("src/main.cpp"),
                QString("src/modules/page/implementation/file.cpp"), QString("src/modules/logic/contract.h"),
                QString("src/modules/logic/implementation/placeholder.h"), QString("src/modules/logic/implementation/CON.cpp"),
                QString("src/modules/logic/implementation/../file.cpp")}) {
            auto r = result(); r.files[0].relativePath = path;
            QVERIFY2(!GenerationService::persistCandidate(dir.path(), "bad", r, "model", "prompt", &error), qPrintable(path));
        }
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "../bad", result(), "model", "prompt", &error));
        auto r = result(); r.nodeId = "page";
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "bad", r, "model", "prompt", &error));
        r = result(); r.files.append({candidatePath+"/nested.cpp", {}, "x"});
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "bad", r, "model", "prompt", &error));
        r = result(); r.files.append({candidatePath.toUpper(), {}, "x"});
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "bad", r, "model", "prompt", &error));
        QCOMPARE(read(dir.path()+"/generation-manifest.json"), before);
    }
    void tamperingAndFilesystemFailureLeaveProjectUntouched()
    {
        QTemporaryDir dir; QString error;
        QVERIFY(ProjectScaffolder::create(blueprint(), dir.path(), &error));
        QVERIFY(GenerationService::persistCandidate(dir.path(), "v1", result(), "model", "prompt", &error));
        auto p = GenerationService::previewCandidate(dir.path(), "v1", "logic", candidatePath, &error); QVERIFY(p);
        QVERIFY(write(dir.path()+"/candidates/v1/logic/"+candidatePath, "tampered"));
        QVERIFY(!GenerationService::acceptCandidate(dir.path(), *p, true, std::nullopt, &error));
        QVERIFY(!QFileInfo::exists(dir.path()+"/generated-project/"+candidatePath));
        QVERIFY(GenerationService::persistCandidate(dir.path(), "v2", result(), "model", "prompt", &error));
        p = GenerationService::previewCandidate(dir.path(), "v2", "logic", candidatePath, &error); QVERIFY(p);
        QVERIFY(QDir().mkpath(dir.path()+"/generated-project/"+candidatePath));
        const QByteArray before = read(dir.path()+"/generation-manifest.json");
        QVERIFY(!GenerationService::acceptCandidate(dir.path(), *p, true, std::nullopt, &error));
        QCOMPARE(read(dir.path()+"/generation-manifest.json"), before);
        QVERIFY(write(dir.path()+"/generation-manifest.json", "{}"));
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "v3", result(), "model", "prompt", &error));
    }
    void canonicalBlueprintFingerprintIncludesControlNodes()
    {
        QTemporaryDir a, b; QString error; auto d = blueprint();
        QVERIFY(ProjectScaffolder::create(d, a.path(), &error));
        std::reverse(d.nodes.begin(), d.nodes.end()); std::reverse(d.edges.begin(), d.edges.end());
        QVERIFY(ProjectScaffolder::create(d, b.path(), &error));
        QCOMPARE(read(a.path()+"/generation-manifest.json"), read(b.path()+"/generation-manifest.json"));
        for (auto &n : d.nodes) if (n.id == "end2") n.constraints = {"Changed control metadata"};
        QVERIFY(!ProjectScaffolder::create(d, a.path(), &error));
    }
    void parserRejectsFileDirectoryPrefixCollision()
    {
        QTemporaryDir dir; QString error;
        const QByteArray payload = QJsonDocument(QJsonObject{{"nodeId", "logic"}, {"summary", "x"},
            {"files", QJsonArray{QJsonObject{{"path", "a.cpp"}, {"content", "x"}},
                                 QJsonObject{{"path", "a.cpp/nested.cpp"}, {"content", "y"}}}}}).toJson();
        QVERIFY(!GenerationService::parseAndValidate(payload, "logic", dir.path(), {}, &error));
    }
    void rechecksJunctionsAtWriteTime()
    {
        QTemporaryDir dir, outside; QString error;
        QVERIFY(ProjectScaffolder::create(blueprint(), dir.path(), &error));
        auto r = result(); r.files[0].relativePath = "src/modules/logic/implementation/nested/worker.cpp";
        QVERIFY(GenerationService::persistCandidate(dir.path(), "v1", r, "model", "prompt", &error));
        auto p = GenerationService::previewCandidate(dir.path(), "v1", "logic", r.files[0].relativePath, &error); QVERIFY(p);
        const QString link = dir.path()+"/generated-project/src/modules/logic/implementation/nested";
#ifdef Q_OS_WIN
        if (QProcess::execute("cmd.exe", {"/c", "mklink", "/J", QDir::toNativeSeparators(link), QDir::toNativeSeparators(outside.path())}) != 0)
            QSKIP("Directory junction creation unavailable");
        const auto cleanup = qScopeGuard([&] { QDir().rmdir(link); });
#else
        if (!QFile::link(outside.path(), link)) QSKIP("Directory symlink creation unavailable");
        const auto cleanup = qScopeGuard([&] { QFile::remove(link); });
#endif
        QVERIFY(!GenerationService::acceptCandidate(dir.path(), *p, true, std::nullopt, &error));
        QVERIFY(!QFileInfo::exists(outside.path()+"/worker.cpp"));
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "v2", r, "model", "prompt", &error));
    }
    void rejectsMalformedBatchesAndDiskCaseAliases()
    {
        QTemporaryDir dir; QString error;
        QVERIFY(ProjectScaffolder::create(blueprint(), dir.path(), &error));
        QVERIFY(write(dir.path()+"/generated-project/src/modules/logic/implementation/WORKER.cpp", "external local code"));
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "v1", result(), "model", "prompt", &error));
        auto manifest = QJsonDocument::fromJson(read(dir.path()+"/generation-manifest.json")).object();
        manifest["batches"] = QJsonObject{{"bad", QJsonObject{}}};
        QVERIFY(write(dir.path()+"/generation-manifest.json", QJsonDocument(manifest).toJson()));
        QVERIFY(!GenerationService::persistCandidate(dir.path(), "v2", result(), "model", "prompt", &error));
    }
    void rejectsFinalNewlineInPortableSegments()
    {
        QVERIFY(!WorkspaceIo::safeId("batch\n"));
        QVERIFY(!WorkspaceIo::safeRelative("src/modules/logic\n/implementation/a.cpp"));
    }
};
QTEST_GUILESS_MAIN(ProjectScaffolderTest)
#include "tst_project_scaffolder.moc"
