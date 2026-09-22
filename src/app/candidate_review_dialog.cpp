#include "app/candidate_review_dialog.h"
#include "generation/project_scaffolder.h"
#include <QEvent>
#include <QLabel>
#include <QListWidget>
#include <QMessageBox>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalBlocker>
#include <QSplitter>
#include <QGridLayout>
#include "ui/theme.h"
#include <QTextBlock>
#include <QVBoxLayout>

struct CandidateReviewDialog::Impl {
    enum class Status { None, PreviewError, RefreshError, Refreshed, ConsistencyError, AcceptError, Accepted, RejectError, Rejected, CancelError };
    struct File {
        QString path, draft;
        std::optional<CandidatePreview> preview;
        bool finished = false, accepted = false;
        Status status = Status::None;
        QString error;
    };
    CandidateReviewDialog *q;
    BlueprintDocument snapshot;
    QString workspace, generation, node;
    QVector<File> files;
    int row = -1;
    bool valid = true;
    QListWidget *list;
    QPlainTextEdit *current, *candidate;
    QLabel *currentLabel, *candidateLabel, *status, *conflict;
    QPushButton *accept, *editAccept, *reject, *cancel, *refresh;

    void renderStatus() {
        QString message;
        if (!valid) message = CandidateReviewDialog::tr("The blueprint or workspace changed. Acceptance is disabled.");
        else if (row >= 0) {
            const auto &f = files[row];
            switch (f.status) {
            case Status::None: break;
            case Status::PreviewError: message = CandidateReviewDialog::tr("Cannot preview candidate: %1").arg(f.error); break;
            case Status::RefreshError: message = CandidateReviewDialog::tr("Cannot refresh preview: %1").arg(f.error); break;
            case Status::Refreshed: message = CandidateReviewDialog::tr("Preview refreshed. Review the files before accepting."); break;
            case Status::ConsistencyError: message = CandidateReviewDialog::tr("Project consistency check failed: %1").arg(f.error); break;
            case Status::AcceptError: message = CandidateReviewDialog::tr("Cannot accept candidate: %1. Refresh the preview if files have changed.").arg(f.error); break;
            case Status::Accepted: message = CandidateReviewDialog::tr("File accepted."); break;
            case Status::RejectError: message = CandidateReviewDialog::tr("Cannot reject candidate: %1").arg(f.error); break;
            case Status::Rejected: message = CandidateReviewDialog::tr("File rejected."); break;
            case Status::CancelError: message = CandidateReviewDialog::tr("Cannot cancel remaining candidates: %1").arg(f.error); break;
            }
        }
        status->setText(message);
    }
    void setStatus(Status value, const QString &error = {}) {
        if (row >= 0) { files[row].status = value; files[row].error = error; }
        renderStatus();
    }

