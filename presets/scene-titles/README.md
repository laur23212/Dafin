# Scene Title library

Scene Title files (`docs/scene-titles.md`) that ship with Dafin. Statuses are honest: nothing here is
creator-approved until a native export has been reviewed.

| File | Category | Status |
|---|---|---|
| `04_Chapter_C_reconstructed.scene.json` | Chapter Titles | RECONSTRUCTED_APPROX. The original script is lost (`SOURCE_MISSING`, ChatGPT: `NU_MAI_EXISTA`) and so is the font (`FONT_UNKNOWN`). Rebuilt from the approved MP4 `04_Chapter_C` (sha256 b344a562…ded46f). Native render: NOT_RUN. |

## 04_Chapter_C_reconstructed

- Reference renderer vs the approved MP4, over an *estimated* photo plate (research input, not shipped):
  whole frame SSIM 0.976 on the hold frames; title line 1 0.979 / 0.51 px, line 2 0.948 / 0.04 px,
  label 0.78, subtitle 0.75, rule 0.79 (1.5 px). Small text and the rule are below the 0.97 gate.
- Fonts are substitutes (see `scene-fonts/README.md`). Position and timing come from measurements of the MP4,
  not from recovered code. Texts are left-anchored, so a longer text does not re-centre.
- The zoom of the photo behind the title (1.0 → 1.059, smoothstep) belongs to the footage, not to this title.
- Reference renderer limit: `letterSpacing` draws glyph by glyph and drops precomposed accents (Î, Ă, ș) off the
  line, so the two accented texts use no letterSpacing and a fitted size instead. Native shaping is not affected.
