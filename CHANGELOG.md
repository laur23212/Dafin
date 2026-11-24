# Unreleased changes

Tracks work done on `main` **since the last public release**. Use this to see what is already fixed or added before filing an issue. Cleared when a new release ships.

**Last released version:** `0.9.1`

---

## ✅ Fixed

## ✨ Added

- AI Fill: a ✦ button on the timeline toolbar. Drag across the timeline to mark a space, describe what goes there (a title, lower third, arrow, counter…), and AI Assist puts an animation exactly there. The space can be resized or redrawn before generating, Change it / Try again add versions instead of replacing, ◀ ▶ switches between them (undoable, saved with the project), and the request can reach beyond the space ("…and arrows on the next clip"): the AI has every tool the chat has.
- Agent access: Copy for Codex, Connect to ChatGPT (web access through a tunnel, secret address, off by default), a Usage tab in the Agent log (calls, failures, repeats, time and reply sizes as Dafin measures them; the assistant's own tokens and price are not shown because they are not sent), and a model picker feeding copied Claude/Codex launch commands.
- Tutorial: a Tutorial button left of the Marketplace button opens a scrollable feature list; each feature has a summary and a guided tour that dims the screen, outlines the real button or panel and explains it step by step. Small ? buttons on the Assets, Properties, Preview and Timeline panels open their page. A welcome offers it once on first start.
- Text animations: **String Blur** and **String Blur (whole block)**, each with In and Out. The text starts blurred and transparent and sharpens into place together (Out reverses it); no letter-by-letter delay unless you raise Stagger.

## 🎨 Improved

- Timeline: while you hold a clip with the mouse near the left or right edge of the timeline, or outside it, the timeline scrolls that way by itself (slowly at the edge, faster the further out you go) and can run past the end of the project, so a clip can be moved far without zooming out or dragging the scrollbar. It stops as soon as you let go.
- Text animation timing: Stagger, Duration/Period and "Readable within" can now be typed in milliseconds or seconds. A small ms / s button beside each field switches the unit (seconds show three decimals, so 1 ms precision either way).