    void translate() {
        q->setWindowTitle(CandidateReviewDialog::tr("Review generated candidates"));
        currentLabel->setText(CandidateReviewDialog::tr("Current file")); candidateLabel->setText(CandidateReviewDialog::tr("Candidate / edited draft"));
        accept->setText(CandidateReviewDialog::tr("Accept original")); editAccept->setText(CandidateReviewDialog::tr("Accept edited draft"));
        reject->setText(CandidateReviewDialog::tr("Reject file")); cancel->setText(CandidateReviewDialog::tr("Cancel remaining")); refresh->setText(CandidateReviewDialog::tr("Refresh preview"));
        conflict->setText(CandidateReviewDialog::tr("Conflict: this file contains manual or newer changes. Accepting requires overwrite confirmation."));
        renderStatus();
    }
    void update() {
        const bool pending = row >= 0 && !files[row].finished;
        const bool ready = pending && valid && files[row].preview.has_value();
        accept->setEnabled(ready); editAccept->setEnabled(ready); reject->setEnabled(pending);
        refresh->setEnabled(pending && valid); candidate->setReadOnly(!ready);
        conflict->setVisible(pending && files[row].preview && files[row].preview->conflict);
        renderStatus();
        for (int i = 0; i < files.size(); ++i)
            list->item(i)->setText(files[i].path + (files[i].finished
                ? (files[i].accepted ? CandidateReviewDialog::tr(" — Accepted") : CandidateReviewDialog::tr(" — Rejected")) : CandidateReviewDialog::tr(" — Pending")));
    }
    void diff() {
        auto highlight = [](QPlainTextEdit *editor, QPlainTextEdit *other, QColor color) {
            auto lines = editor->toPlainText().split('\n'), otherLines = other->toPlainText().split('\n');
            QList<QTextEdit::ExtraSelection> selections;
            for (int i = 0; i < lines.size(); ++i) {
                if (i < otherLines.size() && lines[i] == otherLines[i]) continue;
                QTextEdit::ExtraSelection s;
                s.cursor = QTextCursor(editor->document()->findBlockByNumber(i));
                s.format.setBackground(color); s.format.setProperty(QTextFormat::FullWidthSelection, true);
                selections.append(s);
            }
            editor->setExtraSelections(selections);
        };
        highlight(current, candidate, EditorTheme::colors().diffRemoved);
        highlight(candidate, current, EditorTheme::colors().diffAdded);
    }
    void select(int selected) {
        row = selected;
        QSignalBlocker blocker(candidate);
        if (row < 0) { current->clear(); candidate->clear(); update(); return; }
        auto &f = files[row];
        if (!f.preview && !f.finished) {
            QString error;
            f.preview = GenerationService::previewCandidate(workspace, generation, node, f.path, &error);
            if (f.preview) f.draft = QString::fromUtf8(f.preview->candidateContent);
            else setStatus(Status::PreviewError, error);
        }
        current->setPlainText(f.preview ? QString::fromUtf8(f.preview->currentContent.value_or(QByteArray())) : QString());
        candidate->setPlainText(f.draft); diff(); update();
    }
    void refreshFile() {
        if (!valid || row < 0 || files[row].finished) return;
        if (QMessageBox::question(q, CandidateReviewDialog::tr("Refresh preview"), CandidateReviewDialog::tr("Load the current files again? Review the updated comparison before accepting."),
            QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes || !valid) return;
        QString error;
        auto preview = GenerationService::previewCandidate(workspace, generation, node, files[row].path, &error);
        if (!preview) { setStatus(Status::RefreshError, error); return; }
        files[row].preview = preview;
        current->setPlainText(QString::fromUtf8(preview->currentContent.value_or(QByteArray())));
        setStatus(Status::Refreshed); diff(); update();
    }
    void acceptFile(bool edited) {
        if (!valid || row < 0 || files[row].finished || !files[row].preview) return;
        QString error;
        if (!ProjectScaffolder::create(snapshot, workspace, &error)) {
            setStatus(Status::ConsistencyError, error); return;
        }
        // Preserve the exact hashes the user inspected across the confirmation dialog.
        const auto preview = *files[row].preview;
        bool confirmed = false;
        if (preview.conflict) {
            confirmed = QMessageBox::question(q, CandidateReviewDialog::tr("Confirm overwrite"),
                CandidateReviewDialog::tr("This file contains manual or newer changes. Overwrite it with the reviewed candidate?"),
                QMessageBox::Yes | QMessageBox::No, QMessageBox::No) == QMessageBox::Yes;
            if (!confirmed) return;
        }
        if (!valid) return;
        if (!ProjectScaffolder::create(snapshot, workspace, &error)) {
            setStatus(Status::ConsistencyError, error); return;
        }
        auto content = edited ? std::optional<QByteArray>(files[row].draft.toUtf8()) : std::nullopt;
        if (!GenerationService::acceptCandidate(workspace, preview, confirmed, content, &error)) {
            setStatus(Status::AcceptError, error); return;
        }
        files[row].finished = files[row].accepted = true;
        setStatus(Status::Accepted); update();
    }
    void rejectFile() {
        if (row < 0 || files[row].finished) return;
        QString error;
        if (!GenerationService::rejectCandidate(workspace, generation, node, files[row].path, &error)) {
            setStatus(Status::RejectError, error); return;
        }
        files[row].finished = true; setStatus(Status::Rejected); update();
    }
    bool pending() const { for (const auto &f : files) if (!f.finished) return true; return false; }
    bool cancelAll() {
        if (!pending()) return true;
        QString error;
        if (!GenerationService::cancelCandidate(workspace, generation, node, &error)) {
            setStatus(Status::CancelError, error); return false;
        }
        for (auto &f : files) if (!f.finished) { f.finished = true; f.status = Status::Rejected; }
        update(); return true;
    }
};

CandidateReviewDialog::CandidateReviewDialog(const BlueprintDocument &snapshot, const QString &workspace,
    const QString &generationId, const GenerationResult &result, QWidget *parent)
    : QDialog(parent), d(std::make_unique<Impl>())
{
    d->q = this; d->snapshot = snapshot; d->workspace = workspace; d->generation = generationId; d->node = result.nodeId;
    resize(1000, 650);
    auto *layout = new QVBoxLayout(this);
    auto *summary = new QLabel(result.summary, this); summary->setTextFormat(Qt::PlainText); summary->setWordWrap(true); layout->addWidget(summary);
    layout->setContentsMargins(EditorTheme::SpaceLarge, EditorTheme::SpaceLarge,
                               EditorTheme::SpaceLarge, EditorTheme::SpaceLarge);
    layout->setSpacing(EditorTheme::SpaceMedium);
    d->list = new QListWidget(this); d->list->setObjectName("candidateFiles"); d->list->setMaximumHeight(112); layout->addWidget(d->list, 1);
    auto *splitter = new QSplitter(this); splitter->setChildrenCollapsible(false); layout->addWidget(splitter, 3);
    auto addEditor = [splitter](QLabel *&label, QPlainTextEdit *&editor, const char *name) {
        auto *panel = new QWidget(splitter); auto *column = new QVBoxLayout(panel);
        label = new QLabel(panel); column->addWidget(label);
        editor = new QPlainTextEdit(panel); editor->setObjectName(name); column->addWidget(editor);
        editor->setFont(EditorTheme::codeFont());
        column->setContentsMargins(0, 0, 0, 0); column->setSpacing(EditorTheme::SpaceMedium);
    };
    addEditor(d->currentLabel, d->current, "currentEditor"); addEditor(d->candidateLabel, d->candidate, "candidateEditor");
    d->current->setReadOnly(true);
    d->conflict = new QLabel(this); d->conflict->setObjectName("conflictIndicator"); d->conflict->setTextFormat(Qt::PlainText); d->conflict->setWordWrap(true); layout->addWidget(d->conflict);
    d->status = new QLabel(this); d->status->setObjectName("reviewStatus"); d->status->setTextFormat(Qt::PlainText); d->status->setWordWrap(true); layout->addWidget(d->status);
    auto *buttons = new QGridLayout; layout->addLayout(buttons);
    auto addButton = [this, buttons, index = 0](const char *name) mutable { auto *b = new QPushButton(this); b->setObjectName(name); b->setAutoDefault(false); buttons->addWidget(b, index / 3, index % 3); ++index; return b; };
    d->accept = addButton("acceptCandidateButton"); d->editAccept = addButton("editAcceptCandidateButton");
    d->reject = addButton("rejectCandidateButton"); d->refresh = addButton("refreshPreviewButton"); d->cancel = addButton("cancelRemainingButton");
    d->accept->setProperty("role", "primary");
    for (const auto &file : result.files) { d->files.append({file.relativePath, {}, std::nullopt, false, false}); d->list->addItem(file.relativePath); }
    connect(d->list, &QListWidget::currentRowChanged, this, [this](int row) { d->select(row); });
    connect(d->candidate, &QPlainTextEdit::textChanged, this, [this] { if (d->row >= 0) d->files[d->row].draft = d->candidate->toPlainText(); d->diff(); });
    connect(d->accept, &QPushButton::clicked, this, [this] { d->acceptFile(false); });
    connect(d->editAccept, &QPushButton::clicked, this, [this] { d->acceptFile(true); });
    connect(d->reject, &QPushButton::clicked, this, [this] { d->rejectFile(); });
    connect(d->refresh, &QPushButton::clicked, this, [this] { d->refreshFile(); });
    connect(d->cancel, &QPushButton::clicked, this, [this] { if (d->cancelAll()) QDialog::reject(); });
    d->translate(); if (!d->files.isEmpty()) d->list->setCurrentRow(0); d->update();
}
CandidateReviewDialog::~CandidateReviewDialog() = default;
void CandidateReviewDialog::invalidateContext() {
    d->valid = false; d->update();
}
void CandidateReviewDialog::reject() {
    if (d->pending() && QMessageBox::question(this, tr("Close candidate review"),
        tr("Reject all remaining candidates and close? Accepted files will be retained."),
        QMessageBox::Yes | QMessageBox::No, QMessageBox::No) != QMessageBox::Yes) return;
    if (d->cancelAll()) QDialog::reject();
}
void CandidateReviewDialog::changeEvent(QEvent *event) {
    QDialog::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) { d->translate(); d->update(); }
    // The diff highlights are theme colours captured when the comparison was built, so an open
    // dialog has to rebuild them when the theme changes.
    else if (event->type() == QEvent::PaletteChange || event->type() == QEvent::StyleChange) { d->diff(); }
}
