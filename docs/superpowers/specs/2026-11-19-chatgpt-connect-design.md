# One-click ChatGPT connection, model dropdowns, simpler Agent window

## Why
The user could not connect ChatGPT: the four manual steps (install cloudflared, run it in a
terminal, paste the address back, follow ChatGPT menu names that have since changed) were too
confusing. Dafin should do every step it can by itself, and the window should show only what a
normal user needs, with the technical explanation one click away for people who want to check.

## Goals
- **Connect ChatGPT** is one button. Dafin turns agent access and web access on, finds or
  downloads the tunnel helper (cloudflared, after one OK), starts it in the background (no
  terminal), reads the public https address it prints and builds the ChatGPT address.
- **Copy prompt for ChatGPT**: a complete prompt with the address in it, asking ChatGPT to walk
  the user through adding a custom MCP server in *its current* interface, one step at a time.
  ChatGPT explains its own screens, so Dafin never describes ChatGPT's menus again.
- **Test connection** checks the public address end to end (an MCP `initialize` through the
  tunnel). **Stop** closes the tunnel and web access.
- Every copy button confirms: Dafin reads the clipboard back and the button shows "✓ Copied"
  (or a red "Could not copy").
- Model fields are dropdowns. AI Assist lists the provider's own models (Anthropic
  `/v1/models`, OpenAI-compatible `/models`, incl. Ollama / LM Studio) once a key is set and
  consent given; a built-in list is shown before that or when loading fails; "Other…" is the
  only place a name is typed. Codex gets a dropdown with a fixed list plus Other…
- The Agent window shows: one row of connect buttons, the status, Stop, Copy prompt, the model
  and the "ask before irreversible" switch. Everything else moves under **Details & security**.

## Non-goals / anti-goals
- Dafin does not click in ChatGPT for the user and does not describe ChatGPT's menus.
- No PowerShell or terminal for the user.
- Nothing is removed: manual setup, launch commands, start-on-launch, New key and usage stay,
  under Details & security.
- No network call before the user presses Connect ChatGPT (tunnel) or sets up AI Assist with
  consent (model list).

## Approaches considered
1. **Rewrite the text only** — still leaves the terminal; rejected.
2. **Use an OpenAI API key in AI Assist instead** — no tunnel, but billed apart from the ChatGPT
   subscription; kept as it is, not the answer to "connect ChatGPT".
3. **Dafin runs the tunnel itself + a ChatGPT prompt for the last clicks (chosen).**

## How (decided)
- New `ChatGptConnector` (src/app/mcp/ChatGptConnector.*), owned by McpController and reached as
  `EditorState.mcp.chatGpt`, so the 6000-line controller barely changes.
  - States: `idle`, `needsDownload` (asks OK), `downloading`, `starting`, `connected`, `failed`.
  - Finds cloudflared next to Dafin's data (`<AppData>/tools/`) or on PATH; otherwise downloads
    the latest release asset for this platform (Windows amd64/386, Linux amd64/arm64) from
    GitHub and checks it against the SHA-256 GitHub publishes for that asset (the API's
    `digest`, or the checksum list in the release notes). Mismatch = deleted, nothing run.
    macOS: PATH only (Homebrew), otherwise the manual steps.
  - Starts `cloudflared tunnel --no-autoupdate --url http://127.0.0.1:<port>` with QProcess,
    reads `https://….trycloudflare.com` from its output (30 s timeout), stops it on Stop, on
    access off and on quit.
  - Pure helpers, unit-tested: `parseTunnelAddress(line)`, `chatGptPrompt(url)`,
    `pickReleaseAsset(json, platform)`, `checksumFromRelease(json, asset)`.
- Clipboard: `McpController::copyText(text)` returns whether the clipboard read back equal; all
  copy buttons use it.
- Models: `ModelCatalog` (src/app/assistant/ModelCatalog.*): built-in lists with labels and
  hints, `parseModelList(provider, json)`, labels for known families. AssistantController gets
  `modelOptions`, `modelsLoading`, `refreshModels()`.
- QML: `AgentAccessControls.qml` rebuilt as summary + Details & security; `CopyButton.qml` with
  the ✓ state; `ModelPicker.qml` (dropdown + Other… field) used by AiAssistPanel and Codex.

## Left to the builder
Exact wording, spacing, and the order inside Details & security.
