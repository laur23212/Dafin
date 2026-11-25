# Smart Keycaps (Scene Title actor `keycaps`)

One generator draws every keyboard-shortcut title. It replaces the hand-built actors of the approved
static titles (`13_A_Shortcut_Keys`, `13C_U1/U2/U3`) and of the 18 Stage13F templates T01–T18
(Titles › Keyboard Shortcuts). The approved design is frozen: at default values the generator
reproduces the approved frames, and any difference is an engine bug.

| Part | Status |
|---|---|
| Key dictionary, grammar, profiles, widths, layout (`tools/scene-ref/keycaps.py`, `keycaps/key-dictionary.json`) | IMPLEMENTED, tested headless (`test_keycaps.py`, 26 tests) |
| Parity with the approved scripts, 150 frames each | 13_A and 13C U1/U2/U3: identical bit for bit, with the real `NotoSansSymbols2-Regular.ttf` (`test_parity.py --keycaps13c … --symbols scene-fonts/NotoSansSymbols2-Regular.ttf`). The hold frame of U1/U2/U3 also equals the approved `…_SOURCE_HOLD.png` of the Stage13C package, 0 differing pixels. Not compared: the encoded MP4s (lossy) |
| 18 templates T01–T18 (`scenes/T*.scene.json`) | generated and rendered; **the creator has not approved their visuals** |
| Native C++ mirror (`src/core/scene/SceneKeycaps.{h,cpp}`, loader, painter, catalog) | IMPLEMENTED_UNTESTED until the CI runs `tst_scenetitle` (see "Native" at the end). Checked outside CI against Qt 6.4 without Skia: phase A and phase B equal the reference line for line on all 22 scenes and on edited shortcuts (every label policy, wrapping, THEN / OR, custom keys), given the reference's own measurements; the errors are word for word |

## What it is

`keycaps` is a generator actor. It does not draw anything itself. The loader expands it into plain
actors (flattened groups and texts), in two phases:

1. **Phase A, `expand_block`, at load.** Structure and timing only: which keys exist, their ids,
   their `in`/`out` times. It needs no font, so the actor list never depends on how wide a label is.
2. **Phase B, `layout(scene, measure)`, once text can be measured.** Widths, x/y, wrapping, the
   position of separators and joiners. The only measurement is the ink box of a string
   (`getbbox(text, anchor='ls')`), the same primitive both renderers already have.

Editing the shortcut text re-runs both phases. Ids are `{block}.a{alt}c{chord}k{key}`
(`.plate`, `.shine`, `.text`, `.text2`), separators `{block}.a{alt}c{chord}p{key}`, joiners
`{block}.or{alt}` and `{block}.a{alt}then{chord}`.

## Shortcut grammar

| Written | Meaning |
|---|---|
| `Ctrl+Shift+S` | a chord: keys in one row joined by "+" |
| `Ctrl+K THEN Ctrl+C` | a sequence: chords one under the other, "THEN" between them |
| `Ctrl+Y OR Ctrl+Shift+Z` | alternatives: "OR" between them |
| `custom:Macro 7` | a key that is not in the dictionary, drawn as typed |
| `plus` | the literal plus key |

`THEN` and `OR` are case-insensitive words. Empty parts (`Ctrl+`, `+C`, `Ctrl++C`) are errors.

## Key identity

A key is an **identity** plus a **legend per platform**. The dictionary
(`keycaps/key-dictionary.json`, one file read by both renderers) lists each identity, its aliases and
its legends.

- `alt` has the aliases `option`, `opt`, `⌥`. It is **one** key: ALT on Windows, `⌥` with the name
  "Option / Alt" on a Mac. Option is the Alt key of a Mac keyboard.
- `command` (`cmd`, `⌘`) is a different identity and exists on macOS only. Writing it in a Windows
  block is an error that names the key. The generator never turns Command into Alt or Ctrl.
- `super` (`win`) is Windows only.
- `mod` is neutral: Ctrl on Windows, Command on macOS.
- Letters, digits and F1–F24 need no entry. Anything else unknown is an error, except `custom:`.

## Profiles and label policies

`platform` is `windows`, `macos`, or both (a "Universal" title is two blocks, one per platform,
each with its own tag label).

| Platform | `labelPolicy` | Legend |
|---|---|---|
| windows | `names` | CTRL, ALT, SHIFT, WIN, ESC |
| macos | `glyph` | `⌘ ⌥ ⇧ ⌃` |
| macos | `glyphPlusNames` | glyph on top, name below ("Option / Alt"), taller plate |
| macos | `names` | Command, Option, Shift, Control |
| macos | `physical` | the legend printed on the keycap; `legends` overrides per identity, others fall back to `names` |

Dual-label keys use `dualHeight`. Keys that have no glyph (Esc, Tab) stay single-label.

## Skins and automatic widths

A skin is data in the dictionary file. Each skin names its width model:

