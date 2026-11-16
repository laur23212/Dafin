# Dafin Taste Engine v0.9.2 + Creator Style v6 — source audit and integration plan

Status: **AUDIT ONLY. No Dafin source file was changed. Nothing was pushed.**
Audited: `laur23212/Dafin`, branch `dafin-edition`, commit `ce7e3203f482cb25fd0d686cbbab53b19406e683` (tag `v2.8.2`, "Tests: fix four expectations that were written without running them"). Shallow clone of what is on GitHub; work that is unpushed in other chats is not visible here.

Labels used below: **VERIFIED** = read in the Dafin source or docs at that commit. **RAN** = I executed it. **UNKNOWN** = could not be checked from source alone. **PROPOSED** = not done.

---

## 1. Summary and opinions

1. **Dafin's MCP already does most of what the Taste Engine's infrastructure was going to build.** It has progressive tool discovery (`catalog` → `search` → `toolbox` → `apply`), word-level transcripts with cut-by-words and a `dry_run`, contact-sheet frames, an activity profile, async jobs with cancel, one-undo-step batches and a history you can jump back through. Porting the Python router, a clarification gate and a usage benchmark into C++ would mostly duplicate this.
2. **What is genuinely new and valuable is the editorial knowledge:** the 12 Skills, 10 styles, the tutorial rules and the v6 cards (about 6.6 KB of cards in total). That knowledge can reach the editor with **no native code at all**, as a small skill that the connected assistant loads. I recommend doing that first (Stage B0) and only adding native code where the audit found a real gap.
3. **The handoff assumes an "existing Dafin AI assistant panel". It does not exist.** Dafin has an Agent Access switch and an Agent log. The conversation happens in the external client (Claude, Cursor). So the "native one-question clarification UI" would be a brand-new Qt widget, not an extension. I recommend deferring it (see 4, Stage C).
4. **The real safety gaps are different from the ones the packages focus on:** there is no per-action approval (Agent Access is one session-wide switch), no "protected clip/range" concept exposed to MCP, and some ops are not undoable. These are small and worth building before any autonomous tutorial trimming.
5. **The reference router has one real bug** (section 3, item 3): a prompt that asks for audio repair plus captions plus a title silently loses the captions and the title.
6. **Licensing blocks publishing the packages in the public repo.** The Taste Engine says its redistribution license is still unselected, the repo is GPL-3.0, and v6 is explicitly private. Only this plan document should go into the repo.

---

## 2. Requirement → Dafin source → status → decision → test

