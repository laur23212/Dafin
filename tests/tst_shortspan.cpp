// Dafin: where a Smart Shorts highlight sits on the timeline when its clip is sped up, ramped or
// reversed. The preview plays that span; it has to land on the highlight's own picture.

#include "core/edit/ShortClipPlan.h"
#include "core/project/clip/Clip.h"

#include <QtTest>

#include <cmath>

using namespace drift;

namespace {

Clip baseClip()
{
    Clip clip;
    clip.type = ClipType::Video;
    clip.srcIn = secondsToUs(0.0);
    clip.srcOut = secondsToUs(20.0);
    clip.timelineStart = secondsToUs(10.0);
    clip.timelineDuration = secondsToUs(20.0);
    return clip;
}

// The span must map back to the highlight through the clip's own timeline-to-source mapping.
void verifyRoundTrip(const Clip &clip, double sourceStart, double sourceEnd)
{
    const QPair<double, double> span = shorts::timelineSpanOfSource(clip, sourceStart, sourceEnd);
    QVERIFY(span.first <= span.second);
    const double lo = std::min(usToSeconds(clip.timelineToSourceUs(secondsToUs(span.first))),
                               usToSeconds(clip.timelineToSourceUs(secondsToUs(span.second))));
    const double hi = std::max(usToSeconds(clip.timelineToSourceUs(secondsToUs(span.first))),
                               usToSeconds(clip.timelineToSourceUs(secondsToUs(span.second))));
    QVERIFY2(std::abs(lo - sourceStart) < 0.02, qPrintable(QStringLiteral("start %1 vs %2").arg(lo).arg(sourceStart)));
    QVERIFY2(std::abs(hi - sourceEnd) < 0.02, qPrintable(QStringLiteral("end %1 vs %2").arg(hi).arg(sourceEnd)));
}

SpeedCurve ramp()
{
    SpeedCurve curve;
    SpeedPoint a;
    a.pos = 0.0;
    a.speed = 0.5;
    SpeedPoint b;
    b.pos = 1.0;
    b.speed = 4.0;
    curve.setPoints({a, b});
    return curve;
}

} // namespace

class TestShortSpan : public QObject
{
    Q_OBJECT

private slots:
    void plainClip()
    {
        Clip clip = baseClip();
        const auto span = shorts::timelineSpanOfSource(clip, 4.0, 8.0);
        QVERIFY(std::abs(span.first - 14.0) < 1e-6);
        QVERIFY(std::abs(span.second - 18.0) < 1e-6);
    }

    void doubleSpeed()
    {
        Clip clip = baseClip();
        clip.speed = 2.0;
        clip.timelineDuration = secondsToUs(10.0);
        const auto span = shorts::timelineSpanOfSource(clip, 4.0, 8.0);
        QVERIFY(std::abs(span.first - 12.0) < 1e-6);
        QVERIFY(std::abs(span.second - 14.0) < 1e-6);
    }

    void reversedClip()
    {
        Clip clip = baseClip();
        clip.reverse = true;
        verifyRoundTrip(clip, 4.0, 8.0);
    }

    void speedCurveClip()
    {
        Clip clip = baseClip();
        clip.speedCurve = ramp();
        clip.syncDurationFromSpeedCurve();
        verifyRoundTrip(clip, 4.0, 8.0);
        verifyRoundTrip(clip, 12.0, 19.0);
    }

    void speedCurveAndReverse()
    {
        Clip clip = baseClip();
        clip.speedCurve = ramp();
        clip.reverse = true;
        clip.syncDurationFromSpeedCurve();
        verifyRoundTrip(clip, 4.0, 8.0);
        verifyRoundTrip(clip, 12.0, 19.0);
    }
};

QTEST_GUILESS_MAIN(TestShortSpan)
#include "tst_shortspan.moc"