| Skin | Used by | Width model |
|---|---|---|
| `classic-pc` | 13_A, 13C Windows | `ink`: label ink + 90, minimum 114, even |
| `classic-mac` | 13C Mac | `inkMac`: minimum 122, dual label ink + 70, Spacebar 360, plus an overflow guard (a legend must clear the plate; it never fires on the proofs) |
| `smart-pc`, `smart-mac` | T01–T18 | `semantic`: a base width per identity, grown by the label ink + 64 or the dual label ink + 58 |

The Spacebar is a wide key in every model. Widths are always rounded to an even number.

## Flow, wrapping, 9:16

`flow` fields: `gap`, `plusDx`, `plusDy` (`"center"` or a number), `maxWidth`, `rowPitch`,
`chordPitch`, `orPitch`, `thenPitch`, `joinerDx`, `orDy`, `thenDy`.

When a chord is wider than `maxWidth`, it wraps. The "+" that would start a row is kept but hidden
(`_hidden`), so ids and timing do not shift. T16 is the 9:16 template (1080×1920, `maxWidth` 880).

## `lock`: approved hand-set geometry

13_A has hand-set widths (213, 250, 112) that no single formula gives. The block carries a `lock`
with the key identities and the approved x/w/separator boxes. It applies **only when the resolved
key list equals `lock.keys`**. Any other shortcut uses the automatic widths.

## Timing

```json
"timing": {
  "key":    { "start": 0.88, "keyStep": 0.17, "altStep": 0, "chordStep": 0,
              "in":  { "dur": 0.40, "dy": 14, "snap": "trunc", "ease": "…" },
              "out": { "start": 4.72, "revStep": 0.0, "dur": 0.30 } },
  "sep":    { "list": …, "rel": "left|right|mid", "off": 0.17, "in": {…}, "out": {…} },
  "joiner": { "orStart": …, "orStep": …, "thenStart": …, "thenStep": …, "dur": …, "dy": …,
              "outStart": …, "outDur": …, "font": …, "size": …, "color": … },
  "label":  { "start": …, "dur": …, "dy": …, "outStart": …, "outDur": … }
}
```

`snap: "trunc"` truncates the offset to whole pixels, as Stage13 A did. The expanded actors carry
plain numbers, so the renderer needs nothing keycap-specific for timing. Group fields added for the
generator: `ss` (supersampling of that group's sprite, 2 or 3) and `snap`.

## Templates T01–T18

`keycaps/template-catalog.json` defines the 18 templates (name, layout, aspect, eyebrow, title,
footer, variants per platform). `build_keycap_scenes.py` turns it into `scenes/T01_….scene.json` …
`T18_….scene.json`, all on the same actor. Their params are `eyebrow`, `title`, `footer` and
`shortcut` (or `shortcutWindows` / `shortcutMac`), plus `macLegends` where the template shows the
Mac legend options. Positions follow the 13F proofs, shifted for the engine's 2× sprite (13F drew
text at 3×): gap 48, plusDx 19, plusDy 32.

Two notes from the port:
- T06 uses the legend "Esc", as in the approved U3. The 13F proof printed "ESC".
- The three width formulas of the proofs (hand-set, ink + 90, semantic) are three skins. They were
  not unified, because that would redraw approved designs.

## Tests and commands

```
python3 -I tools/scene-ref/test_keycaps.py          # headless, no archive
python3 -I tools/scene-ref/test_parity.py --keycaps13 <Stage13 source> --keycaps13c <Stage13C source> \
        [--symbols <NotoSansSymbols2-Regular.ttf>]
python3 -I tools/scene-ref/build_keycap_scenes.py --receipts <84_EXACT_MEDIA_RECEIPTS.json>
```

`NotoSansSymbols2-Regular.ttf` (sha256 876598db…1688, `fonts-noto-core 20201225-2`) is in `scene-fonts/` and
pinned by the Mac scenes.

## Native

`src/core/scene/SceneKeycaps.{h,cpp}` mirrors `keycaps.py` function for function: `parseShortcut`,
`resolveKey`, `expandKeycaps` (phase A), `keyWidth` (`ink`, `inkMac`, `semantic`) and
`layoutKeycaps` (phase B: rows, wrapping with the hidden "+", THEN / OR joiners, the label, `lock`).
Where the Python reads `a.get(k, default)` the C++ does the same, and the arithmetic keeps the
reference's order and its `int()`, `ceil` and `//` steps. The messages are the same words: Command in
a Windows block is `keycaps: 'command' is a macOS key and has no place on a Windows block`.

- **Dictionary.** `tools/scene-ref/keycaps/key-dictionary.json` is compiled into the Qt resources as
  `:/keycaps/key-dictionary.json` (`CMakeLists.txt`, next to the scene GLOB). One file, two readers.
  The native reader refuses a dictionary where an alias is another key's id or belongs to two keys,
  because a `QJsonObject` has no file order to break such a tie the way Python does.
