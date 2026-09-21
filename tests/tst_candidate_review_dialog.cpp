#include "app/candidate_review_dialog.h"
#include "generation/project_scaffolder.h"

#include <QFile>
#include <QAbstractButton>
#include <QJsonDocument>
#include <QJsonObject>
#include <QListWidget>
#include <QLabel>
#include <QTranslator>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTimer>
#include <QtTest>

namespace {
class ReviewTranslator final : public QTranslator {
public:
    bool isEmpty() const override { return false; }
    QString translate(const char *context, const char *source, const char *, int) const override {
        return QByteArray(context) == "CandidateReviewDialog" ? QString("translated: ") + QString::fromUtf8(source) : QString();
    }
};
BlueprintDocument blueprint()
{
    BlueprintDocument d;
    d.projectId = "review-project";
    d.projectName = "Review";
    d.target = "qt6-widgets-cpp17-cmake";
    d.nodes = {{"start", NodeType::Start, "Start"},
               {"logic", NodeType::LogicModule, "Logic", "Compute a result"},
               {"end", NodeType::End, "End"}};
    d.edges = {{"a", "start", "logic", {}}, {"b", "logic", "end", {}}};
    return d;
}
const QString path = "src/modules/logic/implementation/worker.cpp";
const QString secondPath = "tests/logic/worker.cpp";
GenerationResult candidate()
{
    return {"logic", "A candidate summary", {{path, {}, "// candidate\n"},
                                             {secondPath, {}, "int main() { return 0; }\n"}}};
}
QByteArray read(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return {};
    return f.readAll();
}
bool write(const QString &path, const QByteArray &content)
{
    QFile f(path);
    return f.open(QIODevice::WriteOnly) && f.write(content) == content.size();
}
bool prepare(const QString &workspace)
{
    return ProjectScaffolder::create(blueprint(), workspace)
        && GenerationService::persistCandidate(workspace, "review", candidate(), "fake", "prompt");
}
QString currentPath(const QString &workspace) { return workspace + "/generated-project/" + path; }
QString state(const QString &workspace, const QString &file = path)
{
    return QJsonDocument::fromJson(read(workspace + "/generation-manifest.json")).object()
        .value("batches").toObject().value("review").toObject().value("files").toObject()
        .value(file).toObject().value("state").toString();
}
void answerNextQuestion(QMessageBox::StandardButton answer)
{
    QTimer::singleShot(0, [answer] {
        auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
        if (box && box->button(answer)) box->button(answer)->click();
    });
}
}