| # | Requirement | Existing Dafin source (VERIFIED) | Status | Decision | Test |
|---|---|---|---|---|---|
| 1 | MCP registry / tool discovery | `src/app/mcp/tools/McpCatalog.cpp` (`homepageTools()` L1549, op table, ~330 ops in 22 toolboxes by static scan, approximate); `McpProtocol.cpp` L34 (`instructions`); `docs/MCP.md` L115-132 | EXISTS | **Reuse.** No new server | `tst_mcp`; run `scripts/mcp-probe.py` for reply sizes |
| 2 | "Connected AI assistant UI" | Only `AgentAccessControls.qml`, `AgentAccessDialog.qml`, `AgentLogPanel.qml` (log of calls). No chat/assistant panel | **MISSING** (assumption in packages is wrong) | **Defer** native question UI; use the client's own question UI | Review in section 3 item 1 |
| 3 | Read-only vs write permission model | One session switch "Allow for this session" (`AgentAccessControls.qml`); `docs/MCP.md` L863-869 ("any local process with the token has full editor access"); per-op hints `toolAnnotations(readOnly,destructive,idempotent)` (`McpJson.h` L166, `McpCatalog.cpp` L1158, `isReadOnlyOp` L1708). The only per-action consent I found is for cloud voice/transcription providers and the marketplace (`consent_required`, `McpController.cpp` L5338; error list in `docs/MCP.md`) | PARTIAL | **Extend** (small): native confirm for the non-undoable list (Stage C) | New test: non-undoable op without native confirm is refused |
| 4 | Whisper / transcript timestamps | `src/engine/ml/speech/transcription/{WhisperTranscriber,LocalTranscription,CtcAligner,WhisperTokenizer}.h`, `src/core/project/Transcript.h`; MCP `transcribe` (`McpController.cpp` L4943), `get_transcript` (L5091), `cut_words` (L5935), `keep_ranges` (L5744), `assemble` (L5808); catalog `McpCatalogExtendedOps.inl` L815-873; docs `MCP.md` L268-323 | EXISTS | **Reuse. No second Whisper.** Whether the whisper/align models are *installed on the owner's machine* is UNKNOWN | `tst_mcp`: `getTranscriptReadsPhrasesAndWords`, `cutWordsByTextAndIndex`, `keepRangesRebuildsLinkedPair` already exist; add the alignment guard test T7 |
| 5 | Frame-accurate evidence for a cut | `frames({at:[…]})`, `capture`, `activity`, `get_waveform({image:true})` (words lane) — `MCP.md` L223-266, L296-297 | EXISTS | **Reuse**; the plan must show frames on both sides of every cut | T4 |
| 6 | Scene / shot tools | `detect_scenes`, `list_scenes`, `find_scenes`, `split_on_scenes`, `bookmark_scenes` (`MCP.md` L359-380) | EXISTS | **Reuse** | covered by T4 |
| 7 | Timeline / edit history / undo | `list_history`, `undo_to`, `take_snapshot`, `restore_snapshot` (`McpCatalog.cpp` L689-714); `apply` = one undo step (`McpDispatcher.cpp` ~L700) | EXISTS | **Reuse.** Bind plans to the history `hash`, not the integer revision | T5 |
| 8 | Project revision | `inspect().revision`, `since` short-circuit (`McpController.cpp` L591, L717). Plain per-instance counter | PARTIAL | **Adapter**: store `(project path, revision, history hash)` in the plan | T5 |
| 9 | Clip protection (`must_keep`) | **Not exposed.** Dispatcher refuses moves on a locked track (`McpDispatcher.cpp` L1404) but `set_track` only has `muted`/`hidden` (`McpCatalog.cpp` L621-627) and I found no `locked` field in `inspect` | MISSING | **Build, small** — only if autonomous trimming is wanted (Stage C). Otherwise rely on dry-run + approval | T3, T4 |
| 10 | Cancellation | `get_job`, `cancel_job` (`McpCatalogExtendedOps.inl` L1863-1869); export `export_status`; `apply` is not atomic (`MCP.md` Atomicity row) | PARTIAL | **Reuse**; plan must handle partial `apply` using `done`/`stopped` + `undo_to` | T5 |
| 11 | Caption / text / animation / preset IDs | `generate_subtitles`, `set_subtitle_cues`, `list_text_presets`, `apply_text_preset`, `apply_text_look`, `list_text_animations`, `set_text_animation`, `list_fonts` | EXISTS | **Reuse.** IDs must be read at run time, never copied from the packages | T1 |
| 12 | Reframe / orientation / export | `auto_reframe` (canvas toolbox), `apply_canvas_crop`, `set_clip_orientation`, `list_export_presets`, `export_with_preset`, `export_video`, `export_status`. Preview "Show Safe Zones" is UI only | EXISTS | **Reuse** | T1 |
| 13 | Independent portrait layout for a landscape project | One project = one canvas. A portrait variant means a second project | PARTIAL | **Adapter**: duplicate project → set canvas → `auto_reframe` → own captions/undo/revision | T1 |
| 14 | Pair matching by source range/hash, not title | `set_metadata` has only fixed fields `title`/`author`/`description` (`McpCatalog.cpp` L914-918), so it cannot hold a link cleanly; no source-hash registry found | MISSING | **Build, small**: new project field `{assetHash, srcStart, srcEnd, otherProject}` saved with the project (a project-file change); the free-text `description` is only a stopgap | T1, T2 |
| 15 | Privacy of what the model sees | `AgentImageLog.{h,cpp}` records every image returned to the agent ("so the user can see what the agent was shown"); `frames` supports `return:"path"` | EXISTS (audit trail) / no redaction | **Reuse** the log. Redaction of account names in frames is **defer** | T8 |
| 16 | Taste task router (19 tasks) | Python only: `dafin_taste/skill_graph.py`, `graph/skill_graph.json` (13,207 bytes) | NOT IN DAFIN | **Defer porting.** First deliver as a skill the client reads (Stage B0) | Routing matrix (T6) |
| 17 | `tool_groups_advisory` names | Abstract labels (`transcript.read`, `captions.edit`, `preview.frames`, …). They are not Dafin ops | N/A | **Adapter**: one mapping table to real ops (section 4, Stage B0) | review |
| 18 | One native clarification question | No panel. The client (Claude app / Cursor) can ask its own multiple-choice question | MISSING | **Defer** | T6 |
| 19 | Provider token telemetry | Dafin sees calls and replies, **not** tokens. `AgentLogPanel.qml` shows call duration. `scripts/mcp-probe.py` prints reply size in characters | PARTIAL | **Extend small** (reply bytes + op + history hash in the log) + read tokens at the client | section 6 |
| 20 | Licenses | Dafin is GPL-3.0 (`LICENSE`). Taste `NOTICE.md`: "redistribution license … still needs selection". v6 `NOTICE.md`: private, do not bundle frames | OPEN | **Owner decision** before anything from the packages enters the public repo | — |

