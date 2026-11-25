// Dafin: alpha-correct compositing on the GPU path (research Stage 12, C12-A / C12-B / C12-C).
//
// Every expectation is arithmetic written here, independent of the shaders: Porter-Duff source-over
// with a separable blend (W3C Compositing 1), and a premultiplied crossfade. The scenes render over
// a transparent canvas, because that is what titles composed with alpha over footage see.
//
// GpuCompositor::render() returns straight RGBA8888 (premultiplied canvas, un-premultiplied by the
// readback), so every probe below is compared as a straight colour plus alpha.
//
// These need an OpenGL context; without one they QSKIP, like the other GPU tests.

#include <QtTest>

#include <array>
#include <cmath>
#include <memory>

#include "core/effects/BlendMode.h"
#include "engine/compositor/GpuCompositor.h"
#include "engine/gpu/effects/GpuEffectExecutor.h"
#include "engine/packages/transitions/TransitionCatalog.h"

#ifdef DRIFT_WITH_SKIA
#include "engine/render2d/skia/SkiaRuntime.h"
#include "engine/render2d/skia/VectorPainter.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkColor.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRect.h"
#endif

namespace {

constexpr int kSide = 64;
const QSize kCanvas(kSide, kSide);
const QRectF kFull(0, 0, kSide, kSide);

struct Rgba
{
    double r = 0, g = 0, b = 0, a = 0; // straight, 0..1
};

QImage solid(const Rgba &c)
{
    QImage img(kSide, kSide, QImage::Format_RGBA8888);
    img.fill(QColor::fromRgbF(float(c.r), float(c.g), float(c.b), float(c.a)));
    return img;
}

GpuItem plainItem(const QImage &image, drift::BlendMode blend = drift::BlendMode::Normal)
{
    GpuItem item;
    item.blend = blend;
    item.layer.source = image;
    item.layer.rect = kFull;
    item.layer.valid = true;
    return item;
}

GpuScene transparentScene()
{
    GpuScene scene;
    scene.canvasSize = kCanvas;
    scene.backgroundColor = QColor(0, 0, 0, 0);
    return scene;
}

QImage renderScene(const GpuScene &scene)
{
    return GpuCompositor::render(scene).convertToFormat(QImage::Format_RGBA8888);
}

// --- reference arithmetic -------------------------------------------------------------------

double blendChannel(drift::BlendMode mode, double cb, double cs)
{
    switch (mode) {
    case drift::BlendMode::Multiply: return cb * cs;
    case drift::BlendMode::Screen: return 1.0 - (1.0 - cb) * (1.0 - cs);
    case drift::BlendMode::Overlay:
        return cb < 0.5 ? 2.0 * cb * cs : 1.0 - 2.0 * (1.0 - cb) * (1.0 - cs);
    case drift::BlendMode::Darken: return std::min(cb, cs);
    case drift::BlendMode::Lighten: return std::max(cb, cs);
    default: return cs;
    }
}

// Straight colours cb (backdrop, alpha ab) and cs (source, alpha as); result is straight too.
Rgba referenceBlend(drift::BlendMode mode, const Rgba &cb, const Rgba &cs)
{
    const double ab = cb.a;
    const double as = cs.a;
    const double outA = as + ab * (1.0 - as);
    const double bk[3] = {cb.r, cb.g, cb.b};
    const double sk[3] = {cs.r, cs.g, cs.b};
    double out[3];
    for (int i = 0; i < 3; ++i) {
        const double premul = (1.0 - ab) * as * sk[i]
                              + ab * as * blendChannel(mode, bk[i], sk[i])
                              + (1.0 - as) * ab * bk[i];
        out[i] = outA > 1e-6 ? premul / outA : 0.0;
    }
    return {out[0], out[1], out[2], outA};
}

// Premultiplied linear mix of two straight colours: the crossfade contract of C12-B.
Rgba referenceCrossfade(const Rgba &from, const Rgba &to, double p)
{
    const double outA = from.a + (to.a - from.a) * p;
    const double fp[3] = {from.r * from.a, from.g * from.a, from.b * from.a};
    const double tp[3] = {to.r * to.a, to.g * to.a, to.b * to.a};
    double out[3];
    for (int i = 0; i < 3; ++i) {
        const double premul = fp[i] + (tp[i] - fp[i]) * p;
        out[i] = outA > 1e-6 ? premul / outA : 0.0;
    }
    return {out[0], out[1], out[2], outA};
}

// 8-bit chains (the backdrop layer is stored, then read back) justify a few levels of slack. At
// low alpha the straight colour is amplified by 1/alpha, so colour is only compared where the
// pixel has enough coverage to carry it.
bool close(const QColor &got, const Rgba &want, int tol, QString *why)
{
    const int wa = int(std::lround(want.a * 255.0));
    if (qAbs(got.alpha() - wa) > tol) {
        *why = QStringLiteral("alpha %1, want %2").arg(got.alpha()).arg(wa);
        return false;
    }
    if (wa < 24)
        return true;
    const int colourTol = tol + int(std::ceil(255.0 * tol / double(wa)));
    const int wr = int(std::lround(want.r * 255.0));
    const int wg = int(std::lround(want.g * 255.0));
    const int wb = int(std::lround(want.b * 255.0));
    if (qAbs(got.red() - wr) > colourTol || qAbs(got.green() - wg) > colourTol
        || qAbs(got.blue() - wb) > colourTol) {
        *why = QStringLiteral("rgb %1,%2,%3, want %4,%5,%6 (tol %7)")
                   .arg(got.red()).arg(got.green()).arg(got.blue()).arg(wr).arg(wg).arg(wb)
                   .arg(colourTol);
        return false;
    }
    return true;
}

QString name(drift::BlendMode mode)
{
    return drift::blendModeToString(mode);
}

// --- transitions ----------------------------------------------------------------------------

GpuItem transitionItem(const QImage &from, const QImage &to, double progress, bool broken)
{
    GpuItem item;
    item.isTransition = true;
    item.from.source = from;
    item.from.rect = kFull;
    item.from.valid = true;
    item.to.source = to;
    item.to.rect = kFull;
    item.to.valid = true;
    item.progress = progress;
    if (!broken) {
        if (const TransitionPresetEntry *def = transitionDefForId(QStringLiteral("crossfade"))) {
            item.transitionKey = QLatin1String(kTransitionCacheKeyPrefix) + QStringLiteral("crossfade");
            item.transitionGpu = &def->gpu;
        }
    }
    // broken: no GPU definition, which is exactly what the compositor's grace mode answers.
    return item;
}

#ifdef DRIFT_WITH_SKIA
// Antialiased white shapes with every kind of edge a glyph has: curves, diagonals, hairlines,
// concave corners. Drawn through Skia, so the pixels cross the same Skia -> GL texture -> compositor
// boundary Scene Title text will. `alpha` < 255 makes it translucent text.
class GlyphLikePainter : public drift::skia::VectorPainter
{
public:
    explicit GlyphLikePainter(quint8 alpha, quint64 key) : m_alpha(alpha), m_key(key) {}
    QSize size() const override { return kCanvas; }
    quint64 cacheKey() const override { return m_key; }
    void paint(SkCanvas &canvas) const override
    {
        SkPaint fill;
        fill.setAntiAlias(true);
        fill.setColor(SkColorSetARGB(m_alpha, 255, 255, 255));
        canvas.drawCircle(18.3f, 18.7f, 10.4f, fill);
        canvas.drawRoundRect(SkRect::MakeXYWH(34.4f, 8.6f, 22.3f, 16.2f), 5.1f, 5.1f, fill);
        SkPaint stroke = fill;
        stroke.setStyle(SkPaint::kStroke_Style);
        stroke.setStrokeWidth(1.3f);
        canvas.drawLine(6.2f, 40.1f, 57.9f, 33.4f, stroke);
        stroke.setStrokeWidth(3.7f);
        canvas.drawLine(8.4f, 56.2f, 30.6f, 44.8f, stroke);
        SkPathBuilder tri;
        tri.moveTo(38.2f, 58.4f);
        tri.lineTo(52.7f, 43.9f);
        tri.lineTo(58.3f, 58.9f);
        tri.close();
        canvas.drawPath(tri.detach(), fill);
    }

private:
    quint8 m_alpha;
    quint64 m_key;
};
#endif

} // namespace

class AlphaCompositeTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    // Fixtures: the plain path must not move.
    void normalOverTransparentIsIdentity();

    // C12-A
    void multiplyOverClearKeepsSource();
    void alphaRampThroughBlendKeepsRamp();
    void separableBlendsMatchReferenceOverEveryBackdropAlpha_data();
    void separableBlendsMatchReferenceOverEveryBackdropAlpha();

    // C12-B
    void crossfadeRedToClearIsPremultiplied();
    void crossfadeMatchesReferenceAtEveryProgress_data();
    void crossfadeMatchesReferenceAtEveryProgress();

    // C12-C
    void brokenShaderFallbackIsTheCanonicalCrossfade_data();
    void brokenShaderFallbackIsTheCanonicalCrossfade();

    // Scene Title preparation: Skia text edges over coloured backgrounds.
    void skiaEdgesOverColouredBackgroundsHaveNoHalo();
};

void AlphaCompositeTest::initTestCase()
{
    reloadTransitionCatalog({QString::fromUtf8(DRIFT_TEST_TRANSITIONS_DIR)});
}

#define NEED_GL()                                                                                  \
    do {                                                                                           \
        if (!GpuCompositor::isAvailable())                                                         \
            QSKIP("OpenGL offscreen context unavailable");                                         \
    } while (0)

