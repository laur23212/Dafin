# Scene Title format, v1

A **Scene Title** is a title with several actors: texts, plates, lines, traced routes, circles. Each
actor has its own place, timing and editable fields. One JSON file describes it, and two renderers
read that file:

- `tools/scene-ref/render.py` is the **reference renderer** (Python: PIL + numpy + OpenCV). ChatGPT
  writes and checks titles with it, and it is the source of truth for every pixel of a ported
  design.
- the **native renderer** in Dafin (`src/engine/render2d/scene/ScenePainter.cpp`, loader
  `src/core/scene/SceneTitle.cpp`). It produces the same frames on native alpha layers, in one Skia
  pass per frame.

| Part | Status |
|---|---|
| Format v1 (this document) | written |
| Reference renderer | IMPLEMENTED, checked against the approved scripts: identical bit for bit, every frame of the two ports (see Validation) |
| Native renderer | NATIVE_TESTED headless: both ports pass the gate against their approved MP4s (see Native renderer) |
| Dafin editor integration (clip type, library, drop, CLI) | IMPLEMENTED_UNTESTED (built and run only in CI) |
| Creator approval of any native export | none yet |

The approved design is frozen. When the native output differs from the reference at default
values, that is an engine bug, not a design choice. A title is never shipped as a baked opaque MP4.

## File

```json
{
  "format": "dafin.scene-title", "version": 1,
  "id": "dafin.instructional.step-focus", "label": "Step Focus", "category": "Instructional Titles",
  "source":  { "asset": "15_A_Step_Focus", "sha256": "…", "script": "…", "status": "…" },
  "canvas":  { "width": 1920, "height": 1080 },
  "timing":  { "duration": 6.0, "fps": 25, "cadence": "match" },
  "render":  { "profile": "sprite2x", "mixColor": "DARK" },
  "preview": { "background": { "kind": "base" }, "encode": { "preset": "veryfast", "crf": 18 } },
  "palette": { "AQUA": [123, 209, 192], "STRAP": [20, 38, 48, 239] },
  "fonts":   { "semi": { "family": "Noto Sans Display", "weight": 600, "file": "NotoSansDisplay-SemiBold.ttf",
                         "package": "fonts-noto-extra 20201225-2", "sha256": "…" } },
  "params":  [ { "id": "currentStep", "type": "int", "default": 2, "min": 1, "max": "totalSteps", "clamp": true } ],
  "actors":  [ … ]
}
```

Rules for the top-level keys:
- Unknown keys are ignored.
- Missing `format`, `version`, `id`, `canvas`, `timing` or `actors` is an error.
- `source` ties a port to its approved MP4 (the asset id and sha256 from
  `docs/01_CONSOLIDATED_84_ASSETS_PROVENANCE_AND_IMPLEMENTATION.csv`) and to the script it was
  ported from.

### Fonts

A scene names font **files** and never bundles them. Fonts are looked up in this order:
1. `--font-dir`
2. the system font folders
3. the repo's `title-fonts/`

When `sha256` is given, the renderer must find that exact file or stop with an error.

This matters because files with the same name can be different cuts:
- `title-fonts/noto-serif/NotoSerif-Regular.ttf` (cut from the variable font) is not the
  `fonts-noto-core` file the Stage15 proofs used. With it, the "02" of Step Focus comes out
  different.
- `title-fonts/` has no Noto Sans Display at all.

So the native port must ship the proofs' exact files. This is a **native gap**.

| Proof family | Files | Package (Debian/Ubuntu) |
|---|---|---|
| Stage13/14/15 | NotoSansDisplay-Regular/SemiBold/Bold, NotoSerif-Regular | fonts-noto-core / fonts-noto-extra 20201225 |
| Stage03b | InterDisplay-SemiBold.otf, Inter-Regular.otf, Inter-Medium.otf | fonts-inter 4.0 |
| Stage10 | NotoSansDisplay-Light/Black/Medium, NotoSerifDisplay-Medium | fonts-noto-core / -extra |

## Canvas and coordinates

The design canvas is **1920×1080 px**, with the origin at the top left and y pointing down. Every
coordinate in a scene is in design pixels. The native renderer scales the whole scene to the
project size: for 16:9 the scale factor is `projectHeight / 1080`. Other aspect ratios need their
own approved layout. They are not re-flowed automatically in v1.

### Text anchors

