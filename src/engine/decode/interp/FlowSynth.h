#pragma once

#include "engine/decode/interp/FlowField.h"

#include <QImage>

namespace drift::interp {

struct FlowSynthOptions
{
    // Mean confidence over the frame below which the whole pair is declined and the caller
    // blends instead. A frame built mostly from vectors the solver does not believe is worse
    // than a blend, not better: the blend's fault is a ghost in the right place, and this
    // path's fault is detail in the wrong one.
    double minMeanConfidence = 0.35;

    // Fraction of the frame that is being uncovered, above which the pair is declined. Large
    // disocclusion means a big object crossing the frame or a cut nobody declared; neither is
    // something to warp through.
    double maxDisoccludedFraction = 0.25;

    // Per-pixel. Where the two endpoints' weights add up to less than this, that pixel takes
    // the plain unwarped blend. This is the floor that keeps the output a picture: a pixel the
    // solver cannot place is still a pixel that was photographed, and it gets the honest
    // approximation rather than a hole, a black dot, or a transparent one.
    double minPixelWeight = 0.02;
};

// What the synthesis did, for diagnostics and for the A/B harness. None of this is shown to
// anyone editing; it is here so that "flow is on and nothing looks different" and "flow is on
// and it declined on every frame" can be told apart.
struct FlowSynthStats
{
    double meanConfidence = 0.0;
    double disoccludedFraction = 0.0;
    // Share of output pixels that fell back to the plain blend.
    double blendFallbackFraction = 0.0;
    bool declined = false;
};

// Build the frame at `alpha` between `a` and `b` by moving each endpoint toward it along its own
// flow field, weighting the two by confidence and visibility separately.
//
// Confidence weights a sample; visibility decides whether that endpoint has anything to say
// about this pixel at all. They are read from different maps at different positions and are
// never multiplied into one number before they get here, because they fail in different places:
// a blank wall is perfectly visible and completely untrustworthy, and a patch about to be
// uncovered by a moving arm is perfectly trustworthy in the frame that has it and simply absent
// from the one that does not.
//
// Returns a null image when the pair is declined -- too little confidence across the frame, too
// much of it being uncovered, or a malformed analysis. A null return is an instruction to the
// caller to blend, not an error and not a frame to show. Every pixel of a returned image is
// written, so nothing of whatever the buffer held before survives into the picture.
QImage synthesizeFlowFrame(const QImage &a, const QImage &b, double alpha,
                           const FlowPairAnalysis &analysis, const FlowSynthOptions &options,
                           FlowSynthStats *stats = nullptr);

} // namespace drift::interp
