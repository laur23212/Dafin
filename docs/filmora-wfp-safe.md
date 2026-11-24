# Filmora `.wfp`: strict reader and compatibility report (C11, wave 0)

Plan: Stage 21 Pass 5 `02_CLAUDE_MASTER_IMPLEMENTATION_HANDOFF.md` **PR-02** (D-002) and **PR-04**
(D-004), with the Pass 4 handoff. Repository state when started: `dafin-edition` @ `2a41059`
(the SHA the research pinned). Branch: `day9-filmora-wfp-safe`.

Status words used here: `IMPLEMENTED_UNTESTED`, `NATIVE_TESTED`, `NOT_RUN`. Nothing below is
"approved"; approval needs a native export that the user has seen.

## Step 0 — what existed (at `2a41059`)

| Item | State | Where |
|---|---|---|
| Central/local metadata consistency | **MISSING** | `ZipArchive.cpp:173-207` reads the local header only to find where the data starts; name, method, sizes, CRC are never compared with the central directory |
| Member size limit, total inflation budget | **MISSING** | `ZipArchive.cpp:70-72` allocates from the declared 32-bit size; no limit anywhere in `FilmoraProjectParser.cpp:41-55` |
| Narrowing before allocation | **MISSING** | `ZipArchive.cpp:70-71` and `:193` cast `quint32` to `int` before checking (2 GiB+ becomes negative) |
| CRC-32 check | **MISSING** | `Entry` (`ZipArchive.h:15-23`) has no CRC field; the reader never computes one |
| Final `Z_STREAM_END` | **PARTIAL** | happy path `ZipArchive.cpp:76-81`; the fallback `:106` accepts **any** output without it; and `:95-104` **loops forever** on a truncated stream (`Z_BUF_ERROR` is treated as "go on"). Measured, see below |
| Duplicate names rejected | **MISSING** | `ZipArchive.cpp:141-168` appends every entry; `FilmoraProjectParser.cpp:44` takes the first case-insensitive match |
| No ZIP64 | **PARTIAL** | documented (`ZipArchive.h:9`) but not enforced: `0xFFFFFFFF` sentinels and ZIP64 records are read as ordinary values |
| EOCD / directory checks | **PARTIAL** | `findEOCD` `:36-56` takes the last signature without checking the comment length; `cdOffset` (`:133-135`) is not checked against the file; a short directory just `break`s (`:143-147`) |
| Unsafe names | **PARTIAL** | `safeRelativePath` (`:27-34`) drops such entries silently (`:166`), so a hostile entry is hidden instead of refusing the archive |
| JSON bounded | **MISSING** | `FilmoraProjectParser.cpp:52` parses whatever came out; depth is only Qt's own limit |
| Out-of-range times | **MISSING** | `FilmoraProjectParser.cpp:27-30` casts a JSON double to `int64_t` unchecked (undefined for NaN/huge) |
| Scanner reads `.wfp` on its own | **EXISTS** | `ProjectScanner.cpp:236-237` (`inspect`, run from `QtConcurrent::run` at `:469`, `:505`) for every `.wfp` found under Documents/Videos/Desktop/Downloads and added folders; no size check; the destructor waits for the scan (`:143-148`), so a hung read also hangs quitting |
| WFP read on the GUI thread | **EXISTS** (unchanged here) | `ProjectFileController.cpp:1073` (parse), `:1017` (media probe in `fitImportedLayouts`), dispatch `:1554-1567`. Moving this to a cancellable worker is **PR-03**, not this task |
| Per-construct report | **MISSING** | only `skippedClips`/`skippedKinds`/`notes` (`ImportedTimeline.h`); `effectChainList` is read nowhere in the importer (`FilmoraProjectParser.cpp` at `2a41059` has no mention); `ProjectFileController.cpp:1105-1107` calls the import "success" when media is found and nothing was counted as skipped |
| 30000/1001 | **PARTIAL** | kept as 29.97… in `ImportedTimeline::fps` (`FilmoraProjectParser.cpp:154-155`), rounded to 30 silently in `ImportedTimelineProject.cpp:127` |
| Speed ramp | **PARTIAL** | flagged in a note (`:218-226`) but with no clip, time or applied value |
| Tests for WFP | **MISSING** / **PARTIAL** | `tests/tst_linkedlibraries.cpp` has no `.wfp` or zip test (0 mentions); the only one is the scanner's valid-file case at `tests/tst_editorstate.cpp:17369-17378` |

