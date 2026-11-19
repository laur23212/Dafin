---
name: dafin-baseline-editing-skill
description: Dafin's built-in editing knowledge (Taste Engine v0.9.2 + optional Creator Style v6). Read first for any editing request.
---

# Dafin baseline editing skill

Built into Dafin. Dafin's tools are the authority for what can be done; this text only guides *which* creative work is good. Where a tool result disagrees with this text, the tool result wins. Nothing here is permission to change the project: the user's request, protected ranges, the irreversible-action dialog and undo always apply.

## How to use it (keep it cheap)

1. Work out the actual task from the request. Do not load every file. Use `read_skill_file({path})` for ONE to THREE files below, only when the task needs them.
2. Task decides the owner Skill, not the video genre. A one-step fix (hum, silence, export) needs only its own Skill and no creative style.
3. If the request has several deliverables (for example "fix the hum, add punchy captions, a cinematic title and sound design"), plan ALL of them. Name the owner Skill for each and do them in order. Never drop captions, titles or sound because the first task was audio repair.
4. "Make it better" with no direction: ask at most one `ask_user` question, then proceed.
5. Respect what the user said ("no captions" means none). Do not invent drama, hooks or rewatch tricks for tutorials.

## Task to owner Skill (read `skills/<name>/SKILL.md`)

| Task | Owner | Read also if needed |
|---|---|---|
| Not sure / mixed request | creative-router | skills/creative-router/SKILL.md first |
| Cuts, pacing, structure, tutorial trimming | editorial-director | its references/direct-to-task-tutorials.md for tutorials |
| Story arc, hook, YouTube storytelling | youtube-story-strategist, story-structure | |
| Retention / viewer experience | youtube-retention, viewer-experience-director | |
| Captions, titles, text motion | typography-director | motion-director |
| Hum, silence, music, sound design | sound-director | |
| Look, thumbnails, optional AI assets | art-director | |
| Review a finished edit | taste-critic | its references/evidence-protocol.md |
| Reference lookup | reference-director | |

Styles (only when the user names one or the task clearly fits): `styles/tutorial.json`, `shorts.json`, `podcast.json`, `documentary.json`, `cinematic.json`, `commercial.json`, `gaming.json`, `youtube_dynamic.json`, `youtube_explainer.json`, `youtube_storytelling.json`. The routing graph is `graph/skill_graph.json` (large; read only if the table above is not enough).

## Abstract labels in the Skill files map to real Dafin ops

The Skill files use abstract names. Verify real ops with `search` before calling.

| Label | Real ops |
|---|---|
| transcript.read | get_transcript, transcribe (Dafin's own Whisper; never ask for another model) |
| cuts | cut_words, keep_ranges, remove_silence (use dry_run first) |
| captions.* | generate_subtitles, set_subtitle_cues |
| text.* | set_text, apply_text_preset, apply_text_look, set_text_animation, list_text_presets, list_text_animations, list_fonts |
| timeline.inspect | inspect, list_history, list_protected_ranges |
| preview.frames | frames, capture, activity (only if frames are allowed) |
| audio.* | get_waveform, detect_silence, analyze_loudness, set_volume, add_audio_effect, duck_under |
| export.* | list_export_presets, export_with_preset, export_status |
| analytics.read | not available in Dafin; say so and ask the user for the numbers |

## Always

- Read before editing. Prefer `dry_run`. Use `apply` for several edits as one undo step. End by saying what changed and how to undo it.
- Cuts and narration trims need real transcript timestamps from Dafin; if words are estimated or not aligned, do not claim frame-accurate cuts.
- Never remove a correct step, a warning, a backup reminder or a data-loss notice from a tutorial.
- Do not publish, upload or share footage or frames; frames go only to the AI service the user agreed to.

## Creator tutorial style (private, opt-in only)

Use only when the user explicitly asks for their creator tutorial style, or says it was already chosen for this project. It adds read-only guidance to the selected Skill; it does not replace the Skills or styles above.

1. `read_skill_file({path:"creator-style/SKILL_ROUTER_INDEX_V6.json"})`, then `creator-style/cards/core.json`.
2. Load only the cards the router lists for the task (for example `creator-style/cards/warnings.json`) plus any mandatory safety overlay it lists.
3. A landscape/portrait pair is a pair only when source media and source ranges match over the whole shared span, never because titles or durations look alike. A portrait version is its own project with its own captions and a phone-size legibility check.
