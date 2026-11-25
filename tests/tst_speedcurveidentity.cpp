// Dafin: three places that read a clip's timing and have to notice its speed curve.
//
//   * the audio tempo of a reversed, ramped clip (the mixer told the stretcher a negative rate, which
//     it clamped to its floor, so the sound crawled at 0.1x against a picture that was ramping);
//   * the identity of the beat / onset cache (it saw only that a clip *has* a curve, not its shape,
//     the audio stream it plays or the file it reads);
//   * the tracker's idea of "same placement" and of how far apart two samples may sit.
//
// Pure logic: no decoder, no GPU, no ffmpeg.

#include "core/animation/SpeedCurve.h"
#include "core/animation/SpeedRamp.h"
#include "core/project/AudioLayoutFingerprint.h"
#include "core/project/Project.h"
#include "core/tracking/TrackerTiming.h"

#include <QtTest>

#include <cmath>

using drift::Clip;
using drift::SpeedCurve;
using drift::SpeedPoint;
using drift::TimeUs;

namespace {

constexpr TimeUs kSourceUs = 4'000'000;
constexpr int kRate = 48'000;
constexpr int kBlockFrames = 1024;
constexpr TimeUs kBlockUs = static_cast<TimeUs>(kBlockFrames) * drift::kUsPerSecond / kRate;

SpeedCurve ramp(double from, double to)
{
    QList<SpeedPoint> points;
    points.append(SpeedPoint{.pos = 0.0, .speed = from});
    points.append(SpeedPoint{.pos = 1.0, .speed = to});
    SpeedCurve curve;
    curve.setPoints(points);
    return curve;
}

Clip rampedClip(const SpeedCurve &curve, bool reverse)
{
    Clip clip;
    clip.id = QStringLiteral("c");
    clip.type = drift::ClipType::Audio;
    clip.assetId = QStringLiteral("asset-1");
    clip.path = QStringLiteral("/media/music.wav");
    clip.timelineStart = 1'000'000;
    clip.srcIn = 0;
    clip.srcOut = kSourceUs;
    clip.reverse = reverse;
    clip.speedCurve = curve;
    clip.syncDurationFromSpeedCurve();
    return clip;
}

drift::Project projectWith(const Clip &clip)
{
    drift::Project project;
    project.tracks().clear();
    drift::Track track{.type = drift::TrackType::Audio};
    track.clips.append(clip);
    project.tracks().append(track);
    return project;
}

} // namespace

class TstSpeedCurveIdentity : public QObject
{
    Q_OBJECT

private slots:
    // ---- the audio tempo of a reversed ramp --------------------------------------------------
    void reversedRampKeepsTheAudioTempoPositive();
    void reversedRampPlaysTheSameTempoAsTheForwardOne();
    void tempoOverAReversedRampCoversTheSourceSpan();
    void overhangingFinalBlockIsPositiveEitherWay();
    void scalarSpeedTempoIsUnchanged();

    // ---- the beat cache's identity ------------------------------------------------------------
    void fingerprintIsStableForTheSameProject();
    void fingerprintSeesTheShapeOfTheSpeedCurve();
    void fingerprintSeesTheAudioStreamAndTheFile();
    void fingerprintIgnoresTransformKeyframes();

    // ---- the tracker's placement and match tolerance ------------------------------------------
    void placementSeesTheShapeOfTheSpeedCurve();
    void placementIgnoresTransformEdits();
    void toleranceCoversTheSampleSpacingOfARamp();
    void toleranceForAScalarSpeedIsHalfTheSampleSpacing();
};

// ---------------------------------------------------------------------------------------------

void TstSpeedCurveIdentity::reversedRampKeepsTheAudioTempoPositive()
{
    const Clip clip = rampedClip(ramp(0.5, 2.0), true);
    QVERIFY(clip.reverse);
    QVERIFY(clip.hasSpeedCurve());
    int blocks = 0;
    for (TimeUs at = clip.timelineStart; at + kBlockUs <= clip.timelineEnd(); at += kBlockUs, ++blocks) {
        const double tempo = drift::clipAudioTempo(clip, at, at + kBlockUs);
        QVERIFY2(tempo > 0.0,
                 qPrintable(QStringLiteral("block %1 tempo %2").arg(blocks).arg(tempo)));
        // Inside the ramp's own range, never a clamp artefact.
        QVERIFY2(tempo > 0.45 && tempo < 2.1,
                 qPrintable(QStringLiteral("block %1 tempo %2").arg(blocks).arg(tempo)));
    }
    QVERIFY(blocks > 50);
}

