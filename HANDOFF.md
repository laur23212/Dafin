# HANDOFF: Smart Keycaps (C9), branch `day10-fx-keycaps`

Base: `origin/day9-fx-scene-engine` (6dfbf71). `origin/dafin-edition` has no Scene Title engine yet, so this
branch has to land after day9 (or together with it).

## Done (Python reference, tested headless)
- `tools/scene-ref/keycaps.py` + `keycaps/key-dictionary.json`: identities, Windows/macOS profiles, label
  policies, widths (skins classic-pc, classic-mac, smart-pc, smart-mac), N keys, chords, THEN/OR, Spacebar, wrap, 9:16, `lock`.
- 13_A_Shortcut_Keys and 13C_U1/U2/U3 as `keycaps` scenes; T01–T18 (`keycaps/template-catalog.json` → `build_keycap_scenes.py`).
- `test_keycaps.py` (26 tests), `test_parity.py --keycaps13 --keycaps13c` (150 frames each, 0 differences;
  Mac keys with a stand-in symbol font), ctest entries `SceneRefPython`, `SmartKeycapsPython`.
- Docs: `docs/scene-keycaps.md`, `docs/scene-titles.md` (actor type), `scene-fonts/README.md`, DAFIN.md row.

## NOT done
1. **Native C++ mirror.** Nothing native was written (no Qt headers in the authoring sandbox, so no compile). Plan:
   - `src/core/scene/SceneKeycaps.{h,cpp}`: port `keycaps.py` (`parse_shortcut`, `resolve_key`, `expand_block`, `key_width`, `layout`),
     dictionary read from qrc (add `keycaps/key-dictionary.json` to the scene resources in CMakeLists.txt, next to the GLOB at ~l.879).
   - `SceneTitle.cpp`: accept type `keycaps` (the `types` set ~l.990), Actor fields `hidden`, `ss`, `snap`; run phase A in the expander.
   - `ScenePainter.cpp`: run phase B next to `resolveConstraints`, with the painter's ink measurement (`getbbox(..., anchor='ls')` equivalent).
   - Tests: `core_values.py` dump of expanded actors + `tests/tst_scenetitle.cpp` for 13_A and 13C_U1; then the SSIM ≥ 0.97 / 0.5 px gate.
   Until then `SceneCatalog` silently skips every keycap scene (it drops scenes whose `loadScene` fails), so nothing breaks.
2. **QML category "Keyboard Shortcuts"** in `TextAssetsTab.qml` (needs the native load first).
3. **Real-font parity of the Mac keys**: run `test_parity.py --keycaps13c … --symbols <proof NotoSansSymbols2-Regular.ttf>`;
   add that file and its sha256 to `scene-fonts/` and the scenes' `fonts.symbols`.
4. **Creator approval** of the 18 template visuals and of a native export.

## Things to know
- The comma key (T13) shows the comma mid-plate like an apostrophe: the approved 13F proof centres the ink box, so this is the proof's own look.
- T06 uses "Esc" (approved U3), the 13F proof printed "ESC".
- 13_A's widths are hand-set: kept through `lock`, only for Ctrl+Shift+S.