A text's position is written as `at`, in one of three forms. Ports use the canonical form
(`baseline`), so that editing the text never moves its line.

| `at` | Meaning | Used by |
|---|---|---|
| `{"baseline": [x, y]}` | **Canonical.** `x` = pen origin of the first glyph; `y` = baseline. Independent of the string. | all ports, native |
| `{"sprite": [x, y]}` | Legacy sprite2x: top-left of the PIL sprite. | conversion only |
| `{"lt": [x, y]}` | Legacy mask1x: PIL anchor `lt` (left, top of the *ink* of this string). | conversion only |

Both legacy anchors depend on the string itself. In the scripts, typing a name without Ș/Ț moved
its baseline, and the canonical anchor fixes that. The formulas below convert between the forms
exactly. `render.py anchor --profile … --font … --size … --text … --at X Y` does the conversion.

- **sprite2x.** The scripts draw the text at 2× size into a sprite that has 7 px of padding on the
  left and 8 px on top (at 1×), then reduce it with Lanczos. Here `box` is
  `font2x.getbbox(text)` and `ascent2x` is `font2x.getmetrics()[0]`, with the font loaded at 2× the
  size.
  `pen_x = sprite_x + (14 − box[0]) / 2`
  `baseline_y = sprite_y + (16 − box[1] + ascent2x) / 2`
  The result is a multiple of ½ px. When an edited text lands on a half pixel, the reference snaps
  the sprite to whole pixels with round-half-even, like the scripts did. Native should place the
  text at the exact sub-pixel.
- **mask1x.** `baseline_y = lt_y + (bboxLT[1] − bboxLS[1])`, where both boxes are
  `font.getbbox(text, anchor=…)` with RAQM layout. x is the same in both forms.

### Shape boxes

`box: [x0, y0, x1, y1]` uses PIL's **inclusive** corners:
- a `rect` covers the whole pixels x0…x1;
- shapes drawn at 2× (`roundRect`, `ellipse`, `ring`) cover x0 … x1+½.

Lines use `from`/`to` points on integer pixels. A route uses `points`.

## Timing

- `timing.duration` is the design length. It defaults to 6 s (150 frames at 25 fps).
- `timing.fps` is the fps the design was proofed at.
- `timing.cadence` is how time is sampled:
  - `"match"` (default): time is continuous and sampled at the project fps;
  - `25`, `30` or `60`: the motion steps at that rate inside a faster project, using
    `t = floor(t·c)/c`.
  A separate 60 fps proof (Stage03b had them) is a separate approved scene, not a cadence setting.

### Per-actor timing

```json
"in":  { "start": 0.18, "dur": 0.35, "ease": "smoothstep", "from": { "opacity": 0, "dy": 9 } },
"out": { "start": 4.68, "dur": 0.33, "ease": "smoothstep", "to":   { "opacity": 0 } }
```

The two blocks are combined like this:
- `pin = ease_in((t − in.start) / in.dur)` and `pout = ease_out((t − out.start) / out.dur)`, both
  clamped to 0…1.
- **Reveal channels** (`opacity`, `wipe`, `progress`, `scale`) multiply:
  `value = (from + (1 − from)·pin) · (1 − (1 − to)·pout)`.
  With `from` = 0 and `to` = 0 this is the scripts' `enter·(1 − exit)`.
- **Offset channels** (`dx`, `dy`) add: `value = from·(1 − pin) + to·pout`. The scripts animate
  offsets on the way in only.
- `in.from` defaults to `{"opacity": 0}` and `out.to` defaults to `{"opacity": 0}`. Leaving out a
  block means the actor is at rest on that side.
- `wipe` reveals a sprite or a rectangle from the left. `progress` draws a line or a route from its
  start point.

Easing names:

| `ease` | Curve | Dafin keyframe (`src/core/animation`) |
|---|---|---|
| `smoothstep` (default) | 3u² − 2u³ | exactly `cubic-bezier(1/3, 0, 2/3, 1)` |
| `easeOutCubic` | 1 − (1−u)³ | exactly `cubic-bezier(1/3, 1, 2/3, 1)` |
| `easeOutQuart` | 1 − (1−u)⁴ | **no exact cubic bezier.** `EasingFn` only has Elastic/Bounce, so native needs a polynomial ease function |
| `smootherstep` | 6u⁵ − 15u⁴ + 10u³ (Stage03b C revision) | no exact cubic bezier; native has the polynomial |
| `linear` | u | linear |
| `cubic-bezier(x1,y1,x2,y2)` | Dafin's own curve | same bisection as `core/base/Bezier.h` |