void AlphaCompositeTest::normalOverTransparentIsIdentity()
{
    NEED_GL();
    // Checkerboard of opaque white/black 8px tiles, with a half-alpha quadrant.
    QImage board(kSide, kSide, QImage::Format_RGBA8888);
    for (int y = 0; y < kSide; ++y)
        for (int x = 0; x < kSide; ++x) {
            const bool white = ((x / 8) + (y / 8)) % 2 == 0;
            const int a = (x >= 32 && y >= 32) ? 128 : 255;
            board.setPixelColor(x, y, QColor(white ? 255 : 0, white ? 255 : 0, white ? 255 : 0, a));
        }
    GpuScene scene = transparentScene();
    scene.items.append(plainItem(board));
    const QImage out = renderScene(scene);
    QVERIFY(!out.isNull());
    for (int y = 2; y < kSide; y += 8)
        for (int x = 2; x < kSide; x += 8) {
            const QColor g = out.pixelColor(x, y);
            const QColor w = board.pixelColor(x, y);
            QVERIFY((qAbs(g.alpha() - w.alpha())) <= (1));
            if (w.alpha() == 255) {
                QVERIFY((qAbs(g.red() - w.red())) <= (1));
            } else {
                // Half-alpha tile: white stays white, black stays black (straight readback).
                QVERIFY((qAbs(g.red() - w.red())) <= (3));
            }
        }
}

void AlphaCompositeTest::multiplyOverClearKeepsSource()
{
    NEED_GL();
    // Opaque red, Multiply, over a fully transparent canvas: nothing to multiply with, so the
    // source is what is left. The shader as found returns black here.
    GpuScene scene = transparentScene();
    scene.items.append(plainItem(solid({1, 0, 0, 1}), drift::BlendMode::Multiply));
    const QImage out = renderScene(scene);
    const QColor c = out.pixelColor(32, 32);
    QVERIFY((qAbs(c.red() - 255)) <= (1));
    QVERIFY((c.green()) <= (1));
    QVERIFY((c.blue()) <= (1));
    QVERIFY((qAbs(c.alpha() - 255)) <= (1));

    // Half-alpha source over clear keeps both its colour and its alpha.
    GpuScene half = transparentScene();
    half.items.append(plainItem(solid({1, 0, 0, 0.5}), drift::BlendMode::Multiply));
    const QColor h = renderScene(half).pixelColor(32, 32);
    QVERIFY((qAbs(h.alpha() - 128)) <= (1));
    QVERIFY((qAbs(h.red() - 255)) <= (4));
    QVERIFY((h.green()) <= (3));
}

