#pragma once

#include "core/interchange/CompatibilityReport.h"

#include <QList>
#include <QString>
#include <QStringList>

#include <QtGlobal>
#include <cmath>
#include <cstdint>

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

} // namespace drift::interchange