The first two rows are exact because Dafin's segments evaluate `cubicBezier` with x handles at 1/3
and 2/3, which makes x(s) = s. The tests check this to 1e-6.

### Stretching to the clip length

- **Entrances stay anchored to the clip start and exits to the clip end. The hold in between is
  elastic.**
- The hold runs from `hs` (the latest `in.start + in.dur`) to `he` (the earliest `out.start`). A
  scene with `he < hs` is an error.
- For a clip of length D, with `tail = duration − he`:
  - while `t ≤ hs`, design time equals t;
  - from `t ≥ D − tail` onwards, design time is `he + (t − (D − tail))`;
  - between the two, the actor is at rest.
- If D is shorter than `hs + tail` (no hold left), the hold-less timeline is scaled uniformly to D.
- During the hold every actor must be static. `render.py validate` checks this.

## Actors

Draw order is the order of the `actors` array.

Fields common to every actor:

| Field | Meaning |
|---|---|
| `id` | Unique, semantic (`title`, `strapMain`, `segment{i}`). Saved projects and inspector fields refer to it. |
| `role` | What the actor is for (`title`, `eyebrow`, `carrier`, `accent`, `progressSegment`…), for the inspector and for templates that share an engine |
| `type` | `text` `rect` `roundRect` `line` `route` `ellipse` `ring` `group` `repeat` |
| `optional` | May be switched off (`enabled: false`, or an expression). Switching off an actor that is not optional is an error. |
| `dependsOn` | Actors this one measures or clips against. They must exist. Native: they are updated together, in one undo step. |
| `sync` | Copy `in`/`out` from another actor (for example, a text that must never outlive its strap) |
| `in`, `out` | Timing, as above |
| `align` | Static constraint, see Constraints |
| `offsetSnap` | `"round"` (default, half to even) or `"trunc"`: how `dx`/`dy` snap to whole pixels. The Stage13 keycaps moved by `int(18·(1−p))`, which is `trunc`. |

Fields of each type:

| `type` | Fields |
|---|---|
| `text` | `text` (may hold `{param}` slots), `font` (key into `fonts`), `size` (px at 1×), `color`, `at`, `letterSpacing` (mask1x), `clipTo`, `clipRect` (mask1x) |
| `rect` | `box`, `color` (RGBA alpha allowed), `fitTo` (`max` caps the fitted width), `widthOf`, `wipeDir` (`"right"` default, `"down"` grows the height: the Stage03b C stem) |
| `roundRect` | `box`, `radius`, `fill`, `strokeColor`, `stroke` |
| `line` | `from`, `to`, `width`, `color`, `opacityMode` (`"mixWith"`: see profiles) |
| `route` | `points` (polyline traced by `progress`), `width`, `color` |
| `ellipse` | `box`, `fill`, `strokeColor`, `stroke` |
| `ring` | `box`, `color`, `stroke` (outline only) |
| `group` | `at` (offset), `children`. Plain groups pass their opacity, offset, wipe and progress on to their children. With `flatten: true` and `size: [w, h]`, the children are drawn into **one** 2× sprite, the way a keycap is a plate plus centred text (`align: {"h":"center","v":"center","dy":-2}`). Children of a flattened group cannot animate on their own. `supersample` (default 2) is the factor the group is drawn at before the Lanczos reduction: the Stage13C/14 keys and menu tags were drawn at 3×. A child text may also use `align: {"h":"center","v":"top","top":15}` (its ink top at that group y), as the Stage13C Mac keys stack a symbol over its name. |
| `repeat` | `count` (number or expression), `as` (variable name, default `i`), `actor` (a template whose fields may be `{"expr": …}`, with an id like `segment{i}`) |

## Constraints

- **`fitTo`** (rect): `{"actor": "nameText", "pad": 45, "min": 590}`. The rect's width becomes
  `max(min, inkRight(target) − box.x0 + pad)`. In other words, the strap grows to the other
  actor's ink width plus padding, measured from the strap's own left edge. The ink is the text
  mask at rest. A longer name widens the strap, while a short name keeps the approved minimum.
- **`widthOf`** (rect): `["strapMain", "strapRole"]` means the width is the minimum of those
  actors' rest widths (the Stage03b seam).
