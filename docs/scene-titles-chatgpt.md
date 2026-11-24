# Writing a Dafin Scene Title (for ChatGPT)

You design titles for Dafin as **Scene Title** JSON files, and Dafin renders the same file natively.
The full specification is `docs/scene-titles.md`; this page is the short version. The tools are in
`tools/scene-ref/` and need only Python 3 with Pillow (with RAQM), numpy, opencv-python and ffmpeg.

## The rule

**If the format cannot draw something, do not mimic it.** That means no baked bitmaps, no fake glow
made of rectangles, no crossfade instead of a morph, and no opaque MP4. Stop and ask for an
extension instead: name the missing primitive, the frame where it shows and what it must do.
Effects the format already reserves (`sdfMorph`, `maskedLight`, `focusMix`) are refused on purpose
until they are added.

## Writing one

1. Start from a port in `tools/scene-ref/scenes/`. `15_A_Step_Focus.scene.json` uses sprite texts,
   lines and a repeat with params. `03b_LT_B_25.scene.json` uses straps that fit the text, clips and
   sync.
2. Set the basics:
   - `canvas` is 1920×1080 design pixels.
   - `timing.duration` is usually 6 s at 25 fps.
   - `render.profile` is `sprite2x` for new work. Use `mask1x` only for Stage03b-style text.
3. Give every actor a semantic `id` and `role`.
   - Put text on its **baseline**: `"at": {"baseline": [x, y]}`.
   - If you have a PIL draw position from an old script, convert it:
     `python3 render.py anchor --profile sprite2x --font <file> --size 71 --text "Title" --at 589 447`
4. Time each actor with `in` and `out`, as in this example:
   ```json
   "in":  {"start": 0.72, "dur": 0.5, "ease": "smoothstep", "from": {"opacity": 0, "dy": 12}},
   "out": {"start": 4.68, "dur": 0.33, "ease": "smoothstep", "to": {"opacity": 0}}
   ```
   - Available eases: `smoothstep`, `easeOutCubic`, `easeOutQuart`, `linear`, `cubic-bezier(...)`.
   - Every actor must be completely still between the last entrance and the first exit, because
     Dafin stretches that hold to fit the clip length.
5. Make editable things into `params`, and use them in two ways:
   - in text: `"text": "STEP {currentStep:02}"`;
   - in fields: `{"expr": "AQUA if i < currentStep else FAINT2"}`, `"type": "repeat"` for
     N segments.
6. Use constraints instead of hard-coded sizes:
   - `fitTo`: a strap follows its text width;
   - `clipTo`: text is revealed by its strap;
   - `sync`: a text shares its strap's timing;
   - `align`: line up with another actor's start or baseline.
7. Fonts are named by file, never bundled. Add the `package` the file comes from, and its `sha256`
   when the exact cut matters.

## Rendering and checking

```sh
python3 render.py validate my.scene.json                 # loads, checks params, checks the hold is still
python3 render.py render my.scene.json --out out/        # out/alpha/NNNN.png (alpha) + out/<id>.mp4 preview
python3 render.py render my.scene.json --out out/ --param currentStep=3 --param totalSteps=7
python3 render.py render my.scene.json --out out/ --duration 9   # check the stretch rule
python3 compare.py --ref approved.mp4 --test out/<id>.mp4 --scene my.scene.json --out cmp/
python3 test_scene_ref.py                                 # tool self-tests
```

- `compare.py` checks frames 0, 10, 20, 40, 65, 90, 115, 135 and 149 by default. It writes
  `cmp/report.csv`, `cmp/summary.json` and `cmp/contact.jpg` (reference | test | difference ×8).
- The gate is **SSIM ≥ 0.97** on every visible actor and **position ≤ 0.5 px**. A one-pixel shift
  fails.
- When you port an approved title, the render must pass against its original MP4. If it does not,
  find the cause. Never change the design to make it pass.

## Do not

- Redesign an approved title, even by one pixel.
- Add anything from Filmora: no shaders, fonts, previews or scripts.
- Call a title "approved". Only the creator approves, and only after seeing Dafin's native export.