void TstSpeedCurveIdentity::reversedRampPlaysTheSameTempoAsTheForwardOne()
{
    // The curve is in playback order (SpeedPoint::pos), so a reversed clip ramps exactly as the
    // forward one does; only the direction the source is walked differs.
    const SpeedCurve curve = ramp(0.5, 2.0);
    const Clip forward = rampedClip(curve, false);
    const Clip reversed = rampedClip(curve, true);
    QCOMPARE(forward.timelineDuration, reversed.timelineDuration);
    for (TimeUs at = forward.timelineStart; at + kBlockUs <= forward.timelineEnd(); at += 17 * kBlockUs) {
        const double f = drift::clipAudioTempo(forward, at, at + kBlockUs);
        const double r = drift::clipAudioTempo(reversed, at, at + kBlockUs);
        QVERIFY2(std::abs(f - r) < 1e-6,
                 qPrintable(QStringLiteral("at %1: forward %2 reversed %3").arg(at).arg(f).arg(r)));
    }
}

void TstSpeedCurveIdentity::tempoOverAReversedRampCoversTheSourceSpan()
{
    // Tempo times duration, summed over the clip, is the source the stretcher has to consume. A
    // negative or floor-clamped tempo is what made the audio fall behind the picture.
    for (const bool reverse : {false, true}) {
        const Clip clip = rampedClip(ramp(0.5, 2.0), reverse);
        double consumedUs = 0.0;
        TimeUs at = clip.timelineStart;
        for (; at + kBlockUs <= clip.timelineEnd(); at += kBlockUs)
            consumedUs += drift::clipAudioTempo(clip, at, at + kBlockUs) * static_cast<double>(kBlockUs);
        const double sourceSeenUs = static_cast<double>(kSourceUs) * (double(at - clip.timelineStart) / double(clip.timelineDuration));
        QVERIFY2(std::abs(consumedUs - sourceSeenUs) < 0.02 * sourceSeenUs,
                 qPrintable(QStringLiteral("reverse=%1 consumed %2 of about %3 us")
                                .arg(reverse).arg(consumedUs).arg(sourceSeenUs)));
    }
}

void TstSpeedCurveIdentity::overhangingFinalBlockIsPositiveEitherWay()
{
    for (const bool reverse : {false, true}) {
        const Clip clip = rampedClip(ramp(0.5, 2.0), reverse);
        const TimeUs at = clip.timelineEnd() - kBlockUs / 2; // the block runs past the end
        const double tempo = drift::clipAudioTempo(clip, at, at + kBlockUs);
        QVERIFY2(tempo > 1.5 && tempo < 2.1, qPrintable(QStringLiteral("reverse=%1 tempo %2").arg(reverse).arg(tempo)));
    }
}

void TstSpeedCurveIdentity::scalarSpeedTempoIsUnchanged()
{
    for (const bool reverse : {false, true}) {
        Clip clip;
        clip.timelineStart = 0;
        clip.srcIn = 0;
        clip.srcOut = kSourceUs;
        clip.speed = 1.5;
        clip.reverse = reverse;
        clip.timelineDuration = static_cast<TimeUs>(kSourceUs / 1.5);
        QCOMPARE(drift::clipAudioTempo(clip, 0, kBlockUs), 1.5);
    }
}

// ---------------------------------------------------------------------------------------------

void TstSpeedCurveIdentity::fingerprintIsStableForTheSameProject()
{
    const Clip clip = rampedClip(ramp(0.5, 2.0), false);
    QCOMPARE(drift::audioLayoutFingerprint(projectWith(clip)), drift::audioLayoutFingerprint(projectWith(clip)));
}

