# Scene Title reference tools

Reference renderer and comparator for the Scene Title format. Spec: `docs/scene-titles.md`;
the short guide for title authors is `docs/scene-titles-chatgpt.md`.

| File | What it does |
|---|---|
| `scene_core.py` | Loading, params, expressions, easing, timing and stretching, constraints (no pixels) |
| `render.py` | `render` (alpha PNG + MP4 preview), `validate`, `anchor` (script position → baseline) |
| `compare.py` | Reference MP4 vs render: per-actor SSIM / PSNR / max difference / position, CSV + contact sheet |
| `native_compare.py` | Dafin's native frames (`dafin --render-scene … --frames all`) over the same background and encode, then compare.py against the approved MP4 |
| `core_values.py` | Value dump of the loader, for the native test's `tests/data/scene-titles/core_*.txt` |
| `calibrate.py` | Runs compare on the faithful render and on deliberate errors, to set the thresholds |
| `test_scene_ref.py` | Headless self-tests; pixel tests skip when the proofs' fonts are missing |
| `test_parity.py` | Bit-exact check of the ports against the original scripts (needs the design archive) |
| `scenes/` | Ports: `15_A_Step_Focus`, `03b_LT_B_25` |

Requirements: Python 3.10+, Pillow with RAQM, numpy and opencv-python, plus ffmpeg on PATH. For
pixel-exact ports you also need the font packages named in each scene (on Debian/Ubuntu:
`fonts-noto-core`, `fonts-noto-extra`, `fonts-inter`).

The 03b preview background (`16x9_SIMULATED_PHOTO_PAN.mp4` and `16x9_STATIC_FEATHERED_SCRIM.png`)
is not in the repo. It comes from the design archive (`Dafin_Part5_Stage03b_C_v1_Optical_Refinement/inputs`)
and is passed with `--assets`.
