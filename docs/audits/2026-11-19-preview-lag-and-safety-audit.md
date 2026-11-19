# Audit: preview/scrub lag, autosave and recovery, relink, export default

Date: 2026-11-19. Base: `dafin-edition` at f4ab88d. Static reading of the code only: nothing was built or profiled, so the lag ranking is a hypothesis until measured (see "Measure first"). No code was changed.

## Measure first (10 minutes, on the weak PC)

Start Dafin with `DRIFT_PERF_LOG=1`. Every second the log prints `DriftPerf <name> n= p50= p95= max=` for `seek.gui`, `seek.engine`, `seek.emit`, `composite.work`, `composite.work.approx`, `decode.wait`, `frame.latency.seek`. Scrub a 4K/HEVC clip, then a 1080p H.264 clip. If `decode.wait` dominates, causes 1 and 2 matter most. If `composite.work` dominates, cause 3. If `seek.gui` is high, cause 4.

## Part A: preview/scrub lag, ranked

Already fine (no action): the compositor runs on its own thread and keeps only the newest pending frame (`CompositorService`, `maxInFlight() == 1`); a scrub uses an approximate-decode ladder (`ClipReader::decodePreviewVideoFrameApprox`) and settles on the exact frame after 180 ms; timeline thumbnails use a low-priority pool (`ClipThumbnailStore`, max 3 threads) and the filmstrip decoder is its own low-priority thread; the timeline is a scene-graph item; inspector rows are throttled to 100 ms while scrubbing; the autosave snapshot is built on a worker.

| # | Cause | Where | Impact | Fix | Effort |
|---|---|---|---|---|---|
| 1 | Scrubbing reads the full-size original, never the proxy. `allowProxies = m_playing \|\| !m_pausedUsesOriginal`, and "Use original when paused" defaults to on, so a scrub counts as paused. Proxies (short GOP, made for scrubbing) are only used while playing. | `PlaybackEngine.cpp:1124`, default at `:222` | High on 4K, HEVC, 10-bit, high-bitrate clips (exactly the clips the proxy dialog targets). None if no proxy exists. | `allowProxies = m_playing \|\| m_scrubbing \|\| !m_pausedUsesOriginal`; the exact frame from the original still lands after release. Add a case to `tst_playback`. | XS, about 0.5 h |
| 2 | Filmstrip and waveform decoding keep running while scrubbing. They are suspended only while playing, so they compete with the preview decoder for CPU, disk and the hardware decoder. | `AppController.cpp:561` (`updateFrameCacheSuspension`) | Medium on weak CPUs, low on good ones | Include `m_scrubbing` in `suspended` and call it from `scrubbingChanged`. Tiles resume on release (queue is newest-first). | XS, about 0.5 h |
| 3 | No lower preview scale while scrubbing. Auto quality only reacts to late frames during playback (`CompositorService::onWorkerFrameReady`, `m_playbackActive`), so scrub steps composite at the chosen scale on integrated GPUs. | `CompositorService.cpp`, `PlaybackEngine::playbackRenderOptions` | Medium on weak GPUs, low elsewhere | While scrubbing (from the 2nd step), cap `previewScale` at 0.5; exact scale on settle. Risk: the code comments that scale changes rebuild the present ring and caused black frames, so test for flicker. | M, about 1 day |
| 3b | Long-GOP 1080p H.264 (phone, screen recordings, downloads) is not "heavy", so no proxy is offered, and every settled frame decodes from the keyframe. | `DafinProxyPolicy::heavyReasons` | Medium for that footage | Add "long keyframe interval" to the heavy reasons. Unverified: whether `MediaProbe` exposes GOP length (the reader has a keyframe index). | S to M, 3 to 6 h |
| 4 | Bindings that re-run on every scrub step, outside the 100 ms throttle: `keyframeAtPlayhead` (a C++ call each step) and `freezeFrameAvailable` in `TimelineToolbar.qml:125-134`, `playheadRel` per keyframed clip in `TimelineKeyframeMarkers.qml:26`, four in `SpeedFadeInspector.qml:470-599`. Also `selectClipUnderPlayhead()` on every step when "auto-select under playhead" is on (off by default). | files listed | Low to medium, only with a selected clip or open inspector | Bind to `inspectorPlayheadSeconds`; skip auto-select while `m_scrubbing`. | S, 1 to 2 h |
| 5 | Scopes: with a scopes panel open, every paused composite (scrubs included) does a GPU readback; the 100 ms throttle only applies while playing. | `PlaybackEngine.cpp:1129` | Low, only with scopes open | Apply the throttle when `m_scrubbing` too. | XS |
| 6 | Fullscreen preview slider never calls `beginScrub/endScrub`, so every move is an exact seek. | `PreviewPanel.qml:256` | Low (fullscreen only) | Call them on `pressed` change. | XS |
| 7 | Autosave takes its GUI-thread snapshot (session JSON, undo history) while scrubbing; skipped only when playing or exporting. | `ProjectFileController.cpp:105-113` | Low, one hitch a minute | Also skip while `m_scrubbing`. | XS |