- **`clipTo`** (text): `{"actor": "strapMain", "inset": [l, t, r, b]}`. The text is visible only
  inside the target's *current* box, shrunk by the inset, so it follows the strap's wipe. An empty
  clip hides the text.
- **`clipRect`** (mask1x text): `{"box": [x0, y0, x1, y1], "rise": 85, "riseSnap": "trunc"}`. A fixed
  window (PIL-inclusive corners) whose top sits `snap((1 − p)·rise)` below `y0`, with p the actor's
  entrance phase, so the text is revealed upwards and the window stays open through the exit
  (Stage03b A and C v1). `riseSnap` is `"trunc"` (int(), default) or `"round"` (half to even).
- **`align`**: `{"x": {"to": "title", "edge": "start", "offset": 3}, "y": {"to": "title", "edge": "baseline", "offset": 78.5}}`.
  The actor moves so that its own edge of the same kind (`ownEdge` overrides which one) lands on
  the target's edge plus the offset.
  - Text edges: `start`, `baseline`.
  - Shape edges: `left`, `right`, `top`, `bottom`.
  - Alignment is static (rest geometry). Cycles are an error.

## Params and bindings

`params` are the creator-intended fields of the inspector. Each one has `id`, `type`, `default`,
`label` and optional `min`, `max` and `clamp`.

| Type | Values |
|---|---|
| `text` | a string |
| `int` | integers only (`2.5` is refused) |
| `number` | any number |
| `bool` | true / false |
| `color` | a colour |
| `enum` | one of `options` |

- `min` and `max` may be expressions that refer to other params (`"max": "totalSteps"`).
- Out of range is an error unless `clamp` is set.
- Overriding an unknown param is an error.

Params reach the actors in two ways:
- **Text slots:** `"STEP {currentStep:02}  /  {totalSteps:02}"` uses Python format specs. `{{` and
  `}}` are literal braces.
- **Expressions:** `{"expr": "AQUA if i < currentStep else FAINT2"}` can stand anywhere a number,
  colour or string is expected. The allowed syntax is a small safe subset of Python: numbers,
  names (params, palette entries, repeat variables), `+ − * / // % **`, comparisons,
  `and/or/not`, `a if c else b`, and `min max abs round int float floor ceil`. Nothing else
  parses.

**Native mapping.** A param is the Scene counterpart of `TemplateControl` in `core/text/TextTemplate.h`.
`bind` paths become actor-id paths (`actor.<id>.text`, `actor.<id>.color`…), so reordering actors
never re-targets a control. One param drives every slot that uses it, which keeps one source of
truth. For example, Step Focus updates the label, the number and the segments together.

## Render profiles

The approved scripts drew in two different ways. The profile selects which maths the reference
renderer copies, so a port matches its proof bit for bit. The native engine reproduces the
*result* of the profile at default values. It does not have to copy the quirks, but any difference
from the reference must be measured and documented with `compare.py`.

### sprite2x (Stage13/14/15)

Source: `frozen_stage13_approved_renderer_reference.py`.
- Text is a sprite drawn at 2× with PIL (no RAQM) and reduced with Lanczos.
- `roundRect`, `ellipse` and `ring` are 2× sprites. `roundRect` has 2/3 px of sprite padding;
  `ellipse` and `ring` have 8 px.
- `rect` is a 1× solid sprite (`slim_bar`).
- Sprites are laid on an **opaque RGB frame**: `uint8(clip(dst·(1−a·o) + src·a·o))`, which
  truncates instead of rounding. Offsets are rounded half to even.
- Lines and routes are `cv2.line(…, LINE_AA)` drawn straight into the frame. `progress` shortens
  them from their start.
- **Line opacity**: an opaque frame has no alpha, so the Stage15 divider fakes opacity by mixing
  its colour with `render.mixColor` (DARK). The alpha PNG output uses real alpha instead. Over BASE,
  the two differ by ≤ 3 levels (this is tested).

### mask1x (Stage03b)

Source: `Stage03b_Alternative_LowerThird_Families_v1/source/render.py`.
- Text is a full-size 8-bit mask drawn at 1× with RAQM and anchor `lt`, then moved with a bicubic
  affine (sub-pixel offsets are allowed).
- The mask is cut by `clipTo` rectangles and scaled by opacity through a `round(p·α)` table.
- The result is `alpha_composite`d on an **RGBA** canvas.
- Rectangles are written straight into the canvas: later shapes *replace* earlier ones, alpha
  included. This is how the coral seam overwrites both straps.

