#pragma once

#include "core/animation/SpeedCurve.h"
#include "core/interchange/CompatibilityReport.h"

#include <QByteArray>
#include <QList>
#include <QString>
#include <QStringList>

#include <QtGlobal>
#include <cmath>
#include <cstdint>
#include <numeric>

// Dafin: a timeline read out of another editor's project file (Premiere Pro .prproj, Filmora .wfp)
// before it becomes a Dafin project. The readers fill this from the file alone — Qt and zlib, no
// project model — so they can be checked against real files on their own; toProject() then builds
// the Dafin project from it.
namespace drift::interchange {

struct ImportedClip
{
    QString name;
    QString path;            // the media file as the project names it (absolute, as stored)
    QString relativePath;    // the same, relative to the project file, when the project keeps one
    int64_t startUs = 0;     // on the timeline
    int64_t durationUs = 0;  // on the timeline
    int64_t srcInUs = 0;     // in the source
    double speed = 1.0;
    // The part of the source the clip plays, when the file states it (its out point minus its in
    // point); 0 = derive it from the length on the timeline times the speed.
    int64_t srcSpanUs = 0;
    // A speed ramp the reader has proven (see FilmoraProjectParser): control points over the
    // source span in playback order. Empty = a constant speed.
    QList<SpeedPoint> speedPoints;
    // What the project recorded about the file, when it records anything: lets a search for a
    // moved file tell the right one from another with the same name. -1 / empty = not recorded.
    qint64 expectedSize = -1;
    QByteArray expectedPartialHash;
    bool reverse = false;
    bool still = false;      // the source is a still image
    // Filmora: a video clip with no separate audio record keeps the sound of its file; Premiere
    // always has the sound as clips of its own on audio tracks.
    bool keepsOwnAudio = false;
    // Where the clip sits in the source file (CompatEntry::sourceId), empty when the reader gives
    // none. Lets the project builder name the clip in the report when its media is missing.
    QString sourceId;
};

struct ImportedTrack
{
    bool audio = false;
    QString name;
    bool muted = false;      // audio: muted; video: hidden (Premiere's eye)
    bool locked = false;
    QList<ImportedClip> clips;
};

struct ImportedTimeline
{
    QString name;
    int width = 1920;
    int height = 1080;
    double fps = 30.0;
    // The rate as the file states it, when it states a ratio (Filmora: 30000/1001); 0 when not.
    int fpsNum = 0;
    int fpsDen = 0;
    // The ratio Dafin keeps next to its whole-number fps (see keptFrameRate); 0/0 when the rate is
    // a whole number, or when it is one Dafin cannot keep.
    int keptFpsNum = 0;
    int keptFpsDen = 0;
    int sampleRate = 0;
    // Dafin order: index 0 is the top track. Video tracks first (the highest numbered on top), then
    // audio tracks from A1 down.
    QList<ImportedTrack> tracks;
    // What could not come along, for the message the import ends with.
    int skippedClips = 0;
    QStringList skippedKinds;
    QStringList notes;
    // Per construct, what the reader kept and what it did not (the counts and notes above stay as
    // they were). Filled by the Filmora reader; empty from readers that do not report yet.
    CompatibilityReport report;
};

// The whole-number frame rate a Dafin project gets for a source rate: Dafin stores whole frames
// per second, so 30000/1001 (29.97) becomes 30. Shared by the project builder and the readers'
// reports so the two cannot drift apart.
inline int projectFpsFor(double fps)
{
    return qBound(1, int(std::lround(fps)), 240);
}

// The exact ratio Dafin keeps beside a project's whole-number fps, or {0, 0}. Dafin edits on a whole
// frame grid, but exports at a rational rate (23.976, 29.97, 59.94), so a project that came in at
// 30000/1001 can keep saying so and "Same as project" exports at 29.97. That is possible only for a
// ratio that really is the whole rate's NTSC-style neighbour, within 1 % of it; any other rate
// (12.5, 14.4, 47.5…) is not kept, and the importer reports the rounding.
struct KeptRate
{
    int num = 0;
    int den = 0;
    bool valid() const { return num > 0 && den > 1; }
};
inline KeptRate keptFrameRate(int num, int den)
{
    if (num <= 0 || den <= 1)
        return {};
    const int g = std::gcd(num, den);
    const int n = num / g;
    const int d = den / g;
    if (d <= 1)
        return {};
    const double rate = double(n) / double(d);
    const int whole = projectFpsFor(rate);
    if (std::abs(rate - whole) > 0.01 * whole)
        return {};
    return {n, d};
}

} // namespace drift::interchange
