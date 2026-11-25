# Filmora `.wfp`: robust import (C20)

Plan: Stage 21 Pass 5 `02_CLAUDE_MASTER_IMPLEMENTATION_HANDOFF.md` PR-03 / PR-05 and
`data/CLAUDE_IMPLEMENTATION_WORK_PACKAGES.csv` D-003, D-005, D-006, D-013, D-014.
**The Filmora pack was not attached to this session**, so those files were not read. Everything
below was built from the text of the task prompt alone. Check it against the pack before relying on it.

Status words: `NATIVE_TESTED` = ran in the scratch build (Qt 6.4, core + the files below, with
ASan and UBSan); `IMPLEMENTED_UNTESTED` = written, never compiled or run. Nothing is approved; that
needs a real `.wfp` from Filmora and a native export the user has seen.

## Step 0 — what existed (at `6556131`, before this change)

| Item | State | Where |
|---|---|---|
| Strict `.wfp` reader | EXISTS | `src/core/interchange/filmora/WfpArchive.cpp:291` (`open`), `:509` (`inflateMember`) |
| Loss report | EXISTS | `CompatibilityReport.h:23` (`CompatStatus`), `:98` (`hasLoss`); filled in `FilmoraProjectParser.cpp` |
| WFP read off the GUI thread | MISSING | `ProjectFileController.cpp:1060` `loadOtherEditorProject` parsed (`:1073`), built (`:1088`) and probed (`:1089`) on the GUI thread. The start screen's `ProjectScanner::inspect` (`ProjectScanner.cpp:206`, parse at `:237`) is a separate path; not changed |
| Cancel | MISSING | no token anywhere in the reader, parser or builder |
| One apply at the end | PARTIAL | the project was built whole, then applied, but on the GUI thread, and the build probed media while it ran |
| Moved media | PARTIAL | `ImportedTimelineProject.cpp:43` `MediaFinder`: stored path, then relative path, then **first file with the same name** (lower-cased index). No identity check, no ranking, no question |
| Texture paths in effects | MISSING | `GpuPackageParse.cpp:499` `QDir(packageDir).filePath(tex.file)`, so `../..` and absolute paths left the package folder; `resolvePackageAsset` (`:631`, used at `:307`, `:320`) had the same hole |
| fps | PARTIAL | `Project::fps` is an `int` (`Project.h:184`); `FilmoraProjectParser.cpp:316-336` reported `30000/1001` as Approximated and rounded it. Export already took a rational `fpsNum/fpsDen` |
| Speed curves | PARTIAL | `constantSpeedOf` (`FilmoraProjectParser.cpp:71`): any ramp became a constant speed and was reported Approximated (`:459`) |

## What changed

**D-003 background, cancellable, single apply** (`NATIVE_TESTED` for the pipeline; the controller
is `IMPLEMENTED_UNTESTED`).
`interchange::runFilmoraImport` (`ImportPipeline.*`) reads, builds and fits layouts with no access
to the open project, and returns an `ImportOutcome` (Failed / Cancelled / NeedsChoice / Ok).
A `CancelToken` is polled by the archive's inflate loops, per clip in the parser, in the media
search and in the builder. The controller runs it with `QtConcurrent`, and only the GUI thread
calls `applyProjectJson`, once, on Ok. Failure or cancel leaves the open project untouched.
The load dialog has a Cancel button for this load.

**D-005 relink by verified identity** (`NATIVE_TESTED`). `MediaRelink.*`. Order: stored path,
project-relative path, then same name. Same-name candidates are rejected when their size or
partial hash disagrees with what the clip expects, or when a known duration is shorter than the
clip needs (200 ms + 0.5 % tolerance). One survivor is taken and reported as *verified* (by what)
or *unverified* (name only, Approximated). Several byte-identical survivors (size + SHA-256 of the
size and first/last 64 KiB) are one file, ranked by facts verified, then shared trailing folders,
then path. Several different survivors are never resolved by taking the first: the import returns
`NeedsChoice`, `ImportRelinkDialog.qml` asks, and the answers re-run the import. Declining leaves
the clip missing and reported. Symlinks are not search candidates; the search is capped at 20 000
entries and says so if it stopped early.

**D-006 effect texture paths** (`NATIVE_TESTED`). `core/base/ConfinedPath.*`:
`resolveConfined(root, relative)` refuses absolute, drive, UNC and `\` paths and `..` that leave
the root, then checks the *canonical* target (symlinks resolved) is a file under the *canonical*
root (case-insensitive on Windows). Used for pipeline textures and for `resolvePackageAsset`
(thumbnails, FilePath/Image defaults). An empty package folder now fails closed.

**D-013 rational fps** (`NATIVE_TESTED` except the Exporter line). A rate within 1 % of a whole
fps and not a whole number (30000/1001, 24000/1001, 48000/1001, 60000/1001) is kept as an exact
ratio next to the integer editing fps (`Project::setExactFrameRate`, saved as `fpsNum/fpsDen`
only when set, dropped if `setFps` changes the grid). "Same as project" export uses it
(`Exporter.cpp`, `exportFrameRate`). Anything else (25/2, 30/1, odd ratios) is still rounded and
reported Approximated.

**D-014 speed curves** (`NATIVE_TESTED`). A ramp is mapped to a Dafin `SpeedCurve` only when
`proveRamp` accepts it: every key has time and value, speeds within 0.1–8, key times strictly
increasing from 0 and either 0..1 or source ticks ending at the source length, and straight-line
integration of the keys reproduces the clip's recorded timeline length within max(20 ms, 0.2 %).
Otherwise it stays constant-speed, Approximated, with "It was not mapped because: …".

## Limits, stated plainly

- No pack and no real `.wfp` were available. The ramp "proof" is my criterion: it shows the keys
  explain the recorded length, **not** that Filmora's easing between keys is straight lines.
  Handles are not read, so a curved ramp could still differ in the middle.
- The `.wfp` carries no recorded media size, hash or duration, so "verified identity" means:
  the candidates agree with each other and fit the clip's needed source length. Fields for an
  expected size and hash exist on the clip (`expectedSize`, `expectedPartialHash`) but nothing
  fills them yet.
- The app layer (`ProjectFileController`, `ProjectLoadDialog.qml`, `ImportRelinkDialog.qml`,
  `Main.qml`, `Exporter.cpp`) needs Qt 6.10 and FFmpeg and was **not compiled or run** here. QML
  was not loaded. Build and click through it once.
- One `MediaProbe` call already running cannot be interrupted; cancel takes effect when it returns.
- The Premiere import is still synchronous, and now treats a same-name tie as missing (it has no
  question dialog).
- `ProjectScanner` (start screen) calls `toProject` without choices, so a tie shows as missing there.
- Shader file paths in `GpuPackageParse` were not confined (out of D-006's stated scope).
- The export dialog still reads "Same as project (30 fps)" and does not show the exact rate.

## Tests

`tests/tst_wfpimport.cpp` (49), `tests/tst_confinedpath.cpp` (15), `tests/tst_wfpsafe.cpp` (94; one
C11 test renamed and inverted for D-013). All pass in the scratch build, normally and under
ASan + UBSan. Mutation checks: disabling the ramp length gate, widening the kept-rate tolerance,
and ignoring the partial hash each made tests fail.
