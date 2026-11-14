# Adaptive Backlit Subject Rescue

Automatically raises a masked backlit subject only when sampled background brightness exceeds sampled subject brightness.

**Difference from V3/V4:** V4 Subject Pop uses a constant user-defined lift, not scene-measured foreground/background exposure differential.

**Dependency:** mask.

**Caution:** Sampling is an approximation; motion can change averages, so check flicker on cutouts.

Native shader source with Inspector-editable parameters. Requires in-app QA.
