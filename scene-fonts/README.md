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
(`OFL-Noto.txt`, `OFL-Inter.txt`), except FreeSerif (GNU FreeFont: GPL-3+ with the font exception,
which lets documents and renders embed it freely; `COPYING-FreeFont.txt`), the ParaType PT fonts
(ParaType Free Font License, `COPYING-ParaType.txt`) and Roboto Slab (Apache 2.0, `COPYING-RobotoSlab.txt`).

| File | sha256 | Package |
|---|---|---|
| NotoSansDisplay-Regular.ttf | 3dd456595e9226472ab8adaf9408400c671b97dbba241d883d05e2ab70ceadac | fonts-noto-core 20201225-2 |
| NotoSansDisplay-SemiBold.ttf | 9fa1f6b9fc7d95e0aa49ec75a90ce858e0a4c55b839811eb64abb7798afc94fd | fonts-noto-extra 20201225-2 |
| NotoSerif-Regular.ttf | 9d7583b7dc9e812afd32a14280c5cac3160012efe50c8d08938f4fea266ff67f | fonts-noto-core 20201225-2 |
| InterDisplay-SemiBold.otf | 12aec2b9f93d2c7a977dad5dac3427516b15e7041b578052b0aba9f9a9b0774f | fonts-inter 4.0+ds-1 |
| Inter-Regular.otf | be6d709dcb730ddaf050cbdae6fe89bd56d5d14dc66885639bbf6f06bd03521b | fonts-inter 4.0+ds-1 |
| NotoSansSymbols2-Regular.ttf | 876598db5f1e3bef2bc77c4d94c5b0d5d2c1620be4878a06f18fb5434e331688 | fonts-noto-core 20201225-2 (the ⌘ ⌥ keys of Stage13C) |
| Inter-Medium.otf | 1e65171fccd6f445b7742728454620ec2867356c849af78e71bf2c4f0e3d97ba | fonts-inter 4.0+ds-1 |
| NotoSansDisplay-Medium.ttf | 0d7de6c6a264dfa9b0e93efc1338414bff20f79aaf9ff85e3a9fc901bdf9dbea | fonts-noto-extra 20201225-2 |
| NotoSansDisplay-SemiCondensedExtraBold.ttf | b224e7ec937a28e4b7f4925acf5be35ac480489fac28f598185f3e1af91473a4 | fonts-noto-extra 20201225-2 (Stage03b C revision) |
| NotoSansDisplay-SemiCondensedMedium.ttf | 1d1ab248177e1fd53daf9f481215e27ddf948578b8d99c51962de399f7a77e96 | fonts-noto-extra 20201225-2 (Stage03b C revision) |
| FreeSerif.ttf | c57bf5de095af4e070c2acb5bce634316409ed4203b5098f4fdadb691dc7b3d2 | fonts-freefont-ttf 20211204+svn4273-2 (Stage03b A); GPL-3+ with the font exception, `COPYING-FreeFont.txt` |
| InterDisplay-Medium.otf | cf438d4cd4979fda59150c773af3381d5b18627826672234846722bb6d06b86c | fonts-inter 4.0+ds-1 (Part5 Stage05) |
| NotoSerifDisplay-Medium.ttf | ae125c4ab952dd4e9c70beb9b4b7a268ed541b4d5f609935c00ace16e7409d19 | fonts-noto-extra 20201225-2 (Part5 Stage05) |
| PTS75F.ttf | 576b59732fff967895eba1eed0fc5f8cdb069ea31fde878c0cdc36aa2d4615ef | fonts-paratype 20181108-3 (PT Sans Bold) |
| PTC55F.ttf | 17831d9f44e62fb9b80c91827af8eafa51cb0f61529a7176c9c5135555384e70 | fonts-paratype 20181108-3 (PT Sans Caption) |
| PTC75F.ttf | 9a1a5425abb2f16d85cd8a677879b59b595c5c55014b0fc60233f810aedd7292 | fonts-paratype 20181108-3 (PT Sans Caption Bold) |
| PTF55F.ttf | d074251529defb38523f3d89cfadfc2491515b32c22f154a8fc6b4c184415a69 | fonts-paratype 20181108-3 (PT Serif) |
| PTF75F.ttf | 71a57caa616bec6f7266bddb176defd481f4edfca357123d4d3d8bb942b10f26 | fonts-paratype 20181108-3 (PT Serif Bold) |
| RobotoSlab-Bold.otf | f860d0a2b4323863d94f76d579d4ddb54048c87d07f3bff936f40c22d7cb49dd | fonts-roboto-slab 1.100263+20170512-2 |
| EBGaramond12-Regular.ttf | 689aa494b0eb1913085eaeb84d28b18e3b788b45dddbb3083a676d2e8896b5a4 | fonts-ebgaramond-extra 0.016+git20210310.42d4f9f2-1 (OFL) |
| EBGaramond12-Bold.ttf | 3209fd31257e096b80c84e49d611ddb513d57d3b2f4dd20719d2ae102906fe1e | fonts-ebgaramond-extra 0.016+git20210310.42d4f9f2-1 (OFL; Dark Chapter A v2, Chapter A v4) |
| NotoSansDisplay-Black.ttf | 4bf498b3b6a4b3fe311b8771c001cef5077a339f329649542d237cc284d3b13d | fonts-noto-extra 20201225-2 (Stage09 Word Shift B7) |
| NotoSansDisplay-SemiCondensedBold.ttf | 7b2e466a506a2f6ce6ee807dd97fc157acc11d5c7c88dcb4c7af5729ae427906 | fonts-noto-extra 20201225-2 (Stage09 Split Motion C1) |
| Lato-Heavy.ttf | 4ce7ec20cd9c391b271b4149ad8cf5a71301d98965403f14fc5530d017146dbb | fonts-lato 2.015-1 (OFL) |

Stage10 (Light, Black) is not here yet: those scenes use the
reserved effects, which v1 refuses.
