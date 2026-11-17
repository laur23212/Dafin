#pragma once

#include <QString>

#include <cstdint>
#include <functional>
#include <vector>

// Dafin: the streaming part of the Vocal remover, kept apart from the model so it can be tested
// with a stand-in (tests/tst_vocalremover.cpp) and so a different separation model only has to
// supply SegmentModel.
//
// The model sees fixed-length segments (HTDemucs: 343 980 frames, 7.8 s at 44.1 kHz). Long audio
// is cut into segments that overlap by `overlapFrames`; inside each overlap the outgoing segment
// fades out linearly while the incoming one fades in, and the two weights always add up to one,
// so the seams are invisible and nothing is boosted or lost. Only one segment of input and one of
// output is held at a time: memory does not grow with the length of the clip, and finished audio
// leaves through the sinks as soon as no later segment can change it.
//
// "Music" is the mix minus the vocals, so the two outputs played together give back the original
// exactly (anything the model fails to place stays in the music rather than vanishing).
namespace drift::stems {

inline constexpr int kChannels = 2;

// Fills dst with up to maxFrames interleaved stereo frames, in order. Returns how many it wrote,
// 0 at the end of the audio, or -1 to cancel. A short read is allowed; the rest of the requested
// length is asked for again.
using Feed = std::function<int(float *dst, int maxFrames)>;

// One segment through the model: planarIn holds [2][segmentFrames] (left, then right), and the
// vocals estimate goes to planarVocalsOut in the same layout. False (with *error) on failure.
using SegmentModel = std::function<bool(const float *planarIn, float *planarVocalsOut, QString *error)>;

// Receives finished audio in order, interleaved stereo. False stops the run (a write failed).
using Sink = std::function<bool(const float *interleaved, int frames)>;

// Called with 0..1 after each segment; false cancels.
using Progress = std::function<bool(double)>;

struct Layout
{
    int segmentFrames = 0;
    int overlapFrames = 0; // 0 <= overlapFrames <= segmentFrames / 2
};

// Where each segment starts for a stream of totalFrames: 0, then every segmentFrames -
// overlapFrames until a segment reaches the end. One segment for anything up to segmentFrames.
std::vector<int64_t> segmentStarts(int64_t totalFrames, const Layout &layout);

// The weight segment `index` of `count` gives to its frame `frame` (0 <= frame < segmentFrames).
// Linear fade-in over the first overlap (not on the first segment) and fade-out over the last one
// (not on the last segment); adjacent weights sum to exactly 1.
float segmentWeight(const Layout &layout, int index, int count, int frame);

struct Result
{
    bool ok = false;
    bool cancelled = false;
    QString error;
    int64_t framesWritten = 0; // per sink; always totalFrames on success
};

// Runs the whole stream. Either sink may be empty (Vocals only / Music only). Exactly totalFrames
// frames reach each sink on success; if the feed ends early the rest is treated as silence, so the
// output always keeps the clip's length. Blocking: call it off the GUI thread.
Result separateVocals(int64_t totalFrames, const Layout &layout, const Feed &feed,
                      const SegmentModel &model, const Sink &vocals, const Sink &music,
                      const Progress &progress = {});

} // namespace drift::stems