---

## 3. Conflicts and inaccurate assumptions in the three packages

Package paths are relative to the unzipped handoff. Original v0.9.2 and v6 contracts take priority; nothing was edited.

1. **"Existing Dafin AI panel" does not exist.** `dafin-taste-engine-v0.9.2/CLAUDE_START_HERE.md` L8 (item 4), `docs/CURRENT_CLAUDE_INTEGRATION_v092.md` L22, `docs/CLAUDE_IMPLEMENTATION_PROMPT.md` L5 and L9. At `ce7e320` the only agent UI is the access switch and the call log. The same documents say a different assistant may connect over MCP, which is how Dafin actually works.
2. **Stale reference.** `dafin-taste-engine-v0.9.2/skills/README.md` L1 says "v0.9.0" and L3 points to `docs/CURRENT_CLAUDE_INTEGRATION_v090.md`. That file is not in the package (only `…_v092.md`). Already acknowledged by `INTEGRATION_MAP.md`; the missing path is confirmed (RAN `ls`).
3. **Router bug (RAN, reproducible).** `python -m dafin_taste graph-plan "Fix the hum in the audio and add punchy captions with a cinematic title and sound design"` returns `task: audio_repair`, `active_skills: [sound-director]`, `deferred_skills: []`, `execution_stages: [[sound-director]]`, while `recognized_task_matches` lists `title_design` and `subtitles`. Cause: `dafin_taste/skill_graph.py` L309-327 adds secondary deliverables only `if primary in GOAL_TASKS`, and `GOAL_TASKS` (L26) is `{tutorial, explainer, challenge_story, documentary, film_scene, podcast}`. `audio_repair` is not in it, so the captions, the title and the sound-design request vanish. This contradicts `CURRENT_CLAUDE_INTEGRATION_v092.md` L19 ("do not silently truncate support"). The 552 unit tests do not catch it.
4. **Test-time claim.** `README_START_HERE.md` says 552 tests in about 16 s. I measured 552 OK in about 1.8 s on this machine. Not a defect; the count matches.
5. **Counts check out (RAN).** `supplemental/…/data/inventory.json` has 60 items, numbers 91-160 minus 143-152, 54 at 25 fps and 6 at 60 fps, 43 landscape and 17 portrait. All 276 rows of `MANIFEST_SHA256.csv` match their files. `validate-pack` → valid, 12 skills, 10 styles. `tests/validate_v6.py` → PASS.
6. **v6 `cards/core.json` has `"token_hint": "~110 tokens"`.** That is the author's estimate, not a measurement. Do not quote it as a saving.
7. **Pair evidence is thin.** v6 verifies only the first 12 seconds of audio for pairs 075/076, 077/078, 079/080, 081/082, 086/087, 088/089 (`evidence/AUDIO_RELATIONS_BATCH06.json`). 084/085 are alternate remedies, not a landscape/portrait pair (`docs/CLAUDE_INTEGRATION_HANDOFF_V6.md`, check-before-build item 3). Raw MP4s are not in any of the three packages, so none of this can be re-verified without the owner's files.
8. **Playlist claim.** `research/tutorial_channel_style_provisional.json` L5: `"playlist_inspection": "blocked_by_fetch_throttling"`. Keep treating the channel style as self-reported until the owner confirms.
9. **Redaction promise vs Dafin.** v6 asks to "redact Gmail/account/network/device identifiers from contact sheets and model requests". Dafin has no redaction step; `frames`/`capture` return the picture as rendered. Only the audit log exists (row 15).
10. **License conflict.** Public GPL-3.0 repo vs the Taste Engine's unselected license vs v6's private-only notice. See row 20.

---

## 4. Staged minimal patch plan

