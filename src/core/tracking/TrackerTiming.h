#pragma once

#include "core/base/Time.h"
#include "core/project/clip/Clip.h"

#include <QtGlobal>

#include <cmath>

namespace drift {

// How far apart a tracker sample and a wanted source time may be and still count as the same
// frame. `frameUs` is one project frame. Samples sit one frame apart on the timeline, so in the
// source they are `frame x speed` apart and a lookup may land half of that away; on a ramp the speed
// that matters is the fastest one, because Clip::speed stays at its old value once a curve is set.
inline TimeUs trackerMatchToleranceUs(TimeUs frameUs, const Clip &host)
{
    const double speed = host.hasSpeedCurve() ? host.speedCurve.peakSpeed() : host.effectiveSpeed();
    return qMax<TimeUs>(1, static_cast<TimeUs>(std::llround(double(frameUs) * qMax(1.0, speed) / 2.0)));
}

// Where a follower sits on the timeline and in its source; a change re-aims its keys.
inline bool sameTrackerPlacement(const Clip &a, const Clip &b)
{
    return a.timelineStart == b.timelineStart && a.timelineDuration == b.timelineDuration && a.srcIn == b.srcIn
           && a.srcOut == b.srcOut && a.speed == b.speed && a.reverse == b.reverse && a.speedCurve == b.speedCurve;
}

} // namespace drift
