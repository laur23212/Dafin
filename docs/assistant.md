# AI Assist (Dafin)

A chat panel inside the editor: type what you want ("cut the pauses longer than a second", "add
bold captions"), and a model edits the open project through Dafin's own MCP tools. It is the
same tool set Claude or Cursor use from outside (see [MCP.md](MCP.md)), called in-process.

## Opening it

The **Agent** button in the header opens the panel with the message box ready. Enter sends,
Shift+Enter starts a new line, **Stop** cancels the request in flight. **New chat** (+) clears
the conversation. The gear opens the settings, which also hold **Connect Claude or Cursor
instead…** (the old Agent access dialog, still in Settings › Agent access too).

## Setting it up

| Setting | Notes |
|---|---|
| AI service | **Anthropic (Claude)** or **OpenAI-compatible**: OpenAI itself, or a local server such as Ollama or LM Studio (`http://localhost:11434/v1`). |
| Model | Default `claude-sonnet-5-5` for Anthropic; for OpenAI-compatible, the name the server lists. |
| API key | Saved in the same credential store as the asset-provider sign-ins (DPAPI on Windows, the Secret Service keyring on Linux, otherwise a file only your account can read; the panel says when it is not encrypted). `ANTHROPIC_API_KEY` / `OPENAI_API_KEY` in the environment win. A local server needs no key. The key is never put in the conversation, the logs, the usage file or MCP replies. |
| Allow the assistant to see frames | **Off** by default. Off, the model is not offered `frames`/`capture`, and is refused if it asks anyway. On, the pictures go to the AI service and are listed in the Agent images log. |
| Editing knowledge folder | Optional. A folder with a `SKILL.md` (for example your Taste Engine) whose text, up to 24,000 characters, is added to the instructions. Read from your disk on each request; nothing is bundled with Dafin. |
| Consent | Per AI service. The panel says exactly what is sent and where. **Until a service is set up and agreed to, no network call is made.** |

## What it can and cannot do

- Every edit goes through the normal tools: `apply` is one undo step; Ctrl+Z works as usual.
- Protected ranges and the "Ask before irreversible agent actions" dialog apply to the panel
  exactly as to an outside agent: a model can never confirm for you.
- **Questions.** When a real choice changes the result, the model can ask one multiple-choice
  question. It shows as buttons with **Skip** and **Not sure**; only a click answers it, never
  text typed in the chat. The answer is a preference, not permission. It expires if the project
  changes before you click.
- A request is stopped after 40 tool calls; ask again to continue.

## Token use

Shown under the message box after each request, as the provider reports it (input, output,
read from cache). Nothing is estimated: a number the provider does not send is "unknown". Each
request also adds a row to `assistant-usage.csv` in Dafin's local data folder (path shown in the
settings): time, provider, model, the four token counts, model calls, tool calls, failed tool
calls, duration, a short project fingerprint before and after, and the outcome (`ok`, `error`,
`stopped`, `limit`).

## Not yet

Answers arrive whole, not word by word; the panel floats over the editor rather than docking into
a workspace; a running render or transcription started by the model keeps running after Stop.

## Code

`src/app/assistant/` — `LlmProtocol.*` (request/reply formats of both services, pure),
`LlmTransport.*` (network, swappable for tests), `AssistantController.*` (conversation, tool
loop, settings, keys, usage). Tools run through `drift::mcp::runTool` (`McpServer.cpp`), shared
with the MCP server. UI: `src/qml/components/panels/AiAssistPanel.qml`, opened from
`EditorHeader.qml`. Tests: `tst_mcp` `assistant*`.
