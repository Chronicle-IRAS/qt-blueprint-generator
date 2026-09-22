#pragma once

#include "ai/ai_provider_settings.h"
#include "blueprint/blueprint_document.h"
#include "blueprint/blueprint_validator.h"
#include "generation/generation_service.h"
#include <functional>

struct CandidateBatch
{
    QString generationId;
    QString workspace;
    GenerationResult result;
};
Q_DECLARE_METATYPE(CandidateBatch)

// A single GUI-thread generation session. The editor must call invalidateContext
// after semantic document or workspace changes (layout/selection changes are not semantic).
class GenerationController final : public QObject
{
    Q_OBJECT
public:
    enum class State { Idle, Generating, Success, Failed, Cancelled };
    Q_ENUM(State)
    // The returned client is owned by the controller, even if the factory omits its parent.
    using ClientFactory = std::function<IAiClient *(const AiProviderSettings &, QObject *)>;
    explicit GenerationController(QObject *parent = nullptr);
    explicit GenerationController(ClientFactory factory, QObject *parent = nullptr);
    // Take snapshots before any factory or signal callback can modify caller state.
    bool start(BlueprintDocument document, QString nodeId,
               QString absoluteWorkspace, AiProviderSettings provider);
    void cancel();
    void invalidateContext(const BlueprintDocument &document, const QString &workspace);
    State state() const { return m_state; }
    QString errorMessage() const { return m_error ? tr(m_error) : QString(); }
    // Blueprint validation diagnostics of the last failed start(); empty for every
    // other failure (provider, workspace, selection) and for a successful session.
    QVector<BlueprintDiagnostic> diagnostics() const { return m_diagnostics; }
    std::optional<CandidateBatch> candidateBatch() const { return m_batch; }
signals:
    void stateChanged(GenerationController::State state);
    void candidateReady(const CandidateBatch &batch);
private:
    void finish(State state, const char *safeError = nullptr);
    ClientFactory m_factory;
    State m_state = State::Idle;
    const char *m_error = nullptr;
    QPointer<QObject> m_session;
    QUuid m_token;
    BlueprintDocument m_document;
    QString m_workspace;
    QVector<BlueprintDiagnostic> m_diagnostics;
    std::optional<CandidateBatch> m_batch;
};