void AlphaCompositeTest::alphaRampThroughBlendKeepsRamp()
{
    NEED_GL();
    // Left-to-right alpha ramp of one colour: through any blend over clear, each column must come
    // out with the ramp's own alpha and colour.
    QImage ramp(kSide, kSide, QImage::Format_RGBA8888);
    for (int x = 0; x < kSide; ++x)
        for (int y = 0; y < kSide; ++y)
            ramp.setPixelColor(x, y, QColor(200, 60, 20, x * 255 / (kSide - 1)));
    for (const auto mode : {drift::BlendMode::Multiply, drift::BlendMode::Screen,
                            drift::BlendMode::Overlay, drift::BlendMode::Darken,
                            drift::BlendMode::Lighten}) {
        GpuScene scene = transparentScene();
        scene.items.append(plainItem(ramp, mode));
        const QImage out = renderScene(scene);
        for (int x = 4; x < kSide; x += 6) {
            const QColor g = out.pixelColor(x, 32);
            const QColor w = ramp.pixelColor(x, 32);
            QVERIFY2(qAbs(g.alpha() - w.alpha()) <= 2, qPrintable(name(mode)));
            if (w.alpha() >= 64) {
                QVERIFY2(qAbs(g.red() - 200) <= 8 && qAbs(g.green() - 60) <= 8
                             && qAbs(g.blue() - 20) <= 8,
                         qPrintable(QStringLiteral("%1 column %2 rgb %3,%4,%5")
                                        .arg(name(mode)).arg(x).arg(g.red()).arg(g.green()).arg(g.blue())));
            }
        }
    }
}

void AlphaCompositeTest::separableBlendsMatchReferenceOverEveryBackdropAlpha_data()
{
    QTest::addColumn<int>("mode");
    QTest::addColumn<double>("backdropAlpha");
    QTest::addColumn<double>("sourceAlpha");
    const drift::BlendMode modes[] = {drift::BlendMode::Multiply, drift::BlendMode::Screen,
                                      drift::BlendMode::Overlay, drift::BlendMode::Darken,
                                      drift::BlendMode::Lighten};
    for (const auto mode : modes)
        for (const double ab : {0.0, 0.25, 0.5, 1.0})
            for (const double as : {0.5, 1.0})
                QTest::addRow("%s_ab%.2f_as%.2f", qPrintable(name(mode)), ab, as)
                    << int(mode) << ab << as;
}

void AlphaCompositeTest::separableBlendsMatchReferenceOverEveryBackdropAlpha()
{
    NEED_GL();
    QFETCH(int, mode);
    QFETCH(double, backdropAlpha);
    QFETCH(double, sourceAlpha);
    const auto blend = drift::BlendMode(mode);

    const Rgba backdrop{0.8, 0.3, 0.6, backdropAlpha};
    const Rgba source{0.4, 0.7, 0.2, sourceAlpha};

    GpuScene scene = transparentScene();
    if (backdropAlpha > 0.0)
        scene.items.append(plainItem(solid(backdrop)));
    scene.items.append(plainItem(solid(source), blend));
    const QImage out = renderScene(scene);
    QVERIFY(!out.isNull());

    // The backdrop goes through 8 bits once before the blend reads it back, so the reference
    // uses the same quantised backdrop.
    const QColor q = QColor::fromRgbF(float(backdrop.r), float(backdrop.g), float(backdrop.b),
                                      float(backdrop.a));
    const Rgba quantisedBackdrop{q.redF(), q.greenF(), q.blueF(), q.alphaF()};
    const Rgba want = referenceBlend(blend, quantisedBackdrop, source);

    QString why;
    QVERIFY2(close(out.pixelColor(32, 32), want, 3, &why), qPrintable(why));
}

