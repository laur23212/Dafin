#pragma once

// Dafin: the framing maths behind auto-reframe, shared by the agent tool (McpController::
// autoReframe) and AI › Smart short clips. Pure: face samples in, transform keys out; the callers
// read the faces and write the keys onto a clip.

#include <QList>

namespace drift::reframe {

// Where the subject is at time t (seconds from the clip's start), in normalised source
// coordinates (0..1, top-left origin): the face's centre and the half-axes of its oval.
struct FaceSample
{
    double t = 0.0;
    double cx = 0.5;
    double cy = 0.5;
    double rx = 0.2;
    double ry = 0.25;
};

// A face-less sample for a centre crop: ry 0.5 asks for the whole source height, so a 9:16 crop of
// a 16:9 source is its full-height middle strip, not a zoom.
inline FaceSample centreSample(double t)
{
    FaceSample s;
    s.t = t;
    s.rx = 0.5;
    s.ry = 0.5;
    return s;
}

// Moving average of the centres over ±radius samples; times and sizes are kept.
QList<FaceSample> smoothSamples(const QList<FaceSample> &samples, int radius);

// The clip's transform (canvas pixels) at one time.
struct Key
{
    double t = 0.0; // seconds from the clip's start
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

struct Result
{
    QList<Key> keys;
    // How far the crop blows the source up at its tightest (frame pixels per source pixel); 0 when
    // the source width is unknown. Above 1 the picture will look soft.
    double tightestScale = 0.0;
};

// One key per (already smoothed) sample. The crop window of `targetAspect` follows the face and is
// fitted inside the canvas when the canvas has another aspect. sourceAspect is the source's display
// aspect (width / height); sourcePixelWidth is its width in pixels, 0 when unknown.
Result reframeKeys(const QList<FaceSample> &samples, double canvasW, double canvasH, double sourceAspect,
                   double targetAspect, int sourcePixelWidth);

// Fills the gaps of a face scan sampled every `step` seconds over `duration`: `found[i]` is the face
// at i * step or a sample with t < 0 when none was seen there. Gaps hold the nearest face seen
// (before, else after); with no face at all every sample is a centre crop.
QList<FaceSample> fillFaceGaps(const QList<FaceSample> &found, double step);

} // namespace drift::reframe
