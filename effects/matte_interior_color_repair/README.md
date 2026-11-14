# Matte Interior Color Repair

Replaces contaminated semi-transparent boundary colors with nearby higher-confidence interior pixels while preserving a feathered alpha.

**Difference from V3/V4:** V4 Mask Background Soft Focus blurs outside subject; V3 Chroma Key keys colors. This specifically repairs mixed foreground/background boundary RGB.

**Dependency:** mask.

**Caution:** Best with a good moving mask; can smear fine hair or small details at large radius.

Native shader source with Inspector-editable parameters. Requires in-app QA.