Quick wins 1, 2, 5, 6, 7 together: about half a day, all one-line gates.

## Part B: features, checked against the code

### 1. Autosave and crash recovery: EXISTS, with gaps
- EXISTS autosave: every 60 s by default (off, 15 s, 30 s, 1 min, 5 min in Settings), only when dirty, not while playing or exporting. Zstd snapshot on a worker to `<AppData>/recovery/autosave.json`, atomic write then rename (`ProjectFileController::writeRecoveryFile`). Quit with unsaved work writes it synchronously.
- EXISTS crash recovery: on launch a modal "Unsaved work from last session" (Restore / New session) appears when the snapshot is newer than the project file (`detectRecoveryFile`, `recoveryIsNewer`). Restore marks the project unsaved and keeps the snapshot until the next save. Every manual save also keeps the replaced file in a `backups` folder (newest five), and a project that will not open offers the newest good backup.
- PARTIAL saved/unsaved state: a dot in the header pill (`EditorHeaderProjectsButton.qml`) plus tooltip "Unsaved changes". Missing: not colour-only, no "Saving...", no "Autosaved 14:32". I found no dirty mark in the window title (not verified).
- PARTIAL "never overwrite newer work silently":
  - A. `writeRecoveryFile` has no guard for a pending, unanswered snapshot (`m_recoveryAvailable`). If the dialog does not show (the retry timer in `Main.qml` gives up after 20 tries of 150 ms, e.g. behind the language prompt) and the user edits, the next autosave overwrites it. The Android code already warns about this. Fix: `if (m_recoveryAvailable) return;` in the timer and in the writer. XS.
  - B. "New session" deletes the snapshot at once, no copy, no confirmation (`discardAutosave` -> `newProject` -> `deleteRecoveryFile`). Fix: move it to `recovery/discarded/autosave-<time>.json` and keep the last three. S, 1 to 2 h.
  - C. An older snapshot is deleted silently at startup, decided by file modification time only. A restored or synced file or a clock change can hide a newer snapshot. Same fix as B: move, do not delete. Included in B.
  - Not a problem: Restore then Save cannot lose the previous file, because the save backs it up first.

### 2. Relink on open: PARTIAL
- EXISTS: missing media opens anyway with an offline badge on clips and cards; a toast "Project loaded, but N media file(s) could not be found" (`ProjectFileController.cpp:1763`); right-click Locate file... (replace flow); after one file is located it offers the others from the same old folder with the same name and size, as one undo step (`MediaBinController::findSiblingRelinks`, `relinkSiblings`).
- MISSING: a relink dialog at open, a list of all missing files, "choose a folder and search it (with subfolders)". Plain Save references local media by path (`captureSave(false)`, `:701`), so this really happens.
- Plan (no parallel system): `RelinkDialog.qml` opened from `finishLoad` when `missingCount() > 0`: list of missing files, Locate folder... (new `MediaBinController::findRelinksInFolder`, recursive, same name+size matching as `findSiblingRelinks`), Relink N, per-row Locate, Skip. Apply through the existing `remapProjectPaths` + `pushProjectEdit("Relink media")` path. M, 6 to 8 h.

### 3. Export default: mostly EXISTS
- EXISTS: default is H.264 (software unless "Prefer hardware encoder"), AAC 192 kbps, CRF 18, preset medium, project height (`targetHeight 0`), project frame rate (`fpsNum 0`) (`Exporter::defaultSettings`, `:2119`). Advanced is collapsed and reset to collapsed on every open (`ExportDialog.qml:303`). Platform chips, Downscale chips, Video/Audio/GIF mode bar.
- PARTIAL: after the first export the dialog reopens with the last used settings, so the simple default is gone once someone tries another codec. There is no one-line summary of what will be written and no Reset to default.
- Plan: add a summary line ("MP4 · H.264 · 1920x1080 · 30 fps") and a Reset to default link that reuses `EditorState.exportDefaultSettings()`. Defaults stay as they are. S, 2 to 3 h.

## Suggested order
1. One batch of one-line gates: Part A 1, 2, 5, 6, 7 and Part B 1A. About 1 day with tests.
2. Recovery safety B and C (move, do not delete) and the saved-state label.
3. Relink dialog.
4. Export summary + Reset.
5. After measuring: A3 / A3b.

Each worker prompt should start with a check-first step (search the code, report EXISTS / PARTIAL / MISSING, do not rebuild what exists).
