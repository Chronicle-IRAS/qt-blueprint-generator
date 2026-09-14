# Issue #17 AI Provider Settings and Real API Integration Plan

> **For agentic workers:** REQUIRED SUB-SKILLS: Use test-driven development and execute this plan task-by-task. Each implementation group must receive an independent specification review and code-quality review before integration.

**Goal:** Add a usable OpenAI-compatible provider configuration and connection test, keep API credentials outside project files and logs, and provide an opt-in real DeepSeek integration path without weakening existing generation validation or candidate safeguards.

**Architecture:** Introduce a non-secret `AiProviderSettings` value object persisted through an allowlisted `QSettings` adapter. Extend the AI client with structured, sanitized failures and a small connection-test request, then expose the settings through a modal `AiSettingsDialog` opened from the main window. The API key remains environment-only (`BLUEPRINT_AI_API_KEY`). A separate manual integration executable exercises the real provider through the existing `PromptCompiler`, `OpenAiCompatibleClient`, `GenerationService`, and candidate persistence boundary; it is never registered in CTest.

**Tech Stack:** C++17, Qt 6 Widgets/Network/Test, CMake, Qt Linguist, OpenAI-compatible Chat Completions API.

---

## File map

- Create `src/ai/ai_provider_settings.h/.cpp`: provider defaults, URL/model validation, allowlisted non-secret `QSettings` persistence, environment credential status.
- Modify `src/ai/ai_client.h`, `src/ai/fake_ai_client.*`, `src/ai/openai_compatible_client.*`: structured safe failures and optional low-cost connection-probe request fields.
- Create `src/ai/ai_connection_tester.h/.cpp`: one-shot asynchronous connection state around an injected `IAiClient`.
- Create `src/app/ai_settings_dialog.h/.cpp`: modal settings form, environment-key status, save/cancel, offline-injectable Test Connection action.
- Modify `src/app/main_window.*`: add an AI menu/action and open the settings dialog.
- Modify `CMakeLists.txt` and `tests/CMakeLists.txt`: compile new production files and offline Qt Test targets.
- Create `tools/manual_ai_integration.cpp`: opt-in real-provider workflow; never part of CTest and never prints credentials.
- Create `tests/tst_ai_provider_settings.cpp`, `tests/tst_ai_connection_tester.cpp`, `tests/tst_ai_settings_dialog.cpp`; update existing AI/generation/language tests.
- Modify `translations/BlueprintEditor_zh_CN.ts`, `README.md`, `doc/project-usage-guide.md`, and `doc/implementation-progress.md`.

## Task 1: Define and persist non-secret provider settings

- [x] Add failing tests for the expected DeepSeek-compatible defaults: endpoint `https://api.deepseek.com/chat/completions`, model `deepseek-flash`, provider `openai-compatible`, credential source `environment`.
- [x] Add failing tests rejecting non-HTTPS endpoints, missing hosts, user information, query strings, fragments, and empty model names.
- [x] Add failing tests proving load/save only touches the provider, endpoint, model, and credential-source keys and never writes the API key or any arbitrary environment value.
- [x] Implement `AiProviderSettings`, `AiCredentialSource`, validation, allowlisted persistence, and `BLUEPRINT_AI_API_KEY` availability checks.
- [x] Keep defaults usable without persisting them until the user explicitly saves.
- [x] Run the focused settings test to GREEN and commit this completed group.

Expected value boundary:

```cpp
enum class AiCredentialSource { Environment };

struct AiProviderSettings {
    QString providerId = QStringLiteral("openai-compatible");
    QUrl endpoint = QUrl(QStringLiteral("https://api.deepseek.com/chat/completions"));
    QString model = QStringLiteral("deepseek-flash");
    AiCredentialSource credentialSource = AiCredentialSource::Environment;
};
```

## Task 2: Add sanitized client errors and offline connection testing

- [x] First update Qt tests so they fail against the current string-only `requestFailed` signal and absent connection tester.
- [x] Define `AiErrorKind` and `AiClientError` with a safe user-facing message and optional HTTP status; register the metatype for `QSignalSpy`.
- [x] Preserve `GenerationService`'s public string failure contract by mapping structured client failures to safe text at that boundary.
- [x] Classify configuration, missing credential, network, timeout, authentication, payment, rate limiting, endpoint/model, invalid response, and provider-unavailable failures without copying response bodies, prompts, or keys into signals/logs.
- [x] Extend `AiRequest` only with probe-safe optional fields (`maxTokens`, `disableThinking`) that normal generation leaves unset; verify the request JSON includes them only when requested.
- [x] Implement `AiConnectionTester` around an injected client. It must emit Testing then Success or one sanitized failure, ignore unrelated request IDs, and handle repeated/cancelled object lifetimes safely.
- [x] Cover every branch with fake clients and network reply stubs; no test may access the real network.
- [x] Run `openai_compatible_client`, `generation_service`, and `ai_connection_tester` tests to GREEN and commit this completed group.

