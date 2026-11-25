#include <QtTest>

#include "engine/decode/interp/FrameBracket.h"

using namespace drift;
using namespace drift::interp;

// The pair-and-alpha decision behind smooth slow motion, tested without a decoder or a video
// file: it is pure arithmetic on the timestamps a decoder reports, and keeping it that way is
// what lets the preview and the export reach the same answer.
class FrameInterpTest : public QObject
{
    Q_OBJECT

private slots:
    void alphaComesFromRealTimestampsNotNominalRate();
    void landingOnAFrameNeedsNoSynthesis();
    void aMissingSecondFrameRepeats();
    void duplicateOrBackwardTimestampsRepeat();
    void aPairThatDoesNotStraddleRepeats();
    void theTrimWindowKeepsCutsOutOfTheBlend();
    void longVariableRateGapsAreNotTweened();
    void aDeclaredCutRepeats();
    void samplingStaysTheDefaultBehaviour();
    void reverseIsJustAForwardReadOfAMirroredWindow();
};

namespace {

constexpr TimeUs ms(TimeUs v)
{
    return v * 1000;
}

// A query with the guards switched off, so each test can turn on the one it is about.
Query plainQuery(TimeUs readUs)
{
    Query q;
    q.readUs = readUs;
    return q;
}

} // namespace

// Stage 19's irregular sequence. A 30 fps assumption would put frame boundaries at 33.3 ms
// intervals; the real frames are nowhere near that, and alpha has to follow the real ones.
void FrameInterpTest::alphaComesFromRealTimestampsNotNominalRate()
{
    const Pair pair{ms(80), ms(101), true, true};

    // A quarter of the way through a 21 ms gap.
    Decision d = decide(Mode::Blending, pair, plainQuery(ms(80) + 5250));
    QCOMPARE(d.route, Route::Blend);
    QCOMPARE(d.reason, Reason::None);
    QVERIFY(qAbs(d.alpha - 0.25) < 1e-9);

    // Halfway.
    d = decide(Mode::Blending, pair, plainQuery(ms(80) + 10500));
    QCOMPARE(d.route, Route::Blend);
    QVERIFY(qAbs(d.alpha - 0.5) < 1e-9);

    // The last microsecond before B is still inside this pair, not the next one.
    d = decide(Mode::Blending, pair, plainQuery(ms(101) - 1));
    QCOMPARE(d.route, Route::Blend);
    QVERIFY(d.alpha < 1.0);
    QVERIFY(d.alpha > 0.999);

    // Across the whole irregular run, alpha stays in range and the route never changes
    // because of where in the sequence we are.
    const TimeUs pts[] = {ms(0), ms(32), ms(80), ms(101), ms(148), ms(179)};
    for (size_t i = 0; i + 1 < std::size(pts); ++i) {
        const Pair p{pts[i], pts[i + 1], true, true};
        for (int step = 1; step < 8; ++step) {
            const TimeUs at = pts[i] + (pts[i + 1] - pts[i]) * step / 8;
            const Decision each = decide(Mode::Blending, p, plainQuery(at));
            QCOMPARE(each.route, Route::Blend);
            QVERIFY(each.alpha >= 0.0 && each.alpha <= 1.0);
        }
    }
}

void FrameInterpTest::landingOnAFrameNeedsNoSynthesis()
{
    const Pair pair{ms(80), ms(101), true, true};

    Decision d = decide(Mode::Blending, pair, plainQuery(ms(80)));
    QCOMPARE(d.route, Route::Exact);
    QCOMPARE(d.reason, Reason::OnFrame);
    QCOMPARE(d.alpha, 0.0);

    // Within tolerance counts as landing on it: a clip at normal speed against a matching
    // output rate must not blend every frame with its neighbour at alpha 0.001.
    Query q = plainQuery(ms(80) + 400);
    q.onFrameToleranceUs = 500;
    d = decide(Mode::Blending, pair, q);
    QCOMPARE(d.route, Route::Exact);

    // Just outside it does synthesize.
    q.readUs = ms(80) + 600;
    d = decide(Mode::Blending, pair, q);
    QCOMPARE(d.route, Route::Blend);

    // Flow is held to the same shortcut; there is nothing to invent on a real frame.
    QCOMPARE(decide(Mode::Flow, pair, plainQuery(ms(80))).route, Route::Exact);
}

