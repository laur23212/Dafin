# Isolated Hot / Dead Pixel Repair

Corrects isolated single-pixel bright/dark defects using spatial outlier detection; protects textured areas when neighbors disagree.

**Difference from V3/V4:** V4 Edge-Safe Sharpen and Shadow Chroma Clean enhance detail/denoise chroma, not isolated sensor defects.

**Dependency:** none (source pixels).

**Caution:** May remove stars or tiny intentional highlights; inspect source at 100% magnification.

Native shader source with Inspector-editable parameters. Requires in-app QA.