Expected safe failure boundary:

```cpp
enum class AiErrorKind {
    InvalidConfiguration,
    CredentialUnavailable,
    Network,
    Timeout,
    Authentication,
    PaymentRequired,
    EndpointNotFound,
    ModelNotFound,
    RateLimited,
    InvalidResponse,
    ProviderUnavailable,
    Unknown
};

struct AiClientError {
    AiErrorKind kind = AiErrorKind::Unknown;
    int httpStatus = 0;
    QString safeMessage;
};
```

## Task 3: Add the bilingual AI settings dialog

- [ ] Add failing GUI tests for stable object names, loaded defaults, edit/save/reopen persistence, Cancel isolation, environment credential status, invalid-form rejection, and Test Connection button state.
- [ ] Inject an AI-client factory so success, authentication failure, timeout, endpoint failure, and provider failure are tested entirely offline.
- [ ] Implement a modal `AiSettingsDialog` with Provider, full Chat Completions HTTPS endpoint, Model, API key source, key availability, Test Connection, status, Save, and Cancel.
- [ ] Do not provide a plaintext key editor or persist secrets. Explain in the dialog that the key comes from `BLUEPRINT_AI_API_KEY`.
- [ ] Test the unsaved form values, disable duplicate connection attempts while running, reset stale status when fields change, and save only after validation succeeds.
- [ ] Add `AI > Provider Settings...` to `MainWindow`, preserving existing menus, actions, blueprints, docks, and undo history.
- [ ] Add complete Simplified Chinese translations and verify runtime English/Chinese switching does not erase unsaved settings drafts.
- [ ] Run GUI and language tests to GREEN and commit this completed group.

Required stable object names:

```text
aiMenu
aiSettingsAction
aiSettingsDialog
aiProviderCombo
aiEndpointEdit
aiModelEdit
aiApiKeySourceCombo
aiApiKeyStatusLabel
aiTestConnectionButton
aiConnectionStatusLabel
aiSettingsSaveButton
aiSettingsCancelButton
```

## Task 4: Add the opt-in real integration path and user documentation

- [ ] Add an opt-in CMake target for `tools/manual_ai_integration.cpp`; keep it out of the default build if practical and never call `add_test()` for it.
- [ ] Make the tool load only `BLUEPRINT_AI_API_KEY`, accept optional endpoint/model arguments, create a temporary/scoped workspace, compile a real blueprint prompt, invoke `GenerationService`, and persist only a validated candidate.
- [ ] Never print or serialize the API key, Authorization header, full prompt, raw provider body, or sensitive environment values. Never automatically accept generated code.
- [ ] Add an offline test for argument/default handling and the manual workflow's pre-network failure path where practical.
- [ ] Document environment setup, GUI configuration, connection testing, DeepSeek defaults, manual integration invocation, possible HTTP/provider failures, costs, and key rotation guidance in README and the usage guide.
- [ ] Update implementation progress with RED/GREEN evidence and completed commit IDs.
- [ ] Build the opt-in target. Run its real DeepSeek path only when the key is supplied through the process environment; record the sanitized result without recording the secret.
- [ ] Commit this completed group.

## Task 5: Verify, review, and deliver

- [ ] Run a clean configure/build in an ASCII-only build directory and full CTest; all automated tests must remain offline.
- [ ] Verify Qt translation hygiene, startup behavior, Markdown local links, `git diff --check`, and absence of tracked secret-like values.
- [ ] Re-run focused security regressions for schema, node ID, path traversal, file extension/size, absolute timeout, manual-edit protection, and candidate boundaries.
- [ ] Obtain an independent specification review followed by an independent code-quality review; resolve every Critical, Important, and actionable Minor finding.
- [ ] Update README and progress with final verification only after evidence exists.
- [ ] Push normally and create a PR titled `[feat] Add AI provider settings and real API integration (#17)` whose body includes `Closes #17`; do not force-push or auto-merge.

## Scope guard

- Do not add the normal Generate button or candidate-review GUI; those belong to Issue #18.
- Do not add Python/TypeScript workers, a multi-provider plugin framework, or migrate the current C++ architecture.
- Do not store credentials in `QSettings`, files, Git, exported projects, candidates, test fixtures, logs, exceptions, signals, or screenshots.
- Do not expose raw provider response bodies in user-visible failures.
- Do not weaken `GenerationService`, serializer/schema, node ownership, traversal, extension/size, timeout, manual-edit, candidate, or external-code boundaries.
- Do not commit `start-blueprint-editor.cmd` or any build output.
