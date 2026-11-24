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

# HANDOFF: C10 (progress, menu, anchors), branch day10-fx-smart-utilities

Base: day9-fx-scene-engine at 6dfbf71 = the C5 result (confirmed against Dafin_C5_SceneEngine_day9_1.zip: same tip hash). C5 itself is not on dafin-edition yet and its first full CI build is pending; this branch depends on it.

## Done (reference side, tested)
- Scenes: 15_B, 14_A, 14_B, 11, 12_A, 12_B, 12B_C (tools/scene-ref/scenes). 150/150 frames identical to the Part6 scripts
  (`test_parity.py`, needs the archives; 9 checks incl. 12B_C blank). 15_A also re-checked, 150/150.
- Semantic tests: `tools/scene-ref/test_smart_utilities.py` (22) + `test_scene_ref.py` (22), CI job `scene-ref` in tests.yml (not yet run on CI).
- Docs: docs/scene-titles.md "Progress, menu path and anchors (C10)"; DAFIN.md row added.

## NOT done: native C++ mirror (status NOT_RUN)
Loader (`SceneTitle.cpp`) refuses `ss`, `fitBox`, `after`, `fillGroup` and the `circle` type on purpose. To mirror, in this order:
1. `circle` actor + `ss` on text (ss=1 path: sprite `(box.w + 2*4, box.h + 2)`, pen `(bx-4, by-asc)`), group `ss` 2|3.
2. `Sprite2x.layout()` of render.py: `fitBox` (w = ceil(ink/ss + 2*padX), max with min) then `after` (x = right edge + gap), resolved in `resolveConstraints`.
3. `preview.background.kind:"solid"`, `mix:"trunc"` on circle, `fillGroup` insets.
4. `core_values.py` dumps + `tests/data/scene-titles/<scene>/` frames + `tst_scenetitle.cpp` cases; native gate SSIM >= 0.97, 0.5 px.
All pure 2D, so compatibility mode (OpenGL < 3.3) needs nothing new.

## Open creator decisions / assumptions
- 15_B accent: 178 px track assumed (89 px at 4/8).
- 14_A: gap 94 (+24 before the target card), unit gaps 71/73 in 14_B follow the approved frames.
- 15_A for totalSteps != 5 never designed. No text wrapping. Anchor 12B_C has two fixed items.
