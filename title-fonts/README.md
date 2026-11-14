# Title fonts

The fonts the ChatGPT title templates (`presets/text-styles/`, ids `chatgpt.*`) draw with that the
Fonts Essentials add-on does not have. The build copies this folder to `<appDir>/fonts`
(`Contents/Resources/fonts` on macOS), which `FontCatalog` searches; the installers ship it.
It is not the repo's `/fonts`, which is the fetched Essentials bundle the tests read.

Every family is from [google/fonts](https://github.com/google/fonts) under the SIL Open Font
License 1.1; each folder keeps its `OFL.txt`. Only the faces the titles use are kept.

- Static families are the upstream files, unchanged.
- Cabin, EB Garamond, Inter Display (Inter at optical size 32), Noto Sans, Noto Serif Display,
  Open Sans Condensed (Open Sans at width 75) and Roboto Condensed are cut from the upstream
  variable fonts with fontTools. None of those declares a Reserved Font Name.
- Comfortaa does declare one, so it was not cut, and its one title was left out.

Roboto Slab (Regular, Bold) is Apache-2.0, not OFL: its folder keeps `LICENSE.txt`. It is cut
from the upstream variable font; ș/ț are present.

Amiri, Comic Neue and Lobster Two have no ș/ț: Romanian text in those titles falls back for
those letters.

Rebuild: `python3 tools/asset-audit/build_title_fonts.py <google-fonts download> title-fonts`,
then trim to the used faces (see `docs/asset-audit/README.md`).
