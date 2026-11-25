#pragma once

#include "engine/decode/interp/FlowField.h"

#include <QImage>
#include <QString>

namespace drift::interp {

struct FlowOptions
{
    // Longest side of the grid the solver works on. Flow is smooth compared with texture, so
    // solving it at frame resolution buys little and costs a great deal; the field is scaled up
    // when it is sampled, with the unit conversion written down rather than assumed.
    int analysisLongSide = 480;
    // Pixels of the *analysis* grid. A vector longer than this is not believed: at ordinary
    // frame rates nothing in shot moves a quarter of the frame between two frames except a cut
    // or a whip pan, and in both cases a tween of the two frames is not what should be shown.
    float maxDisplacement = 96.0f;
    int pyramidLevels = 4;
    int iterationsPerLevel = 3;
    // Half-width of the window each pixel's vector is solved over.
    int windowRadius = 3;

    QString algorithmId() const { return QStringLiteral("dense-lk-v1"); }
    // Everything that changes the numbers, for a cache key. Deliberately not a hash of the
    // struct's bytes, which would change with padding and with fields that do not affect output.
    QString settingsFingerprint() const;
};

// Dense optical flow between two frames, both directions, with confidence and visibility worked
// out separately.
//
// An original local least-squares solver on an image pyramid. Not the sparse Lucas-Kanade in
// MotionTracking, which tracks chosen points and cannot be stretched into a dense field by
// spreading its vectors around; and not OpenCV, which is not a dependency Dafin has and not one
// to acquire quietly inside a quality setting.
//
// Research-grade and off by default. It exists so that flow can be *measured* against plain
// blending on real footage, which is the only thing that can decide whether it belongs in a
// picture anyone sees. An independent reference run of a comparable classical solver made three
// of four test scenes worse, so the burden of proof sits here and not with the blend.
FlowPairAnalysis estimateFlow(const QImage &a, const QImage &b, const FlowOptions &options);

} // namespace drift::interp