Only stages marked **BUILD** change native code. Model/effort is the suggestion for the chat that does that stage (per `CLAUDE.md` rule 6).

**Stage 0 — now, no code.** Owner chooses a license for the Taste Engine text. Push only this plan document (and one `DAFIN.md` row if wanted). *Sonnet, Low.*

**Stage B0 — deliver the knowledge, zero native code. PROPOSED.**
Build one small runtime skill for the Claude app (and any client that reads `SKILL.md`): root `SKILL.md`, `graph/skill_graph.json`, the 12 Skill `SKILL.md` files (about 33 KB in total; load one to three per task), the `direct-to-task-tutorials.md` reference, and for v6 only `SKILL_ROUTER_INDEX_V6.json` + `cards/core.json` + the route cards. **Exclude** `reference_sheets/` (9.9 MB of frames), `evidence/`, `supplemental/` images and the Python code. Add a table that maps each abstract `tool_groups_advisory` label to real ops, for example:

| Abstract label | Real Dafin ops (verify at run time with `search`) |
|---|---|
| `transcript.read` | `get_transcript` (`view:"phrases"`, source seconds) |
| `captions.edit` / `captions.inspect` | `generate_subtitles`, `set_subtitle_cues`; `inspect({clips:true,cues:true})` |
| `text.style` / `text.animate` / `text.inspect` | `set_text`, `apply_text_preset`, `apply_text_look`, `set_text_animation`; `list_text_presets`, `list_text_animations`, `list_fonts` |
| `timeline.inspect` | `inspect`, `list_history` |
| `preview.frames` | `frames`, `capture`, `activity` |
| `audio.inspect` / `audio.adjust` | `get_waveform`, `detect_silence`, `analyze_loudness`; `set_volume`, `add_audio_effect`, `duck_under` |
| `export.configure` / `export.inspect` | `list_export_presets`, `export_with_preset`; `export_status` |
| `analytics.read`, `story.annotate`, `assets.*` | No native analytics. Annotation → `add_bookmark`. Assets → `market` toolbox. Treat `analytics.read` as **external / defer** |

Gate: the routing matrix (T6) gives the same owner Skill and the same deferred work as the Python reference, and the router bug from section 3 item 3 is fixed in the skill's own task table. *Sonnet, Medium.*

**Stage B1 — optional, only if B0 misroutes or costs too much context. BUILD, small.**
One read-only op (for example `taste_plan`) in an existing toolbox that returns the deterministic plan from the bundled 13 KB graph, with real op names. No model, no new dependency, no write access. Do not port the 10 styles or the 12 Skills into C++. *Sonnet, High.*

**Stage C — safety gaps. BUILD, small, owner decides which.**
(a) A native Qt confirm (a real button handler, not an MCP argument) for ops that cannot be undone. Start from the list in `docs/MCP.md` "Undo" row; it names `set_beat_layers` as not undoable.
(b) Protected ranges: store a list in a new project field saved with the project (`set_metadata` only has title/author/description), and make `cut_words`, `keep_ranges` and `remove_silence` refuse a range that overlaps it (`protected_range` error), with a `tst_mcp` test each.
(c) Skip the native clarification widget; ask preference questions through the client. A preference answer is not an authorization, and every edit is undoable.
*Opus, High* for (a) and (b); security-relevant and touches shared dispatch code.

**Stage D — tutorial dry-run, no native code.** The assistant follows this fixed pipeline (a skill procedure, not C++):
1. `inspect` → record `revision` and the `list_history` hash.
2. `transcribe` (whole source, cached) → `get_transcript` phrases. Refuse frame-accurate trimming if words are `estimated` or `aligned:false`.
3. `frames({at})` at each step boundary; map every phrase to an action, a UI state and a result.
4. Write a plan (schema in `supplemental/…/references/edit-plan-schema.md`) with reason, source range, affected clips, confidence.
5. `cut_words`/`keep_ranges` with `dry_run:true`; show before/after frames on both sides of each cut.
6. After the user's OK: `apply` (one undo step) → `capture` the result → report `history hash` so it can be undone.
7. Portrait: duplicate project → canvas 9:16 → `auto_reframe` → own captions → check a phone-size `capture`; write the pair link into the new project field (row 14).
*Sonnet, High.*

**Stage E — measurement.** Section 6. *Sonnet, Medium.*

---

## 5. Test cases (use a small, source-backed set; the owner must supply the MP4s)