class CandidateReviewDialogTest final : public QObject
{
    Q_OBJECT
private slots:
    void conflictIndicatorPersistsAndTranslates()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        QVERIFY(write(currentPath(dir.path()), "// manual\n"));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        dialog.show();
        auto *indicator = dialog.findChild<QLabel *>("conflictIndicator");
        QVERIFY(indicator); QVERIFY(indicator->isVisible());
        const QString original = indicator->text();
        QVERIFY(original.contains("Conflict"));
        answerNextQuestion(QMessageBox::No);
        dialog.findChild<QPushButton *>("acceptCandidateButton")->click();
        QVERIFY(indicator->isVisible());
        ReviewTranslator translator; QApplication::installTranslator(&translator);
        QEvent event(QEvent::LanguageChange); QApplication::sendEvent(&dialog, &event);
        QCOMPARE(indicator->text(), "translated: " + original);
        auto *files = dialog.findChild<QListWidget *>("candidateFiles");
        files->setCurrentRow(1); QVERIFY(!indicator->isVisible());
        files->setCurrentRow(0); QVERIFY(indicator->isVisible());
    }
    void statusRetranslatesWithoutLosingReviewState_data()
    {
        QTest::addColumn<QString>("action");
        for (const auto &action : {"accepted", "rejected", "invalidated", "refreshed", "error"})
            QTest::newRow(action) << QString(action);
    }
    void statusRetranslatesWithoutLosingReviewState()
    {
        QFETCH(QString, action);
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        auto *editor = dialog.findChild<QPlainTextEdit *>("candidateEditor");
        editor->setPlainText("// preserved draft\n");
        if (action == "accepted") dialog.findChild<QPushButton *>("acceptCandidateButton")->click();
        if (action == "rejected") dialog.findChild<QPushButton *>("rejectCandidateButton")->click();
        if (action == "invalidated") dialog.invalidateContext();
        if (action == "refreshed") {
            answerNextQuestion(QMessageBox::Yes);
            dialog.findChild<QPushButton *>("refreshPreviewButton")->click();
        }
        if (action == "error") {
            QVERIFY(write(currentPath(dir.path()), "// changed\n"));
            dialog.findChild<QPushButton *>("acceptCandidateButton")->click();
        }
        auto *status = dialog.findChild<QLabel *>("reviewStatus");
        const auto original = status->text(); QVERIFY(!original.isEmpty());
        const auto manifestBefore = read(dir.path() + "/generation-manifest.json");
        ReviewTranslator translator; QApplication::installTranslator(&translator);
        QEvent event(QEvent::LanguageChange); QApplication::sendEvent(&dialog, &event);
        QCOMPARE(status->text(), "translated: " + original);
        QCOMPARE(editor->toPlainText(), QString("// preserved draft\n"));
        auto *files = dialog.findChild<QListWidget *>("candidateFiles");
        files->setCurrentRow(1); files->setCurrentRow(0);
        QCOMPARE(status->text(), "translated: " + original);
        QCOMPARE(read(dir.path() + "/generation-manifest.json"), manifestBefore);
        if (action == "invalidated" || action == "accepted" || action == "rejected")
            QVERIFY(!dialog.findChild<QPushButton *>("acceptCandidateButton")->isEnabled());
    }
    void showsFilesAndEditableDiff()
    {
        QTemporaryDir dir;
        QVERIFY(prepare(dir.path()));
        QVERIFY(write(currentPath(dir.path()), "// manual\n"));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        auto *files = dialog.findChild<QListWidget *>("candidateFiles");
        auto *current = dialog.findChild<QPlainTextEdit *>("currentEditor");
        auto *next = dialog.findChild<QPlainTextEdit *>("candidateEditor");
        QVERIFY(files); QVERIFY(current); QVERIFY(next);
        QCOMPARE(files->count(), 2);
        QVERIFY(files->item(0)->text().contains(path));
        QVERIFY(current->isReadOnly()); QVERIFY(!next->isReadOnly());
        QCOMPARE(current->toPlainText(), QString("// manual\n"));
        QCOMPARE(next->toPlainText(), QString("// candidate\n"));
        QVERIFY(!current->extraSelections().isEmpty());
        QVERIFY(!next->extraSelections().isEmpty());
    }
    void acceptsOnlySelectedFile()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        auto *button = dialog.findChild<QPushButton *>("acceptCandidateButton"); QVERIFY(button);
        button->click();
        QCOMPARE(read(currentPath(dir.path())), QByteArray("// candidate\n"));
        QCOMPARE(state(dir.path()), QString("accepted"));
        QCOMPARE(state(dir.path(), secondPath), QString("pending"));
    }
    void preservesDraftAcrossSelectionAndLanguageChange()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        auto *editor = dialog.findChild<QPlainTextEdit *>("candidateEditor"); QVERIFY(editor);
        auto *files = dialog.findChild<QListWidget *>("candidateFiles"); QVERIFY(files);
        editor->setPlainText("// draft\n");
        files->setCurrentRow(1); files->setCurrentRow(0);
        QEvent languageChange(QEvent::LanguageChange);
        QApplication::sendEvent(&dialog, &languageChange);
        QCOMPARE(editor->toPlainText(), QString("// draft\n"));
        dialog.findChild<QPushButton *>("acceptCandidateButton")->click();
        QCOMPARE(read(currentPath(dir.path())), QByteArray("// candidate\n"));
        dialog.findChild<QPushButton *>("cancelRemainingButton")->click();
        QCOMPARE(state(dir.path()), QString("accepted"));
        QVERIFY(QFile::exists(dir.path() + "/candidates/review/logic/" + path));
    }
    void editThenAcceptRetainsOriginalCandidate()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        auto *editor = dialog.findChild<QPlainTextEdit *>("candidateEditor"); QVERIFY(editor);
        editor->setPlainText("// edited\n");
        auto *button = dialog.findChild<QPushButton *>("editAcceptCandidateButton"); QVERIFY(button);
        button->click();
        QCOMPARE(read(currentPath(dir.path())), QByteArray("// edited\n"));
        QCOMPARE(read(dir.path() + "/candidates/review/logic/" + path), QByteArray("// candidate\n"));
    }
    void rejectsAndCancelsWithoutWritingProject()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        auto *reject = dialog.findChild<QPushButton *>("rejectCandidateButton"); QVERIFY(reject);
        reject->click();
        QCOMPARE(state(dir.path()), QString("rejected"));
        auto *cancel = dialog.findChild<QPushButton *>("cancelRemainingButton"); QVERIFY(cancel);
        cancel->click();
        QCOMPARE(state(dir.path(), secondPath), QString("rejected"));
        QVERIFY(!QFile::exists(currentPath(dir.path())));
    }
    void conflictRequiresExplicitConfirmation()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        QVERIFY(write(currentPath(dir.path()), "// manual\n"));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        auto *accept = dialog.findChild<QPushButton *>("acceptCandidateButton"); QVERIFY(accept);
        answerNextQuestion(QMessageBox::No); accept->click();
        QCOMPARE(read(currentPath(dir.path())), QByteArray("// manual\n"));
        QCOMPARE(state(dir.path()), QString("pending"));
        answerNextQuestion(QMessageBox::Yes); accept->click();
        QCOMPARE(read(currentPath(dir.path())), QByteArray("// candidate\n"));
    }
    void changedAfterPreviewRequiresRefreshAndNewAcceptance()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        QVERIFY(write(currentPath(dir.path()), "// changed after preview\n"));
        auto *accept = dialog.findChild<QPushButton *>("acceptCandidateButton"); QVERIFY(accept);
        accept->click();
        QCOMPARE(state(dir.path()), QString("pending"));
        QCOMPARE(read(currentPath(dir.path())), QByteArray("// changed after preview\n"));
        auto *refresh = dialog.findChild<QPushButton *>("refreshPreviewButton"); QVERIFY(refresh);
        answerNextQuestion(QMessageBox::No); refresh->click();
        QCOMPARE(dialog.findChild<QPlainTextEdit *>("currentEditor")->toPlainText(), QString());
        answerNextQuestion(QMessageBox::Yes); refresh->click();
        QCOMPARE(state(dir.path()), QString("pending"));
        answerNextQuestion(QMessageBox::Yes); accept->click();
        QCOMPARE(state(dir.path()), QString("accepted"));
    }
    void invalidationBlocksAccept()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        dialog.invalidateContext();
        auto *accept = dialog.findChild<QPushButton *>("acceptCandidateButton"); QVERIFY(accept);
        QVERIFY(!accept->isEnabled()); accept->click();
        QVERIFY(!QFile::exists(currentPath(dir.path())));
    }
    void blueprintMismatchAndProtectedEditsBlockAccept()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        auto changed = blueprint(); changed.projectName = "Changed";
        CandidateReviewDialog dialog(changed, dir.path(), "review", candidate());
        auto *accept = dialog.findChild<QPushButton *>("acceptCandidateButton"); QVERIFY(accept);
        accept->click();
        QVERIFY(!QFile::exists(currentPath(dir.path())));
        CandidateReviewDialog second(blueprint(), dir.path(), "review", candidate());
        const auto protectedFile = dir.path() + "/generated-project/src/main.cpp";
        QVERIFY(write(protectedFile, "// manual protected file"));
        second.findChild<QPushButton *>("acceptCandidateButton")->click();
        QVERIFY(!QFile::exists(currentPath(dir.path())));
        QCOMPARE(read(protectedFile), QByteArray("// manual protected file"));
    }
    void closingAsksBeforeCancellingRemaining()
    {
        QTemporaryDir dir; QVERIFY(prepare(dir.path()));
        CandidateReviewDialog dialog(blueprint(), dir.path(), "review", candidate());
        dialog.show();
        answerNextQuestion(QMessageBox::No); dialog.reject();
        QVERIFY(dialog.isVisible()); QCOMPARE(state(dir.path()), QString("pending"));
        answerNextQuestion(QMessageBox::Yes); dialog.reject();
        QVERIFY(!dialog.isVisible()); QCOMPARE(state(dir.path()), QString("rejected"));
    }
};
QTEST_MAIN(CandidateReviewDialogTest)
#include "tst_candidate_review_dialog.moc"
