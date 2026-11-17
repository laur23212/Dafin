#include "engine/ml/audio/StemSeparation.h"

#include <QCoreApplication>

#include <algorithm>

namespace drift::stems {

namespace {

// Same context as the controller's strings, so one translation covers both.
QString trVocals(const char *text)
{
    return QCoreApplication::translate("VocalRemoverController", text);
}

} // namespace

std::vector<int64_t> segmentStarts(int64_t totalFrames, const Layout &layout)
{
    std::vector<int64_t> starts;
    if (totalFrames <= 0 || layout.segmentFrames <= 0)
        return starts;
    const int64_t stride = std::max<int64_t>(1, layout.segmentFrames - layout.overlapFrames);
    int64_t start = 0;
    starts.push_back(start);
    while (start + layout.segmentFrames < totalFrames) {
        start += stride;
        starts.push_back(start);
    }
    return starts;
}

float segmentWeight(const Layout &layout, int index, int count, int frame)
{
    const int n = layout.segmentFrames;
    const int o = layout.overlapFrames;
    if (o <= 0)
        return 1.0f;
    // The half-sample offset makes the fade-out of one segment and the fade-in of the next land
    // on the same frames with weights that add up to exactly one.
    if (index > 0 && frame < o)
        return (float(frame) + 0.5f) / float(o);
    if (index + 1 < count && frame >= n - o)
        return 1.0f - (float(frame - (n - o)) + 0.5f) / float(o);
    return 1.0f;
}

Result separateVocals(int64_t totalFrames, const Layout &layout, const Feed &feed,
                      const SegmentModel &model, const Sink &vocals, const Sink &music,
                      const Progress &progress)
{
    Result result;
    const int n = layout.segmentFrames;
    const int o = layout.overlapFrames;
    // An overlap past half a segment would let three segments meet on one frame, which the
    // pairwise fades below do not cover.
    if (n <= 0 || o < 0 || o * 2 > n) {
        result.error = QStringLiteral("Invalid separation layout");
        return result;
    }
    if (totalFrames <= 0) {
        result.error = trVocals("The clip is too short to separate");
        return result;
    }
    if (!feed || !model || (!vocals && !music)) {
        result.error = QStringLiteral("Nothing to separate into");
        return result;
    }

    const std::vector<int64_t> starts = segmentStarts(totalFrames, layout);
    const int count = int(starts.size());
    const int stride = n - o;

    // The mix for the current segment, interleaved, and the weighted vocals and weights summed so
    // far over the same frames. Index 0 is always frame starts[k] of the stream.
    std::vector<float> mix(size_t(n) * kChannels, 0.0f);
    std::vector<float> accum(size_t(n) * kChannels, 0.0f);
    std::vector<float> weights(size_t(n), 0.0f);
    std::vector<float> planarIn(size_t(n) * kChannels, 0.0f);
    std::vector<float> planarOut(size_t(n) * kChannels, 0.0f);
    std::vector<float> outVocals;
    std::vector<float> outMusic;

    int64_t readSoFar = 0;
    bool feedEnded = false;
    // Appends frames [from, to) of the current segment from the feed; silence past its end.
    const auto fill = [&](int from, int to) -> bool {
        int at = from;
        while (at < to) {
            const int64_t left = totalFrames - readSoFar;
            if (left <= 0 || feedEnded)
                break;
            const int want = int(std::min<int64_t>(to - at, left));
            const int got = feed(mix.data() + size_t(at) * kChannels, want);
            if (got < 0)
                return false;
            if (got == 0) {
                feedEnded = true;
                break;
            }
            at += std::min(got, want);
            readSoFar += std::min(got, want);
        }
        std::fill(mix.begin() + ptrdiff_t(at) * kChannels, mix.begin() + ptrdiff_t(to) * kChannels, 0.0f);
        return true;
    };

    if (!fill(0, n)) {
        result.cancelled = true;
        result.error = trVocals("Vocal remover cancelled");
        return result;
    }

    for (int k = 0; k < count; ++k) {
        for (int f = 0; f < n; ++f) {
            planarIn[size_t(f)] = mix[size_t(f) * 2];
            planarIn[size_t(n) + size_t(f)] = mix[size_t(f) * 2 + 1];
        }
        QString error;
        if (!model(planarIn.data(), planarOut.data(), &error)) {
            result.error = error.isEmpty() ? trVocals("The separation model failed") : error;
            return result;
        }
        for (int f = 0; f < n; ++f) {
            const float w = segmentWeight(layout, k, count, f);
            accum[size_t(f) * 2] += planarOut[size_t(f)] * w;
            accum[size_t(f) * 2 + 1] += planarOut[size_t(n) + size_t(f)] * w;
            weights[size_t(f)] += w;
        }

        // What no later segment touches any more: up to the next segment's start, or for the
        // last one everything that is left of the stream.
        const bool last = k + 1 == count;
        const int64_t start = starts[size_t(k)];
        const int ready = int(last ? std::min<int64_t>(n, totalFrames - start) : stride);
        if (ready > 0) {
            if (vocals)
                outVocals.resize(size_t(ready) * kChannels);
            if (music)
                outMusic.resize(size_t(ready) * kChannels);
            for (int f = 0; f < ready; ++f) {
                const float w = std::max(weights[size_t(f)], 1e-6f);
                for (int c = 0; c < kChannels; ++c) {
                    const size_t i = size_t(f) * kChannels + size_t(c);
                    const float v = accum[i] / w;
                    if (vocals)
                        outVocals[i] = v;
                    if (music)
                        outMusic[i] = mix[i] - v;
                }
            }
            if ((vocals && !vocals(outVocals.data(), ready)) || (music && !music(outMusic.data(), ready))) {
                result.error = trVocals("Could not write the separated audio");
                return result;
            }
            result.framesWritten += ready;
        }
        if (progress && !progress(double(k + 1) / double(count))) {
            result.cancelled = true;
            result.error = trVocals("Vocal remover cancelled");
            return result;
        }
        if (last)
            break;

        // Slide by one stride: the overlap moves to the front, the rest is read fresh.
        std::move(mix.begin() + ptrdiff_t(stride) * kChannels, mix.end(), mix.begin());
        std::move(accum.begin() + ptrdiff_t(stride) * kChannels, accum.end(), accum.begin());
        std::fill(accum.begin() + ptrdiff_t(o) * kChannels, accum.end(), 0.0f);
        std::move(weights.begin() + stride, weights.end(), weights.begin());
        std::fill(weights.begin() + o, weights.end(), 0.0f);
        if (!fill(o, n)) {
            result.cancelled = true;
            result.error = trVocals("Vocal remover cancelled");
            return result;
        }
    }

    result.ok = result.framesWritten == totalFrames;
    if (!result.ok)
        result.error = QStringLiteral("Separated %1 of %2 frames").arg(result.framesWritten).arg(totalFrames);
    return result;
}

} // namespace drift::stems
