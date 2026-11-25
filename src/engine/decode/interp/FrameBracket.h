#pragma once

#include "core/base/Time.h"

namespace drift::interp {

// Slow motion asks the timeline for times that fall *between* two frames the file actually
// holds. Mode says what to show there. Serialized with the project, so these numbers are part
// of the file format: append, never renumber.
enum class Mode : int {
    // The frame at or before the time, held until the next one is due. What Dafin did before
    // any of this existed, and still the default: it invents nothing.
    Sampling = 0,
    // Mix the two frames either side by how far between them the time falls. Cheap, always
    // available, and wrong in a predictable way — moving edges go double rather than smooth.
    Blending = 1,
    // Motion-compensated synthesis. Falls back to Blending wherever the motion cannot be
    // trusted, which is often, so it is a quality setting and not a different feature.
    Flow = 2,
};

// What the sampler did, as opposed to what it was asked to do. A clip set to Flow still
// reports Blend or Repeat on the frames where flow was declined, and the difference is worth
// surfacing in diagnostics: "it looked the same as before" and "it fell back on every frame"
// are the same picture and different bugs.
enum class Route : int {
    Repeat,
    Exact,
    Blend,
    Flow,
};

// Why a route weaker than the clip's Mode was taken. Diagnostics only. None of these is ever
// a reason to drop a frame, leave a hole, or make anything transparent — the weaker route
// always produces a real picture.
enum class Reason : int {
    None,
    ModeIsSampling,
    // No frame after the requested time: the end of the stream, or a decode that only managed
    // one frame.
    NoPair,
    // A pair arrived but does not straddle the time. Seeking to before the first frame leaves
    // the decoder with a cover whose timestamp is *after* the request, so this is a real state
    // and not a paranoid check.
    PairNotStraddling,
    // ptsB <= ptsA. Duplicate or non-monotonic timestamps; container repair streams do this.
    PairNotOrdered,
    // The time is within tolerance of a real frame, so there is nothing to invent and no cost
    // worth paying.
    OnFrame,
    // The far frame lies outside the clip's trim. That footage was trimmed away, and where two
    // clips of one file meet at a cut it belongs to the other shot. Never blend across it.
    OutsideTrim,
    // A cut was declared between the two frames by something that knows better than we do.
    KnownCut,
    // The two frames are too far apart in time for anything synthesized between them to mean
    // anything. Variable-frame-rate captures leave second-long gaps when nothing moved.
    GapTooLong,
    // Flow was asked for and no estimator is built in.
    FlowUnavailable,
    // The estimator ran and declined, which is its right.
    FlowNotConfident,
};

// Two real decoded frames around a requested time, with the timestamps the decoder reports.
//
// These are *read-space* timestamps: the times of the file actually being decoded, after
// resolveVideoRead has already swapped in a reverse proxy or a preview proxy and mapped the
// time to match. Read space always runs forwards — a reverse proxy holds the range flipped
// end for end and is read front to back — which is the whole reason reverse needs no special
// case below. Doing this in clip source time instead would put B before A on every reversed
// clip and silently invert alpha.
struct Pair
{
    TimeUs ptsA = 0;
    TimeUs ptsB = 0;
    bool hasA = false;
    bool hasB = false;
};

// What the decision needs besides the pair.
struct Query
{
    // The requested time, read space. Not an output frame index and not derived from the
    // project frame rate: those are the same number only when the source happens to be
    // constant-rate at the timeline's rate.
    TimeUs readUs = 0;

    // The clip's trim, read space, inclusive. A reverse proxy is matched by containment, so it
    // routinely holds frames outside the clip's trim; a preview proxy keeps the source's
    // timestamps. Either way the window has to be carried here rather than assumed.
    TimeUs trimLoUs = 0;
    TimeUs trimHiUs = 0;
    bool haveTrim = false;

    // Set by whoever knows a cut sits between these two frames. Nothing detects source-internal
    // cuts yet; an editorial cut is already handled by the trim window.
    bool knownCut = false;

    // Longest gap between the two frames that is still worth synthesizing across. 0 = no limit.
    TimeUs maxGapUs = 0;

    // How near a real frame counts as landing on it. Keeps the ordinary case — a clip at normal
    // speed whose frames line up with the output — on the cheap path instead of blending every
    // frame with its neighbour at alpha 0.001.
    TimeUs onFrameToleranceUs = 0;
};

struct Decision
{
    Route route = Route::Repeat;
    // 0 at A, 1 at B. Computed from the two int64 timestamps and nothing else. Only meaningful
    // for Blend and Flow; Repeat and Exact leave it at 0.
    double alpha = 0.0;
    Reason reason = Reason::None;
};

// The clip's trim window, in read space, for Query::trimLoUs / trimHiUs.
struct ReadWindow
{
    TimeUs loUs = 0;
    TimeUs hiUs = 0;
};

// Reading the file itself, forwards: the window is the trim. This covers the ordinary clip, a
// preview proxy (same timestamps, different path) and a reversed clip whose proxy has not been
// rendered yet — that last one walks the trim downwards as the timeline advances, but it walks
// it inside the same window.
constexpr ReadWindow readWindowForward(TimeUs srcIn, TimeUs srcOut)
{
    return {srcIn, srcOut};
}

// Reading a reverse proxy: the proxy holds a covered range flipped end for end and is read
// front to back, so a source time appears at coverEndUs - sourceUs. The trim's two ends swap
// over. Worth being exact about, because the proxy is matched by *containment* — trimming a
// reversed clip inward or splitting it keeps hitting the same proxy — so the file very often
// holds frames outside this window, and they are the ones that must not be blended in.
constexpr ReadWindow readWindowMirrored(TimeUs srcIn, TimeUs srcOut, TimeUs coverEndUs)
{
    return {coverEndUs - srcOut, coverEndUs - srcIn};
}

// Pure: same arguments, same answer, no clock, no cache, no decoder. Which is what makes the
// preview and the export agree, and what lets this be tested without a video file.
Decision decide(Mode mode, const Pair &pair, const Query &query);

// Longest gap worth blending across, as a multiple of the pair's own spacing is not knowable
// here, so this is an absolute default: a third of a second. Past that the two frames are
// different enough that a mix of them reads as a double exposure rather than a tween.
constexpr TimeUs kDefaultMaxGapUs = 333'333;

} // namespace drift::interp
