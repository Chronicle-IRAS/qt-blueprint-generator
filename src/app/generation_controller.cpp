#include "app/generation_controller.h"
#include "ai/openai_compatible_client.h"
#include "blueprint/blueprint_validator.h"
#include "generation/project_scaffolder.h"
#include "generation/prompt_compiler.h"
#include "generation/workspace_io.h"
#include <QDir>
#include <QFileInfo>
#include <QJsonDocument>
#include <QNetworkAccessManager>
#include <memory>
#include <utility>

namespace {
QString normalized(const QString &workspace)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(workspace));
}
bool readScaffold(const QString &workspace, const QString &relative, QByteArray &bytes)
{
    std::optional<QByteArray> content;
    if (!WorkspaceIo::read(workspace, "generated-project/" + relative, content, nullptr) || !content)
        return false;
    bytes = *content;
    return true;
}
}

GenerationController::GenerationController(QObject *parent)
    : GenerationController([](const AiProviderSettings &s, QObject *owner) {
          // Code generation can take longer than a connection check.
          auto *network = new QNetworkAccessManager(owner);
          return new OpenAiCompatibleClient(s.endpoint, s.model, network,
                                            std::chrono::seconds(180), owner);
      }, parent)
{
}

GenerationController::GenerationController(ClientFactory factory, QObject *parent)
    : QObject(parent), m_factory(std::move(factory))
{
}