| ID | Case | Input | Pass condition |
|---|---|---|---|
| T1 | **True landscape/portrait pair** | 075 + 076 (candidates; first 12 s audio already matches) | Pairing is accepted only after source hash + source range match over the whole shared span, not the first 12 s. Portrait is a separate project with its own captions, undo and revision. A phone-size `capture` shows the tap target readable. |
| T2 | **False pair** | 084 + 085 (alternate remedies per v6 handoff) and one same-title, different-source pair | The system refuses to merge them, says why, and keeps both timelines independent. |
| T3 | **UI warning / data-loss** | 072 (shows a backup warning at the start) or any "clear data / reset" step | The warning and the backup step survive every trim. `data-loss-guard` and `warnings` cards are loaded. A cut that would remove them is blocked or needs explicit approval. |
| T4 | **Evidence-backed trim** | One tutorial with dead narration | Every removed range has: Whisper word times, frames before and after, a reason. `dry_run` diff shown. After approval, one undo step restores the previous history hash. |
| T5 | **Stale revision** | Plan created, then the user edits, then `apply` | The plan is rejected or re-planned because the history hash changed. A failing batch is recovered with `done`/`stopped` + `undo_to`. |
| T6 | **Ambiguous multi-Skill prompt** and negative controls | (a) "Fix the hum, add punchy captions with a cinematic title and sound design" (b) "Make my video better" (c) audio-only repair (d) export only (e) "no captions" | (a) primary + deferred work lists captions, title and sound; today the Python reference **fails** this (section 3 item 3). (b) at most one question. (c) and (d) trigger no creative style. (e) captions stay off. |
| T7 | **Alignment guard** | A language with no `align-model` installed | Words are `aligned:false`/`estimated`; the plan refuses frame-accurate cuts and tells the user which model is missing. No new Whisper is installed. |
| T8 | **Privacy** | Phone-screen tutorial with an account name visible | No network call to Dafin from the plan step; `AgentImageLog` shows every frame the assistant saw; the user is told that contact sheets go to the connected assistant's provider. |

**Honest limit:** passing these in Python or on synthetic clips would not prove native editing quality. Success means a rendered result inspected in Dafin and a frame-accurate check at each cut.

---

## 6. Context and usage structure, and telemetry (no token savings are claimed)

Measured in **bytes/characters, not tokens** (RAN):

| Piece | Size |
|---|---|
| `graph/skill_graph.json` | 13,207 B |
| Each Skill `SKILL.md` | 1,476 – 6,622 B (12 files) |
| `direct-to-task-tutorials.md` | 4,963 B |
| v6 `SKILL_ROUTER_INDEX_V6.json` + `cards/core.json` | 1,778 B + 393 B |
| v6 cards, all 20 | 6,671 B |
| v6 full profile (load only to justify a disputed edit) | 37,448 B |
| Python `graph-context` reply for four sample prompts | 1,139 – 1,906 B |
| Python `graph-plan` reply for the same prompts | 1,807 – 3,003 B |
| Dafin `catalog` | about 3k tokens per `docs/MCP.md` (their figure, not re-measured) |

What I will collect:
- **At the client (the only place tokens are visible):** provider-reported input, cached-read, cache-write and output tokens, model ID, number of model and tool calls, failed calls, wall time. Use the client's own usage report or the API `usage` object. Missing cost = unknown.
- **At Dafin:** per call op name, reply bytes, duration, history hash before and after. `AgentLogPanel` already shows duration; reply bytes and history hash are the small addition (Stage E). `scripts/mcp-probe.py` gives reply sizes for a fixed call list.
- **Design:** same project history hash, same prompt, same model, same permissions; with and without the skill; at least 3 runs each; cold and warm cache; failed runs included. Quality is judged blind on rendered outputs, separately from usage.

---

## 7. Open questions for the owner

1. Which license for the Taste Engine text (the public repo is GPL-3.0)? Until chosen, do not commit the packages.
2. Are the Whisper model and the alignment model for your tutorial language installed? (`ai_capabilities` / `list_addons`).
3. Can you provide the source MP4s for 072, 075, 076, 084, 085 plus one dead-narration tutorial? None of the three packages contains raw video.
4. Which client do you use to drive Dafin (Claude app, Claude Code, Cursor)? It decides how Stage B0 is delivered.
5. Do you want Stage C (native confirm for non-undoable ops, protected ranges) now, or only after Stage B0 works?