### pil1x (Part5 Stage05–08)

Source: `render_stage05.py` and its siblings.
- Text is a 1× PIL sprite (basic layout) with 8 px of padding, composited at `round(x + dx)`.
  `reveal: "x"` (from the left) or `"y"` (from the bottom) cuts the sprite by the actor's `wipe`
  (`0 … round(w·v) − 1`, inclusive); opacity is a `round(α·p)` table, clamped to 0…255.
- Every shape is drawn on its own transparent full-frame pane, then `alpha_composite`d, in order.
  A `group` with `pane: true` draws its children into one pane (a leader line and its dots).
  Rects take `x1 = x0 + w·wipe` (`wipeSnap: "int1"`: `x0 + max(1, int(w·wipe))`); `vscale` trims a
  rect or a line equally at both ends (`collapse: "ends"` on a line trims its rest length; otherwise
  `progress` grows it from `from` and `vscale` collapses that to its centre). Lines are PIL lines
  (no anti-aliasing). A `route` may give `segmentWeights` (shares of progress per segment). An
  `ellipse`/`ring` may be `center` + `radius`, the radius being `round(radius·scale)`.
- `visibleWhen: [{"actor": id, "channel": "wipe", "gt": 0.01}]` draws an actor only past a
  threshold, as the scripts' `if p > .01:` did (`actor` defaults to itself; groups apply to
  their children).
- Scene `exitWipe: {"start", "dur", "ease", "left", "right"}` closes the whole title: everything
  right of `round(right − (right − left)·p)` is removed; at p = 1 nothing is left. It counts as an
  exit for the hold.
- Stage06 settings (`render`): `minOpacity` 0.002, `minReveal` 0.002, `fullOpacity` 0.999,
  `alphaTable: "trunc"` (`int(a·p)`), `skipEmptyRects` (its `pane()` drew nothing when x1 ≤ x0).
  `wipeSnap: "round"` (`x0 + round(w·p)`); a line with `progressSnap: "round"` ends at
  `base + round((to.x − base)·p)` (`progressBase`, default `from.x`).
- `pin` (Stage06 Location): `center`, `radius`, `stem`, `dotRadius`, `stroke`, `ringColor`,
  `dotColor`, `stemColor`. Ring radius `round(radius·scale)`, a fixed dot, a stem of
  `round(stem·scale)` below the ring: one editable motif.
- A text `counter: {"start", "dur", "ease", "to"}` shows `str(round(to·phase))` (Stage06 Metric),
  with `at: {"right": x, "top": y}` keeping the sprite's right edge fixed.
- Scene `exitSlide: {"start", "dur", "ease", "rect": [x, y, w, h], "distance", "hold"}`: Stage06's
  rigid exit. The title as it stood at `hold`, cropped to `rect`, slides down by
  `round(distance·p)`, cut at the frame's bottom edge; at p = 1 nothing is left.
