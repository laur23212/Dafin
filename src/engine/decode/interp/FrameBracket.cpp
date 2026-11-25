#include "engine/decode/interp/FrameBracket.h"

#include <algorithm>

namespace drift::interp {

Decision decide(FrameInterpMode mode, const Pair &pair, const Query &query)
{
    Decision out;

    // Structural checks first, and in this order, because they decide whether the pair is a
    // pair at all. They run whatever the Mode is: a caller that asks for the route wants to
    // know "there was no second frame here" rather than "sampling was selected".

    if (!pair.hasA || !pair.hasB) {
        out.reason = Reason::NoPair;
        return out;
    }
    if (pair.ptsB <= pair.ptsA) {
        // Duplicate timestamps, or a stream that goes backwards. Dividing by this span would
        // be a zero-divide or a negative alpha, and a frame built from either is garbage.
        out.reason = Reason::PairNotOrdered;
        return out;
    }
    if (query.readUs < pair.ptsA || query.readUs >= pair.ptsB) {
        out.reason = Reason::PairNotStraddling;
        return out;
    }

    const TimeUs span = pair.ptsB - pair.ptsA;
    const TimeUs into = query.readUs - pair.ptsA;

    // Landing on A is the ordinary case for a clip running at normal speed against a matching
    // output rate, and it is also the exact-frame case the decoder reports when the time hits
    // a real timestamp. Answer it before spending anything.
    if (into <= query.onFrameToleranceUs) {
        out.route = Route::Exact;
        out.reason = Reason::OnFrame;
        return out;
    }

    if (mode == FrameInterpMode::Sampling) {
        out.reason = Reason::ModeIsSampling;
        return out;
    }

    // The trim window. This is the guard that keeps a cut from being blended through, and it
    // is deliberately strict at both ends:
    //
    //  - A frame at or past the out point is the first frame of whatever comes next. Where two
    //    clips of one file abut, that is literally the next shot's opening frame, and mixing it
    //    in puts a one-frame ghost of the incoming shot on the end of the outgoing one — at
    //    every cut in the project.
    //  - A frame before the in point is the outgoing shot, by the same argument.
    //
    // Nothing here can tell a split in the middle of a continuous take (where blending across
    // would have been harmless) from an assembled cut (where it is a visible fault), so it
    // refuses both. The price is that the first and last frame interval of every clip repeats
    // instead of blending, which is not visible; the price of guessing the other way is.
    if (query.haveTrim && (pair.ptsA < query.trimLoUs || pair.ptsB >= query.trimHiUs)) {
        out.reason = Reason::OutsideTrim;
        return out;
    }

    if (query.knownCut) {
        out.reason = Reason::KnownCut;
        return out;
    }

    // Variable-frame-rate captures — screen and game recorders especially — emit nothing while
    // the picture is still, so a pair can be seconds apart. Whatever sits between those two
    // frames, it is not a tween of them.
    if (query.maxGapUs > 0 && span > query.maxGapUs) {
        out.reason = Reason::GapTooLong;
        return out;
    }

    // Both timestamps are int64 microseconds straight from the decoder, so this is the one
    // division in the whole path and it is the only place alpha comes from. In particular it is
    // never derived from an output frame index, a nominal frame rate, or a shader's float
    // microsecond uniform — each of which drifts away from the real timestamps on exactly the
    // variable-rate and ramped material this feature exists for.
    out.alpha = std::clamp(static_cast<double>(into) / static_cast<double>(span), 0.0, 1.0);
    out.route = (mode == FrameInterpMode::Flow) ? Route::Flow : Route::Blend;
    return out;
}

} // namespace drift::interp
