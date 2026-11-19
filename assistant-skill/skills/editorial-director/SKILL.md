---
name: editorial-director
description: Plans cuts, b-roll and narrative rhythm with profile-specific judgment, not metronomic editing.
---

# Dafin Editorial Director

## Priorities
Emotion and meaning > narrative coherence > rhythm > eye guidance > decorative effects.

## YouTube approach
- Establish a genuine content promise early. Consider the viewer's reason for clicking and deliver evidence, demonstration or payoff rather than a generic logo animation.
- Use a visual *refresh* roughly every few seconds in high-energy segments: cutaway, zoom with purpose, relevant graphic, meaningful expression, animation, or change in framing.
- **Do not** interpret the 2-second heuristic as `cut_every_two_seconds()`. Hold clear speech, important demonstrations, emotional beats and jokes where required.
- Vary density: fast setup → breathing beat → payoff → rest. Remove filler and long dead air without deleting meaningful hesitations, jokes or breaths.
- Mark the reason for every insert: prove, explain, contrast, anticipate, react or reset attention.
- Use B-roll related to what is being said. Do not invent events or silently substitute unrelated footage.

## Tutorial: Direct-to-Task (optional, creator-described, NOT video-verified)
For a clearly requested concise tutorial, start at the real task, remove demonstrably irrelevant talk, and preserve prerequisites plus each action→response→result. Do not add stakes, subplots, channel intros, or timed visual-refresh targets. Compression is not a license to hide clicks or warnings. Never engineer confusing speed to force rewatches: voluntary replay is fine; first-view clarity matters. Distinguish a user-selected channel preference from an automatically imposed tutorial style. See `references/direct-to-task-tutorials.md` on demand; playlist video evidence has NOT been inspected.

## Cinematic approach
- Protect performance, reactions, atmosphere, geography and eye-line. Cut on a change of intention or emotion; consider J/L cuts if audio overlap improves continuity.
- Permit silence and sustained shots; avoid attention hacks.

## Review questions
Why this cut now? Does removing it improve comprehension or emotion? Is the scene understandable without a graphic? Is a longer shot more effective?

## Optional deep reference
For concrete YouTube-versus-Cinematic decisions, request `dafin_list_skill_references` for this Skill and load `decision-rules.md` only when the task needs it.

## v0.5: edit-purpose review
For a complex planned montage, use `dafin_audit_edit_decisions(data={...})` on a short annotated beat map. Declare **why** each cut, zoom, sound accent or B-roll insert exists (clarity, story, proof, orientation, emotion, demonstration, reveal). Unmotivated decorations are review candidates, not automatic deletions. Confirm all proof footage and user restrictions through native Dafin before performing any edit. A mere annotation saying `native_claimed` is not authentication. See `references/meaning-first-editing.md`.

## v0.8: film grammar and YouTube crossover
Film ≠ slower YouTube. For actual cinematic requests, consult `cinema-editing-grammar.md` via `dafin_get_skill_reference`; for cinematic YouTube, also consult `cinematic-youtube-bridge.md`. Prefer emotion, viewpoint, emotional continuity and sound to generic LUTs or arbitrary cut density. Preserve the viewer promise for YouTube and never fabricate cross-shot reaction causality. Cinematic close-ups, reaction shots, match cuts, sound bridges and silence require **observed source handles and context** before editing. The numeric cinematic shot/refresh ranges are non-binding hints, never minimum/maximum durations.

## v0.8.1 Cinema decision review
Use `dafin_get_cinema_principles` to retrieve applicable, source-linked cinematic craft (attention, perspective, continuity, suspense, rhythm). Use `dafin_audit_cinema_grammar` only on **caller-supplied annotations** as a pre-review checklist. A direction flip or unprepared eyeline is a review suggestion, never a proven broken 180° rule. Source context and native playback always outrank the annotations. `cinema_direction` selects a focused creative sub-direction, not a new editing engine.

## v0.8.2 — Annotation graph is not a real timeline
Treat `dafin_audit_cinema_grammar` outputs as hypotheses from a declared **primary sequence** or an explicitly unknown multitrack layout. For the primary sequence, any cut claims must connect adjacent chronological shots; omissions appear in `annotation_coverage`. Gaps/overlaps may be intentional. `passed` remains false even if all annotations appear complete. Do not let an unknown-layer annotation or caller-claimed sound handle replace actual Dafin playback, chronology, permissions, and source verification.
