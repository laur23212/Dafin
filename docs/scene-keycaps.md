# Smart Keycaps (Scene Title actor `keycaps`)

One generator draws every keyboard-shortcut title. It replaces the hand-built actors of the approved
static titles (`13_A_Shortcut_Keys`, `13C_U1/U2/U3`) and of the 18 Stage13F templates T01–T18
(Titles › Keyboard Shortcuts). The approved design is frozen: at default values the generator
reproduces the approved frames, and any difference is an engine bug.

| Part | Status |
|---|---|
| Key dictionary, grammar, profiles, widths, layout (`tools/scene-ref/keycaps.py`, `keycaps/key-dictionary.json`) | IMPLEMENTED, tested headless (`test_keycaps.py`, 26 tests) |
| Parity with the approved scripts, 150 frames each | 13_A: identical bit for bit. 13C U1/U2/U3: identical with a stand-in symbol font (mechanics only). **Real-font run of the Mac keys: NOT_RUN** until `NotoSansSymbols2-Regular.ttf` is supplied (`test_parity.py --symbols`) |
| 18 templates T01–T18 (`scenes/T*.scene.json`) | generated and rendered; **the creator has not approved their visuals** |
| Native C++ mirror | NOT_RUN (see "Native" at the end) |

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

`NotoSansSymbols2-Regular.ttf` comes with `fonts-noto-core` (`/usr/share/fonts/truetype/noto/`),
which the Linux CI image installs. Its sha256 is not recorded yet: take it from the proof package and
add it to `scene-fonts/README.md` before any scene pins it.

## Native (not done)

Planned: `src/core/scene/SceneKeycaps.{h,cpp}` mirrors `keycaps.py` (same dictionary, read from the
Qt resources; same two phases; layout in the painter next to `resolveConstraints`, with the
painter's own ink measurement). The loader gets the actor type and the `hidden`, `ss`, `snap`
fields. Until then the keycap scenes are in the catalog but do not load natively, and the value
tests (`core_values.py` dumps, `tests/tst_scenetitle.cpp`) are not written.