bool GenerationController::start(BlueprintDocument document, QString nodeId,
                                 QString workspace, AiProviderSettings provider)
{
    if (m_state == State::Generating) return false;
    m_batch.reset();
    m_diagnostics.clear();
    const auto fail = [this](const char *message) { finish(State::Failed, message); return false; };
    bool generatable = false;
    for (const auto &node : document.nodes) {
        if (node.id == nodeId)
            generatable = node.type == NodeType::UiPage || node.type == NodeType::LogicModule
                          || node.type == NodeType::Decision;
    }
    if (!generatable) return fail(QT_TR_NOOP("Select a UI page, logic module, or decision node."));
    if (!provider.validate()) return fail(QT_TR_NOOP("AI provider settings are invalid."));
    if (!QDir::isAbsolutePath(workspace) || !QFileInfo(workspace).isDir())
        return fail(QT_TR_NOOP("Select an existing absolute workspace directory."));
    const QVector<BlueprintDiagnostic> validationDiagnostics =
        BlueprintValidator::validate(document, {workspace});
    if (!validationDiagnostics.isEmpty()) {
        m_diagnostics = validationDiagnostics;
        return fail(QT_TR_NOOP("Fix blueprint validation errors before generating."));
    }
    if (!ProjectScaffolder::create(document, workspace))
        return fail(QT_TR_NOOP("The workspace scaffold could not be initialized or does not match the blueprint."));

    QByteArray ir, types, contract;
    if (!readScaffold(workspace, "src/contracts/blueprint.json", ir)
        || !readScaffold(workspace, "src/contracts/types.h", types)
        || !readScaffold(workspace, "src/modules/" + nodeId + "/contract.h", contract))
        return fail(QT_TR_NOOP("Could not read the workspace contracts."));
    QJsonParseError parseError;
    const QJsonDocument parsed = QJsonDocument::fromJson(ir, &parseError);
    if (parseError.error != QJsonParseError::NoError || !parsed.isObject())
        return fail(QT_TR_NOOP("The workspace blueprint contract is invalid."));
    auto prompt = PromptCompiler::compileModulePrompt(parsed.object(), nodeId);
    if (!prompt) return fail(QT_TR_NOOP("Could not prepare the node generation prompt."));
    *prompt += QStringLiteral("\n\nPublic types:\n") + QString::fromUtf8(types)
        + QStringLiteral("\nNode contract:\n") + QString::fromUtf8(contract)
        + QStringLiteral("\nCandidate file boundary:\n"
                         "- Every returned path must start with exactly one allowed root:\n"
                         "  - src/modules/%1/implementation/\n"
                         "  - tests/%1/\n"
                         "- Allowed extensions: .h, .hpp, .cpp, .cc\n"
                         "- Do not return protected scaffold files or paths outside these roots.\n"
                         "- Include the actual node contract above and derive from its class, using its exact namespace and signatures.\n"
                         "- The contract include is \"modules/%1/contract.h\" and the shared types include is \"contracts/types.h\". Do not infer header paths from module metadata.\n"
                         "- Each test .cpp/.cc is a separate executable and must provide its own main() (or Qt Test main macro).\n").arg(nodeId);
    if (!m_factory) return fail(QT_TR_NOOP("AI client is unavailable."));
    auto *session = new QObject(this);
    IAiClient *client = m_factory(provider, session);
    if (!client) { session->deleteLater(); return fail(QT_TR_NOOP("AI client is unavailable.")); }
    client->setParent(session);
    // Capture only the structured kind before GenerationService forwards its failure.
    // Associate it with the request ID so unrelated or late client signals cannot classify it.
    auto providerFailure = std::make_shared<std::pair<QUuid, AiErrorKind>>();
    connect(client, &IAiClient::requestFailed, session,
            [providerFailure](const QUuid &id, const AiClientError &error) {
        *providerFailure = {id, error.kind};
    });
    auto *service = new GenerationService(client, session);
    m_document = document;
    m_workspace = normalized(workspace);
    m_session = session;
    // Allocate identity before generate(): a fake client may complete inside that call.
    const QUuid token = m_token = QUuid::createUuid();
    const QString generationId = QStringLiteral("generation_") + token.toString(QUuid::Id128);
    const QString capturedWorkspace = m_workspace;
    connect(service, &GenerationService::generationSucceeded, this,
            [this, token, document, capturedWorkspace, provider, prompt = *prompt, generationId]
            (const QUuid &, const GenerationResult &result) {
        if (m_token != token || m_state != State::Generating) return;
        if (!ProjectScaffolder::create(document, capturedWorkspace)) {
            finish(State::Failed, QT_TR_NOOP("The workspace scaffold changed during generation."));
            return;
        }
        if (!GenerationService::persistCandidate(capturedWorkspace, generationId, result, provider.model, prompt)) {
            finish(State::Failed, QT_TR_NOOP("The generated candidate could not be safely saved."));
            return;
        }
        const CandidateBatch batch{generationId, capturedWorkspace, result};
        m_batch = batch;
        const QPointer<GenerationController> guard(this);
        finish(State::Success);
        if (guard && m_batch && m_batch->generationId == generationId)
            emit candidateReady(batch);
    });
    connect(service, &GenerationService::generationFailed, this,
            [this, token, providerFailure, clientGuard = QPointer<IAiClient>(client)](const QUuid &id, const QString &) {
        if (m_token != token || m_state != State::Generating) return;
        if (!clientGuard) { finish(State::Failed, QT_TR_NOOP("AI client is unavailable.")); return; }
        const auto kind = providerFailure->first == id ? providerFailure->second : AiErrorKind::InvalidResponse;
        const char *message = QT_TR_NOOP("AI generation failed. Check provider settings and try again.");
        switch (kind) {
        case AiErrorKind::InvalidConfiguration: message = QT_TR_NOOP("AI provider settings are invalid."); break;
        case AiErrorKind::CredentialUnavailable: message = QT_TR_NOOP("AI provider credentials are unavailable."); break;
        case AiErrorKind::Network: message = QT_TR_NOOP("AI generation failed due to a network error."); break;
        case AiErrorKind::Timeout: message = QT_TR_NOOP("AI generation timed out. Try again."); break;
        case AiErrorKind::Authentication: message = QT_TR_NOOP("AI provider authentication failed."); break;
        case AiErrorKind::PaymentRequired: message = QT_TR_NOOP("AI provider payment or quota is required."); break;
        case AiErrorKind::EndpointNotFound: message = QT_TR_NOOP("AI provider endpoint was not found."); break;
        case AiErrorKind::ModelNotFound: message = QT_TR_NOOP("The configured AI model was not found."); break;
        case AiErrorKind::RateLimited: message = QT_TR_NOOP("AI provider rate limit reached. Try again later."); break;
        case AiErrorKind::InvalidResponse: message = QT_TR_NOOP("AI returned an invalid or unsafe generation response."); break;
        case AiErrorKind::ProviderUnavailable: message = QT_TR_NOOP("AI provider is unavailable. Try again later."); break;
        case AiErrorKind::Unknown: break;
        }
        finish(State::Failed, message);
    });
    m_state = State::Generating;
    m_error = nullptr;
    const QPointer<GenerationController> guard(this);
    emit stateChanged(m_state);
    if (!guard || m_token != token || m_state != State::Generating) return true;
    service->generate(*prompt, nodeId, QDir(capturedWorkspace).filePath("candidates/" + generationId + '/' + nodeId));
    return true;
}

void GenerationController::finish(State state, const char *safeError)
{
    m_token = QUuid();
    if (m_session) m_session->deleteLater();
    m_session.clear();
    m_state = state;
    m_error = safeError;
    emit stateChanged(state);
}

void GenerationController::cancel()
{
    if (m_state == State::Generating) finish(State::Cancelled);
}

void GenerationController::invalidateContext(const BlueprintDocument &document, const QString &workspace)
{
    if (document == m_document && normalized(workspace) == m_workspace) return;
    m_batch.reset();
    if (m_state == State::Generating || m_state == State::Success) finish(State::Cancelled);
}
