---
name: youtube-story-strategist
description: Audience-first YouTube storytelling based on researched MrBeast, Hayden Hillier-Smith and Paddy Galloway principles; honest promise, narrative contrast and payoff.
---

# Dafin YouTube Story Strategist (v0.3)

## What this Skill is for
Use for creator videos, YouTube documentaries, challenges, explainers or a request to improve opening, viewer satisfaction or storytelling. It does **not** imitate any individual creator's exact voice, assets, thumbnails or proprietary production methods.

## Order of decisions (before effects)
1. Identify the actual target viewer, the *truthful* title/thumbnail promise, and what the viewer expects to see. If unknown, mark unknown; don't invent it.
2. Determine the central dramatic question and the strongest real proof available in footage.
3. Shape a clear opening; test fast cold-open and more measured versions against audience and intent. A generic logo sequence is seldom an effective YouTube hook, but do not force an artificial high-stakes intro.
4. Map setup → attempt/obstacle → progress → earned reveal/payoff → meaningful response. These are optional tools, not a required plot for every format.
5. Edit for **emotion + story + rhythm**. Protect character reactions, jokes, silence and proof when they need time. Let an earned long shot breathe.
6. Use cuts, animation, SFX and callouts only for evidence, orientation, contrast, comedy or emotional effect. Avoid a timer-driven cut grid and pointless zooms.
7. Check that every teased thread resolves or is clearly and intentionally left open; don't sacrifice viewer satisfaction for inflated watch time.
8. Distinguish measured retention dips from hypotheses. A spike can mean confusion or replay; neither proves what caused it.

## Format adaptations
- **YouTube Dynamic:** Faster semantic changes when energy is high; dialogue, readable text and reaction can still hold.
- **YouTube Storytelling:** Character goal, obstacle, contrast, human response, and earned payoff take precedence over kinetic graphics.
- **YouTube Explainer / experiment:** Show what, why and how; compress only nonessential process and keep critical evidence fully visible.
- **Cinematic / documentary:** Performance, authenticity and continuity override platform-inspired retention tricks. Never manufacture causality.
- **Shorts:** Concise opening and framing, but no forced unreadable captions or unearned cliffhangers.

## Helpful on-demand tools
- `dafin_get_story_rules(focus='opening' | 'story' | 'editing' | 'sound' | 'packaging' | 'analytics')`: retrieve a small source-linked rule set.
- `dafin_story_audit(story={...})`: review **provided editorial annotations**, not the footage. Requires explicit `promise_status` for any alignment assertion.
- For supplied retention numbers, delegate to `youtube-retention` and its `dafin_retention_audit(data={...})`; do not infer causes from the graph.
- `dafin_plan_attention`: choose meaningful treatment for semantic beats, still subject to source availability.

## Check the actual result
Preview the opening, each payoff, transition in tone, reaction and any suggested problem spots. Use actual rendered frames and sound only through Dafin's existing authorized tools. If these are unavailable, report 'unverified' instead of claiming a visual review.

## Further reading (optional)
`dafin_get_skill_reference('youtube-story-strategist','creator-case-studies.md')` contains original, context-aware analyses and sources. Rulecards are independently retrievable; do not load every reference on every edit.


## New 2026 supplemental creator synthesis
- From **Logan Smith**: after a Shorts hook, demand an actual development and earned payoff; do not end with misleading suspense.
- From **Daniel Bitton**: re-hooks may emphasize a true change in subject or stakes, not an arbitrary timer; keep visuals and speech understandable.
- From **@isaacverse**: identify the audience's underlying goal and value delivered rather than imitating a generic "viral script".
- From **Kallaway**: test a visual-first opening when real footage supports it; the image, spoken line and title must tell one story.
- Learn By Leo's continuity and four-channel alignment are owned by `viewer-experience-director`, NOT duplicated here.
- All supplementary advice defers to MrBeast's story-and-personality guidance, Hillier-Smith's emotion/story/rhythm, and Galloway's honest packaging/audience satisfaction, as well as user intent. See `docs/SECONDARY_CREATOR_RESEARCH_2026.md` for sources, uncertainty and rejected tactics.

## v0.6.1: Macro/micro narrative rewards (no dopamine metric)
For sustained YouTube long-form, distinguish the overarching viewer promise from smaller **authentic** sub-stories. A payoff must come from real source material; changing the camera angle or using a whoosh is not a new narrative reward. Use `dafin_audit_narrative_rewards(data={...})` selectively when a **supplied, caller-annotated** beat map exists. Do not claim to have inspected a video or optimized retention. The tool proposes only review hypotheses. No mandatory number or interval of mini-arcs; cinematic and documentary pacing can stay quiet. When making an optional comparison, `dafin_plan_style_audition(request={..., story_role:"story_arc"})` plans only 3–15 second treatments; it cannot validate a whole multi-minute arc. Read `nested-story-arcs.md` using `dafin_get_skill_reference` for schema assumptions, exceptions, sourcing and native handoff. This is part of the Story Strategist—**do not create a separate conflicting Narrative Rewards Skill**.

## v0.6.2: Contextual review before narrative auditions
For a proposed change to the meaning, pacing or reveal of a **multi-shot story arc**, retrieve the annotated arc and call `dafin_plan_story_context_review(request={...})` instead of judging solely from `dafin_plan_style_audition`'s 3–15 second highlight. The full arc's **annotated time envelope** must be seen/heard via authorized native Dafin playback before claiming that a revision improves its story. Callers' annotation flags, `native_claimed`, and revision strings do not authenticate footage. The new planner is always read-only, includes a finite review/render-seconds budget, and **cannot authorize** any render or edit. A comparison is optional; it requires an actual user request and permits baseline, neither, or skip. An incomplete or missing setup/payoff may indicate incomplete annotation, not a weak video—never invent events to fill it. Do not turn a preference on a short highlight into a whole-project taste profile.
