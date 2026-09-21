#pragma once

#include "blueprint/blueprint_document.h"
#include "generation/generation_service.h"
#include <QDialog>
#include <memory>

class CandidateReviewDialog final : public QDialog
{
    Q_OBJECT
public:
    CandidateReviewDialog(const BlueprintDocument &snapshot, const QString &workspace,
                          const QString &generationId, const GenerationResult &result,
                          QWidget *parent = nullptr);
    void invalidateContext();
    ~CandidateReviewDialog() override;
    void reject() override;
protected:
    void changeEvent(QEvent *event) override;
private:
    struct Impl;
    std::unique_ptr<Impl> d;
};
