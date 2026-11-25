#pragma once

#include "core/base/Time.h"

#include <QImage>
#include <QMetaType>

// The two real frames either side of a requested source time, with the timestamps the decoder
// reported for them. What smooth slow motion is built on: between two frames a file actually
// holds, these are the only two pictures that exist.
//
// It is a report of what the decoder has, not a promise. `hasB` is false at the end of a stream,
// and `ptsA` can sit *after* the requested time when a seek landed before the first frame of a
// stream. drift::interp::decide() checks all of that and decides what to show; nothing should
// assume this pair straddles anything.
//
// Timestamps are in the time base of the file being read, which after resolveVideoRead may be a
// reverse proxy or a preview proxy rather than the clip's own media.
//
// Namespace scope and a metatype because ClipReaderPool marshals it back from a decode worker
// thread, the same way it does PreviewVideoFrame.
struct VideoFramePair
{
    QImage a;
    QImage b;
    drift::TimeUs ptsA = 0;
    drift::TimeUs ptsB = 0;
    bool hasA = false;
    bool hasB = false;
};

Q_DECLARE_METATYPE(VideoFramePair)
