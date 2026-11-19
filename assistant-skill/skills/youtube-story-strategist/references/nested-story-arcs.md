# Nested story arcs and meaningful rewards (v0.6.1)

**Status: original interpretive guidance. Do not describe as a validated retention formula or neurological finding.**

## What to look for
- Macro story: one truthful core promise or question that the video eventually answers.
- Micro stories: existing smaller questions, attempts, setbacks, discoveries, comic payoffs, demonstrations or human reactions that develop that main story.
- A meaningful *reward* is new understanding or felt resolution, not a cut, animation preset, whoosh, zoom, flashing caption or artificially introduced obstacle.
- Favor contrast. Let a true moment breathe; a quiet explanation, suspenseful delay, or sincere reaction can do more work than constant novelty.
- Never impose an interval, count, dopamine score, or guaranteed retention improvement.

## Decision process (annotations → hypotheses → evidence → authorized edit)
1. Ask what the original footage shows; mark missing information `unverified` rather than filling it in.
2. Identify main promise, then annotate real beats using `dafin_audit_narrative_rewards(data={...})`. Each beat has time bounds, `arc_id`, editorial role and caller-provided source status.
3. Audit subordinate arcs for setup, development, an actual result, and a connection to the main story. `arc_id: main` denotes the main story, others denote smaller threads.
4. Treat tool `findings` and `candidate_story_treatments` as suggestions from untrusted annotations. No preview, footage, caption, or recording has been inspected by the Python engine.
5. If native Dafin can verify real footage, try a *local* alternative: reorganize the existing setup and earned payoff while preserving truth and performance. Use native undo and user authorization.
6. Use optional `story_role: story_arc` in Style Audition to compare an unchanged original with a more clearly told scene. A 3–15 s excerpt often **cannot demonstrate the success of an entire micro-story**: provide wider context or skip the comparison when needed.
7. Before applying a change, test whether it really advances the main story or merely adds stimulus.

## Critical exceptions
- Cinema, documentary, interviews, educational demonstrations and reflective videos are not required to deliver a payoff every few minutes. Silence or an unresolved question may be the right ending.
- A cold-open spoiler or flash-forward is sometimes intentional. Mark `intentional_teaser` **on the actual early payoff beat**, not on an unrelated event.
- A creator can invent an authentic challenge **before filming**, but an AI editor must not fabricate footage, reactions, evidence, chronology, stakes or causal connections after the fact.
- If the footage does not contain several satisfying smaller arcs, reduce the video length or recommend additional truthful filming; do not manufacture rewards.

## Sources and evidentiary limits
- MrBeast discussed the difficulty of sustaining a double-digit-minute video with one story, and using subordinate stories for re-engagement. Public interview transcript: https://singjupost.com/colin-and-samir-show-with-mrbeast-transcript/ . Verify against the original interview before attributing verbatim phrasing.
- MrBeast also publicly criticized overly fast, overstimulating editing in March 2024: https://www.tubefilter.com/2024/03/04/mrbeast-editing-style-number-of-cuts-per-video/ . Both positions are compatible: stronger narrative developments, **not** more obligatory visual effects.
- Mrwhosetheboss discussed experimentation with retention graphs and adjusting editing style in an interview with Ali Abdaal: https://www.listennotes.com/podcasts/deep-dive-with-ali/how-to-build-a-10-million-6iM9TvxzPW3/ . This does **not** independently establish a 'dopamine spike' theory or the exact 'compilation of Shorts' phrase.
- YouTube warns that retention spikes may reflect interest **or confusion**, and dips can have multiple explanations: https://support.google.com/youtube/answer/9314415 . This module never treats a hypothetical beat as audience evidence.

Do not download/repackage these source videos without rights. Source links are for editorial context, not validated A/B examples.
