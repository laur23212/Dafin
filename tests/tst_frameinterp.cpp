#include <QtTest>

#include "engine/decode/interp/FlowEstimator.h"
#include "engine/decode/interp/FlowSynth.h"
#include "engine/decode/interp/FrameBlend.h"
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

    void blendIsExactAtBothEnds();
    void blendHappensInLinearLight();
    void blendRefusesWhatItCannotMix();
    void blendKeepsHiddenColourOutOfEdges();
    void blendIsOrderIndependentAndRepeatable();

    void flowDeclinesRatherThanGuessOnAFeaturelessWall();
    void flowBeatsBlendOnACleanTranslation();
    void flowIsExactAtBothEnds();
    void flowIsSymmetricUnderSwappingTheFrames();
    void flowNeverPunchesAHoleInThePicture();
    void confidenceAndVisibilityAreSeparateSignals();

    void theModeSurvivesAProjectFileRoundTrip();
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


// A deterministic textured field, smooth enough that a gradient-based solver has something to
// lock onto and irregular enough that it is not a repeating pattern -- a repeating one is
// genuinely ambiguous and a solver that claimed otherwise would be the thing at fault.
double valueNoise(double x, double y)
{
    const auto hash = [](int i, int j) {
        quint32 h = static_cast<quint32>(i) * 374761393u + static_cast<quint32>(j) * 668265263u;
        h = (h ^ (h >> 13)) * 1274126177u;
        return static_cast<double>((h ^ (h >> 16)) & 0xffffu) / 65535.0;
    };
    const auto octave = [&](double scale) {
        const double sx = x / scale;
        const double sy = y / scale;
        const int x0 = static_cast<int>(std::floor(sx));
        const int y0 = static_cast<int>(std::floor(sy));
        const double fx = sx - x0;
        const double fy = sy - y0;
        const double ex = fx * fx * (3.0 - 2.0 * fx);
        const double ey = fy * fy * (3.0 - 2.0 * fy);
        const double v00 = hash(x0, y0);
        const double v10 = hash(x0 + 1, y0);
        const double v01 = hash(x0, y0 + 1);
        const double v11 = hash(x0 + 1, y0 + 1);
        return (1 - ey) * ((1 - ex) * v00 + ex * v10) + ey * ((1 - ex) * v01 + ex * v11);
    };
    return 0.6 * octave(9.0) + 0.3 * octave(4.0) + 0.1 * octave(21.0);
}

// A window onto that field, offset by whole pixels. Whole pixels on purpose: the shifted frames
// are then exact crops of one image rather than resamplings of each other, so the middle frame
// really is the right answer and the test is not grading the solver against a blur.
QImage texturedWindow(int offsetX, int offsetY, int width = 192, int height = 144)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    for (int y = 0; y < height; ++y) {
        quint8 *row = image.scanLine(y);
        for (int x = 0; x < width; ++x) {
            const double n = valueNoise(x + offsetX, y + offsetY);
            quint8 *p = row + x * 4;
            p[0] = static_cast<quint8>(std::clamp(int(n * 255.0), 0, 255));
            p[1] = static_cast<quint8>(std::clamp(int(n * 220.0) + 20, 0, 255));
            p[2] = static_cast<quint8>(std::clamp(int((1.0 - n) * 200.0) + 30, 0, 255));
            p[3] = 255;
        }
    }
    return image;
}

QImage flatWall(int width = 192, int height = 144, int level = 130)
{
    QImage image(width, height, QImage::Format_RGBA8888);
    image.fill(QColor(level, level, level, 255));
    return image;
}

double psnrAgainst(const QImage &candidate, const QImage &truth)
{
    const QImage c = candidate.convertToFormat(QImage::Format_RGBA8888);
    const QImage t = truth.convertToFormat(QImage::Format_RGBA8888);
    double squared = 0.0;
    qint64 count = 0;
    for (int y = 0; y < t.height(); ++y) {
        const quint8 *rc = c.constScanLine(y);
        const quint8 *rt = t.constScanLine(y);
        for (int x = 0; x < t.width(); ++x) {
            for (int ch = 0; ch < 3; ++ch) {
                const double d = double(rc[x * 4 + ch]) - double(rt[x * 4 + ch]);
                squared += d * d;
                ++count;
            }
        }
    }
    if (count == 0)
        return 0.0;
    const double mse = squared / double(count);
    return mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
}

