#pragma once

#include <QString>

namespace drift {

// What a clip shows at a timeline time that falls *between* two frames its media actually
// holds — which is what slow motion spends most of its length doing.
//
// Lives in core because a clip carries it and the project file stores it; the machinery that
// acts on it is in engine/decode/interp.
enum class FrameInterpMode : int {
    // The frame at or before the time, held until the next one is due. What Dafin did before
    // any of this existed, and still the default: it invents nothing, so a slowed shot steps
    // rather than glides, and nothing on screen was never photographed.
    Sampling = 0,
    // Mix the two frames either side by how far between them the time falls. Cheap and always
    // available. Wrong in a predictable, visible way — a moving edge goes double rather than
    // smooth — which is better than wrong unpredictably.
    Blending = 1,
    // Motion-compensated synthesis, falling back to Blending wherever the motion cannot be
    // trusted. A quality setting on top of Blending, not a separate feature.
    Flow = 2,
};

inline QString frameInterpModeToString(FrameInterpMode mode)
{
    switch (mode) {
    case FrameInterpMode::Blending:
        return QStringLiteral("blending");
    case FrameInterpMode::Flow:
        return QStringLiteral("flow");
    case FrameInterpMode::Sampling:
        break;
    }
    return QStringLiteral("sampling");
}

// Anything unrecognised reads as Sampling. A project written by a later Dafin that knows a mode
// this one does not should open and play, with the one setting dropped back to the behaviour
// that needs no support.
inline FrameInterpMode frameInterpModeFromString(const QString &value)
{
    if (value == QLatin1String("blending"))
        return FrameInterpMode::Blending;
    if (value == QLatin1String("flow"))
        return FrameInterpMode::Flow;
    return FrameInterpMode::Sampling;
}

} // namespace drift
