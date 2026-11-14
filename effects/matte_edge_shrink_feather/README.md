# Matte Edge Shrink & Feather

Uses the native moving segmentation matte to create an alpha-carrying feathered/shrunken cutout, mitigating hard edges.

**Difference from V3/V4:** V4 Subject Outline Neon/Subject Rim Outliner draw color around masks; this changes matte alpha geometry for compositing.

**Dependency:** mask.

**Caution:** Requires alpha-aware compositing. This effect consumes the clip mask and outputs a transparent cutout.

Native shader source with Inspector-editable parameters. Requires in-app QA.
