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
| Editing knowledge folder | Optional. **Empty = Dafin's built-in editing skill** (Taste Engine v0.9.2 text, 12 Skills + 10 styles, plus the private Creator Style v6 cards, used only when you ask for that style; source in `assistant-skill/`, compiled into the app, text only, no frames). A folder with your own `SKILL.md` replaces it. Its `SKILL.md` (up to 24,000 characters) is added to the instructions; the model opens other files of it one at a time with the read-only `read_skill_file` tool (12,000 characters each, only inside the skill). Your own folder is read from disk on each request. |
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

## AI Fill (timeline)

The **✦ AI Fill** button on the timeline toolbar uses the same AI service. Drag across the timeline
to mark a space (a click marks 3 s), describe what goes there in the card beside it, and press
Enter. The model gets a conversation of its own, separate from the chat, with every tool the
chat has (so a request can reach beyond the space: "…and arrows on the next clip") plus two more:

| Tool | What it does |
|---|---|
| `check_fill({json \| svg})` | Parse a document and report what will draw and what is skipped, without placing it. |
| `fill_region({json \| svg, title})` | Place the animation exactly on the marked space (on the drawn row when it is a free graphic lane, else on a new graphic track right above it), or, for **Change it** / **Try again**, give the clip there a new version. One undo step. Calling it again within the same request replaces that request's version. |

Protected ranges, the irreversible-action dialog and "Allow the assistant to see frames" apply as in
the chat, and an `ask_user` question shows as buttons on the AI Fill card. A document file is read
in, so each version (its request, document and time) keeps its own copy; versions are saved with the
project, up to 20, and the card's **◀ Version N of M ▶** switches between them as an undoable edit.
Everything else the AI does is ordinary edits (Ctrl+Z). 40 tool calls per request, as in the chat.
Token use is shown and logged as for the chat.

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