void TstSpeedCurveIdentity::fingerprintSeesTheShapeOfTheSpeedCurve()
{
    // 0.5 -> 2 and 2 -> 0.5 take the same time over the same source, so the clip's duration and
    // every scalar field are equal; the beats inside it are in different places.
    Clip up = rampedClip(ramp(0.5, 2.0), false);
    Clip down = rampedClip(ramp(2.0, 0.5), false);
    down.timelineDuration = up.timelineDuration;
    QVERIFY(up.hasSpeedCurve() && down.hasSpeedCurve());
    QVERIFY(drift::audioLayoutFingerprint(projectWith(up)) != drift::audioLayoutFingerprint(projectWith(down)));

    // A handle dragged without moving the point's value reshapes the ramp too.
    Clip bent = up;
    QList<SpeedPoint> points = bent.speedCurve.points();
    points[0].outDx = 0.3;
    points[0].outDy = 1.0;
    bent.speedCurve.setPoints(points);
    bent.timelineDuration = up.timelineDuration;
    QVERIFY(drift::audioLayoutFingerprint(projectWith(up)) != drift::audioLayoutFingerprint(projectWith(bent)));
}

void TstSpeedCurveIdentity::fingerprintSeesTheAudioStreamAndTheFile()
{
    const Clip base = rampedClip(ramp(0.5, 2.0), false);
    const QByteArray before = drift::audioLayoutFingerprint(projectWith(base));

    Clip otherStream = base;
    otherStream.audioStreamIndex = 1; // a second language track plays different audio
    QVERIFY(drift::audioLayoutFingerprint(projectWith(otherStream)) != before);

    Clip relinked = base;
    relinked.path = QStringLiteral("/media/other-take.wav"); // same asset id, another file
    QVERIFY(drift::audioLayoutFingerprint(projectWith(relinked)) != before);
}

void TstSpeedCurveIdentity::fingerprintIgnoresTransformKeyframes()
{
    // The contract the digest was written to: moving a picture does not move the beats.
    Clip base = rampedClip(ramp(0.5, 2.0), false);
    const QByteArray before = drift::audioLayoutFingerprint(projectWith(base));
    base.transformX.setKeyframe(0, 10.0);
    base.transformX.setKeyframe(500'000, 80.0);
    QCOMPARE(drift::audioLayoutFingerprint(projectWith(base)), before);
}

// ---------------------------------------------------------------------------------------------

void TstSpeedCurveIdentity::placementSeesTheShapeOfTheSpeedCurve()
{
    Clip up = rampedClip(ramp(0.5, 2.0), false);
    Clip down = rampedClip(ramp(2.0, 0.5), false);
    down.timelineDuration = up.timelineDuration; // same length, different ramp
    QVERIFY(drift::sameTrackerPlacement(up, up));
    QVERIFY(!drift::sameTrackerPlacement(up, down));
}

void TstSpeedCurveIdentity::placementIgnoresTransformEdits()
{
    const Clip a = rampedClip(ramp(0.5, 2.0), false);
    Clip b = a;
    b.transformX.setKeyframe(0, 30.0);
    QVERIFY(drift::sameTrackerPlacement(a, b));
}

void TstSpeedCurveIdentity::toleranceCoversTheSampleSpacingOfARamp()
{
    // Samples sit one project frame apart on the timeline, so in the source they are `frame x speed`
    // apart; a lookup may land half of that away. At the fast end of a 1x -> 8x ramp that is four
    // frames, not the half frame a clip with a curve and speed == 1 used to get.
    constexpr TimeUs frame = drift::frameDurationUs(30);
    const Clip clip = rampedClip(ramp(1.0, 8.0), false);
    const TimeUs tolerance = drift::trackerMatchToleranceUs(frame, clip);
    QVERIFY2(tolerance >= static_cast<TimeUs>(std::llround(double(frame) * 8.0 / 2.0)) - 1,
             qPrintable(QStringLiteral("tolerance %1 us, frame %2 us").arg(tolerance).arg(frame)));
}

void TstSpeedCurveIdentity::toleranceForAScalarSpeedIsHalfTheSampleSpacing()
{
    constexpr TimeUs frame = drift::frameDurationUs(30);
    Clip clip;
    clip.srcOut = kSourceUs;
    clip.speed = 4.0;
    QCOMPARE(drift::trackerMatchToleranceUs(frame, clip), static_cast<TimeUs>(std::llround(double(frame) * 4.0 / 2.0)));
    clip.speed = 0.25; // slow motion never goes under half a frame
    QCOMPARE(drift::trackerMatchToleranceUs(frame, clip), static_cast<TimeUs>(std::llround(double(frame) / 2.0)));
}

QTEST_APPLESS_MAIN(TstSpeedCurveIdentity)
#include "tst_speedcurveidentity.moc"
