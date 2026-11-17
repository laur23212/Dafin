#pragma once

#include "core/project/clip/Clip.h"
#include "core/base/Time.h"
#include "core/project/Transcript.h"

#include <QList>
#include <QString>

namespace drift {

// Pieces the cut engine makes can be far shorter than an interactive split allows: a kept word
// between two removed fillers is a few hundred ms.
constexpr TimeUs kCutMinEdgeUs = 10'000;

struct TimeRangeUs
{
    TimeUs startUs = 0;
    TimeUs endUs = 0;
    TimeUs length() const { return endUs - startUs; }
};

// Sorted, with overlapping or touching ranges merged and empty ones dropped.
QList<TimeRangeUs> normalizedRanges(QList<TimeRangeUs> ranges);

// The parts of `clip` (timeline µs) left after removing `removed`. A kept piece shorter than
// minKeepUs is removed too, so no sliver survives between two cuts or at an edge.
QList<TimeRangeUs> keptTimelineIntervals(const Clip &clip, const QList<TimeRangeUs> &removed,
                                         TimeUs minKeepUs);

// `src` cut to the timeline span [from, to), down to kCutMinEdgeUs pieces.
bool sliceClipExact(const Clip &src, TimeUs from, TimeUs to, Clip &out);

// `src` sliced to each kept timeline interval and packed end to end from src.timelineStart. The
// group's outer edges keep src's fades; every edge a cut made gets a declickUs audio ramp. Ids
// are left as src's; the caller assigns them.
QList<Clip> packedSegments(const Clip &src, const QList<TimeRangeUs> &kept, TimeUs declickUs);

// One segment per source range, in list order (reordering is allowed), packed end to end from
// src.timelineStart. Each range is padded by padUs, clamped to [0, mediaDurationUs], and merged
// with the previous range when they touch. A reversed clip plays each segment reversed. Clips
// with a speed ramp are refused: a ramp is shaped over the clip's own range.
QList<Clip> segmentsFromSourceRanges(const Clip &src, QList<TimeRangeUs> sourceRanges,
                                     TimeUs mediaDurationUs, TimeUs padUs, TimeUs declickUs,
                                     QString *error);

// Where source span `source` plays on the timeline in `clip` (speed, ramp and reverse aware).
// False when the span misses the clip's source window.
bool sourceRangeToTimeline(const Clip &clip, TimeRangeUs source, TimeRangeUs &timelineOut);

class Project;

struct TimelineWord
{
    TranscriptWord word; // times moved onto the timeline
    int index = -1;      // into the asset's transcript
    QString assetId;
    QString clipId;
};

// Transcript words the timeline plays inside [viewStart, viewEnd), in time order: every audible
// clip (unmuted track; a video's own sound only when it isn't separated), each linked pair once.
// onlyClipId limits it to one clip, whatever track it is on.
QList<TimelineWord> transcriptWordsOnTimeline(const Project &project, TimeUs viewStart, TimeUs viewEnd,
                                              const QString &onlyClipId = {});

// Dafin: AI › Text-based editing. The source ranges to cut so the speech tokens `wordIndexes`
// (into transcript.words) no longer play in `clip`: cut_words' boundary rule. Neighbouring removed
// words (in speech order) make one run; a run also takes the pauses around it, leaving padUs of
// air beside the words that stay (less when the pause is shorter), and reaches the clip's edge
// when no kept speech of the clip lies between. Words outside the clip's source window are skipped.
// Sorted by source time.
QList<TimeRangeUs> sourceCutsForWords(const Transcript &transcript, const Clip &clip,
                                      const QList<int> &wordIndexes, TimeUs padUs);

// Where old timeline time `t` lands once `removed` (old timeline µs, normalized here) are cut out
// and what follows moves up: unchanged before the first cut, the cut's start for a time inside
// one, earlier by everything removed before it after.
TimeUs timeAfterCut(const QList<TimeRangeUs> &removed, TimeUs t);

// Dafin: captions follow a cut. `before` is the media clip as it was and `removed` the parts of it
// (old timeline µs) that were cut out, with the gaps closed. Every caption cue tied to that media
// (cue.source == subtitleSourceKey(before)) moves by timeAfterCut. A cue lying over the clip that
// lost some of its time has its words read again from `transcript` (when given) over the source it
// still plays, and goes when no word is left. A subtitle clip holding such cues moves its edges the
// same way, and goes when it has no cue left. Hand-written cues keep their place on the timeline.
// True when anything changed.
bool captionsFollowCut(Project &project, const Clip &before, const QList<TimeRangeUs> &removed,
                       const Transcript *transcript);

} // namespace drift
