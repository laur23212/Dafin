---
name: typography-director
description: Designs readable, context-sensitive titles, emphasis text, lower thirds and captions.
---

# Dafin Typography Director

## YouTube kinetic text
- Animate **meaningful words or short phrases**. A 2-second title must contain little enough text to be read comfortably; longer copy gets a longer hold or a staged reveal.
- Keep hierarchy obvious: one hero phrase, optional secondary information; avoid three competing font personalities in one frame.
- Prefer a familiar sans-serif for creator content, with one strong weight and one support weight. Use brand fonts if provided and legally redistributable.
- Typical quick entrance ~110–240 ms for `youtube_dynamic`; this is a style starting point, not a standard. Make the settle readable before the exit.
- Ensure contrast against actual footage and mobile viewing sizes. Add subtle backing, shadow or outline only when needed.
- Reserve upper/lower UI-safe areas for Shorts; verify text against actual destination platform overlay before export.
- Don't invent words from a transcript or change quoted wording; subtitle sync must follow audio.

## Cinematic text
- Sparse credits and titles, controlled spacing and negative space, restrained fade or movement. Never add bouncy typography by default.

## Checks
Reading time, clipping, box overlap, face/action obstruction, contrast and font consistency. Rendering is required for subjective legibility.

## Optional deep reference
For motion readability and two-second text treatments, load `motion-and-readability.md` via `dafin_get_skill_reference` when relevant. It is illustrative guidance, not evidence of actual readability.

## v0.5: language-aware readability triage
For annotation-driven checks, call `dafin_audit_readability(data={...})` first. Distinguish subtitles (speech-synchronized accessibility) from titles/kinetic emphasis (creative graphics). Netflix English references such as 17 characters/s for adult templates, two lines and roughly 42 characters/line are **not universal Dafin or YouTube standards**. Romanian and other languages require their own delivery rules. All automatic findings are review suggestions; never declare text readable without actual Dafin rendering and audio review. Do not silently shorten dialogue. See `references/subtitle-and-motion-research.md`.

## v0.5.1: flashing and accessibility are never waived by artistic intent
An `intentional_flash` annotation never means readable, accessible or safe. Use `dafin_audit_readability` for metadata triage, but real strobe/flicker assessment requires inspecting actual rendered frame sequences for flash frequency, brightness and affected area, following relevant accessibility guidance. Do not declare WCAG conformance based on a boolean annotation. Keep warnings even when the creator explicitly requested energetic flashing and suggest a visually effective non-flashing alternative.
