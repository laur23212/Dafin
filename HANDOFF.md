# HANDOFF: Smart Keycaps, native (C9n), branch `day11-fx-keycaps-native`

Base: `origin/dafin-edition` (65561313) with `day10-fx-keycaps` (C9, Python reference, 3032d357) merged in
(`--no-ff`, no conflicts). The C9 handoff it replaces said "native: nothing written"; that is now done, below.

## Done
- `src/core/scene/SceneKeycaps.{h,cpp}`: the 1:1 port of `tools/scene-ref/keycaps.py` (grammar, dictionary from
  `:/keycaps/key-dictionary.json`, `resolveKey`, phase A `expandKeycaps`, `keyWidth` with `ink` / `inkMac` /
  `semantic`, phase B `layoutKeycaps` with wrap + hidden "+", THEN / OR, label, `lock`). Same error messages.
- `SceneTitle.{h,cpp}`: actor type `keycaps`; `Actor::hidden` (`_hidden`), `ss`, `snapTrunc` (`snap: "trunc"`),
  `keycapsBlock`; `Scene::keycaps` (the plan phase B reads).
- `ScenePainter.cpp`: phase B in `SceneRenderer::build`, before the text sprites, measuring with `pilBBox` (PIL's
  `getbbox(anchor='ls')` on Skia's FreeType glyph boxes, not the drawn ink); per-group `ss` for the sprites and
  box shapes (ss = 2 is the old code, unchanged); `snap: "trunc"`; hidden actors skipped; `sceneRestBoxes` gives a
  flattened group one box (as `render.py rest_boxes`).
- `CMakeLists.txt`: the sources, and the dictionary as resource `dafin_keycaps` next to the scene GLOB.
- `TextAssetsTab.qml`: Titles › **Keyboard Shortcuts** (22, the approved four next to their template);
  Titles › Scenes keeps the other scenes.
- `tools/scene-ref/core_values.py`: `--geometry` (phase B values) and `--ink-table`; the old output is unchanged
  (the three existing `core_*.txt` regenerate byte for byte).
- `tests/tst_scenetitle.cpp` + `tests/data/scene-titles/`: 9 keycaps rows for `coreMatchesReference` (exact) and
  `keycapsGeometryMatchesReference` (≤ 0.5 px), 13A + 13C U1/U2/U3 in `nativeMatchesReference` (frames 25, 40, 75,
  110; SSIM ≥ 0.97, ≤ 0.5 px), `keycapsRelayoutFollowsTheShortcut`, `keycapsCommandOnWindowsIsAnError`,
  `keycapsDictionaryAndGrammar`, `keycapsScenesLoadNatively`.
- Docs: `docs/scene-keycaps.md` ("Native"), `docs/scene-titles.md` (actor table), new DAFIN.md row.

## Checked outside CI (authoring sandbox: Qt 6.4.2 QtCore, no Skia)
- SceneTitle.cpp + SceneKeycaps.cpp compile and run: phase A dump and phase B geometry equal the reference line
  for line on all 22 scenes and on edited shortcuts (all four Mac label policies, wrapping, THEN / OR, custom, F-keys),
  with the reference's own measurements replayed; 12 error cases word for word.
- The loader-only test slots (all `coreMatchesReference` rows, dictionary, grammar, errors, catalog = 22) pass.
- ScenePainter.cpp and tst_scenetitle.cpp pass `-fsyntax-only` against the pinned Skia headers (e7c90ecc).
- The `pilBBox` model (HarfBuzz advances, `PIXEL()` pens, grid-fitted FT_LOAD_DEFAULT control boxes, union with
  origin/advance/baseline) equals PIL's `getbbox` on all 638 measurements of the 22 scenes (freetype-py + uharfbuzz).

## NOT run (IMPLEMENTED_UNTESTED)
- Anything Skia: `keycapsGeometryMatchesReference`, `nativeMatchesReference` (keycaps rows), `keycapsRelayout…`,
  `keycapsScenesLoadNatively`, the renderer half of `keycapsCommandOnWindowsIsAnError`. First real run = CI.
- Risk to watch in CI: if Skia's FreeType (the build's own copy) hints a glyph's control box 1 px differently from
  Pillow's, a key width moves by 2 px and the geometry gate names the actor. That is an engine difference to fix,
  not a reason to widen the gate.
- QML not run (no Qt Quick here). Creator approval of a native export.

## Things to know
- Not bugs: T13's comma sits mid-plate (the 13F proof centres the ink); T06 prints "Esc" like the approved U3.
- Regenerating test data: commands at the end of `docs/scene-keycaps.md`, "Native tests".

---

# HANDOFF: C10n (native mirror of the C10 scenes), branch day10-fx-smart-utilities-native

Base: origin/dafin-edition (65561313, contains C5 6dfbf71) + merge of the C10 bundle
(day10-fx-smart-utilities, a0d10d0). Push C10 first (its own branch/PR), then this one; if C10
lands with another hash, rebase this branch's own commit onto the new dafin-edition.

## Done
- Loader: ss, circle, fillGroup, fitBox, after, preview solid; refusals removed. `scene::resolveLayout`
  mirrors Sprite2x.layout(). Dump identical to scene_core.py for all 9 scenes and param variants.
- Painter: sprite1x text, group ss 3, centring on PIL textbbox, circle (calibrated vs cv2), solid matte,
  linear glyph coverage (contrast 0, gamma 1).
- Tests (tst_scenetitle): core / layout / native frames for the 7 scenes + 12B_C blank, semantic tests.
- Local headless run (minimal CPU Skia at the pinned commit, Ubuntu 24.04, Qt 6.4): 39/39 functions
  (sceneClipSurvivesSave not linked locally). Worst SSIM 0.976, worst shift 0.47 px.

## Status
IMPLEMENTED_UNTESTED until CI "Tests on Linux" (SceneTitle) passes. If CI fails: bring the first errors,
do not guess. Not creator-approved until a native export is reviewed.

## Open creator decisions
15_A for totalSteps != 5; 15_B 178 px track; 14_A gaps 94/+24; 14_B gaps 71/73; no text wrapping;
12B_C two fixed items; motion-tracking binding not included.
