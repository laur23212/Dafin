// Dafin: AI › Vocal remover. The streaming separation (segments, overlap, length, sync) with a
// stand-in model, and the editor side: Vocals only / Both on a real project, one undo step.
// HTDemucs itself is a 304 MB download, so it is not needed here; the stand-in "model" returns
// half of its input as the vocals, which makes every output frame predictable from the mix and
// shows any frame that lands in the wrong place.
#include "app/bin/AssetLibrary.h"
#include "app/editor/AppController.h"
#include "app/jobs/ai/VocalRemoverController.h"
#include "engine/decode/readers/ClipReaderPool.h"
#include "engine/encode/writers/AudioFileWriter.h"
#include "engine/ml/audio/StemSeparation.h"

#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <random>
#include <vector>

namespace {

using drift::stems::Layout;

constexpr double kPi = 3.14159265358979323846;
constexpr int kRate = 44100;

// Stereo, interleaved: a 440 Hz tone with a slow wobble (the "voice") plus white noise (the
// "band"), different on each side so a swapped channel shows too.
std::vector<float> synthMix(int64_t frames, unsigned seed = 7)
{
    std::mt19937 rng(seed);
    std::normal_distribution<float> noise(0.0f, 0.1f);
    std::vector<float> out(size_t(frames) * 2);
    for (int64_t i = 0; i < frames; ++i) {
        const double t = double(i) / kRate;
        const float tone = float(0.3 * std::sin(2 * kPi * 440.0 * t) * (1.0 + 0.5 * std::sin(2 * kPi * 5.0 * t)));
        out[size_t(i) * 2] = tone + noise(rng);
        out[size_t(i) * 2 + 1] = 0.8f * tone + noise(rng);
    }
    return out;
}

// Half the input is "the vocals".
bool halfModel(int segment, const float *in, float *out, QString *)
{
    for (int i = 0; i < 2 * segment; ++i)
        out[i] = 0.5f * in[i];
    return true;
}

struct Run
{
    drift::stems::Result result;
    std::vector<float> vocals;
    std::vector<float> music;
    int sinkCalls = 0;
    int largestWrite = 0;
    int modelCalls = 0;
};

// Feeds `mix` in uneven pieces (as a decoder does), collects both outputs.
Run separate(const std::vector<float> &mix, const Layout &layout, int64_t total = -1,
             const drift::stems::Progress &progress = {})
{
    Run run;
    if (total < 0)
        total = int64_t(mix.size() / 2);
    size_t pos = 0;
    int piece = 0;
    const auto feed = [&](float *dst, int maxFrames) {
        const int pieces[] = {97, 1000, 313, 4096, 7};
        const int want = std::min(maxFrames, pieces[piece++ % 5]);
        const int left = int((mix.size() - pos) / 2);
        const int n = std::min(want, left);
        std::copy(mix.begin() + ptrdiff_t(pos), mix.begin() + ptrdiff_t(pos + size_t(n) * 2), dst);
        pos += size_t(n) * 2;
        return n;
    };
    const auto model = [&](const float *in, float *out, QString *error) {
        ++run.modelCalls;
        return halfModel(layout.segmentFrames, in, out, error);
    };
    const auto collect = [&run](std::vector<float> &into) {
        return [&run, &into](const float *data, int frames) {
            into.insert(into.end(), data, data + size_t(frames) * 2);
            ++run.sinkCalls;
            run.largestWrite = std::max(run.largestWrite, frames);
            return true;
        };
    };
    run.result = drift::stems::separateVocals(total, layout, feed, model, collect(run.vocals), collect(run.music),
                                              progress);
    return run;
}

double maxError(const std::vector<float> &a, const std::vector<float> &b, double scaleB = 1.0)
{
    double worst = 0.0;
    for (size_t i = 0; i < std::min(a.size(), b.size()); ++i)
        worst = std::max(worst, std::abs(double(a[i]) - scaleB * double(b[i])));
    return worst;
}

// Every frame of `path` (decoded at kRate), until the decoder has no more.
std::vector<float> decodeAll(const QString &path)
{
    std::vector<float> out;
    std::vector<float> buf(4096 * 2);
    drift::TimeUs at = 0;
    int64_t frames = 0;
    for (;;) {
        const int got = ClipReaderPool::instance().readAudioInterleaved(path, 0x7E57'0000'0000'0001ull, at, 4096,
                                                                        kRate, buf.data(), 0);
        if (got <= 0)
            break;
        out.insert(out.end(), buf.begin(), buf.begin() + ptrdiff_t(got) * 2);
        frames += got;
        at = drift::TimeUs(frames * drift::kUsPerSecond / kRate);
        if (got < 4096)
            break;
    }
    return out;
}

} // namespace