### What the existing reader does with the six malformed fixtures (measured)

Run in a separate, time-limited process against the unchanged `drift::zip`:

| Fixture | Existing `ZipArchive` |
|---|---|
| `negative_duplicate_metadata.zip` | both entries read |
| `negative_forged_2gib.zip` | read, 67 bytes (declared size ignored after a failed first attempt) |
| `negative_local_name_mismatch.zip` | read |
| `negative_path_traversal.zip` | entry silently dropped (0 entries) |
| `negative_truncated_deflate.zip` | **never returns** (killed by the 8 s timeout) |
| `negative_wrong_crc.zip` | read, wrong bytes accepted |

The truncated-stream hang is in the shared reader, so it also reaches the eight other production readers listed
below. It is **not** fixed here (see "ZipArchive callers").

## What changed

New, WFP-only (the shared `ZipArchive` is untouched):

- `src/core/interchange/filmora/WfpArchive.{h,cpp}` — the strict reader. Refuses: an end record that
  does not end exactly at the end of the file, ZIP64 (sentinels, extra field, locator), multi-disk, a directory out of place or the
  wrong length, more than `maxEntries`, unsafe / absolute / drive-lettered / `..` / control-char /
  over-long names, two names equal after canonicalisation (slashes, simple case folding), local
  headers that disagree with the central ones (name, method, and size/CRC unless a data descriptor
  is used), data outside the file or before the directory, overlapping members, sizes deflate cannot
  produce (checked before allocating), stored members with two sizes, encrypted members, methods other
  than stored/deflate. A member is returned only after it inflated to exactly its declared size,
  ended with the deflate end marker with no input left over, and matched its CRC-32; all reads share
  one inflation budget. JSON: size-limited by the member limit, depth-limited (128), must be an
  object. Errors carry stable keys (`crc32_mismatch`, `incomplete_deflate_stream`, …).
- Defaults (`WfpLimits`): archive 2 GiB, 20 000 entries, central directory 8 MiB, name 1 KiB,
  member 32 MiB, total 64 MiB, JSON depth 128. All are fields, so a real project that needs more is a
  one-line change.
- `FilmoraProjectParser.cpp` uses it. A damaged/forged archive ends the import with a message; a member
  that is absent or whose JSON does not parse is skipped as before (so the fallback to any `.wesproj`
  still works). Times are range-checked before the cast.
- `src/core/interchange/CompatibilityReport.h` — `CompatStatus` (`preserved`, `approximated`,
  `omitted`, `unsupported`, `missing_dependency`, `unknown`) and `CompatEntry` (construct, stable
  source id `<timelineId>:<video|audio|typeN>:<track>:<clip>`, where, source time, reason, applied,
  hint), `ImportedTimeline::report`, `ProjectBuildResult::report`.
- Constructs reported: media/audio clip (preserved), attached effect chain (omitted, with the effect
  ids found, never executed), speed ramp (approximated: key range and the scalar applied), unusable
  speed (approximated), frame rate (preserved, or approximated with the source ratio and the rate
  used; `30000/1001` → 30), non-media clips (unsupported), zero-length or invalid-time clips
  (omitted), other timelines (omitted), unknown timeline choice (approximated), unrecognised effect
  layout (unknown), media not on this computer (missing_dependency, added when the project is built).
- `projectFpsFor()` (`ImportedTimeline.h`) is the one rule that turns a source rate into the project's
  whole fps; the project builder and the report both use it, so they cannot disagree.
- `summary()` now says how many clips lost their effects, how many timelines/zero-length clips were
  left out and what the frame rate became; `ProjectFileController` shows a **warning** instead of
  **success** whenever the report holds any loss. The old counts, notes and `Clip::speedCurve` are
  untouched.

Not touched on purpose: `ZipArchive.*`, the other eight readers, `ProjectScanner` (it calls `parse()`,
so it is covered), relinking (`MediaFinder`, PR-03), the GUI-thread read (PR-03), texture paths (PR-05).

## ZipArchive callers (audit for a later change to the shared reader)

All use the lenient reader and none has a CRC/size/end-of-stream check of its own; `FacePropImport`
only caps declared sizes (`:409`).

