# Scene Title fonts

The exact font files the approved Scene Title proofs were drawn with (`docs/scene-titles.md`,
Fonts). A scene names each file with its sha256, and the native renderer
(`src/engine/render2d/scene/ScenePainter.cpp`) only accepts the file whose sha256 matches. Files of
the same name elsewhere can be different cuts: `title-fonts/noto-serif/NotoSerif-Regular.ttf` is
cut from the variable font and draws the "02" of Step Focus differently.

They are kept apart from `title-fonts/` on purpose: FontCatalog lists everything in `title-fonts/`,
and a second "Noto Serif" there would clash with the one the ChatGPT titles use. The build copies
this folder to `<appDir>/scene-fonts` (`Contents/Resources/scene-fonts` on macOS); the installers
ship it.

All files are the Debian/Ubuntu package files, byte for byte, under the SIL Open Font License 1.1
(`OFL-Noto.txt`, `OFL-Inter.txt`).

| File | sha256 | Package |
|---|---|---|
| NotoSansDisplay-Regular.ttf | 3dd456595e9226472ab8adaf9408400c671b97dbba241d883d05e2ab70ceadac | fonts-noto-core 20201225-2 |
| NotoSansDisplay-SemiBold.ttf | 9fa1f6b9fc7d95e0aa49ec75a90ce858e0a4c55b839811eb64abb7798afc94fd | fonts-noto-extra 20201225-2 |
| NotoSerif-Regular.ttf | 9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f | fonts-noto-core 20201225-2 |
| InterDisplay-SemiBold.otf | 12aec2b9f93d2c7a977dad5dac3427516b15e7041b578052b0aba9f9a9b0774f | fonts-inter 4.0+ds-1 |
| Inter-Regular.otf | be6d709dcb730ddaf050cbdae6fe89bd56d5d14dc66885639bbf6f06bd03521b | fonts-inter 4.0+ds-1 |

Stage10 (Light, Black, Medium, Noto Serif Display Medium) is not here yet: those scenes use the
reserved effects, which v1 refuses.