int maxAbsDifference(const QImage &lhs, const QImage &rhs)
{
    const QImage a = lhs.convertToFormat(QImage::Format_RGBA8888);
    const QImage b = rhs.convertToFormat(QImage::Format_RGBA8888);
    int worst = 0;
    for (int y = 0; y < a.height(); ++y) {
        const quint8 *ra = a.constScanLine(y);
        const quint8 *rb = b.constScanLine(y);
        for (int i = 0; i < a.width() * 4; ++i)
            worst = std::max(worst, std::abs(int(ra[i]) - int(rb[i])));
    }
    return worst;
}

} // namespace

// Stage 19's irregular sequence. A 30 fps assumption would put frame boundaries at 33.3 ms
// intervals; the real frames are nowhere near that, and alpha has to follow the real ones.
void FrameInterpTest::alphaComesFromRealTimestampsNotNominalRate()
{
    const Pair pair{ms(80), ms(101), true, true};

    // A quarter of the way through a 21 ms gap.
    Decision d = decide(FrameInterpMode::Blending, pair, plainQuery(ms(80) + 5250));
    QCOMPARE(d.route, Route::Blend);
    QCOMPARE(d.reason, Reason::None);
    QVERIFY(qAbs(d.alpha - 0.25) < 1e-9);

    // Halfway.
    d = decide(FrameInterpMode::Blending, pair, plainQuery(ms(80) + 10500));
    QCOMPARE(d.route, Route::Blend);
    QVERIFY(qAbs(d.alpha - 0.5) < 1e-9);

    // The last microsecond before B is still inside this pair, not the next one.
    d = decide(FrameInterpMode::Blending, pair, plainQuery(ms(101) - 1));
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
            const Decision each = decide(FrameInterpMode::Blending, p, plainQuery(at));
            QCOMPARE(each.route, Route::Blend);
            QVERIFY(each.alpha >= 0.0 && each.alpha <= 1.0);
        }
    }
}

void FrameInterpTest::landingOnAFrameNeedsNoSynthesis()
{
    const Pair pair{ms(80), ms(101), true, true};

    Decision d = decide(FrameInterpMode::Blending, pair, plainQuery(ms(80)));
    QCOMPARE(d.route, Route::Exact);
    QCOMPARE(d.reason, Reason::OnFrame);
    QCOMPARE(d.alpha, 0.0);

    // Within tolerance counts as landing on it: a clip at normal speed against a matching
    // output rate must not blend every frame with its neighbour at alpha 0.001.
    Query q = plainQuery(ms(80) + 400);
    q.onFrameToleranceUs = 500;
    d = decide(FrameInterpMode::Blending, pair, q);
    QCOMPARE(d.route, Route::Exact);

    // Just outside it does synthesize.
    q.readUs = ms(80) + 600;
    d = decide(FrameInterpMode::Blending, pair, q);
    QCOMPARE(d.route, Route::Blend);

    // Flow is held to the same shortcut; there is nothing to invent on a real frame.
    QCOMPARE(decide(FrameInterpMode::Flow, pair, plainQuery(ms(80))).route, Route::Exact);
}