void AlphaCompositeTest::crossfadeRedToClearIsPremultiplied()
{
    NEED_GL();
    const TransitionPresetEntry *def = transitionDefForId(QStringLiteral("crossfade"));
    QVERIFY2(def && def->gpu.valid, "canonical crossfade package not loaded");

    // Opaque red out, transparent black in. Halfway the result is red at alpha .5: premultiplied
    // (.5, 0, 0, .5). A straight mix() of RGBA gives straight (.5, 0, 0, .5) = premultiplied .25.
    GpuScene scene = transparentScene();
    scene.items.append(
        transitionItem(solid({1, 0, 0, 1}), solid({0, 0, 0, 0}), 0.5, false));
    const QColor c = renderScene(scene).pixelColor(32, 32);
    QVERIFY((qAbs(c.alpha() - 128)) <= (1));
    QVERIFY((qAbs(c.red() - 255)) <= (3));
    QVERIFY((c.green()) <= (2));
    QVERIFY((c.blue()) <= (2));
}

void AlphaCompositeTest::crossfadeMatchesReferenceAtEveryProgress_data()
{
    QTest::addColumn<double>("progress");
    QTest::addColumn<double>("fromAlpha");
    QTest::addColumn<double>("toAlpha");
    for (const double p : {0.0, 0.25, 0.5, 0.75, 1.0})
        for (const auto &alphas : std::array<std::array<double, 2>, 6>{{
                 {1.0, 1.0}, {1.0, 0.0}, {0.0, 1.0}, {0.5, 1.0}, {1.0, 0.5}, {0.25, 0.75}}})
            QTest::addRow("p%.2f_a%.2f_to_a%.2f", p, alphas[0], alphas[1]) << p << alphas[0] << alphas[1];
}

void AlphaCompositeTest::crossfadeMatchesReferenceAtEveryProgress()
{
    NEED_GL();
    QFETCH(double, progress);
    QFETCH(double, fromAlpha);
    QFETCH(double, toAlpha);
    const Rgba from{1.0, 0.0, 0.0, fromAlpha};
    const Rgba to{0.0, 0.0, 1.0, toAlpha};

    GpuScene scene = transparentScene();
    scene.items.append(transitionItem(solid(from), solid(to), progress, false));
    const QImage out = renderScene(scene);

    // Quantise the inputs the way the textures hold them.
    const auto q = [](const Rgba &c) {
        const QColor k = QColor::fromRgbF(float(c.r), float(c.g), float(c.b), float(c.a));
        return Rgba{k.redF(), k.greenF(), k.blueF(), k.alphaF()};
    };
    const Rgba want = referenceCrossfade(q(from), q(to), progress);
    QString why;
    QVERIFY2(close(out.pixelColor(32, 32), want, 3, &why), qPrintable(why));
}

void AlphaCompositeTest::brokenShaderFallbackIsTheCanonicalCrossfade_data()
{
    QTest::addColumn<double>("progress");
    QTest::addColumn<double>("fromAlpha");
    QTest::addColumn<double>("toAlpha");
    for (const double p : {0.0, 0.25, 0.5, 0.75, 1.0})
        for (const auto &alphas :
             std::array<std::array<double, 2>, 3>{{{1.0, 1.0}, {1.0, 0.0}, {0.5, 1.0}}})
            QTest::addRow("p%.2f_a%.2f_to_a%.2f", p, alphas[0], alphas[1]) << p << alphas[0] << alphas[1];
}

void AlphaCompositeTest::brokenShaderFallbackIsTheCanonicalCrossfade()
{
    NEED_GL();
    QFETCH(double, progress);
    QFETCH(double, fromAlpha);
    QFETCH(double, toAlpha);
    const Rgba from{1.0, 0.0, 0.0, fromAlpha};
    const Rgba to{0.0, 0.0, 1.0, toAlpha};

    // Opaque red -> opaque blue at .5 over a clear canvas must be premultiplied (.5, 0, .5, 1),
    // not the (.25, 0, .5, .75) two successive source-over draws give.
    GpuScene broken = transparentScene();
    broken.items.append(transitionItem(solid(from), solid(to), progress, true));
    GpuScene native = transparentScene();
    native.items.append(transitionItem(solid(from), solid(to), progress, false));
    const QImage fb = renderScene(broken);
    const QImage nat = renderScene(native);
    QVERIFY(!fb.isNull() && !nat.isNull());

    const auto q = [](const Rgba &c) {
        const QColor k = QColor::fromRgbF(float(c.r), float(c.g), float(c.b), float(c.a));
        return Rgba{k.redF(), k.greenF(), k.blueF(), k.alphaF()};
    };
    const Rgba want = referenceCrossfade(q(from), q(to), progress);
    QString why;
    QVERIFY2(close(fb.pixelColor(32, 32), want, 3, &why), qPrintable(QStringLiteral("fallback: ") + why));
    // And the two paths agree with each other, not just with the arithmetic.
    QVERIFY2(qAbs(fb.pixelColor(32, 32).alpha() - nat.pixelColor(32, 32).alpha()) <= 2,
             "fallback alpha differs from the native crossfade");
}

