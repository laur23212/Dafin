# CUDA benchmark (`dafin --bench`)

This benchmark decides whether three possible CUDA changes are worth building: **C4** (export
straight from CUDA into NVENC), **C5** (a lighter CUDA→OpenGL sync in the preview) and **C6** (an
NVDEC surface pool/cache for scrubbing). It measures the routes Dafin already has, on the same
project, in paired runs, and says GO or STOP for each change against a stated bar.

It only measures, and it changes nothing for anyone who doesn't run it. The timers it reads
(`engine/diagnostics/BenchPhases.h`) are off unless a benchmark run turns them on.

## Running it (Windows, PowerShell)

Close Dafin, plug the laptop into mains power, and leave the machine alone while it runs (about
10–15 minutes with the defaults).

```powershell
& 'C:\Program Files\Dafin\dafin.exe' --bench --media 'D:\Videos\clip.mp4' | Out-Host
```

`| Out-Host` makes PowerShell wait for it and show its progress. Results go to
`Documents\Dafin Benchmarks\<date-time>\`. **`summary.md` is the file to send back.** It names
no files (media are identified by size and SHA-256).

Use the **same video file on both machines** (the 3070 and the 3050) so the projects are
identical. The project SHA-256 at the top of `summary.md` shows whether they were. A 1080p or 4K
H.264/HEVC camera clip of at least 20 seconds is a good choice.

Other forms:

```powershell
# Two files: a crossfade between them, over a base layer
dafin.exe --bench --media 'D:\a.mp4,D:\b.mp4' | Out-Host
# Your own project (.drift or project .json)
dafin.exe --bench --project 'D:\Projects\Trip.drift' | Out-Host
# Only one half, more runs, a half-size preview
dafin.exe --bench --media 'D:\clip.mp4' --only preview --runs 7 --preview-scale 0.5 | Out-Host
```

On Linux: `dafin --bench --media ~/clip.mp4` (an AppImage: `./Dafin-x86_64.AppImage --bench …`).

| Option | Default | |
|---|---|---|
| `--runs N` | 5 | measured runs per variant (plus one warm-up each, left out) |
| `--seconds S` | 20 | length of the scene and of each export |
| `--frames N` | 300 | playback frames per preview run |
| `--seeks N` | 60 | scrub seeks per preview run (same positions every run, every machine) |
| `--preview-scale X` | 1 | preview resolution (1 = Full, 0.5 = Half…) |
| `--codec ID` | `h264_nvenc` if present, else Dafin's default | export encoder |
| `--scene layered\|single` | layered | built-in scene for `--media` |
| `--only export\|preview` | both | |
| `--out DIR` | Documents/Dafin Benchmarks/… | |

## What it does

The built-in **layered** scene has two decoded video layers on every frame (the clip with
contrast, vignette and RGB split and a slow zoom, over the same clip with saturation), or a 1 s
crossfade between two clips over a base layer when two files are given. **single** is one clip, no
effects. The project takes the first file's size and frame rate.

Every run is its own process (`dafin --bench-run …`), so decoders, OpenGL and caches start clean
and the variant's switch is set before anything starts. Each pair runs a warm-up of each variant,
then A B, B A, A B… so drift (heat, background work) falls on both equally.

| Variant | What | Switch |
|---|---|---|
| `export_gpu` | export as Dafin does it: GPU compositing → GL NV12 → PBO → host NV12 → encoder | none |
| `export_cpu` | the same export composed on the CPU and converted by swscale | `DRIFT_EXPORT_SWSCALE=1` |
| `preview_hw` | playback sweep + scrub with the normal decoder choice (NVDEC on NVIDIA) | none |
| `preview_sw` | the same with software decoding | `DRIFT_NO_HWACCEL=1` |

Playback frames are timed exactly as the editor's preview worker times them
(`CompositorService`: `compositeToTextureAt`, decoder wait from `ClipReaderPool`), with proxies
off so the original media and its decoder are what gets measured. The Qt Quick present is not
in the figure, and GL work still in flight when the call returns shows up later as back-pressure,
which is why "frames/s achieved" (frames over sweep wall time) is reported too.

Route proof comes from the C0 counters (`RouteStats`): how many frames took `cuda-interop`,
`hw-download`, `software-pbo`, `d3d11-interop`, `vaapi-dmabuf`, which encode route
(`direct_cuda`, `gl_pbo_host_nv12`, `cpu_swscale`) and the export plan. A run that drew an empty
frame is a FAIL, not a fast run.

## Reading the verdict

The benchmark cannot time a change that doesn't exist yet. What it measures is the **ceiling**:
the time spent in the steps that change would remove. A small ceiling rules the change out. A big
one only means an opt-in experiment is worth building, and that experiment's own A/B decides.

| Change | Ceiling measured | GO when |
|---|---|---|
| C4 direct CUDA→NVENC | PBO map + copy to host NV12, plus encode/mux (which includes NVENC's own host→GPU upload), over export wall time | ≥ 10 % of the export, NVENC in use, GPU NV12 route seen |
| C5 preview sync | `cuStreamSynchronize` time inside each frame's CUDA→GL import | p95 frame time over the frame budget **and** (without the sync p95 would meet it, or the sync is ≥ 15 % of frame time); CUDA interop seen |
| C6 NVDEC pool | decoder wait inside each scrub seek | scrub p95 over 100 ms **and** decoder wait ≥ 50 % of it; NVDEC frames seen |

The C4 ceiling is generous on purpose (all of encode/mux counts, not just the upload), so a STOP is
safe. UNSUPPORTED means the route a change would replace never ran on this machine (no NVENC, no
CUDA interop): that is the expected answer on AMD, Intel and machines without a GPU, where every
variant still runs and reports.

Figures are **median / p95 (min–max) across runs**. With 5 runs, nearest-rank p95 is the slowest
run; the summary says so. Paired deltas compare runs with the same index, which ran back to back.

## Files

- `summary.md`, `summary.json`: the tables and verdicts
- `results.jsonl`: one line per run (all samples, routes, phase timers, status)
- `header.json`, `bench-project.json`: machine, build and the exact project every run read
- `runs/*.log`: each run's own log, for when one fails
- `export_gpu.mp4`, `export_cpu.mp4`: the last export of each variant, to compare by eye

Status words follow the CUDA handoff: PASS, FAIL, UNSUPPORTED, BLOCKED, INCONCLUSIVE. PASS
is about the run being correct, not about speed.