- Stage07 settings: `skipCompare: "lt"`, `revealMode: "crop"` (the sprite cut to
  `max(1, int(w·v))` px while `v < fullReveal`), `fullReveal` 0.999. `layer: "base"` actors (the
  frame's matte, its fades and hairlines) are drawn first and are never wiped; `draw: "direct"`
  writes straight on the frame as the scripts' `ImageDraw.Draw(im)` did, which the RGB proofs show
  opaque, so the alpha layer keeps them opaque. A rect with `strokeColor` and no `color` is an
  outline. `exitWipe.mode`: `"right"` keeps x ≤ round(W·(1−q)), `"left"` keeps x ≥ round(W·q),
  `"top"` keeps y ≥ round(H·q); the default `"span"` is Stage05's `left`/`right` form.
- Stage08 Cinematic: a text with `tracking` is laid out glyph by glyph at `round(pen)`, `pen +=
  advance + tracking` (the "advance-rounded" layout), on a sprite `ceil(Σadvances + tracking·(n−1)) + 16`
  wide; `at: {"center": [x, y]}` centres that sprite (`int`, `int(round(…+dy))`); `wipe` crops it to
  `int(w·v)` px; `composite: "numpy"` reproduces the script's `trunc(t·(1−a)) + round(c·a)` blend in
  the reference's direct mode. Ease `easeInCubic` (u³).
- `sprite: "chapter"` (Dark Chapter A v2): glyphs at float pens, `+ getlength(ch) + letterSpacing`,
  4 px of padding; `wipe` crops `round(w·v)` px of that sprite. A `roundRect` may have `fill` and
  `strokeColor`/`stroke` (one pane, the hairline over the fill).
- Stage09: `at: {"left": x, "top": y}` places the sprite's top-left corner; `spritePad` (default 8)
  is its padding (text_layer(): 11). `offsetSnap: "trunc"` moves by int(dx). `clipRect: {"box":
  [x0, y0, x1, y1]}` crops the sprite to a canvas rect (x1, y1 exclusive; mask1x's clipRect is
  PIL-inclusive and rises). A group with `row: {"gap": 33, "width": 1920, "baseline": 556}` sets
  its texts' pens getlength + gap apart, the run centred (A_Crisp word relay); each word keeps its
  own colour param. A `rect` with `wipeDir: "down"` and `wipeSnap: "round"` ends at
  `y0 + round(h·wipe)` (the B guide bar). The chapter sprite follows the render opacity rules.
- A block may run on `"clock": {"actor": id, "channel": "wipe"}` instead of time: Stage07's answer
  tag eases on `(blockReveal − 0.55) / 0.45`.
- `in` and `out` may be arrays of blocks: one curve per channel group (a reveal on one curve, a
  slide on another). Reveal channels multiply across blocks; offsets add.

### cov1x (Chapter A v4 / B v4, RECONSTRUCTED_APPROX)

The original v4 scripts are SOURCE_MISSING; the reference is `dafin_chapters_v4_recon.py`, rebuilt
from measurements of the approved MP4s (`claude/chapters-v4-reconstruction.md` in the project).
Every actor is a float coverage map over the frame, composited in order as
`colour·(1 − m) + ink·m` with `m = coverage · alpha · group opacity · own fade · wipe`.

- `render.supersample` (1–4; an actor may override it with `supersample`): text is laid out at
  that factor and BOX-reduced. Layout is advance-rounded: each glyph at `(round(pen), y)` with
  PIL's anchor `la`, `at: {"ascender": [x, y]}` the ascender line, `pen += getlength(ch) + letterSpacing`.
- `rect` / `roundRect` (`fill`): the inclusive pixel box drawn at 4× and BOX-reduced.
- `alpha`: a constant opacity. `wipeRange: [x0, x1]` with a `wipe` channel: a crop edge moving
  from x0 to x1 with a 1 px anti-aliased edge, so a wiped box is cropped, not re-rounded.
- `clipTo: {"actor": id}`: the actor only shows where that actor's box has coverage and its wipe
  has passed (the B tag text in its tag, the subtitle in its panel).

## Preview and matte

- `preview.background` is the matte the proof was judged on. It is used only for previews and
  comparisons; the title itself always has alpha.
  - `{"kind": "base"}` is the Stage13+ BASE gradient: DARK + 7.2·x + 3.2·(1−y).
  - `{"kind": "video", "file": …, "overlays": [png…]}` is the Stage03b photo pan plus scrim, found
    with `--assets`. These files are not in the repo.
- `preview.encode` holds the proof's x264 preset and CRF, so that re-encoding the reference compares
  like with like.
- `preview.legacy.overlayRect` exists only to reproduce a proof that overlaid a smaller layer:
  ffmpeg's 4:2:0 overlay rounds chroma by layer position. Native ignores it.

## Reserved effects (Stage10)

`effect.type` values `sdfMorph`, `maskedLight` and `focusMix` are reserved. v1 loaders must refuse
them ("ask for the extension") rather than fake them. Their mechanisms, from `render_stage10.py`:

- **sdfMorph** (Typography Morph): a real weight morph, not a crossfade.
  - Every glyph is drawn in two weights (Light and Black) into a cell, on the same baseline.
  - Each is turned into a signed distance field: `edt(inside) − edt(outside)`.
  - The two fields are interpolated by `shape`, then `alpha = clip((sdf + 0.8) / 1.6)`.
  - Advances are interpolated per glyph, tracking goes from 17 to 3.2 px, and the word stays
    centred.
  - Timing: `shape = morph·(1 − leave) + 0.08·leave`.
- **maskedLight** (Masked Light Reveal):
  - Visibility is `glyphMask · clip((beam − x) / 118 + 0.52)`, with the beam sweeping x over an
    eased 2.1 s.
  - The ink is warmed by a Gaussian (σ 165 px) around the beam.
  - The background gains a halo: the glyph mask blurred with σ 20, times a Gaussian (σ 210) around
    the beam, adding (24, 16, 8). The light never leaves glyph-derived areas, so there are no
    rectangles.
- **focusMix** (Precision Focus): per word.
  - `alpha = mix(blurred(σ 2.1), sharp, f)·(0.57 + 0.43 f)`, and the colour is mixed from soft
    grey to ink by `f`.
  - Focus travels from word 1 to word 2 and then both resolve. The blur is per word, never a
    blurred image of the whole title.

## Validation

- `tools/scene-ref/test_parity.py`: a port against its original script, frame by frame and before
  any encoding. Results:

  | Port | Result |
  |---|---|
  | 15_A_Step_Focus | 150/150 frames identical |
  | 03b_LT_B_25 | 150/150 frames identical |
  | 15_B_Definition_Card, 15_C_Pro_Tip | 150/150 frames identical |
  | 14_A_Menu_Path, 14_B_Setting_Change | 150/150 frames identical |
  | 13_A_Shortcut_Keys, 13_B_Source_Credit, 13_C_Editorial_Checklist | 150/150 frames identical |
  | 13C_U1/U2/U3 (Universal and macOS shortcuts) | 150/150 frames identical |
  | 03b_LT_A_25, 03b_LT_C_25_REV | 150/150 frames identical |
  | 03b_LT_B_60, 03b_LT_C_60_REV | 360/360 frames identical |
  | 05_TutorialStep, 05_PrecisionCallout, 05_PullQuote (pil1x, over a test plate: the proofs' photo is missing) | 150/150 frames identical |
  | 06_Metric, 06_Location (pil1x, over a test plate) | 150/150 frames identical |
  | 07_Kinetic, 07_Insights_v2, 07_QA_v3 (pil1x, RGB over the scripts' footage crops) | 150/150 frames identical |
  | 08_Cinematic_Garamond / Inter / RobotoSlab / Lato (pil1x over the script's charcoal) | 150/150 frames identical |
  | 04_Dark_Chapter_A_v2 (pil1x over the script's backdrop) | 150/150 frames identical |
  | 09_WordRelay_A1, 09_SplitMotion_C1, 09_WordShift_B7 (pil1x over the scripts' plates; C's photo is a test plate) | 150/150 frames identical |
  | 04_Chapter_A_v4, 04_Chapter_B_v4 (cov1x against the reconstruction, RGB and alpha) | 150/150 frames identical |
  | 03b_LT_A_60 | 358/360 identical; frames 40 and 61 differ by 1 level on 1 pixel (Pillow's bicubic shift of a full-canvas mask vs the script's 1100×250 layer) |

  The Stage13 keycap, the check ring, the Stage15 plate and the route are also identical to the
  frozen helpers.
- `compare.py`: a render against the approved MP4, on the per-actor regions. It reports SSIM, PSNR,
  the largest difference and the position (phase correlation), and writes a CSV, a JSON summary and
  a contact sheet.
- **Environment floor.** Running the *original* scripts on this machine and re-encoding them gives
  the same numbers as the port. What is left is FreeType/x264 version noise at glyph edges:

  | Frame | 03b_LT_B_25 PSNR | 15_A_Step_Focus PSNR |
  |---|---|---|
  | 0 | ∞ | ∞ |
  | 10 | ∞ | 74.2 |
  | 20 | 66.3 | 64.9 |
  | 40 | 68.0 | 56.3 |
  | 65 | 68.5 | 56.4 |
  | 90 | 68.8 | 56.4 |
  | 115 | 68.9 | 56.4 |
  | 135 | 69.0 | 78.0 |
  | 149 | 67.5 | 78.0 |

  Whole-frame SSIM ≥ 0.9998. Lowest actor-region SSIM: 0.9987 (03b) and 0.9967 (15A). Largest
  position error: 0.006 px and 0.16 px.
- **Thresholds**, calibrated with `calibrate.py`: the faithful render must pass and deliberate
  errors must fail.

  | Case | min actor SSIM (15A / 03b) | max position error px (15A / 03b) |
  |---|---|---|
  | faithful | 0.9967 / 0.9987 | 0.16 / 0.006 |
  | one actor +1 px | 0.885 / 0.876 | 1.01 / 1.00 |
  | one actor +2 px | 0.776 / 0.749 | 2.03 / 2.00 |
  | wrong font weight | 0.637 / 0.653 | 19.3 / 5.8 |
  | all eases linear | 0.910 / 0.420 | 1.05 / 243 |
  | entrances 2 frames late | 0.780 / 0.704 | 4.04 / 119 |

  **Gate: actor SSIM ≥ 0.97 and position ≤ 0.5 px.** The proposed ±1 px would let a whole-pixel
  shift through: it measures 1.00–1.01 px. SSIM alone already catches it.

## Native notes and open points

- **Gaps found:**
  - an easeOutQuart function (polynomial easing);
  - the proofs' exact font files (see Fonts);
  - a sprite-at-2× text path, or proof that Skia's text at 1× passes the gate;
  - a replace-mode rect for mask1x seams, or an equivalent draw order.
- **Weak PCs (OpenGL < 3.3).** Every v1 primitive is plain 2D: text, rectangles, AA lines, circles.
  A CPU raster path (Skia CPU / QPainter) draws all of them, so compatibility mode needs no new
  shader and fast PCs keep the GPU path. The reserved Stage10 effects need per-glyph masks. They
  can be precomputed on the CPU once per text edit.
- **Step Focus**, two open points:
  - `smart_progress_reference.py` builds the label as `STEP 02 / 05` with single spaces, while the
    approved frame has `STEP 02  /  05` with two spaces on each side. The port keeps the approved
    frame.
  - The bar layout for `totalSteps ≠ 5` was never designed. The port keeps the 154 px pitch, so the
    row grows or shrinks. **Creator decision needed** before this is called done.
- Statuses: both ports are NATIVE_TESTED headless (below). They count as approved only after the
  creator reviews a native export from Dafin.

## Native renderer

- **Clip.** A Scene Title is one clip, `ClipType::Scene`, on a Graphic track. The clip embeds the
  scene file and the changed param values (`SceneSource`), so a project opens anywhere and never
  changes when the library's copy is updated. Project format 24.
- **Drawing.** One `VectorPainter` per frame. It draws on a premultiplied, transparent layer, so
  there is no opaque rectangle, and it works the same on the GPU and on the CPU raster (the
  low-spec path; no shader). Fonts, text sprites and masks, and fitted widths are built once per
  document + params. A frame keeps the same cache key as long as no channel moves, so the hold is
  drawn once.
- **Text, both profiles.** HarfBuzz shaping with the font's unhinted (fractional) advances, each
  glyph on a whole pixel, the outline hinted, as PIL does. sprite2x has kerning on and ligatures
  off (PIL's basic layout reads GPOS pair kerning through FreeType). mask1x has HarfBuzz's default
  features (RAQM). sprite2x draws at 2× and reduces with Pillow's exact Lanczos (premultiplied,
  fixed point), so the 2× grid sits where the proof's did.
- **Lines.** cv2's `LINE_AA` is emulated as measured. A width t ≥ 2 covers 2·⌈t/2⌉+1 whole pixels
  plus a 0.22 px ramp on each side; t = 1 covers 1.35 px. Ends are round, and about 0.25 px shorter
  on vertical lines.
- **Fonts.** The exact files ship in `scene-fonts/` (OFL), apart from `title-fonts/`.
- **Timing.** `scene::designTime` maps clip time to design time: cadence first, then stretch.
- **Measured** with `dafin --render-scene <scene> --frames all --out n/` and
  `tools/scene-ref/native_compare.py --native n/ --ref <approved mp4>`:

  | Port | min actor SSIM | max position error |
  |---|---|---|
  | 15_A_Step_Focus | 0.9787 | 0.458 px |
  | 03b_LT_B_25 | 0.9913 | 0.111 px |

- **CI.** `tests/tst_scenetitle.cpp` checks three things:
  - the loader against `scene_core.py`, value for value (`tools/scene-ref/core_values.py`);
  - the native frames against the reference frames in `tests/data/scene-titles/`, at the gate;
  - fitTo, cadence, saving, and the loader's refusals.
- **Still open:**
  - the inspector panel for the params (C6). The API is ready: `sceneParams`, `setSceneParam`, one
    undo step each;
  - the rounded shapes, which no port uses yet. They are drawn as Skia vectors, not measured
    against PIL;
  - other aspect ratios, which are fitted by height and centred.