class VocalRemoverTest : public QObject
{
    Q_OBJECT

private slots:
    void segmentsCoverTheStream()
    {
        const Layout layout{1000, 250};
        QCOMPARE(drift::stems::segmentStarts(0, layout).size(), size_t(0));
        QVERIFY(drift::stems::segmentStarts(1, layout) == std::vector<int64_t>({0}));
        QVERIFY(drift::stems::segmentStarts(1000, layout) == std::vector<int64_t>({0}));
        QVERIFY(drift::stems::segmentStarts(1001, layout) == std::vector<int64_t>({0, 750}));
        QVERIFY(drift::stems::segmentStarts(2500, layout) == std::vector<int64_t>({0, 750, 1500}));
        // The last segment always reaches the end.
        for (int64_t total : {1749, 1750, 1751, 123457}) {
            const std::vector<int64_t> starts = drift::stems::segmentStarts(total, layout);
            QVERIFY(starts.back() + layout.segmentFrames >= total);
            QVERIFY(starts.size() == 1 || starts[starts.size() - 2] + layout.segmentFrames < total);
        }
    }

    void overlapWeightsAddUpToOne()
    {
        const Layout layout{1000, 250};
        const int count = 4;
        std::vector<double> sum(size_t(750 * 3 + 1000), 0.0);
        for (int k = 0; k < count; ++k) {
            for (int f = 0; f < layout.segmentFrames; ++f)
                sum[size_t(k * 750 + f)] += drift::stems::segmentWeight(layout, k, count, f);
        }
        for (size_t i = 0; i < sum.size(); ++i)
            QVERIFY2(std::abs(sum[i] - 1.0) < 1e-6, qPrintable(QStringLiteral("frame %1: %2").arg(i).arg(sum[i])));
    }

    // The seams are invisible: every output frame is exactly what the model said for that frame,
    // the outputs have the input's length, and vocals + music give back the mix.
    void keepsLengthAndSyncAcrossSegments()
    {
        const std::vector<float> mix = synthMix(123457);
        const Layout layout{4410, 1102};
        const Run run = separate(mix, layout);
        QVERIFY2(run.result.ok, qPrintable(run.result.error));
        QCOMPARE(run.result.framesWritten, int64_t(123457));
        QCOMPARE(run.vocals.size(), mix.size());
        QCOMPARE(run.music.size(), mix.size());
        QVERIFY(maxError(run.vocals, mix, 0.5) < 1e-5);
        QVERIFY(maxError(run.music, mix, 0.5) < 1e-5);
        std::vector<float> sum(mix.size());
        for (size_t i = 0; i < mix.size(); ++i)
            sum[i] = run.vocals[i] + run.music[i];
        QVERIFY(maxError(sum, mix) < 1e-6);
        QCOMPARE(run.modelCalls, int(drift::stems::segmentStarts(123457, layout).size()));
    }

    // Streaming: finished audio leaves after each segment, never more than a segment at a time.
    void streamsSegmentBySegment()
    {
        const std::vector<float> mix = synthMix(50000);
        const Layout layout{2000, 500};
        const Run run = separate(mix, layout);
        QVERIFY(run.result.ok);
        QCOMPARE(run.sinkCalls, 2 * run.modelCalls);
        QVERIFY(run.largestWrite <= layout.segmentFrames);
    }

    void shortClipIsOneSegment()
    {
        const std::vector<float> mix = synthMix(1234);
        const Run run = separate(mix, Layout{4410, 1102});
        QVERIFY(run.result.ok);
        QCOMPARE(run.modelCalls, 1);
        QCOMPARE(run.vocals.size(), mix.size());
        QVERIFY(maxError(run.vocals, mix, 0.5) < 1e-6);
    }

    // A decoder that runs dry before the clip's end: the rest is silence, the length stays.
    void earlyEndKeepsTheLength()
    {
        const std::vector<float> mix = synthMix(5000);
        const Run run = separate(mix, Layout{2000, 500}, 8000);
        QVERIFY(run.result.ok);
        QCOMPARE(run.vocals.size(), size_t(8000 * 2));
        for (size_t i = 5000 * 2; i < run.vocals.size(); ++i)
            QCOMPARE(run.vocals[i], 0.0f);
        QVERIFY(maxError(std::vector<float>(run.vocals.begin(), run.vocals.begin() + 10000), mix, 0.5) < 1e-6);
    }

    void cancelStops()
    {
        const std::vector<float> mix = synthMix(20000);
        int calls = 0;
        const Run run = separate(mix, Layout{2000, 500}, -1, [&calls](double) { return ++calls < 2; });
        QVERIFY(!run.result.ok);
        QVERIFY(run.result.cancelled);
        QCOMPARE(run.modelCalls, 2);
    }

