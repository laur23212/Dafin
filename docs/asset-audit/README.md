# Asset handoff audit (13 November 2026)

Audit of `DAFIN_COMPLETE_MASTER_HANDOFF` (the ChatGPT creative, SFX and transition handoff, 97 MB
zip) against `dafin-edition` @ `07bab4f`. Every candidate has one row in
[`reconciliation.csv`](reconciliation.csv): source path, the Dafin built-in it overlaps, whether its
format is native, which checks ran, the action and the wave.

Nothing here was run inside Dafin. The checks are a Python copy of Dafin's own package parser
(`tools/asset-audit/gpu_audit.py`, which agrees with Dafin on all 98 bundled packages), the GLSL
reference compiler, and the shaders executed on Mesa llvmpipe (`tools/asset-audit/gl_harness.py`).
All of that is **preliminary**: the real loaders run in `tst_editorstate`
(`assetWave1PackagesLoadAndApply`) once CI builds the branch.

## Inventory

| Tier | What | Count | Result |
|---|---|---:|---|
| B | Base sound library | 335 | **Already installed.** Same library id, ids, titles and durations as `asset-library/sound-effects`; the 335 WAV masters match their catalog SHA-256. Nothing to add |
| A | GPU effects | 65 | Loader + GLSL OK for all 65. 3 accepted (wave 1), 17 duplicate a built-in, 2 archived, 8 blocked, 35 need review |
| A | GPU transitions | 17 | Loader + GLSL OK for all. 2 accepted (wave 1), 6 duplicates, 9 need review |
| A | Title designs | 101 | **Blocked as delivered.** Every one binds `content` and `fontFamily`, which Dafin's validator rejects (it wants `text`, `font.family`); ids lack `chatgpt.`; 92 use fonts outside Fonts Essentials (only the 9 Inter titles don't) |
| A | Text animations | 23 | Native grammar. 2 copy built-in Fade, 1 is a second Mono Terminal, 7 only re-tune Pop/Shrink numbers (merged), 13 to review. 7 are needed by the 8 V3 kinetic titles |
| A | SVG stickers | 78 | Valid SVG, no external refs. 12 have live Inter `<text>`. Colourways map to Dafin's SVG recolour overrides |
| A | Lottie stickers | 9 | Valid headers (512², 30 fps, 2 s). 3 overlap bundled library stickers. `.controls.json` sidecars are not a Dafin format |
| A | LUTs | 10 | Valid `.cube`, 33³, 6 decimals, domain 0–1. Dafin ships no looks in the repo (Looks add-on / user import) |
| A | Workflow recipes | 20 | Not a Dafin format: archived |
| V6 | New sounds | 12 | No name/tag overlap with the 335. Need listening approval and new browser groups |
| V6 | New transitions | 3 | Loader + GLSL + harness OK (Subject Holdout's no-mask path is clean). Review last |
| D | V1–V5 editable sources | 832 | Archive only, never installed |

**Missing from the handoff and from this checkout:** `chatgpt.elegant-end-card` and the Sampler
Prism template (only named in DAFIN.md), `dafin-chatgpt-title-templates-tested-to-date.zip`, the
Baloo 2 `.driftpkg`.

## Duplicates and overlaps

Skipped because a built-in already does it: Soft Halation (Halation), Filmic Vignette (Vignette),
35 mm Grain (Film Grain), Chroma Key Green Clean (Chroma Key + the toolbar key), Documentary
Duotone (Duotone), Golden Light Leak (Light Leak), Warm Lens Flare (Lens Flare), CRT Scanlines and
VHS Tracking (VHS / CRT, VHS), Signal Glitch (Digital Glitch), Highlight Bloom (Bloom / Glow),
Cinematic Teal Amber (Cinematic + Color Grade), Subject Rim Outliner (Outline), two more
chromatic-aberration variants, Depth Haze (Depth Fog), Depth Soft Focus (Depth of Field); Dip to
Ink, Slide Push, Soft Luma Fade, RGB Glitch Switch, Bright-first Luminance, Luminance-protected
Crossfade. Families to thin before review: zoom (crash/fast-in/fast-out/whip, 4 effects), shake (2),
grading tools (6 that Color Grade may cover).

## Compatibility issues

- **Titles:** not installable until converted (bindings, `chatgpt.` id, a Fonts Essentials font
  with Romanian glyphs) and then passed through `dafin --check-template` plus a visual review. The
  converter must not rename public ids of anything already shipped.
- **Diagnostics** (focus peaking, zebra, clipping, matte/depth/face inspectors, safe frame): Dafin
  has no preview-only flag, so an enabled one is exported. Blocked until it has one.
- **Face-Follow Mosaic:** with no face it mosaics the whole frame (`failClosed`), against Dafin's
  rule that face effects pass through. Not a guaranteed anonymiser. Owner decision.
- **Smart effects:** 17 of the 18 face/depth/mask effects pass the frame through when the data is
  missing (harness). None was tried with a real sidecar.
- **Category names:** candidates use their own (`utility`, `camera`, `smart-color`…), which would
  each become an untranslated browser chip; accepted items are filed under Dafin's categories.
- **Thumbnails:** WebP is Dafin's own format (all 95 built-in thumbnails), so no PNG fallback is
  needed. Candidate thumbnails are 16:9 renders over AI-made photos; wave 1 uses new 256 px cards
  and 12×128 strips rendered from the shipped shaders over procedural pictures (re-run
  `effectthumbs --only <id>` / `transitionthumbs --only <id>` in a build for the house look).
- **V6 sounds:** ids lack the sub-category the 335 use; groups (Editorial Utility, Designed
  Atmospheres, Communications / Story) don't exist in the browser. Rebuild through
  `tools/sfx/build_sfx_index.py`, keep the 24 fps sync pop as WAV. The Opus loops decode
  sample-exact in FFmpeg (gapless expected); not tried in Dafin.

## Licensing

- Sounds: procedural originals, "royalty-free, no attribution", **not CC0**. The 44-item recordings
  acquisition queue is a list of URLs, not bundled audio: never ship or relabel it.
- Stickers/Lottie: the Dafin library index says Dafin originals are CC0. The handoff does not grant
  CC0, so adding these needs a licence label decision first.
- Fonts: none shipped. The 101 titles name 42 families, many from a Linux sandbox (URW, TLWG, GFS,
  ADF…); licences and Ș/Ț/ă/î/â coverage unchecked.
- Preview photos (`reference_images/`) look AI-generated and have no provenance record; wave 1 does
  not use them.

## Integration order

1. **Wave 1 (this branch):** Lens Barrel Warp, Adaptive Vibrance, Camera Shake Handheld, Whip Pan
   Left, Iris Open.
2. Effects/transitions marked wave 2 after you decide the zoom/shake/grade families; text
   animations; Lottie/SVG stickers after the licence label decision; first titles once a converter
   and fonts are agreed.
3. LUTs (after deciding where looks ship), mask/depth/face effects with real sidecars,
   restoration filters.
4. V6 sounds after listening approval.
5. V6 transitions.

## Wave 1 log

| Package | Id (unchanged) | Change from delivery | Checks |
|---|---|---|---|
| `effects/lens_barrel_warp` | `dafin.v3.fx.lens-barrel-warp` | category `glitch`; zooms to fill instead of smearing edge pixels into the corners | loader, GLSL, harness: finite, repeatable, alpha kept, min/max params |
| `effects/adaptive_vibrance` | `dafin.v4.fx.adaptive-vibrance` | category `color` | same |
| `effects/camera_shake_handheld` | `dafin.v3.fx.camera-shake-handheld` | category `impact` | same + same picture for the same second at 24 and 60 fps, moves between frames |
| `transitions/whip_pan_left` | `dafin.v3.transition.whip-pan-left` | category `cinematic`; mirrors the picture past the edge (clamping stretched the edge column into a pale band over up to a quarter of the frame) | loader, GLSL, harness: exact outgoing frame at 0, incoming at 1 |
| `transitions/iris_open` | `dafin.suite.transition.iris-open` | category `geometric`; the circle reaches the farthest corner (it left 15 % of the old clip on screen at 99 % with an off-centre centre, then jumped); feather never zero | same, centre at a corner, feather 0 |

Each has a new thumbnail or preview strip. Installed footprint +55 KB. Not run: Dafin preview and
export, Windows/Linux GPUs, 1080p/4K timing, save/load round trip (the test adds them to a clip
and a cut).

## Wave 2 (14 November 2026)

Everything that works in Dafin went in. Everything else was dropped, with the reason on its row in
`reconciliation.csv`.

- **Effects:** 24 more, 27 with wave 1. Categories were mapped onto Dafin's own: Color, Retro,
  Dreamy, Depth & Lighting, Keying, and a new Restoration. Cards were rendered with a stand-in
  subject, mask and depth map (`SYNTH=1 gl_harness.py`), so the smart effects show what they do.
  - Fixed `old_film_vertical_scratches`: GLSL `pow()` of a negative base is undefined.
  - Dropped 11:
    - Privacy Mosaic and Edge-Safe Sharpen: built-ins already do them.
    - The four progress-driven zooms and Impact Shake Hit: they do nothing until keyframed.
    - Edge-Guarded Soften: it blurs edges.
    - Highlight Smooth Shoulder: it turns highlights grey.
    - Face-Follow Softbox: Dafin starts the face scan only for `face_*` effects.
    - Subject Contact Shadow: it blacks out everything outside the mask.
- **Transitions:** 12 more, 14 with wave 1.
  - Film Gate Jump's last frame was black: `pow(sin(PI), 8)` of a value a hair below 0.
  - Vintage Light Leak Cut had the same `pow()` problem.
- **Titles:** 82 of 101, converted by `tools/asset-audit/convert_titles.py`:
  - The `chatgpt.` id prefix.
  - `text` / `font.family` bindings.
  - Liberation Mono becomes Cousine.
  - The author's readability target moved to `style.animation.timing.maxReadMs`, capped at Dafin's
    200 ms (250 ms for captions).
  - The 19 dropped titles use fonts that are not freely shippable.
  - Checked with Dafin's own validator and control checks, and with `tst_core` built against
    Qt 6.10 (221/221).
- **Fonts:** 21 OFL families in `title-fonts/`, 9 MB, staged to `<appDir>/fonts` (see its README).
- **Text animations:** the 13 new ones, plus the 3 the kinetic titles need.
- **Stickers:** the 6 new Lottie stickers go into the Dafin library, labelled "Royalty-free".
  The 78 SVG stickers were dropped because the library takes Lottie only.
- **LUTs:** dropped from the bundle; Dafin ships no looks. They can be imported as `.cube` files.
- **New sounds:** deferred. They need a listening review and new browser groups.
- **Shipping:** the 41 handoff packages are added to `DRIFT_CORE_EFFECTS` and
  `DRIFT_CORE_TRANSITIONS`. Packaged builds ship only that list, and this fork cannot publish
  add-ons.

## Batch 01 from ChatGPT (14 November 2026)

First batch made from `PROMPT_FOR_CHATGPT.md`; all 8 assets are in.

- **Effects (4):** Atmospheric Dehaze, Midtone Solarization, Editorial Triptych (three crops of one
  clip, not three clips) and Depth Cut-Paper Relief (smart: needs the depth map, else unchanged).
- **Transition (1):** Double-Door Reveal; checked at progress 0, 0.99 and 1.
- **Titles (2):** Source Credit Card (Inter, already in Essentials) and Chapter Placard (Roboto Slab,
  added to `title-fonts/` as Apache-2.0 with its LICENSE.txt). Both pass Dafin's validator.
- **Effect template (1):** Clear Weathered Footage (dehaze + Adaptive Vibrance). Added to
  `DRIFT_CORE_EFFECT_TEMPLATES`, which was empty.
- **Not tested:** the full app was never built; the Depth card uses a stand-in depth map; Inter weight 600.

## Batch 02 from ChatGPT (14 November 2026)

All 19 assets are in: 7 effects (Highlight Shoulder Guard, Magenta Lens Fringe Repair, Smooth Gradient
Deband, Rolling Skew Balance, Vertical Side Reflections, Original / Grade Comparison, Depth Topography
Ink), 2 transitions (Editorial Card Retreat, Left Hinge Turn), 7 titles and 3 effect templates.

- Question Focus asked for Inter weight 650, which Inter does not have; changed to 600.
- Fonts added to `title-fonts/`: Noto Serif and IBM Plex Mono. DM Sans, Inter and Oswald come with Essentials.
- Original / Grade Comparison draws a split into the picture, so it also shows in exports.
- Not tested: the full app, tst_editorstate and tst_engine, real depth maps, Inter weight 600.

## Batch 03 from ChatGPT (14 November 2026)

All 30 assets are in: 14 effects, 2 transitions (Glass Ribbon Handoff, Origami Fan Reveal), 8 titles and 4 effect templates.

- **Restoration (4):** Color Negative to Positive, Shadow Chroma Denoise, Paper Document Cleanup, Neutral Cast Rebalance.
- **Colorless Luma Key** changes the clip's alpha on purpose (all other effects keep it).
- **Smart effects (4):** Masked Subject Offset Echo, Masked Subject Lens Punch (need a mask), Depth Parallax Nudge, Distant Heat Haze (need a depth map). Without one they pass the picture through.
- **Panel effects (5):** Quadrant Crop Mosaic, Asymmetric Detail Split, Circular Detail Inset, Diagonal Dual Crop, Horizontal Focus Stack. Each uses ONE clip.
- No new fonts. Not tested: the full app, tst_editorstate and tst_engine, Inter weights 500/800, real masks and depth maps.

## Batch 04 from ChatGPT (14 November 2026)

All 33 assets are in: 12 effects (3 colour looks, 2 retro prints, Stained-Glass Facets, Gentle Codec Ringing
Repair, 4 smart effects, Offset Photo Montage), 3 transitions (Clock Dial Sweep, Print Dot Reveal, Circular
Waterline), 12 titles and 6 effect templates.

- Two titles use weight 500 (Luxury Collection in Noto Serif, Crew Role Title in IBM Plex Mono), so
  `title-fonts/` now has Noto Serif 500 and IBM Plex Mono Medium.
- Smart effects need a mask or depth map; without one the picture is unchanged.
- Not tested: the full app, tst_editorstate and tst_engine, Inter 500/800, real masks and depth maps.

## Batch 05 from ChatGPT (14 November 2026)

All 33 assets are in: 8 effects (Horizontal Highlight Streak, Rainy Window Droplets, Linen Weave Finish,
Passing Cloud Shadows, Graphic Thermal Palette, Six-Frame Contact Sheet, and the smart Subject Edge Light
Wrap and Depth Near Texture Pop), 6 transitions (Woven Fabric Interlace, Curtain Pleat Pull, Zipper Seam
Open, Spiral Groove Reveal, Torn Paper Column, Prism Column Flip), 12 titles and 7 effect templates.

- IBM Plex Mono Bold was added to `title-fonts/` for Keyboard Shortcut Hint; the other title fonts come with Essentials.
- Horizontal Highlight Streak is strong on bright scenes (it works from highlights).
- Not tested: the full app, tst_editorstate and tst_engine, real masks and depth maps, 4K speed.

## Batch 06 from ChatGPT (14 November 2026)

All 32 assets are in: 11 effects (Indexed Palette Dither, Watercolour Pigment Wash, Saturated Colour Soft
Knee, Channel Routing Lab, Photographic Bas-Relief, Practical Lamp Flicker, Sky Blue Balancer, and the smart
Masked Ambient Colour Match, Depth Stereo Anaglyph, Depth Contact Shading, Masked Ink Window), 2 transitions
(Route-Line Unveil, Ice-Crystal Growth), 12 titles and 7 effect templates. No new fonts were needed.

- Smart effects pass the picture through without a mask or depth map.
- Not tested: the full app, tst_editorstate and tst_engine, real masks and depth maps, 4K speed.

## Batch 07 from ChatGPT (14 November 2026)

All 25 assets are in: 9 effects (Tritone Exposure Map, Blueprint Drafting, Oil-Film Iridescence, Shallow Water
Caustics, LED Stage Screen, Mixed Lighting Soft Balance, Window Lattice Shadows, and the mask effects Masked
Speed Tunnel and Masked Floor Reflection), 3 transitions (Tile Card Flip Cascade, Camera Roll Handoff, Tape
Splice Handoff), 8 titles and 5 effect templates. No new fonts were needed.

- The two mask effects pass the picture through without a mask.
- Not tested: the full app, tst_editorstate and tst_engine, real masks, 4K speed (LED Stage Screen may alias).
