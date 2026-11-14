# Conditional Chroma Moiré Cleanup

Reduces high-frequency chroma moiré selectively while retaining most luminance texture.

**Difference from V3/V4:** V4 Shadow Chroma Clean handles noisy dark-region color; this targets chromatic aliasing patterns irrespective of shadows.

**Dependency:** none (source pixels).

**Caution:** Not a temporal demosaic pipeline; test fine fabrics and LED panels.

Native shader source with Inspector-editable parameters. Requires in-app QA.
