# One-click ChatGPT connection Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans. Steps use checkbox (`- [ ]`) syntax.

**Goal:** Connect ChatGPT with one button, model dropdowns, a short Agent window with a Details & security expander.
**Architecture:** New `ChatGptConnector` (tunnel download/run/test) and `ModelCatalog` (model lists) with pure, unit-tested helpers; QML rebuilt on two small new components.
**Tech Stack:** C++17, Qt 6 (Core, Network, Gui, QtTest), QML.
**Spec:** docs/superpowers/specs/2026-11-19-chatgpt-connect-design.md

## Global Constraints
- No network call before the user presses Connect ChatGPT, or before AI Assist has a key and consent.
- A downloaded cloudflared runs only after its SHA-256 matches the one GitHub publishes for that asset.
- Nothing existing is removed; it moves under "Details & security".
- New files are added to `CMakeLists.txt` (app) and `tests/CMakeLists.txt` (tst_mcp sources).

## Review Focus
- The tunnel prints its address on stderr among many log lines → only `https://<name>.trycloudflare.com` is taken (test `chatGptParsesTunnelAddress`).
- The release JSON has no `digest` → checksum taken from the release notes, else refuse (test `chatGptChecksumFromRelease`).
- No asset for this platform (macOS, 32-bit Linux) → state `failed` with the manual steps offered (test `chatGptPicksAssetPerPlatform`).
- A provider's model list is huge or holds non-chat models (embeddings, tts, whisper) → filtered out (test `modelCatalogParsesAndFilters`).
- The saved model is not in the loaded list → it is still shown and selected (test `modelCatalogKeepsCurrent`).

---

### Task 1: ChatGptConnector helpers + class
**Files:** Create `src/app/mcp/ChatGptConnector.{h,cpp}`; Modify `McpController.{h,cpp}` (own it, `Q_PROPERTY(ChatGptConnector *chatGpt CONSTANT)`, `bool copyText(QString)`), `CMakeLists.txt`, `tests/CMakeLists.txt`; Test `tests/tst_mcp.cpp`.
**Produces:** `namespace dafin::chatgpt { QString parseTunnelAddress(const QString &line); QString chatGptPrompt(const QString &url); QJsonObject pickReleaseAsset(const QJsonObject &release, const QString &platform); QByteArray checksumFromRelease(const QJsonObject &release, const QJsonObject &asset); QString currentPlatform(); }`; class `ChatGptConnector` with `state`, `address`, `chatGptAddress`, `message`, `testResult`, `connect()`, `confirmDownload()`, `stop()`, `test()`, `copyPrompt()`, `copyAddress()`.
- [ ] Tests: `chatGptParsesTunnelAddress`, `chatGptPromptHoldsTheAddress`, `chatGptPicksAssetPerPlatform`, `chatGptChecksumFromRelease`.
- [ ] Implement; commit.

### Task 2: ModelCatalog + AssistantController model list
**Files:** Create `src/app/assistant/ModelCatalog.{h,cpp}`; Modify `AssistantController.{h,cpp}` (`modelOptions`, `modelsLoading`, `refreshModels()`), CMake lists; Test `tst_mcp.cpp`.
**Produces:** `QVariantList builtInModels(Provider)`, `QVariantList parseModelList(Provider, const QByteArray &json)`, `QVariantList withCurrent(QVariantList, const QString &current)`, `QVariantList codexModels()`; each option `{value, label, hint}`.
- [ ] Tests: `modelCatalogParsesAndFilters`, `modelCatalogKeepsCurrent`.
- [ ] Implement; commit.

### Task 3: QML
**Files:** Create `src/qml/components/shell/CopyButton.qml`, `ModelPicker.qml`; Rewrite `AgentAccessControls.qml`; Modify `AiAssistPanel.qml`, `SettingsAiPage.qml` description; register in the QML module list in `CMakeLists.txt`.
- [ ] Implement; commit.

### Task 4: Docs
`docs/MCP.md` (ChatGPT section), `docs/assistant.md` (model list), `DAFIN.md` row. Commit.