    void modelFailureIsReported()
    {
        const std::vector<float> mix = synthMix(3000);
        int64_t pos = 0;
        const auto feed = [&](float *dst, int n) {
            n = int(std::min<int64_t>(n, int64_t(mix.size() / 2) - pos));
            std::copy_n(mix.begin() + ptrdiff_t(pos * 2), size_t(n) * 2, dst);
            pos += n;
            return n;
        };
        const auto broken = [](const float *, float *, QString *error) {
            *error = QStringLiteral("boom");
            return false;
        };
        const drift::stems::Result result = drift::stems::separateVocals(
            3000, Layout{1000, 250}, feed, broken, [](const float *, int) { return true; }, {});
        QVERIFY(!result.ok);
        QCOMPARE(result.error, QStringLiteral("boom"));
    }

    // The editor side: Both puts the voice on the clip and the music on a new linked track under
    // it, both in sync with the source window, and the whole thing is one undo step.
    void bothMakesTwoLinkedTracksInOneUndoStep()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("song.flac"));
        const std::vector<float> mix = synthMix(int64_t(4.0 * kRate));
        {
            drift::AudioFileWriter writer;
            QString error;
            QVERIFY2(writer.open(source, kRate, 2, &error), qPrintable(error));
            QVERIFY2(writer.writeFrames(mix.data(), int(mix.size() / 2), &error), qPrintable(error));
            QVERIFY2(writer.finish(&error), qPrintable(error));
        }

        AssetLibrary library;
        AppController state(&library);
        drift::Project &project = *state.project();
        drift::Clip clip;
        clip.id = QStringLiteral("song-clip");
        clip.type = drift::ClipType::Audio;
        clip.path = source;
        clip.name = QStringLiteral("Song");
        clip.timelineStart = drift::secondsToUs(1.5);
        clip.timelineDuration = drift::secondsToUs(3.0);
        clip.srcIn = drift::secondsToUs(0.5);
        clip.srcOut = drift::secondsToUs(3.5);
        clip.fadeInUs = drift::secondsToUs(0.2);
        clip.fadeOutUs = drift::secondsToUs(0.3);
        drift::Track audio{.type = drift::TrackType::Audio};
        audio.clips.append(clip);
        project.tracks().clear();
        project.tracks().append(drift::Track{.type = drift::TrackType::Video});
        project.tracks().append(audio);
        QVERIFY(!state.undoAvailable());

        // Half a second per segment, so three seconds cross several seams.
        const int segment = kRate / 2;
        state.vocals()->setModelForTesting(
            [segment](const float *in, float *out, QString *error) { return halfModel(segment, in, out, error); },
            segment, kRate);
        QVERIFY(state.vocals()->canProcess(1, 0));

        QSignalSpy finished(state.vocals(), &VocalRemoverController::finished);
        state.vocals()->separateClip(1, 0, VocalRemoverController::Both);
        QVERIFY(state.vocals()->running());
        QVERIFY2(finished.wait(120000), "the vocal remover did not finish");
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));
        QVERIFY(!state.vocals()->running());

        QCOMPARE(project.tracks().size(), 3);
        QCOMPARE(project.tracks().at(1).type, drift::TrackType::Audio);
        QCOMPARE(project.tracks().at(2).type, drift::TrackType::Audio);
        const drift::Clip vocals = project.tracks().at(1).clips.at(0);
        const drift::Clip music = project.tracks().at(2).clips.at(0);
        QCOMPARE(vocals.id, clip.id);
        QVERIFY(music.id != clip.id);
        QVERIFY(vocals.path.endsWith(QStringLiteral("-vocals.flac")));
        QVERIFY(music.path.endsWith(QStringLiteral("-music.flac")));
        QVERIFY(vocals.name.contains(QStringLiteral("vocals")));
        QVERIFY(music.name.contains(QStringLiteral("music")));
        // Linked, and on the same frames as the clip was.
        QVERIFY(!vocals.linkId.isEmpty());
        QCOMPARE(music.linkId, vocals.linkId);
        for (const drift::Clip &c : {vocals, music}) {
            QCOMPARE(c.timelineStart, clip.timelineStart);
            QCOMPARE(c.timelineDuration, clip.timelineDuration);
            QCOMPARE(c.srcIn, drift::TimeUs(0));
            QCOMPARE(c.srcOut, clip.srcOut - clip.srcIn);
            QCOMPARE(c.fadeInUs, clip.fadeInUs);
            QCOMPARE(c.fadeOutUs, clip.fadeOutUs);
        }

        // The files: the source window's length, and in sync with it frame for frame
        // (16-bit FLAC both ways, hence the small tolerance).
        const int64_t windowFrames = int64_t(3.0 * kRate);
        const std::vector<float> window(mix.begin() + ptrdiff_t(0.5 * kRate) * 2,
                                        mix.begin() + ptrdiff_t(0.5 * kRate + windowFrames) * 2);
        const std::vector<float> v = decodeAll(vocals.path);
        const std::vector<float> m = decodeAll(music.path);
        // AudioFileWriter pads the last FLAC frame with silence (under one encoder frame, at most
        // 4608), which the clip's srcOut never plays; everything before it is the window.
        for (const std::vector<float> *file : {&v, &m}) {
            const int64_t frames = int64_t(file->size() / 2);
            QVERIFY2(frames >= windowFrames && frames - windowFrames <= 4608,
                     qPrintable(QStringLiteral("%1 frames, expected %2").arg(frames).arg(windowFrames)));
            for (size_t i = size_t(windowFrames) * 2; i < file->size(); ++i)
                QVERIFY(std::abs((*file)[i]) < 1e-6f);
        }
        QVERIFY2(maxError(v, window, 0.5) < 1e-3, "vocals out of sync with the source window");
        QVERIFY2(maxError(m, window, 0.5) < 1e-3, "music out of sync with the source window");

        // One undo step takes it all back; redo brings both tracks again.
        QVERIFY(state.undoAvailable());
        state.undo();
        QVERIFY(!state.undoAvailable());
        QCOMPARE(project.tracks().size(), 2);
        QCOMPARE(project.tracks().at(1).clips.size(), 1);
        QCOMPARE(project.tracks().at(1).clips.at(0).path, source);
        QCOMPARE(project.tracks().at(1).clips.at(0).srcIn, clip.srcIn);
        QCOMPARE(project.tracks().at(1).clips.at(0).linkId, clip.linkId);
        state.redo();
        QCOMPARE(project.tracks().size(), 3);
        QCOMPARE(project.tracks().at(2).clips.at(0).linkId, project.tracks().at(1).clips.at(0).linkId);

        QFile::remove(vocals.path);
        QFile::remove(music.path);
    }

    void vocalsOnlyRepointsTheClip()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString source = dir.filePath(QStringLiteral("short.flac"));
        const std::vector<float> mix = synthMix(int64_t(1.2 * kRate), 11);
        {
            drift::AudioFileWriter writer;
            QString error;
            QVERIFY(writer.open(source, kRate, 2, &error));
            QVERIFY(writer.writeFrames(mix.data(), int(mix.size() / 2), &error));
            QVERIFY(writer.finish(&error));
        }
        AssetLibrary library;
        AppController state(&library);
        drift::Project &project = *state.project();
        drift::Clip clip;
        clip.id = QStringLiteral("short-clip");
        clip.type = drift::ClipType::Audio;
        clip.path = source;
        clip.timelineStart = 0;
        clip.timelineDuration = drift::secondsToUs(1.0);
        clip.srcIn = drift::secondsToUs(0.1);
        clip.srcOut = drift::secondsToUs(1.1);
        drift::Track audio{.type = drift::TrackType::Audio};
        audio.clips.append(clip);
        project.tracks().clear();
        project.tracks().append(audio);

        const int segment = kRate / 4;
        state.vocals()->setModelForTesting(
            [segment](const float *in, float *out, QString *error) { return halfModel(segment, in, out, error); },
            segment, kRate);
        QSignalSpy finished(state.vocals(), &VocalRemoverController::finished);
        state.vocals()->separateClip(0, 0, VocalRemoverController::VocalsOnly);
        QVERIFY(finished.wait(60000));
        QVERIFY2(finished.at(0).at(0).toBool(), qPrintable(finished.at(0).at(1).toString()));

        QCOMPARE(project.tracks().size(), 1);
        const drift::Clip out = project.tracks().at(0).clips.at(0);
        QCOMPARE(out.id, clip.id);
        QVERIFY(out.path.endsWith(QStringLiteral("-vocals.flac")));
        QVERIFY(out.linkId.isEmpty());
        QCOMPARE(out.timelineStart, clip.timelineStart);
        QCOMPARE(out.srcOut - out.srcIn, clip.srcOut - clip.srcIn);

        state.undo();
        QVERIFY(!state.undoAvailable());
        QCOMPARE(project.tracks().at(0).clips.at(0).path, source);
        QFile::remove(out.path);
    }
};

QTEST_MAIN(VocalRemoverTest)
#include "tst_vocalremover.moc"