- **Loader (`SceneTitle.cpp`).** `keycaps` is an actor type. The generator's text slots
  (`shortcut`, `labelPolicy`, `orWord`, `thenWord`, `label.text`) take the params, then phase A runs
  and its actors are parsed like any others, without a font. The `Actor` gets `hidden` (`_hidden`),
  `ss` (a flattened group's supersampling) and `snapTrunc` (`snap: "trunc"`), and
  `Scene::keycaps` keeps what phase B needs.
- **Painter (`ScenePainter.cpp`).** Phase B runs in `SceneRenderer::build`, before the text sprites
  are made and next to `resolveConstraints`, so every param edit lays the keys out again. Its
  measurement is PIL's `getbbox(text, anchor='ls')` mirrored on Skia (`pilBBox`). That box is not the
  drawn ink. It runs from the pen origin to the advance (FreeType's `PIXEL()`), always includes the
  baseline, and counts each glyph by its grid-fitted control box (`FT_GLYPH_BBOX_PIXELS`) at its
  whole-pixel pen. Skia's FreeType glyph bounds are that same box (`FT_Outline_Get_CBox` of the
  hinted outline, rounded out, with the same `FT_LOAD_TARGET_NORMAL` hinting). Measuring coverage
  instead would make CTRL 186 px wide, not 188. The model was checked against PIL on all 638
  measurements the 22 scenes make, with HarfBuzz and FreeType directly: 0 differences. A
  flattened group's centred text uses the same box, as `render.py` centres by `textbbox`.
  A flattened group draws its children at its own `ss` (2, or 3 for the Mac and Smart skins), and
  its box shapes end `1/ss` px past `x1, y1`, as PIL's do at that scale. At `ss = 2` the sprite maths is
  the old code, unchanged. `snap: "trunc"` truncates the offsets (Stage13 A), and a hidden "+" is not
  drawn and has no rest box.
- **Compatibility mode.** There is no new shader. The keycaps are the same Skia `VectorPainter` pass
  as every Scene Title, on the GPU and on the CPU raster of low-spec mode (OpenGL < 3.3) alike. The
  sprites and the layout are built once per document and param values and cached, so a frame costs
  what any other Scene Title frame costs.
- **Catalog and library.** `SceneCatalog` no longer skips the 22 scenes. Titles › **Keyboard
  Shortcuts** lists them (T01–T18, with 13_A before T04, 13C U3 before T06, U1 before T08 and U2
  before T09), and Titles › Scenes keeps the other scenes (`TextAssetsTab.qml`).

### Native tests (`tests/tst_scenetitle.cpp`)

| Test | What | Data |
|---|---|---|
| `coreMatchesReference` (keycaps rows) | phase A, exactly: actors, texts, timing, channels | `core_values.py` → `tests/data/scene-titles/core_{13_A_default,13_A_relayout,13C_U1_default,13C_U2_default,13C_U3_default,T06_default,T11_default,T12_default,T16_wrap}.txt` |
| `keycapsGeometryMatchesReference` | phase B with the painter's measurement: sizes and flags exact, positions ≤ 0.5 px | `core_values.py --geometry` → `geo_*.txt`, same rows |
| `nativeMatchesReference` (13A, 13C U1/U2/U3) | actor SSIM ≥ 0.97 and centroid ≤ 0.5 px on frames 25, 40 (entrance), 75, 110 (hold) | `render.py render … --frames 25,40,75,110 --no-mp4` → `tests/data/scene-titles/<scene>/` |
| `keycapsRelayoutFollowsTheShortcut` | a `shortcut` param edit: the lock holds for Ctrl+Shift+S only (213, 250, 112), Ctrl+C gets 188, 116 | |
| `keycapsCommandOnWindowsIsAnError` | Command on Windows, Win on a Mac, `Ctrl++C`, an unknown key: the reference's messages | |
| `keycapsDictionaryAndGrammar` | `test_keycaps.py` Dictionary and Grammar, native | |
| `keycapsScenesLoadNatively` | `ss`, `snap`, a wrapped "+" hidden and out of the rest boxes, 22 scenes in the catalog | |

Regenerate the data after a reference change:

```
S=tools/scene-ref; D=tests/data/scene-titles
python3 -I $S/core_values.py $S $S/scenes/13_A_Shortcut_Keys.scene.json > $D/core_13_A_default.txt
python3 -I $S/core_values.py $S $S/scenes/13_A_Shortcut_Keys.scene.json --geometry > $D/geo_13_A_default.txt
python3 -I $S/render.py render $S/scenes/13_A_Shortcut_Keys.scene.json --out out/ --frames 25,40,75,110 --no-mp4
```

`--ink-table FILE` also writes every measurement phase B made, so a port can be compared with the
reference's own numbers without any font engine.

Status: IMPLEMENTED_UNTESTED until the CI's Tests on Linux runs `tst_scenetitle`. NATIVE_TESTED only
after that run passes. Approved only after the creator has seen a native export.