void FrameInterpTest::aMissingSecondFrameRepeats()
{
    // End of stream: a cover and no peek, however much the time wants a tween.
    const Pair coverOnly{ms(179), 0, true, false};
    const Decision d = decide(FrameInterpMode::Blending, coverOnly, plainQuery(ms(185)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::NoPair);

    // Nothing at all.
    const Decision none = decide(FrameInterpMode::Flow, Pair{}, plainQuery(ms(5)));
    QCOMPARE(none.route, Route::Repeat);
    QCOMPARE(none.reason, Reason::NoPair);
}

void FrameInterpTest::duplicateOrBackwardTimestampsRepeat()
{
    // Two frames stamped the same time: the span is zero and dividing by it is the bug this
    // guard exists for.
    Decision d = decide(FrameInterpMode::Blending, Pair{ms(80), ms(80), true, true}, plainQuery(ms(80)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::PairNotOrdered);

    // A stream that goes backwards.
    d = decide(FrameInterpMode::Blending, Pair{ms(101), ms(80), true, true}, plainQuery(ms(90)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::PairNotOrdered);
}

void FrameInterpTest::aPairThatDoesNotStraddleRepeats()
{
    const Pair pair{ms(80), ms(101), true, true};

    // Seeking to before the first frame of a stream leaves the decoder holding a frame whose
    // timestamp is *after* the request, so this is a state that really happens.
    Decision d = decide(FrameInterpMode::Blending, pair, plainQuery(ms(40)));
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::PairNotStraddling);

    // At or past B the pair has already been overtaken; B belongs to the next interval.
    d = decide(FrameInterpMode::Blending, pair, plainQuery(ms(101)));
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
    Decision d = decide(FrameInterpMode::Blending, Pair{ms(80), ms(100), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // Past it, likewise.
    d = decide(FrameInterpMode::Blending, Pair{ms(80), ms(120), true, true}, q);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // A cover before the in point is the outgoing shot, by the same argument.
    q.readUs = ms(52);
    d = decide(FrameInterpMode::Blending, Pair{ms(48), ms(70), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // Wholly inside, it blends.
    q.readUs = ms(60);
    d = decide(FrameInterpMode::Blending, Pair{ms(55), ms(70), true, true}, q);
    QCOMPARE(d.route, Route::Blend);
    QCOMPARE(d.reason, Reason::None);

    // Without a trim window nothing is refused on that ground — the caller has to supply it,
    // and this test is here so that forgetting to is a test failure somewhere rather than a
    // ghost at every cut.
    Query noTrim = plainQuery(ms(95));
    QCOMPARE(decide(FrameInterpMode::Blending, Pair{ms(80), ms(100), true, true}, noTrim).route, Route::Blend);
}

void FrameInterpTest::longVariableRateGapsAreNotTweened()
{
    Query q = plainQuery(ms(500));
    q.maxGapUs = kDefaultMaxGapUs;

    // A screen recorder that emitted nothing for a second while the picture was still. What
    // sits between those two frames is not a mix of them.
    Decision d = decide(FrameInterpMode::Blending, Pair{ms(100), ms(1100), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::GapTooLong);

    // An ordinary gap is fine.
    q.readUs = ms(110);
    d = decide(FrameInterpMode::Blending, Pair{ms(100), ms(140), true, true}, q);
    QCOMPARE(d.route, Route::Blend);

    // Zero means no limit.
    Query unlimited = plainQuery(ms(500));
    QCOMPARE(decide(FrameInterpMode::Blending, Pair{ms(100), ms(1100), true, true}, unlimited).route,
             Route::Blend);
}

void FrameInterpTest::aDeclaredCutRepeats()
{
    Query q = plainQuery(ms(90));
    q.knownCut = true;
    const Decision d = decide(FrameInterpMode::Flow, Pair{ms(80), ms(101), true, true}, q);
    QCOMPARE(d.route, Route::Repeat);
    QCOMPARE(d.reason, Reason::KnownCut);
}

void FrameInterpTest::samplingStaysTheDefaultBehaviour()
{
    // The mode a project gets when nothing asked for anything, and the one every guard above
    // falls back to: the frame at or before the time, held.
    const Decision d = decide(FrameInterpMode::Sampling, Pair{ms(80), ms(101), true, true}, plainQuery(ms(90)));
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
    Decision d = decide(FrameInterpMode::Blending, Pair{ms(2480), ms(2520), true, true}, q);
    QCOMPARE(d.route, Route::Blend);
    QVERIFY(qAbs(d.alpha - 0.5) < 1e-9);

    // The footage the proxy holds past the mirrored out point is still refused.
    q.readUs = ms(3990);
    d = decide(FrameInterpMode::Blending, Pair{ms(3980), ms(4020), true, true}, q);
    QCOMPARE(d.reason, Reason::OutsideTrim);

    // Forwards, the window is simply the trim — which is also what an un-proxied reversed clip
    // uses, since it walks the same trim downwards.
    const ReadWindow f = readWindowForward(srcIn, srcOut);
    QCOMPARE(f.loUs, srcIn);
    QCOMPARE(f.hiUs, srcOut);
}

namespace {

// fill(QColor) and not fill(QRgb): the QRgb overload writes the value straight into the
// buffer, so on a byte-ordered format like RGBA8888 an 0xAARRGGBB literal lands channel-
// reversed and the test would be measuring the wrong one.
QImage solid(int w, int h, const QColor &colour)
{
    QImage image(w, h, QImage::Format_RGBA8888);
    image.fill(colour);
    return image;
}

} // namespace

void FrameInterpTest::blendIsExactAtBothEnds()
{
    const QImage a = solid(8, 4, QColor(10, 20, 30));
    const QImage b = solid(8, 4, QColor(200, 180, 160));

    // Byte for byte, not a transfer round-trip of it: switching blending on must not change a
    // frame that was never going to be mixed.
    QCOMPARE(blendFrames(a, b, 0.0).pixel(0, 0), a.pixel(0, 0));
    QCOMPARE(blendFrames(a, b, 1.0).pixel(0, 0), b.pixel(0, 0));

    // Out of range is clamped to the ends rather than extrapolated.
    QCOMPARE(blendFrames(a, b, -0.5).pixel(0, 0), a.pixel(0, 0));
    QCOMPARE(blendFrames(a, b, 1.5).pixel(0, 0), b.pixel(0, 0));
}

void FrameInterpTest::blendHappensInLinearLight()
{
    const QImage black = solid(4, 4, QColor(0, 0, 0));
    const QImage white = solid(4, 4, QColor(255, 255, 255));

    const QImage mid = blendFrames(black, white, 0.5);
    QVERIFY(!mid.isNull());
    const int value = qRed(mid.pixel(1, 1));

    // Halfway between black and white is half the *light*, which comes back out of an sRGB
    // transfer at about 188 — not the 127 or 128 that mixing the encoded values gives. This is
    // the whole point: at 128 a slowed shot visibly pulses darker on every mixed frame, because
    // the blended frames do not sit between their neighbours in brightness.
    QVERIFY2(value > 180 && value < 195,
             qPrintable(QStringLiteral("linear-light midpoint came out at %1").arg(value)));
    QCOMPARE(qGreen(mid.pixel(1, 1)), value);
    QCOMPARE(qAlpha(mid.pixel(1, 1)), 255);

    // And it moves the right way, without steps backwards.
    int previous = -1;
    for (int step = 0; step <= 10; ++step) {
        const QImage each = blendFrames(black, white, step / 10.0);
        const int level = qRed(each.pixel(0, 0));
        QVERIFY(level >= previous);
        previous = level;
    }
    QCOMPARE(previous, 255);
}

void FrameInterpTest::blendRefusesWhatItCannotMix()
{
    const QImage a = solid(8, 4, QColor(10, 20, 30));

    // A null result tells the caller to repeat the first frame. Showing something half-built
    // would be worse than showing the frame before it.
    QVERIFY(blendFrames(QImage(), a, 0.5).isNull());
    QVERIFY(blendFrames(a, QImage(), 0.5).isNull());
    QVERIFY(blendFrames(a, solid(9, 4, QColor(0, 0, 0)), 0.5).isNull());
}

void FrameInterpTest::blendKeepsHiddenColourOutOfEdges()
{
    // A transparent pixel still holds whatever RGB was in the buffer. Mixing it straight would
    // pull that colour into the edge of anything with an alpha channel.
    const QImage opaqueRed = solid(4, 4, QColor(255, 0, 0, 255));
    const QImage hiddenGreen = solid(4, 4, QColor(0, 255, 0, 0));

    const QImage mixed = blendFrames(opaqueRed, hiddenGreen, 0.5);
    QVERIFY(!mixed.isNull());
    const QRgb pixel = mixed.pixel(2, 2);

    // Half transparent, and still red: the hidden green contributed nothing because it was
    // weighted by its own alpha first.
    QVERIFY(qAlpha(pixel) > 120 && qAlpha(pixel) < 136);
    QCOMPARE(qGreen(pixel), 0);
    QVERIFY(qRed(pixel) > 240);

    // Transparent on both sides stays transparent, and carries no leftover colour at all.
    const QImage bothHidden = blendFrames(hiddenGreen, solid(4, 4, QColor(0, 0, 255, 0)), 0.5);
    QVERIFY(!bothHidden.isNull());
    QCOMPARE(qAlpha(bothHidden.pixel(0, 0)), 0);
    QCOMPARE(qRed(bothHidden.pixel(0, 0)), 0);
    QCOMPARE(qGreen(bothHidden.pixel(0, 0)), 0);
    QCOMPARE(qBlue(bothHidden.pixel(0, 0)), 0);
}

void FrameInterpTest::blendIsOrderIndependentAndRepeatable()
{
    const QImage a = solid(16, 8, QColor(30, 90, 200));
    const QImage b = solid(16, 8, QColor(220, 40, 10));

    // Nothing in here carries state between calls, and in particular nothing reaches for a
    // previously presented frame: the two inputs are the only history there is. So the same
    // pair and the same alpha give the same bytes however the preview wandered to get here —
    // scrubbed backwards, paused, re-rendered — and the export agrees with the preview.
    const QImage first = blendFrames(a, b, 0.375);
    for (double other : {0.0, 1.0, 0.9, 0.1}) {
        const QImage ignored = blendFrames(b, a, other);
        Q_UNUSED(ignored);
    }
    const QImage again = blendFrames(a, b, 0.375);
    QCOMPARE(first, again);

    // Swapping the pair and the weight is the same mix from the other side.
    const QImage mirrored = blendFrames(b, a, 0.625);
    QVERIFY(qAbs(qRed(mirrored.pixel(0, 0)) - qRed(first.pixel(0, 0))) <= 1);
    QVERIFY(qAbs(qBlue(mirrored.pixel(0, 0)) - qBlue(first.pixel(0, 0))) <= 1);
}

void FrameInterpTest::theModeSurvivesAProjectFileRoundTrip()
{
    // The strings the project file stores. Changing one silently reopens every saved project
    // with the setting reset, so they are pinned here rather than left to whatever the enum
    // happens to be called.
    QCOMPARE(frameInterpModeToString(FrameInterpMode::Sampling), QStringLiteral("sampling"));
    QCOMPARE(frameInterpModeToString(FrameInterpMode::Blending), QStringLiteral("blending"));
    QCOMPARE(frameInterpModeToString(FrameInterpMode::Flow), QStringLiteral("flow"));

    for (FrameInterpMode mode : {FrameInterpMode::Sampling, FrameInterpMode::Blending,
                                 FrameInterpMode::Flow})
        QCOMPARE(frameInterpModeFromString(frameInterpModeToString(mode)), mode);

    // A project saved before any of this existed has no such key at all, and one written by a
    // later Dafin may name a mode this build has never heard of. Both have to open and play,
    // with the setting dropped back to the behaviour that needs no support.
    QCOMPARE(frameInterpModeFromString(QString()), FrameInterpMode::Sampling);
    QCOMPARE(frameInterpModeFromString(QStringLiteral("rife-v9")), FrameInterpMode::Sampling);
    QCOMPARE(frameInterpModeFromString(QStringLiteral("Blending")), FrameInterpMode::Sampling);
}


// Flow is a research path and off by default; these fix the properties that decide whether it
// may ever be anything else. The headline one is that it has to actually beat the blend.

void FrameInterpTest::flowDeclinesRatherThanGuessOnAFeaturelessWall()
{
    const QImage a = flatWall();
    const QImage b = flatWall();

    FlowSynthStats stats;
    const FlowPairAnalysis analysis = estimateFlow(a, b, FlowOptions{});
    const QImage result = synthesizeFlowFrame(a, b, 0.5, analysis, FlowSynthOptions{}, &stats);

    // Null is the instruction to blend, and the blend of two identical frames is one of them.
    // A solver that returned a confident answer here would be claiming to know which way an
    // untextured wall moved.
    QVERIFY(result.isNull());
    QVERIFY(stats.declined);
    QVERIFY(stats.meanConfidence < FlowSynthOptions{}.minMeanConfidence);
}

void FrameInterpTest::flowBeatsBlendOnACleanTranslation()
{
    // Four pixels between the two frames, so the frame in the middle is the one two pixels
    // along -- and it exists, which is what makes this measurable rather than a matter of taste.
    const QImage a = texturedWindow(0, 0);
    const QImage truth = texturedWindow(2, 0);
    const QImage b = texturedWindow(4, 0);

    const QImage blended = blendFrames(a, b, 0.5);
    QVERIFY(!blended.isNull());

    FlowSynthStats stats;
    const FlowPairAnalysis analysis = estimateFlow(a, b, FlowOptions{});
    const QImage flowed = synthesizeFlowFrame(a, b, 0.5, analysis, FlowSynthOptions{}, &stats);
    QVERIFY(!flowed.isNull());
    QVERIFY(!stats.declined);

    const double blendPsnr = psnrAgainst(blended, truth);
    const double flowPsnr = psnrAgainst(flowed, truth);

    // The whole case for this path. If it ever stops being true on a clean translation -- the
    // easiest thing flow can be asked to do -- then flow has no business being offered at all
    // and the blend should stay the only option.
    QVERIFY2(flowPsnr > blendPsnr + 1.0,
             qPrintable(QStringLiteral("flow %1 dB vs blend %2 dB: flow must win a clean "
                                       "translation by a clear margin or it is not worth having")
                            .arg(flowPsnr, 0, 'f', 2)
                            .arg(blendPsnr, 0, 'f', 2)));
}

void FrameInterpTest::flowIsExactAtBothEnds()
{
    const QImage a = texturedWindow(0, 0);
    const QImage b = texturedWindow(4, 0);
    const FlowPairAnalysis analysis = estimateFlow(a, b, FlowOptions{});

    // Byte for byte the frame itself, not a round trip through a warp that happens to be the
    // identity: switching this on must not alter a frame that lands on a real timestamp.
    QCOMPARE(synthesizeFlowFrame(a, b, 0.0, analysis, FlowSynthOptions{}), a);
    QCOMPARE(synthesizeFlowFrame(a, b, 1.0, analysis, FlowSynthOptions{}), b);
}

void FrameInterpTest::flowIsSymmetricUnderSwappingTheFrames()
{
    const QImage a = texturedWindow(0, 0);
    const QImage b = texturedWindow(4, 0);

    const QImage forward =
        synthesizeFlowFrame(a, b, 0.25, estimateFlow(a, b, FlowOptions{}), FlowSynthOptions{});
    const QImage swapped =
        synthesizeFlowFrame(b, a, 0.75, estimateFlow(b, a, FlowOptions{}), FlowSynthOptions{});

    QVERIFY(!forward.isNull());
    QVERIFY(!swapped.isNull());

    // Reversing a clip reads the same footage the other way round, so the two have to agree.
    // Not bit-exact: the two estimates are separate solves on swapped inputs.
    QVERIFY2(maxAbsDifference(forward, swapped) <= 24,
             "a frame built between two others must not depend on which way round they came");
}

void FrameInterpTest::flowNeverPunchesAHoleInThePicture()
{
    // A hard-edged bar sweeping over texture: every frame has a band being uncovered, which is
    // exactly where a warp has nothing to sample and where the temptation is to leave a gap.
    const auto withBar = [](int barX) {
        QImage image = texturedWindow(0, 0);
        for (int y = 0; y < image.height(); ++y) {
            quint8 *row = image.scanLine(y);
            for (int x = barX; x < std::min(barX + 30, image.width()); ++x) {
                quint8 *p = row + x * 4;
                p[0] = 20;
                p[1] = 200;
                p[2] = 60;
                p[3] = 255;
            }
        }
        return image;
    };

    const QImage a = withBar(40);
    const QImage b = withBar(58);

    FlowSynthStats stats;
    const QImage result =
        synthesizeFlowFrame(a, b, 0.5, estimateFlow(a, b, FlowOptions{}), FlowSynthOptions{},
                            &stats);
    if (result.isNull()) {
        // Declining is allowed here. Falling over is not.
        QVERIFY(stats.declined);
        return;
    }

    QCOMPARE(result.size(), a.size());
    const QImage checked = result.convertToFormat(QImage::Format_RGBA8888);
    for (int y = 0; y < checked.height(); ++y) {
        const quint8 *row = checked.constScanLine(y);
        for (int x = 0; x < checked.width(); ++x) {
            // Both inputs are fully opaque everywhere, so any transparency in the output is a
            // hole where the solver gave up, and a hole is never the honest answer: something
            // was photographed there.
            QCOMPARE(row[x * 4 + 3], quint8(255));
        }
    }
}

void FrameInterpTest::confidenceAndVisibilityAreSeparateSignals()
{
    // A flat wall: nothing to solve, so confidence should be low -- and yet every part of it is
    // plainly visible in both frames, so visibility should not be. Collapsing the two into one
    // number loses exactly this distinction, which is why they are two maps.
    const QImage wall = flatWall();
    const FlowPairAnalysis flat = estimateFlow(wall, wall, FlowOptions{});
    QVERIFY(flat.isValid());

    double confidenceTotal = 0.0;
    for (quint8 v : flat.confidenceA.value)
        confidenceTotal += v;
    const double meanConfidence = confidenceTotal / (255.0 * flat.confidenceA.value.size());

    int visibleCells = 0;
    for (quint8 v : flat.visibleFromA.value)
        visibleCells += (v > 0) ? 1 : 0;
    const double visibleFraction = double(visibleCells) / flat.visibleFromA.value.size();

    QVERIFY2(meanConfidence < 0.2, "a blank wall gives a solver nothing to be confident about");
    QVERIFY2(visibleFraction > 0.9, "a blank wall is still entirely visible in both frames");
}

QTEST_APPLESS_MAIN(FrameInterpTest)
#include "tst_frameinterp.moc"