void AlphaCompositeTest::skiaEdgesOverColouredBackgroundsHaveNoHalo()
{
#ifndef DRIFT_WITH_SKIA
    QSKIP("built without Skia");
#else
    NEED_GL();
    for (const quint8 textAlpha : {quint8(255), quint8(153)}) {
        const auto painter =
            std::make_shared<GlyphLikePainter>(textAlpha, quint64(0xA1FA0000u) + textAlpha);

        // Coverage reference: the same painter over clear, read back straight. Its alpha is the
        // edge coverage times the paint alpha.
        GpuScene bare = transparentScene();
        GpuItem glyphs;
        glyphs.layer.vector = painter;
        glyphs.layer.rect = kFull;
        glyphs.layer.valid = true;
        bare.items.append(glyphs);
        const QImage coverage = renderScene(bare);
        QVERIFY(!coverage.isNull());
        int fringe = 0;
        for (int y = 0; y < kSide; ++y)
            for (int x = 0; x < kSide; ++x) {
                const int a = coverage.pixelColor(x, y).alpha();
                fringe += (a > 8 && a < textAlpha - 8) ? 1 : 0;
            }
        QVERIFY2(fringe > 40, "fixture has no antialiased edge pixels to test");

        struct Bg
        {
            const char *label;
            QColor colour;
        };
        const Bg backgrounds[] = {{"black", QColor(0, 0, 0)},       {"white", QColor(255, 255, 255)},
                                  {"red", QColor(255, 0, 0)},       {"green", QColor(0, 255, 0)},
                                  {"blue", QColor(0, 0, 255)},      {"saturated teal", QColor(0, 200, 180)}};
        for (const Bg &bg : backgrounds) {
            GpuScene scene = transparentScene();
            QImage ground(kSide, kSide, QImage::Format_RGBA8888);
            ground.fill(bg.colour);
            scene.items.append(plainItem(ground));
            scene.items.append(glyphs);
            const QImage out = renderScene(scene);
            QVERIFY(!out.isNull());

            int bad = 0;
            QString first;
            for (int y = 0; y < kSide; ++y)
                for (int x = 0; x < kSide; ++x) {
                    const double a = coverage.pixelColor(x, y).alphaF();
                    const QColor got = out.pixelColor(x, y);
                    // White source: result = a * 255 + (1 - a) * background, per channel. A dark
                    // halo undershoots this (premultiplied twice), a light one overshoots it.
                    const double want[3] = {a * 255.0 + (1.0 - a) * bg.colour.red(),
                                            a * 255.0 + (1.0 - a) * bg.colour.green(),
                                            a * 255.0 + (1.0 - a) * bg.colour.blue()};
                    const int have[3] = {got.red(), got.green(), got.blue()};
                    for (int i = 0; i < 3; ++i)
                        if (std::abs(have[i] - want[i]) > 3.0) {
                            if (bad++ == 0)
                                first = QStringLiteral("(%1,%2) ch%3 got %4 want %5")
                                            .arg(x).arg(y).arg(i).arg(have[i]).arg(want[i], 0, 'f', 1);
                        }
                }
            QVERIFY2(bad == 0,
                     qPrintable(QStringLiteral("alpha %1 over %2: %3 channel mismatches, first %4")
                                    .arg(textAlpha).arg(bg.label).arg(bad).arg(first)));
        }
    }
#endif
}

QTEST_MAIN(AlphaCompositeTest)
#include "tst_alphacomposite.moc"
