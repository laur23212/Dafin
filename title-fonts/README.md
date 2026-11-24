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

IBM Plex Mono (Regular, Medium, SemiBold, Bold) declares the Reserved Font Name "Plex", so its two static files
are the upstream ones, unchanged. Noto Serif (400/500/600/700) is cut from the variable font.

Kanit (Medium), Baloo 2 (Bold), Rubik (Black), Merriweather (ExtraBold Italic) and Source Sans 3
(Bold) are the static files Google Fonts serves, copied byte for byte (their `OFL.txt` is the font's own
licence text). Merriweather ("Merriweather") and Source Sans 3 ("Source") declare a Reserved Font Name,
which is why they were not cut from a variable font. All five have ș/ț/Ș/Ț and ă/â/î. They are for the
seven ChatGPT reference titles; `build_title_fonts.py` does not produce them.

Amiri, Comic Neue and Lobster Two have no ș/ț: Romanian text in those titles falls back for
those letters.

Rebuild: `python3 tools/asset-audit/build_title_fonts.py <google-fonts download> title-fonts`,
then trim to the used faces (see `docs/asset-audit/README.md`).

## Reference fonts (`ref-*`)

The 43 faces the approved titles were rendered with: byte-identical to the audit set (Debian
packages fonts-inter 4.1+ds-1, fonts-noto-core 20201225-2, fonts-ebgaramond-extra, fonts-lato,
fonts-roboto-slab, fonts-paratype, fonts-clear-sans, fonts-freefont-ttf, fonts-dejavu). Nothing is
cut or converted: `REFERENCE_FONTS.csv` lists the SHA-256 of every file and `tst_referencefonts`
checks it. A font is the same file or it is not used (a different version moves the text metrics).

- Each package is a separate family named `<Family> Ref` (Inter Ref, Lato Ref, ...), so it never
  shadows the family of the same name above or in Fonts Essentials. Category order 100.
- Faces with the same weight but another width (Noto Sans Display Condensed / SemiCondensed) are
  separate packages, so a weight always picks one file.
- Licence text beside every package: `OFL.txt` (OFL families), else `LICENSE.txt` (Apache-2.0,
  Bitstream Vera, ParaType Free Font License, FreeSerif GPL-3+ with font exception).
- EB Garamond 12 Bold and Noto Sans Symbols2 have no Romanian letters; every other face has
  ă â î ș ț Ș Ț (comma below).