void FrameInterpTest::aMissingSecondFrameRepeats()
{
    // End of stream: a cover and no peek, however much the time wants a tween.
    const Pair coverOnly{ms(179), 0, true, false};
    const Decision d = decide(Mode::Blending, coverOnly, plainQuery(ms(185)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::NoPair);

    // Nothing at all.
    const Decision none = decide(Mode::Flow, Pair{}, plainQuery(ms(5)));
    QCOMPARE(none.route, Route::Repeat);
    QCOMPARE(none.reason, Reason::NoPair);
}

void FrameInterpTest::duplicateOrBackwardTimestampsRepeat()
{
    // Two frames stamped the same time: the span is zero and dividing by it is the bug this
    // guard exists for.
    Decision d = decide(Mode::Blending, Pair{ms(80), ms(80), true, true}, plainQuery(ms(80)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::PairNotOrdered);

    // A stream that goes backwards.
    d = decide(Mode::Blending, Pair{ms(101), ms(80), true, true}, plainQuery(ms(90)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::PairNotOrdered);
}

void FrameInterpTest::aPairThatDoesNotStraddleRepeats()
{
    const Pair pair{ms(80), ms(101), true, true};

    // Seeking to before the first frame of a stream leaves the decoder holding a frame whose
    // timestamp is *after* the request, so this is a state that really happens.
    Decision d = decide(Mode::Blending, pair, plainQuery(ms(40)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::PairNotStraddling);

    // At or past B the pair has already been overtaken; B belongs to the next interval.
    d = decide(Mode::Blending, pair, plainQuery(ms(101)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::PairNotStraddling);
}

void FrameInterpTest::theTrimWindowKeepsCutsOutOfTheBlend()
{
    Query q = plainQuery(ms(95));
    q.haveTrim = true;
    q.trimLoUs = ms(50);
    q.trimHiUs = ms(100);

    // A peek at the out point is the first frame of whatever is cut in next. Blending it would
    // put a one-frame ghost of the incoming shot on the end of the outgoing one.
    Decision d = decide(Mode::Blending, Pair{ms(80), ms(100), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // Past it, likewise.
    d = decide(Mode::Blending, Pair{ms(80), ms(120), true, true}, q);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // A cover before the in point is the outgoing shot, by the same argument.
    q.readUs = ms(52);
    d = decide(Mode::Blending, Pair{ms(48), ms(70), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // Wholly inside, it blends.
    q.readUs = ms(60);
    d = decide(Mode::Blending, Pair{ms(55), ms(70), true, true}, q);
    QCOMPARE(d.route, Route::Blend);
    QCOMPARE(d.reason, Reason::None);

    // Without a trim window nothing is refused on that ground — the caller has to supply it,
    // and this test is here so that forgetting to is a test failure somewhere rather than a
    // ghost at every cut.
    Query noTrim = plainQuery(ms(95));
    QCOMPARE(decide(Mode::Blending, Pair{ms(80), ms(100), true, true}, noTrim).route, Route::Blend);
}

void FrameInterpTest::longVariableRateGapsAreNotTweened()
{
    Query q = plainQuery(ms(500));
    q.maxGapUs = kDefaultMaxGapUs;

    // A screen recorder that emitted nothing for a second while the picture was still. What
    // sits between those two frames is not a mix of them.
    Decision d = decide(Mode::Blending, Pair{ms(100), ms(1100), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::GapTooLong);

    // An ordinary gap is fine.
    q.readUs = ms(110);
    d = decide(Mode::Blending, Pair{ms(100), ms(140), true, true}, q);
    QCOMPARE(d.route, Route::Blend);

    // Zero means no limit.
    Query unlimited = plainQuery(ms(500));
    QCOMPARE(decide(Mode::Blending, Pair{ms(100), ms(1100), true, true}, unlimited).route,
             Route::Blend);
}

void FrameInterpTest::aDeclaredCutRepeats()
{
    Query q = plainQuery(ms(90));
    q.knownCut = true;
    const Decision d = decide(Mode::Flow, Pair{ms(80), ms(101), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::KnownCut);
}

void FrameInterpTest::samplingStaysTheDefaultBehaviour()
{
    // The mode a project gets when nothing asked for anything, and the one every guard above
    // falls back to: the frame at or before the time, held.
    const Decision d = decide(Mode::Sampling, Pair{ms(80), ms(101), true, true}, plainQuery(ms(90)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::ModeIsSampling);
    QCOMPARE(d.alpha, 0.0);
}

void FrameInterpTest::reverseIsJustAForwardReadOfAMirroredWindow()
{
    // A clip trimmed to [2 s, 5 s] of a file, played backwards, reading a proxy that covers
    // [1 s, 6 s] flipped end for end. The proxy is matched by containment, so it holds a
    // second of footage either side of the trim that must stay out of the blend.
    const TimeUs srcIn = ms(2000);
    const TimeUs srcOut = ms(5000);
    const TimeUs coverEnd = ms(6000);

    const ReadWindow w = readWindowMirrored(srcIn, srcOut, coverEnd);
    QCOMPARE(w.loUs, ms(1000));
    QCOMPARE(w.hiUs, ms(4000));

    Query q;
    q.haveTrim = true;
    q.trimLoUs = w.loUs;
    q.trimHiUs = w.hiUs;

    // The clip's own source times land inside the window, and read time runs *forwards* as the
    // timeline advances even though source time runs backwards. That is the property the whole
    // design leans on: one direction, so no inverted alpha on reversed clips.
    const TimeUs readAtClipStart = coverEnd - srcOut; // timeline start reads here
    const TimeUs readAtClipEnd = coverEnd - srcIn;
    QCOMPARE(readAtClipStart, ms(1000));
    QCOMPARE(readAtClipEnd, ms(4000));
    QVERIFY(readAtClipStart < readAtClipEnd);

    // Mid-clip blends, with alpha measured forwards in the file being read.
    q.readUs = ms(2500);
    Decision d = decide(Mode::Blending, Pair{ms(2480), ms(2520), true, true}, q);
    QCOMPARE(d.route, Route::Blend);
    QVERIFY(qAbs(d.alpha - 0.5) < 1e-9);

    // The footage the proxy holds past the mirrored out point is still refused.
    q.readUs = ms(3990);
    d = decide(Mode::Blending, Pair{ms(3980), ms(4020), true, true}, q);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // Forwards, the window is simply the trim — which is also what an un-proxied reversed clip
    // uses, since it walks the same trim downwards.
    const ReadWindow f = readWindowForward(srcIn, srcOut);
    QCOMPARE(f.loUs, srcIn);
    QCOMPARE(f.hiUs, srcOut);
}

QTEST_APPLESS_MAIN(FrameInterpTest)
#include "tst_frameinterp.moc"