| Caller | Reads |
|---|---|
| `ui/theme/ThemeManager.cpp:2264,2332` | theme packs |
| `app/online/library/UserLibrary.cpp:245,268,906-984` | `.mogrt` bundles, library imports |
| `app/online/addons/AddonManager.cpp:1668,1791,2002` | add-on packages |
| `app/online/library/MogrtInfo.cpp:342,400` | `.mogrt` |
| `core/interchange/premiere/MogrtReader.cpp:241,267` | `.mogrt` |
| `engine/face/props/FacePropImport.cpp:370-409` | face props (declared-size cap only) |
| `engine/render3d/splats/SplatAsset.cpp:55,691` | splat archives |
| `core/project/sources/DotLottie.cpp:59-136` | `.lottie` |
| `tests/tst_diagnosticslog.cpp:126` | test |

Suggested next step (own PR, after each caller is checked): fix `decompressRawDeflate` so a stream
that cannot finish returns failure instead of looping, then move callers one by one to a shared
bounded extractor. Until then the hang above stays reachable through those formats.

## Tests

`tests/tst_wfpsafe.cpp` (`ctest -R WfpSafe`), header-only fixtures in `tests/WfpFixtures.h`:

- the 14 synthetic fixtures of Pass 4 as bytes (no binary file added), each checked against the hash in
  the pack's `04_PACK_INDEX/SHA256SUMS.txt`. **Pack inconsistency:** 9 of the 14 files do not match
  `Stage21_Pass4/data/fixture_sha256.csv` / `VALIDATION.json` (same sizes; they carry file times, so
  they were probably regenerated afterwards); they do match the pack index, which is what is used.
- the six malformed ones are read **only** by `WfpArchive` / `filmora::parse` in this test, from a
  temporary folder, named `.zip` so the start screen would never list them; each is refused with the
  reason `expected_fixture_outcomes.csv` gives;
- the five handoff regressions: `attached_effect_success_status` → media kept, effect reported
  omitted; `fractional_2997fps` → 30000/1001 reported, 30 used; `ramp_and_attached_effect` → 2×
  approximation and effect loss, separately; `multi_timeline_current_id` → timeline 22, 21 reported
  left out; `unsupported_title_track` → unsupported entry with its source time;
- dozens of hand-built hostile archives (ZIP64 three ways, multi-disk, overlap, local/central mismatch,
  inflation beyond/short of the declared size, trailing bytes, early end, bad CRC, 2 GB claimed from
  2 MB, budget, names, duplicates, non-UTF-8, encryption, JSON depth/syntax/shape, NaN-range times);
- compatibility: archives written by `tests/TestZip.h` and by `drift::zip::Writer` still open.

## Evidence

| Claim | Status |
|---|---|
| Reader + parser + report compile warning-free (`-Wall -Wextra`) and `tst_wfpsafe` passes (94 cases) | **NATIVE_TESTED** locally: Linux x86-64, GCC 13.2, Qt **6.4.2** (the repository needs 6.10, so the whole app could not be built here) |
| Same test under AddressSanitizer + UBSan | **NATIVE_TESTED** locally, clean |
| 30 000 random byte-level mutations of the 14 fixtures through `parse()` + `toProject()` + `verifyAll()` under ASan/UBSan | clean (72 still parsed, the rest refused); harness was local and is not in the repository |
| Full build with Qt 6.10, CI `Tests` on Linux/macOS/Windows | **NOT_RUN** |
| Windows | **NOT_RUN** |
| A real Filmora `.wfp` | **NOT_RUN** — none was available. The strictness may refuse something Filmora's writer does that the fixtures do not (an entry name with `:`, a gap between the directory and the end record, a method other than stored/deflate in an unread member is tolerated until read). If a real project is refused, the key in the message names the rule |
| Visual or timing parity of an imported project | **NOT_ESTABLISHED** (no rendering was touched, so the OpenGL < 3.3 compatibility mode is not involved) |

## Not done / decisions for the user

- PR-03 (worker thread, cancellation, one transactional apply) and relink disambiguation: not started.
- Speed curve ramps still become one constant speed; now they are said to.
- Track mute vs hide are still one flag (`P1 TRACK_METADATA_APPROXIMATION`): not reported yet.
- Media paths inside a `.wfp` can be any path (including network paths) and are probed on the GUI thread
  in `fitImportedLayouts`: left for PR-03/PR-05.
- `:` anywhere in a member name now refuses the archive (the old reader skipped such entries). If a real
  project needs it, relax `WfpArchive::canonicalName`.
- Branch number `9` in `day9-…` is a guess (the last pushed day branch is `day78`); rename freely.
