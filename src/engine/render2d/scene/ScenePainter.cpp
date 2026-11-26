#include "engine/render2d/scene/ScenePainter.h"

#include "engine/render2d/text/fonts/SkiaFonts.h"

#include "include/core/SkBitmap.h"
#include "include/core/SkCanvas.h"
#include "include/core/SkFont.h"
#include "include/core/SkFontMetrics.h"
#include "include/core/SkImage.h"
#include "include/core/SkPaint.h"
#include "include/core/SkPathBuilder.h"
#include "include/core/SkRRect.h"
#include "include/core/SkSamplingOptions.h"
#include "include/core/SkSurface.h"
#include "include/core/SkTypeface.h"
#include "modules/skshaper/include/SkShaper.h"
#include "modules/skshaper/include/SkShaper_harfbuzz.h"
#include "modules/skunicode/include/SkUnicode_icu.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QMutex>
#include <QStandardPaths>

#include <cfenv>
#include <cmath>
#include <cstring>
#include <vector>

namespace drift::skia {

using scene::Actor;
using scene::Channels;
using scene::Profile;
using scene::Rgba;
using scene::Scene;
using scene::TimedBlock;

namespace {

constexpr int kSS = 2; // sprite2x supersampling

// Python's round(): half to even.
double roundHalfEven(double v)
{
    std::fesetround(FE_TONEAREST);
    return std::nearbyint(v);
}

// ---- fonts -----------------------------------------------------------------------------------

QHash<QString, QStringList> fontIndex(const QStringList &dirs)
{
    static QMutex mutex;
    static QHash<QString, QHash<QString, QStringList>> cache;
    const QString key = dirs.join(QLatin1Char('\n'));
    QMutexLocker lock(&mutex);
    if (const auto it = cache.constFind(key); it != cache.constEnd())
        return it.value();
    QHash<QString, QStringList> index;
    for (const QString &dir : dirs) {
        if (!QFileInfo(dir).isDir())
            continue;
        QStringList found;
        QDirIterator it(dir, {QStringLiteral("*.ttf"), QStringLiteral("*.otf"), QStringLiteral("*.ttc"),
                              QStringLiteral("*.TTF"), QStringLiteral("*.OTF")},
                        QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            found << it.next();
        found.sort();
        for (const QString &path : std::as_const(found))
            index[QFileInfo(path).fileName()].append(path);
    }
    cache.insert(key, index);
    return index;
}

QString fileSha256(const QString &path)
{
    static QMutex mutex;
    static QHash<QString, QString> cache;
    QMutexLocker lock(&mutex);
    if (const auto it = cache.constFind(path); it != cache.constEnd())
        return it.value();
    QFile f(path);
    QString hex;
    if (f.open(QIODevice::ReadOnly)) {
        QCryptographicHash h(QCryptographicHash::Sha256);
        h.addData(&f);
        hex = QString::fromLatin1(h.result().toHex());
    }
    cache.insert(path, hex);
    return hex;
}

QString resolveFont(const scene::FontSpec &spec, const QStringList &dirs, QString *error)
{
    QStringList candidates;
    if (QFileInfo(spec.file).isAbsolute() && QFileInfo::exists(spec.file))
        candidates << spec.file;
    else
        candidates = fontIndex(dirs).value(spec.file);
    const QString package = spec.package.isEmpty() ? QString() : QStringLiteral(" (package: %1)").arg(spec.package);
    if (candidates.isEmpty()) {
        *error = QStringLiteral("font file %1 (%2) not found%3").arg(spec.file, spec.family, package);
        return {};
    }
    if (spec.sha256.isEmpty())
        return candidates.first();
    for (const QString &c : std::as_const(candidates))
        if (fileSha256(c) == spec.sha256)
            return c;
    *error = QStringLiteral("font %1: no file with sha256 %2… (found %3 other cut(s))%4")
                 .arg(spec.file, spec.sha256.left(12))
                 .arg(candidates.size())
                 .arg(package);
    return {};
}

// ---- PIL's Lanczos resample, exactly (Resample.c, 8 bpc) -------------------------------------

double sincf(double x)
{
    if (x == 0.0)
        return 1.0;
    x *= M_PI;
    return std::sin(x) / x;
}

double lanczos(double x)
{
    return (x > -3.0 && x < 3.0) ? sincf(x) * sincf(x / 3.0) : 0.0;
}

struct Coeffs
{
    int ksize = 0;
    std::vector<int> bounds;   // xmin, xcount per output
    std::vector<int> kk;       // fixed-point weights
};

constexpr int kPrecisionBits = 32 - 8 - 2;

Coeffs precompute(int inSize, int outSize)
{
    const double scale = double(inSize) / outSize;
    const double filterscale = qMax(1.0, scale);
    const double support = 3.0 * filterscale;
    Coeffs c;
    c.ksize = int(std::ceil(support)) * 2 + 1;
    c.bounds.resize(outSize * 2);
    c.kk.assign(size_t(outSize) * c.ksize, 0);
    std::vector<double> k(c.ksize);
    for (int xx = 0; xx < outSize; ++xx) {
        const double center = (xx + 0.5) * scale;
        const double ss = 1.0 / filterscale;
        int xmin = int(center - support + 0.5);
        if (xmin < 0)
            xmin = 0;
        int xmax = int(center + support + 0.5);
        if (xmax > inSize)
            xmax = inSize;
        xmax -= xmin;
        double ww = 0.0;
        for (int x = 0; x < xmax; ++x) {
            const double w = lanczos((x + xmin - center + 0.5) * ss);
            k[x] = w;
            ww += w;
        }
        for (int x = 0; x < xmax; ++x)
            k[x] = ww != 0.0 ? k[x] / ww : k[x];
        for (int x = xmax; x < c.ksize; ++x)
            k[x] = 0;
        for (int x = 0; x < c.ksize; ++x) {
            const double v = k[x] * (1 << kPrecisionBits);
            c.kk[size_t(xx) * c.ksize + x] = int(v < 0 ? v - 0.5 : v + 0.5);
        }
        c.bounds[xx * 2] = xmin;
        c.bounds[xx * 2 + 1] = xmax;
    }
    return c;
}

inline quint8 clip8(qint64 v)
{
    v >>= kPrecisionBits;
    return quint8(v < 0 ? 0 : (v > 255 ? 255 : v));
}

// 4-channel uint8 image, row-major.
struct Img
{
    int w = 0, h = 0;
    std::vector<quint8> px;
    quint8 *at(int x, int y) { return &px[(size_t(y) * w + x) * 4]; }
    const quint8 *at(int x, int y) const { return &px[(size_t(y) * w + x) * 4]; }
};

Img resampleH(const Img &in, int outW)
{
    const Coeffs c = precompute(in.w, outW);
    Img out{outW, in.h, std::vector<quint8>(size_t(outW) * in.h * 4)};
    for (int y = 0; y < in.h; ++y)
        for (int xx = 0; xx < outW; ++xx) {
            const int xmin = c.bounds[xx * 2], xmax = c.bounds[xx * 2 + 1];
            const int *k = &c.kk[size_t(xx) * c.ksize];
            for (int ch = 0; ch < 4; ++ch) {
                qint64 ss = qint64(1) << (kPrecisionBits - 1);
                for (int x = 0; x < xmax; ++x)
                    ss += qint64(in.at(x + xmin, y)[ch]) * k[x];
                out.at(xx, y)[ch] = clip8(ss);
            }
        }
    return out;
}

Img resampleV(const Img &in, int outH)
{
    const Coeffs c = precompute(in.h, outH);
    Img out{in.w, outH, std::vector<quint8>(size_t(in.w) * outH * 4)};
    for (int yy = 0; yy < outH; ++yy) {
        const int ymin = c.bounds[yy * 2], ymax = c.bounds[yy * 2 + 1];
        const int *k = &c.kk[size_t(yy) * c.ksize];
        for (int x = 0; x < in.w; ++x)
            for (int ch = 0; ch < 4; ++ch) {
                qint64 ss = qint64(1) << (kPrecisionBits - 1);
                for (int y = 0; y < ymax; ++y)
                    ss += qint64(in.at(x, y + ymin)[ch]) * k[y];
                out.at(x, yy)[ch] = clip8(ss);
            }
    }
    return out;
}

inline quint8 muldiv255(int a, int b)
{
    const int tmp = a * b + 128;
    return quint8(((tmp >> 8) + tmp) >> 8);
}

// Image.resize(..., LANCZOS) on an RGBA image: premultiplied (RGBa) both ways, as Pillow does.
Img pilResizeRgba(Img img, int outW, int outH)
{
    for (size_t i = 0; i < img.px.size(); i += 4) {
        const int a = img.px[i + 3];
        for (int ch = 0; ch < 3; ++ch)
            img.px[i + ch] = muldiv255(img.px[i + ch], a);
    }
    if (outW != img.w)
        img = resampleH(img, outW);
    if (outH != img.h)
        img = resampleV(img, outH);
    for (size_t i = 0; i < img.px.size(); i += 4) {
        const int a = img.px[i + 3];
        if (a == 0 || a == 255)
            continue;
        for (int ch = 0; ch < 3; ++ch) {
            const int v = 255 * img.px[i + ch] / a;
            img.px[i + ch] = quint8(v > 255 ? 255 : v);
        }
    }
    return img;
}

// ---- glyph runs ------------------------------------------------------------------------------

struct GlyphRun
{
    SkFont font;
    std::vector<SkGlyphID> glyphs;
    std::vector<SkPoint> pos; // pen positions relative to the text's origin
};

class CollectRuns final : public SkShaper::RunHandler
{
public:
    std::vector<GlyphRun> runs;
    SkScalar x = 0;

    void beginLine() override {}
    void runInfo(const RunInfo &) override {}
    void commitRunInfo() override {}
    Buffer runBuffer(const RunInfo &info) override
    {
        GlyphRun run{info.fFont, std::vector<SkGlyphID>(info.glyphCount), std::vector<SkPoint>(info.glyphCount)};
        runs.push_back(std::move(run));
        return {runs.back().glyphs.data(), runs.back().pos.data(), nullptr, nullptr, {x, 0}};
    }
    void commitRunBuffer(const RunInfo &info) override { x += info.fAdvance.fX; }
    void commitLine() override {}
};

SkFont pixelFont(const sk_sp<SkTypeface> &tf, double size)
{
    SkFont f(tf, SkScalar(size));
    f.setHinting(SkFontHinting::kNormal);
    f.setSubpixel(false);
    // PIL lays glyphs out with unhinted (linear) advances and puts each one on a whole pixel.
    f.setLinearMetrics(true);
    f.setEdging(SkFont::Edging::kAntiAlias);
    f.setBaselineSnap(true);
    return f;
}

// Both profiles shape with HarfBuzz and the font's linear advances. sprite2x is PIL's basic
// layout: cmap glyphs and pair kerning (FreeType reads GPOS pair adjustments), no ligatures or
// contextual forms, so those features are off. mask1x is RAQM: HarfBuzz's default features.
// letterSpacing draws one character at a time, as the proofs did.
std::vector<GlyphRun> shapedLayout(const SkFont &font, const QString &text, double letterSpacing, bool ligatures)
{
    static const sk_sp<SkUnicode> unicode = SkUnicodes::ICU::Make();
    static QMutex mutex; // one shaper, not thread-safe
    QMutexLocker lock(&mutex);
    static const std::unique_ptr<SkShaper> shaper = SkShapers::HB::ShapeDontWrapOrReorder(unicode, nullptr);
    std::vector<GlyphRun> out;
    const auto shapeOne = [&](const QString &s, double atX) {
        const QByteArray utf8 = s.toUtf8();
        const size_t n = size_t(utf8.size());
        CollectRuns h;
        h.x = SkScalar(atX);
        if (!shaper || n == 0)
            return atX;
        // Advances come from the unhinted outline, fractional as PIL's are; the glyphs are then
        // drawn hinted with `font`.
        SkFont unhinted = font;
        unhinted.setHinting(SkFontHinting::kNone);
        unhinted.setSubpixel(true); // SkShaper rounds advances of a non-subpixel font
        SkShaper::TrivialFontRunIterator fonts(unhinted, n);
        SkShaper::TrivialBiDiRunIterator bidi(0, n);
        std::unique_ptr<SkShaper::ScriptRunIterator> script = SkShapers::HB::ScriptRunIterator(utf8.constData(), n);
        SkShaper::TrivialLanguageRunIterator language("", n);
        std::vector<SkShaper::Feature> features;
        if (!ligatures)
            for (const char *tag : {"liga", "clig", "calt", "rlig", "dlig"})
                features.push_back({SkSetFourByteTag(tag[0], tag[1], tag[2], tag[3]), 0, 0, n});
        shaper->shape(utf8.constData(), n, fonts, bidi, *script, language, features.data(), features.size(), 1e9f, &h);
        for (GlyphRun &r : h.runs) {
            r.font = font;
            out.push_back(std::move(r));
        }
        return double(h.x);
    };
    if (letterSpacing == 0.0) {
        shapeOne(text, 0.0);
    } else {
        double x = 0.0;
        for (const QChar c : text)
            x = shapeOne(QString(c), x) + letterSpacing;
    }
    return out;
}

std::vector<GlyphRun> basicLayout(const SkFont &font, const QString &text)
{
    return shapedLayout(font, text, 0.0, false);
}

// Coverage of a glyph layout drawn with its pen at `origin` on an A8 surface the size of `bounds`
// (integer px). Pen positions are put on whole pixels (FreeType's PIXEL()), as PIL does.
SkBitmap coverage(const std::vector<GlyphRun> &runs, SkPoint origin, const SkIRect &bounds)
{
    SkBitmap bm;
    bm.allocPixels(SkImageInfo::MakeA8(qMax(1, bounds.width()), qMax(1, bounds.height())));
    bm.eraseColor(SK_ColorTRANSPARENT);
    SkCanvas canvas(bm);
    canvas.translate(SkScalar(-bounds.left()), SkScalar(-bounds.top()));
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(SK_ColorBLACK);
    for (const GlyphRun &r : runs) {
        std::vector<SkPoint> pos(r.pos.size());
        for (size_t i = 0; i < pos.size(); ++i)
            pos[i] = {SkScalar(std::floor(origin.fX + r.pos[i].fX + 0.5f)),
                      SkScalar(std::floor(origin.fY + r.pos[i].fY + 0.5f))};
        canvas.drawGlyphs({r.glyphs.data(), r.glyphs.size()}, {pos.data(), pos.size()}, {0, 0}, r.font, paint);
    }
    return bm;
}

SkIRect layoutBounds(const std::vector<GlyphRun> &runs, SkPoint origin)
{
    SkRect u = SkRect::MakeEmpty();
    for (const GlyphRun &r : runs) {
        std::vector<SkRect> b(r.glyphs.size());
        r.font.getBounds({r.glyphs.data(), r.glyphs.size()}, {b.data(), b.size()}, nullptr);
        for (size_t i = 0; i < b.size(); ++i)
            u.join(b[i].makeOffset(std::floor(origin.fX + r.pos[i].fX + 0.5f), std::floor(origin.fY + r.pos[i].fY + 0.5f)));
    }
    if (u.isEmpty())
        return SkIRect::MakeXYWH(int(origin.fX), int(origin.fY), 1, 1);
    return u.roundOut().makeOutset(4, 4);
}

// Bounding box of non-zero alpha, relative to the bitmap; empty when blank.
SkIRect alphaBox(const SkBitmap &bm)
{
    int l = bm.width(), t = bm.height(), r = -1, b = -1;
    for (int y = 0; y < bm.height(); ++y) {
        const quint8 *row = static_cast<const quint8 *>(bm.getAddr(0, y));
        for (int x = 0; x < bm.width(); ++x)
            if (row[x]) {
                l = qMin(l, x);
                r = qMax(r, x);
                t = qMin(t, y);
                b = qMax(b, y);
            }
    }
    return r < 0 ? SkIRect::MakeEmpty() : SkIRect::MakeLTRB(l, t, r + 1, b + 1);
}

// ---- per-actor static art --------------------------------------------------------------------

struct TextArt
{
    bool valid = false;
    sk_sp<SkImage> image;   // sprite2x: premultiplied RGBA sprite; mask1x: A8 mask (cropped)
    SkBitmap mask;          // mask1x: the A8 pixels, for the per-frame opacity table
    QPointF origin;         // design px of the image's top-left
    QRectF ink;             // design px, right/bottom exclusive
};

sk_sp<SkImage> premulImage(const Img &spr)
{
    SkBitmap bm;
    bm.allocPixels(SkImageInfo::MakeN32Premul(qMax(1, spr.w), qMax(1, spr.h)));
    for (int y = 0; y < spr.h; ++y) {
        auto *row = static_cast<uint32_t *>(bm.getAddr(0, y));
        for (int x = 0; x < spr.w; ++x) {
            const quint8 *p = spr.at(x, y);
            const int a = p[3];
            row[x] = SkPreMultiplyARGB(U8CPU(a), U8CPU(p[0]), U8CPU(p[1]), U8CPU(p[2]));
        }
    }
    bm.setImmutable();
    return bm.asImage();
}

// sprite2x text: drawn at 2x with 7/8 px (1x) of padding, reduced with Lanczos. The padding's
// parity is chosen so that the pen lands exactly on the actor's baseline anchor.
TextArt spriteText(const SkFont &font2x, const QString &text, const Rgba &color, QPointF pen)
{
    TextArt art;
    const std::vector<GlyphRun> runs = basicLayout(font2x, text);
    const SkIRect bounds = layoutBounds(runs, {0, 0});
    const SkBitmap cov = coverage(runs, {0, 0}, bounds);
    SkIRect ink = alphaBox(cov);
    if (ink.isEmpty())
        ink = SkIRect::MakeXYWH(0, 0, 1, 1);
    else
        ink.offset(bounds.left(), bounds.top());
    // L, T: ink box relative to the pen at 2x (PIL's getbbox, ascender cancelled out).
    const int L = ink.left(), T = ink.top();
    const double pen2x = pen.x() * kSS, base2x = pen.y() * kSS;
    // Sprite top-left at 2x = pen2x - (padL - L); it must be even so the 1x sprite is whole.
    const int padL = (int(std::llround(pen2x)) - (14 - L)) % 2 == 0 ? 14 : 15;
    const int padT = (int(std::llround(base2x)) - (16 - T)) % 2 == 0 ? 16 : 17;
    Img spr{ink.width() + 28, ink.height() + 32, {}};
    spr.px.assign(size_t(spr.w) * spr.h * 4, 0);
    for (int y = 0; y < ink.height(); ++y)
        for (int x = 0; x < ink.width(); ++x) {
            const int cx = ink.left() - bounds.left() + x, cy = ink.top() - bounds.top() + y;
            const quint8 a = *static_cast<const quint8 *>(cov.getAddr(cx, cy));
            quint8 *p = spr.at(x + padL, y + padT);
            p[0] = quint8(color.r);
            p[1] = quint8(color.g);
            p[2] = quint8(color.b);
            p[3] = a;
        }
    // Outside the ink PIL leaves (0,0,0,0); inside, colour with coverage alpha (zero alpha keeps
    // the colour, which premultiplication erases anyway).
    const Img small = pilResizeRgba(std::move(spr), (ink.width() + 28) / kSS, (ink.height() + 32) / kSS);
    art.image = premulImage(small);
    art.origin = QPointF((std::llround(pen2x) - (padL - L)) / double(kSS), (std::llround(base2x) - (padT - T)) / double(kSS));
    int l = small.w, t = small.h, r = -1, b = -1;
    for (int y = 0; y < small.h; ++y)
        for (int x = 0; x < small.w; ++x)
            if (small.at(x, y)[3]) {
                l = qMin(l, x);
                r = qMax(r, x);
                t = qMin(t, y);
                b = qMax(b, y);
            }
    art.ink = r < 0 ? QRectF(art.origin, QSizeF(0, 0))
                    : QRectF(art.origin + QPointF(l, t), QSizeF(r + 1 - l, b + 1 - t));
    art.valid = true;
    return art;
}

// mask1x text: a 1x mask with the pen on the baseline anchor, cropped to its ink.
TextArt maskText(const SkFont &font, const QString &text, double letterSpacing, QPointF pen)
{
    TextArt art;
    const std::vector<GlyphRun> runs = shapedLayout(font, text, letterSpacing, true);
    const SkPoint origin{SkScalar(pen.x()), SkScalar(pen.y())};
    const SkIRect bounds = layoutBounds(runs, origin);
    const SkBitmap cov = coverage(runs, origin, bounds);
    const SkIRect ink = alphaBox(cov);
    if (ink.isEmpty()) {
        art.origin = pen;
        art.ink = QRectF(pen, QSizeF(0, 0));
        art.valid = true;
        return art;
    }
    SkBitmap crop;
    crop.allocPixels(SkImageInfo::MakeA8(ink.width(), ink.height()));
    cov.readPixels(crop.pixmap(), ink.left(), ink.top());
    crop.setImmutable();
    art.mask = crop;
    art.image = crop.asImage();
    art.origin = QPointF(bounds.left() + ink.left(), bounds.top() + ink.top());
    art.ink = QRectF(art.origin, QSizeF(ink.width(), ink.height()));
    art.valid = true;
    return art;
}

// pil1x text: the scripts' 1x PIL sprite (basic layout: kerning, no ligatures), as an A8 mask at
// the pen; drawn in the actor's colour like mask1x.
TextArt pilText(const SkFont &font, const QString &text, QPointF pen)
{
    TextArt art;
    const std::vector<GlyphRun> runs = basicLayout(font, text);
    const SkPoint origin{SkScalar(pen.x()), SkScalar(pen.y())};
    const SkIRect bounds = layoutBounds(runs, origin);
    const SkBitmap cov = coverage(runs, origin, bounds);
    const SkIRect ink = alphaBox(cov);
    if (ink.isEmpty()) {
        art.origin = pen;
        art.ink = QRectF(pen, QSizeF(0, 0));
        art.valid = true;
        return art;
    }
    SkBitmap crop;
    crop.allocPixels(SkImageInfo::MakeA8(ink.width(), ink.height()));
    cov.readPixels(crop.pixmap(), ink.left(), ink.top());
    crop.setImmutable();
    art.mask = crop;
    art.image = crop.asImage();
    art.origin = QPointF(bounds.left() + ink.left(), bounds.top() + ink.top());
    art.ink = QRectF(art.origin, QSizeF(ink.width(), ink.height()));
    art.valid = true;
    return art;
}

// PIL's ImageDraw.line: not anti-aliased, square ends at the end points, pixel centres on whole
// coordinates. Width 1 is a thin line.
void pilLine(SkCanvas &c, double ax, double ay, double bx, double by, double width, SkColor color)
{
    SkPaint paint;
    paint.setAntiAlias(false);
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(SkScalar(qMax(1.0, std::trunc(width))));
    paint.setStrokeCap(SkPaint::kButt_Cap);
    paint.setColor(color);
    c.drawLine(SkScalar(ax + 0.5), SkScalar(ay + 0.5), SkScalar(bx + 0.5), SkScalar(by + 0.5), paint);
}

SkColor skColor(const Rgba &c, double alphaScale = 1.0)
{
    const int a = int(std::lround(qBound(0.0, c.a * alphaScale, 255.0)));
    return SkColorSetARGB(U8CPU(a), U8CPU(c.r), U8CPU(c.g), U8CPU(c.b));
}

} // namespace

class SceneRenderer
{
public:
    Scene scene;
    quint64 key = 0;
    QHash<QString, TextArt> texts;

    void build(const QStringList &dirs, QString *error)
    {
        QHash<QString, sk_sp<SkTypeface>> faces;
        for (auto it = scene.fonts.begin(); it != scene.fonts.end(); ++it) {
            const QString path = resolveFont(it.value(), dirs, error);
            if (path.isEmpty())
                return;
            sk_sp<SkTypeface> tf = systemFontMgr()->makeFromFile(QFile::encodeName(path).constData());
            if (!tf) {
                *error = QStringLiteral("font %1 could not be read").arg(path);
                return;
            }
            faces.insert(it.key(), tf);
        }
        for (const Actor &a : std::as_const(scene.actors)) {
            if (a.type != QLatin1String("text"))
                continue;
            const sk_sp<SkTypeface> tf = faces.value(a.font);
            if (scene.profile == Profile::Pil1x) {
                texts.insert(a.id, pilText(pixelFont(tf, int(a.size)), a.text, a.baseline));
                continue;
            }
            if (scene.profile == Profile::Sprite2x || inFlatGroup(a)) {
                QPointF pen = a.baseline;
                if (a.anchorKind == QLatin1String("sprite")) {
                    *error = QStringLiteral("%1: at.sprite is a conversion-only anchor; use at.baseline").arg(a.id);
                    return;
                }
                const SkFont f2 = pixelFont(tf, int(a.size) * kSS);
                if (a.centerInGroup) {
                    const Actor *g = scene.actor(a.parent);
                    const std::vector<GlyphRun> runs = basicLayout(f2, a.text);
                    const SkIRect b = layoutBounds(runs, {0, 0});
                    SkIRect ink = alphaBox(coverage(runs, {0, 0}, b));
                    ink.offset(b.left(), b.top());
                    const double gw = g ? g->groupW : 0, gh = g ? g->groupH : 0;
                    pen = QPointF(((gw * kSS - ink.width()) / 2.0 - ink.left()) / kSS,
                                  a.alignTop ? (a.centerTop * kSS - ink.top()) / kSS
                                             : ((gh * kSS - ink.height()) / 2.0 - ink.top()) / kSS + a.centerDy);
                }
                texts.insert(a.id, spriteText(f2, a.text, a.color, pen));
            } else {
                if (a.anchorKind != QLatin1String("baseline")) {
                    *error = QStringLiteral("%1: at.lt is a conversion-only anchor; use at.baseline").arg(a.id);
                    return;
                }
                const double size = qMax(10.0, roundHalfEven(a.size));
                texts.insert(a.id, maskText(pixelFont(tf, size), a.text, a.letterSpacing, a.baseline));
            }
        }
        const scene::InkFn ink = [this](const Actor &t) -> std::optional<QRectF> {
            const auto it = texts.constFind(t.id);
            if (it == texts.constEnd())
                return std::nullopt;
            return it->ink;
        };
        scene::resolveConstraints(scene, ink, error);
    }

    bool inFlatGroup(const Actor &a) const
    {
        for (QString p = a.parent; !p.isEmpty();) {
            const Actor *g = scene.actor(p);
            if (!g)
                return false;
            if (g->flatten)
                return true;
            p = g->parent;
        }
        return false;
    }
};

namespace {

// An actor with "offsetSnap": "trunc" moves by whole pixels toward zero, as int() did in the
// Stage13 scripts; everything else is rounded half to even where it lands.
double offsetOf(const Actor &a, double v)
{
    return a.truncOffsets ? std::trunc(v) : v;
}

void drawLine(SkCanvas &c, const Actor &a, const Channels &ch, bool snap)
{
    const double op = qMin(1.0, ch.opacity);
    if (op <= 0.0 || ch.progress <= 0.0)
        return;
    const double q = qBound(0.0, ch.progress, 1.0);
    const double dx = snap ? roundHalfEven(ch.dx) : ch.dx, dy = snap ? roundHalfEven(ch.dy) : ch.dy;
    std::vector<std::pair<QPointF, QPointF>> segs;
    if (a.type == QLatin1String("line")) {
        const QPointF p(std::trunc(a.from.x()), std::trunc(a.from.y()));
        const QPointF e(roundHalfEven(a.from.x() + (a.to.x() - a.from.x()) * q),
                        roundHalfEven(a.from.y() + (a.to.y() - a.from.y()) * q));
        segs.push_back({p, e});
    } else {
        QList<QPointF> pts;
        for (const QPointF &p : a.points)
            pts << QPointF(std::trunc(p.x()), std::trunc(p.y()));
        double total = 0;
        for (int i = 1; i < pts.size(); ++i)
            total += std::hypot(pts[i].x() - pts[i - 1].x(), pts[i].y() - pts[i - 1].y());
        double remain = total * q;
        for (int i = 1; i < pts.size() && remain > 0; ++i) {
            const QPointF p = pts[i - 1], n = pts[i];
            const double len = std::hypot(n.x() - p.x(), n.y() - p.y());
            const double f = len > 0 ? qMin(1.0, remain / len) : 1.0;
            segs.push_back({p, QPointF(roundHalfEven(p.x() + (n.x() - p.x()) * f), roundHalfEven(p.y() + (n.y() - p.y()) * f))});
            remain -= len;
        }
    }
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setStyle(SkPaint::kStroke_Style);
    // cv2.line(..., LINE_AA) covers 2·ceil(t/2)+1 whole pixels plus ~0.22 px of ramp each side
    // for t >= 2 (1.35 px for t = 1): measured from OpenCV, which the proofs drew with.
    const int t = qMax(1, int(std::trunc(a.width)));
    paint.setStrokeWidth(SkScalar(t == 1 ? 1.35 : 2 * ((t + 1) / 2) + 1.43));
    paint.setStrokeCap(SkPaint::kRound_Cap);
    paint.setColor(skColor(a.color, op));
    // cv2 draws pixel centres at whole coordinates; one path so joints do not double up.
    SkPathBuilder path;
    for (const auto &[p, e] : segs) {
        // cv2's round ends reach ~W/2 past a horizontal end but ~0.25 px less past a vertical one.
        const double len = std::hypot(e.x() - p.x(), e.y() - p.y());
        const double k = len > 0.5 ? 0.25 * std::fabs(e.y() - p.y()) / len : 0.0;
        const double ux = len > 0 ? (e.x() - p.x()) / len : 0, uy = len > 0 ? (e.y() - p.y()) / len : 0;
        path.moveTo(SkScalar(p.x() + ux * k + dx + 0.5), SkScalar(p.y() + uy * k + dy + 0.5));
        path.lineTo(SkScalar(e.x() - ux * k + dx + 0.5), SkScalar(e.y() - uy * k + dy + 0.5));
    }
    if (segs.size() <= 1) {
        c.drawPath(path.detach(), paint);
    } else {
        // Separate cv2.line calls overwrite each other in a mask: union, not sum.
        c.saveLayerAlpha(nullptr, paint.getAlpha());
        paint.setAlpha(255);
        c.drawPath(path.detach(), paint);
        c.restore();
    }
}

void drawBoxShape(SkCanvas &c, const Actor &a, const Channels &ch, bool snap, double ox, double oy)
{
    const double dx = (snap ? roundHalfEven(ch.dx) : ch.dx) + ox;
    const double dy = (snap ? roundHalfEven(ch.dy) : ch.dy) + oy;
    const SkRect r = SkRect::MakeLTRB(SkScalar(a.box[0] + dx), SkScalar(a.box[1] + dy),
                                      SkScalar(a.box[0] + a.boxWidth() + 0.5 + dx), SkScalar(a.box[3] + 0.5 + dy));
    c.save();
    if (ch.wipe < 1.0)
        c.clipRect(SkRect::MakeLTRB(r.left() - 8, r.top() - 8, SkScalar(r.left() + roundHalfEven(r.width() * qMax(0.0, ch.wipe))), r.bottom() + 8));
    const bool ellipse = a.type == QLatin1String("ellipse") || a.type == QLatin1String("ring");
    const SkRRect outer = ellipse ? SkRRect::MakeOval(r) : SkRRect::MakeRectXY(r, SkScalar(a.radius), SkScalar(a.radius));
    SkPaint paint;
    paint.setAntiAlias(true);
    if (a.hasFill && a.type != QLatin1String("ring")) {
        paint.setColor(skColor(a.fill, ch.opacity));
        c.drawRRect(outer, paint);
    }
    if (a.hasStroke && a.stroke > 0) {
        SkRRect inner;
        outer.inset(SkScalar(a.stroke), SkScalar(a.stroke), &inner);
        paint.setColor(skColor(a.strokeColor, ch.opacity));
        c.drawDRRect(outer, inner, paint);
    }
    c.restore();
}

class ScenePainterImpl final : public VectorPainter
{
public:
    std::shared_ptr<const SceneRenderer> r;
    double t = 0.0;
    QSize sz;
    quint64 key = 0;

    QSize size() const override { return sz; }
    quint64 cacheKey() const override { return key; }

    void paint(SkCanvas &c) const override
    {
        const Scene &sc = r->scene;
        const double s = sz.height() / double(sc.height);
        const double offX = (sz.width() - sc.width * s) / 2.0;
        c.save();
        c.translate(SkScalar(offX), 0);
        c.scale(SkScalar(s), SkScalar(s));
        const bool exact = s == 1.0 && offX == std::floor(offX);
        const SkSamplingOptions sampling = exact ? SkSamplingOptions() : SkSamplingOptions(SkCubicResampler::Mitchell());
        if (sc.profile == Profile::Pil1x) {
            paintPil(c);
            c.restore();
            return;
        }
        const bool sprite = sc.profile == Profile::Sprite2x;
        for (const Actor &a : sc.actors) {
            bool skip = false;
            const Channels ch = scene::drawChannels(sc, a, t, &skip);
            if (skip)
                continue;
            if (a.type == QLatin1String("group")) {
                if (a.flatten)
                    drawFlatGroup(c, a, ch, sampling);
                continue;
            }
            if (a.type == QLatin1String("text"))
                sprite ? drawSpriteText(c, a, ch, sampling, 0, 0) : drawMaskText(c, a, ch);
            else if (a.type == QLatin1String("line") || a.type == QLatin1String("route"))
                drawLine(c, a, ch, sprite);
            else if (a.type == QLatin1String("rect"))
                sprite ? drawSpriteRect(c, a, ch, 0, 0) : drawReplaceRect(c, a, ch);
            else if (a.hasBox)
                drawBoxShape(c, a, ch, sprite, 0, 0);
        }
        c.restore();
    }

private:
    // ---- pil1x (Part5 Stage05-08): every part on its own layer, in order; scene exitWipe ----
    static double channelValue(const Channels &ch, const QString &name)
    {
        if (name == QLatin1String("opacity"))
            return ch.opacity;
        if (name == QLatin1String("wipe"))
            return ch.wipe;
        if (name == QLatin1String("progress"))
            return ch.progress;
        if (name == QLatin1String("scale"))
            return ch.scale;
        if (name == QLatin1String("vscale"))
            return ch.vscale;
        if (name == QLatin1String("dx"))
            return ch.dx;
        return ch.dy;
    }

    bool pilVisible(const Actor &a) const
    {
        const Scene &sc = r->scene;
        for (const Actor *node = &a; node; node = node->parent.isEmpty() ? nullptr : sc.actor(node->parent)) {
            for (const Actor::Condition &cond : node->visibleWhen) {
                const Actor *other = cond.actor.isEmpty() ? node : sc.actor(cond.actor);
                if (!other)
                    continue;
                if (!(channelValue(scene::drawChannels(sc, *other, t, nullptr), cond.channel) > cond.gt))
                    return false;
            }
        }
        return true;
    }

    void paintPil(SkCanvas &c) const
    {
        const Scene &sc = r->scene;
        double limit = -1.0;
        if (sc.exitWipe.present) {
            const TimedBlock &w = sc.exitWipe;
            const double p = w.dur <= 0.0 ? (t >= w.start ? 1.0 : 0.0) : w.ease((t - w.start) / w.dur);
            if (p >= 1.0)
                return;
            if (p > 0.0)
                limit = roundHalfEven(sc.exitWipeRight - (sc.exitWipeRight - sc.exitWipeLeft) * p);
        }
        c.save();
        if (limit >= 0.0)
            c.clipRect(SkRect::MakeLTRB(0, 0, SkScalar(limit + 1), SkScalar(sc.height)));
        for (const Actor &a : sc.actors) {
            const Actor *parent = a.parent.isEmpty() ? nullptr : sc.actor(a.parent);
            if (parent && parent->pane)
                continue; // drawn with its pane
            const bool group = a.type == QLatin1String("group");
            if ((group && !a.pane) || !pilVisible(a))
                continue;
            if (group) {
                c.saveLayer(nullptr, nullptr);
                for (const Actor &k : sc.actors)
                    if (k.parent == a.id && pilVisible(k))
                        drawPilShape(c, k, scene::drawChannels(sc, k, t, nullptr));
                c.restore();
                continue;
            }
            const Channels ch = scene::drawChannels(sc, a, t, nullptr);
            if (a.type == QLatin1String("text"))
                drawPilText(c, a, ch);
            else
                drawPilShape(c, a, ch);
        }
        c.restore();
    }

    void drawPilText(SkCanvas &c, const Actor &a, const Channels &ch) const
    {
        const TextArt art = r->texts.value(a.id);
        const double op = qBound(0.0, ch.opacity, 1.0);
        if (!art.image || op < 0.001)
            return;
        const double x = roundHalfEven(ch.dx), y = roundHalfEven(ch.dy);
        c.save();
        if (!a.reveal.isEmpty()) {
            // The scripts revealed the whole sprite (ink plus 8 px of padding) from one side.
            const double v = qBound(0.0, ch.wipe, 1.0);
            if (v == 0.0) {
                c.restore();
                return;
            }
            const double L = art.ink.left() - 8 + x, T = art.ink.top() - 8 + y;
            const double w = art.ink.width() + 16, h = art.ink.height() + 16;
            if (a.reveal == QLatin1String("x"))
                c.clipRect(SkRect::MakeLTRB(SkScalar(L), SkScalar(T), SkScalar(L + qMax(0.0, roundHalfEven(w * v))), SkScalar(T + h + 1)));
            else
                c.clipRect(SkRect::MakeLTRB(SkScalar(L), SkScalar(T + h - qMax(0.0, roundHalfEven(h * v))), SkScalar(L + w + 1), SkScalar(T + h + 1)));
        }
        SkPaint paint;
        paint.setColor(skColor(a.color, op));
        c.drawImage(art.image, SkScalar(art.origin.x() + x), SkScalar(art.origin.y() + y), SkSamplingOptions(), &paint);
        c.restore();
    }

    void drawPilShape(SkCanvas &c, const Actor &a, const Channels &ch) const
    {
        const QString &tp = a.type;
        if (tp == QLatin1String("rect") || tp == QLatin1String("roundRect")) {
            double x0 = a.box[0], y0 = a.box[1], x1 = a.box[2], y1 = a.box[3];
            const double w = x1 - x0, h = y1 - y0;
            if (a.wipeSnapInt1)
                x1 = x0 + qMax(1.0, std::trunc(w * ch.wipe));
            else if (ch.wipe != 1.0)
                x1 = x0 + w * ch.wipe;
            if (ch.vscale != 1.0) {
                const double trim = h * (1.0 - ch.vscale) / 2.0;
                y0 += trim;
                y1 -= trim;
            }
            SkPaint paint;
            paint.setAntiAlias(false);
            paint.setColor(skColor(tp == QLatin1String("roundRect") ? a.fill : a.color, ch.opacity));
            const SkRect rr = SkRect::MakeLTRB(SkScalar(x0), SkScalar(y0), SkScalar(x1 + 1), SkScalar(y1 + 1));
            if (a.radius > 0)
                c.drawRRect(SkRRect::MakeRectXY(rr, SkScalar(a.radius), SkScalar(a.radius)), paint);
            else
                c.drawRect(rr, paint);
        } else if (tp == QLatin1String("line")) {
            double ax = a.from.x(), ay = a.from.y(), bx = a.to.x(), by = a.to.y();
            const double p = ch.progress, v = ch.vscale;
            if (a.collapseEnds) {
                const double L = by - ay;
                ay += L * (1.0 - v) / 2.0;
                by -= L * (1.0 - v) / 2.0;
            } else if (p != 1.0 || v != 1.0) {
                const double L = by - ay, top = ay;
                ay = top + (L * p) * (1.0 - v) / 2.0;
                by = top + (L * p) * (1.0 + v) / 2.0;
            }
            pilLine(c, ax, ay, bx, by, a.width, skColor(a.color, ch.opacity));
        } else if (tp == QLatin1String("route")) {
            const double p = qBound(0.0, ch.progress, 1.0);
            const int n = int(a.points.size()) - 1;
            double start = 0.0;
            for (int i = 0; i < n; ++i) {
                const QPointF q0 = a.points.at(i), q1 = a.points.at(i + 1);
                const double wgt = i < a.segmentWeights.size() ? a.segmentWeights.at(i) : 1.0 / n;
                if (i == n - 1 || p <= start + wgt) {
                    const double q = (p - start) / wgt;
                    pilLine(c, q0.x(), q0.y(), q0.x() + (q1.x() - q0.x()) * q, q0.y() + (q1.y() - q0.y()) * q, a.width,
                            skColor(a.color, ch.opacity));
                    break;
                }
                pilLine(c, q0.x(), q0.y(), q1.x(), q1.y(), a.width, skColor(a.color, ch.opacity));
                start += wgt;
            }
        } else if (tp == QLatin1String("ellipse") || tp == QLatin1String("ring")) {
            SkRect bb;
            if (a.hasCenter) {
                const double rad = roundHalfEven(a.circleRadius * ch.scale);
                bb = SkRect::MakeLTRB(SkScalar(a.center.x() - rad), SkScalar(a.center.y() - rad),
                                      SkScalar(a.center.x() + rad + 1), SkScalar(a.center.y() + rad + 1));
            } else {
                bb = SkRect::MakeLTRB(SkScalar(a.box[0]), SkScalar(a.box[1]), SkScalar(a.box[2] + 1), SkScalar(a.box[3] + 1));
            }
            SkPaint paint;
            paint.setAntiAlias(false);
            if (tp == QLatin1String("ring")) {
                SkRRect outer = SkRRect::MakeOval(bb), inner;
                outer.inset(SkScalar(a.stroke), SkScalar(a.stroke), &inner);
                paint.setColor(skColor(a.color, ch.opacity));
                c.drawDRRect(outer, inner, paint);
            } else {
                paint.setColor(skColor(a.fill, ch.opacity));
                c.drawOval(bb, paint);
            }
        }
    }

    void drawSpriteText(SkCanvas &c, const Actor &a, const Channels &ch, const SkSamplingOptions &sampling,
                        double ox, double oy) const
    {
        const TextArt art = r->texts.value(a.id);
        if (!art.image || ch.opacity <= 0.0 || ch.wipe <= 0.0)
            return;
        const double x = roundHalfEven(art.origin.x() + offsetOf(a, ch.dx)) + ox;
        const double y = roundHalfEven(art.origin.y() + offsetOf(a, ch.dy)) + oy;
        c.save();
        const int limit = int(roundHalfEven(art.image->width() * qMin(1.0, ch.wipe)));
        c.clipRect(SkRect::MakeXYWH(SkScalar(x), SkScalar(y), SkScalar(limit), SkScalar(art.image->height())));
        clipToActor(c, a, true);
        SkPaint paint;
        paint.setAlphaf(float(qMin(1.0, ch.opacity)));
        c.drawImage(art.image, SkScalar(x), SkScalar(y), sampling, &paint);
        c.restore();
    }

    void drawMaskText(SkCanvas &c, const Actor &a, const Channels &ch) const
    {
        const TextArt art = r->texts.value(a.id);
        if (!art.image || ch.opacity <= 0.0)
            return;
        c.save();
        if (!clipToActor(c, a, false) || !clipToRect(c, a)) {
            c.restore();
            return;
        }
        sk_sp<SkImage> img = art.image;
        if (ch.opacity < 1.0) {
            // Image.point(lambda p: round(p * alpha)): Python's round, half to even.
            quint8 lut[256];
            for (int i = 0; i < 256; ++i)
                lut[i] = quint8(roundHalfEven(i * ch.opacity));
            SkBitmap bm;
            bm.allocPixels(art.mask.info());
            for (int y = 0; y < bm.height(); ++y) {
                const auto *src = static_cast<const quint8 *>(art.mask.getAddr(0, y));
                auto *dst = static_cast<quint8 *>(bm.getAddr(0, y));
                for (int x = 0; x < bm.width(); ++x)
                    dst[x] = lut[src[x]];
            }
            img = bm.asImage();
        }
        SkPaint paint;
        paint.setColor(skColor(a.color));
        const bool whole = ch.dx == std::floor(ch.dx) && ch.dy == std::floor(ch.dy);
        c.drawImage(img, SkScalar(art.origin.x() + ch.dx), SkScalar(art.origin.y() + ch.dy),
                    whole ? SkSamplingOptions() : SkSamplingOptions(SkCubicResampler::CatmullRom()), &paint);
        c.restore();
    }

    // clipRect: a fixed window whose top rises as the actor enters (Stage03b A/C). The phase is the
    // entrance's alone, so the window stays open through the exit, as in the scripts.
    bool clipToRect(SkCanvas &c, const Actor &a) const
    {
        if (!a.hasClipRect)
            return true;
        double p = 1.0;
        if (a.in.present)
            p = a.in.dur <= 0.0 ? (t >= a.in.start ? 1.0 : 0.0) : a.in.ease((t - a.in.start) / a.in.dur);
        const double rise = (1.0 - p) * a.clipRise;
        const double cy0 = a.clipRect[1] + (a.clipRiseRound ? roundHalfEven(rise) : std::trunc(rise));
        if (cy0 > a.clipRect[3] || a.clipRect[2] < a.clipRect[0]) {
            c.clipRect(SkRect::MakeEmpty());
            return false;
        }
        c.clipRect(SkRect::MakeLTRB(SkScalar(std::trunc(a.clipRect[0])), SkScalar(std::trunc(cy0)),
                                    SkScalar(std::trunc(a.clipRect[2]) + 1), SkScalar(std::trunc(a.clipRect[3]) + 1)));
        return true;
    }

    // clipTo: the text shows only inside the target's current box, shrunk by the inset. PIL's
    // rectangle truncates its corners and includes both. False when the clip is empty.
    bool clipToActor(SkCanvas &c, const Actor &a, bool) const
    {
        if (a.clipActor.isEmpty())
            return true;
        const Actor *t = r->scene.actor(a.clipActor);
        if (!t)
            return true;
        const QRectF b = scene::currentBox(*t, scene::drawChannels(r->scene, *t, this->t, nullptr));
        const double cx0 = b.left() + a.clipInset[0], cy0 = b.top() + a.clipInset[1];
        const double cx1 = b.right() - a.clipInset[2], cy1 = b.bottom() - a.clipInset[3];
        if (cx1 < cx0 || cy1 < cy0) {
            c.clipRect(SkRect::MakeEmpty());
            return false;
        }
        c.clipRect(SkRect::MakeLTRB(SkScalar(std::trunc(cx0)), SkScalar(std::trunc(cy0)),
                                    SkScalar(std::trunc(cx1) + 1), SkScalar(std::trunc(cy1) + 1)));
        return true;
    }

    // sprite2x rect: a solid 1x sprite x0..x1 inclusive, whole-pixel offset and wipe.
    void drawSpriteRect(SkCanvas &c, const Actor &a, const Channels &ch, double ox, double oy) const
    {
        if (ch.opacity <= 0.0 || ch.wipe <= 0.0)
            return;
        const double w = std::trunc(a.boxWidth() + 1), h = std::trunc(a.box[3] - a.box[1] + 1);
        const double x = roundHalfEven(a.box[0] + offsetOf(a, ch.dx)) + ox, y = roundHalfEven(a.box[1] + offsetOf(a, ch.dy)) + oy;
        const double limit = roundHalfEven(w * qMin(1.0, ch.wipe));
        SkPaint paint;
        paint.setColor(skColor(a.fill, qMin(1.0, ch.opacity)));
        c.drawRect(SkRect::MakeXYWH(SkScalar(x), SkScalar(y), SkScalar(limit), SkScalar(h)), paint);
    }

    // mask1x rect: written straight into the canvas — the later shape replaces, alpha included.
    void drawReplaceRect(SkCanvas &c, const Actor &a, const Channels &ch) const
    {
        const QRectF b = scene::currentBox(a, ch);
        const SkRect r = SkRect::MakeLTRB(SkScalar(std::trunc(b.left()) + ch.dx), SkScalar(std::trunc(b.top()) + ch.dy),
                                          SkScalar(std::trunc(b.right()) + 1 + ch.dx), SkScalar(std::trunc(b.bottom()) + 1 + ch.dy));
        SkPaint paint;
        paint.setBlendMode(SkBlendMode::kSrc);
        paint.setColor(SkColorSetARGB(U8CPU(roundHalfEven(a.fill.a * qBound(0.0, ch.opacity, 1.0))), U8CPU(a.fill.r),
                                      U8CPU(a.fill.g), U8CPU(a.fill.b)));
        if (a.radius > 0) {
            paint.setAntiAlias(true);
            c.drawRRect(SkRRect::MakeRectXY(r, SkScalar(a.radius), SkScalar(a.radius)), paint);
        } else {
            c.drawRect(r, paint);
        }
    }

    // A flattened group is one sprite: its children at rest, then the group's own channels.
    void drawFlatGroup(SkCanvas &c, const Actor &g, const Channels &ch, const SkSamplingOptions &sampling) const
    {
        if (ch.opacity <= 0.0 || ch.wipe <= 0.0)
            return;
        const double x = roundHalfEven(g.groupAt.x() + offsetOf(g, ch.dx)), y = roundHalfEven(g.groupAt.y() + offsetOf(g, ch.dy));
        c.save();
        c.clipRect(SkRect::MakeXYWH(SkScalar(x), SkScalar(y), SkScalar(roundHalfEven(g.groupW * qMin(1.0, ch.wipe))),
                                    SkScalar(g.groupH)));
        c.saveLayerAlphaf(nullptr, float(qMin(1.0, ch.opacity)));
        const Channels rest;
        for (const Actor &k : r->scene.actors) {
            if (k.parent != g.id)
                continue;
            if (k.type == QLatin1String("text"))
                drawSpriteText(c, k, rest, sampling, x, y);
            else if (k.type == QLatin1String("rect"))
                drawSpriteRect(c, k, rest, x, y);
            else if (k.hasBox)
                drawBoxShape(c, k, rest, true, x, y);
            else if (k.type == QLatin1String("line")) {
                c.save();
                c.translate(SkScalar(x), SkScalar(y));
                drawLine(c, k, rest, true);
                c.restore();
            }
        }
        c.restore();
        c.restore();
    }
};

struct CacheEntry
{
    std::shared_ptr<const SceneRenderer> renderer;
    QString error;
};

} // namespace

QStringList sceneFontDirs(const QStringList &extra)
{
    QStringList dirs = extra;
    const QString home = QDir::homePath();
#if defined(Q_OS_WIN)
    dirs << qEnvironmentVariable("WINDIR", QStringLiteral("C:/Windows")) + QStringLiteral("/Fonts")
         << qEnvironmentVariable("LOCALAPPDATA") + QStringLiteral("/Microsoft/Windows/Fonts");
#elif defined(Q_OS_MACOS)
    dirs << QStringLiteral("/System/Library/Fonts") << QStringLiteral("/Library/Fonts") << home + QStringLiteral("/Library/Fonts");
#else
    dirs << QStringLiteral("/usr/share/fonts") << QStringLiteral("/usr/local/share/fonts") << home + QStringLiteral("/.fonts")
         << home + QStringLiteral("/.local/share/fonts");
#endif
    if (QCoreApplication::instance()) {
        const QString app = QCoreApplication::applicationDirPath();
        dirs << app + QStringLiteral("/scene-fonts") << app + QStringLiteral("/../Resources/scene-fonts");
    }
#ifdef DAFIN_SCENE_FONTS_SOURCE_DIR
    dirs << QStringLiteral(DAFIN_SCENE_FONTS_SOURCE_DIR);
#endif
    return dirs;
}

SceneBuild sceneRenderer(const QJsonObject &doc, const QJsonObject &params, const QStringList &extraFontDirs)
{
    static QMutex mutex;
    static QHash<quint64, CacheEntry> cache;
    static QList<quint64> order;
    quint64 key = scene::sceneKey(doc, params);
    for (const QString &d : extraFontDirs)
        key = key * 1099511628211ULL ^ qHash(d);
    {
        QMutexLocker lock(&mutex);
        if (const auto it = cache.constFind(key); it != cache.constEnd())
            return {it->renderer, it->error};
    }
    QString error;
    std::shared_ptr<SceneRenderer> r;
    if (std::optional<Scene> sc = scene::loadScene(doc, params, &error)) {
        r = std::make_shared<SceneRenderer>();
        r->scene = std::move(*sc);
        r->key = key;
        r->build(sceneFontDirs(extraFontDirs), &error);
        if (!error.isEmpty())
            r.reset();
    }
    QMutexLocker lock(&mutex);
    cache.insert(key, {r, error});
    order.append(key);
    while (order.size() > 48)
        cache.remove(order.takeFirst());
    return {r, error};
}

const scene::Scene &sceneOf(const SceneRenderer &renderer)
{
    return renderer.scene;
}

std::shared_ptr<const VectorPainter> makeScenePainter(std::shared_ptr<const SceneRenderer> renderer, double designT,
                                                      QSize size)
{
    if (!renderer)
        return {};
    if (size.isEmpty())
        size = QSize(renderer->scene.width, renderer->scene.height);
    auto p = std::make_shared<ScenePainterImpl>();
    p->t = designT;
    p->sz = size;
    // Same channels, same pixels: a frame of the hold shares its key with every other one.
    quint64 h = renderer->key ^ (quint64(size.width()) << 32) ^ quint64(size.height());
    const auto mix = [&h](double v) {
        quint64 bits;
        std::memcpy(&bits, &v, sizeof bits);
        h = (h ^ bits) * 1099511628211ULL;
    };
    for (const Actor &a : renderer->scene.actors) {
        const Channels ch = scene::drawChannels(renderer->scene, a, designT, nullptr);
        for (double v : {ch.opacity, ch.wipe, ch.progress, ch.scale, ch.dx, ch.dy, ch.vscale})
            mix(v);
    }
    // The pil1x exit wipe moves while every actor rests: it must change the key too.
    const TimedBlock &ew = renderer->scene.exitWipe;
    if (ew.present)
        mix(ew.dur <= 0.0 ? (designT >= ew.start ? 1.0 : 0.0) : ew.ease((designT - ew.start) / ew.dur));
    p->key = h ? h : 1;
    p->r = std::move(renderer);
    return p;
}

QImage renderSceneFrame(const std::shared_ptr<const SceneRenderer> &renderer, double designT, QSize size)
{
    const std::shared_ptr<const VectorPainter> p = makeScenePainter(renderer, designT, size);
    if (!p)
        return {};
    // Qt's ARGB32 words are Skia's kBGRA_8888 on little-endian hosts (as SkiaRuntime::rasterize).
    QImage image(p->size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    sk_sp<SkSurface> surface = SkSurfaces::WrapPixels(
        SkImageInfo::Make(image.width(), image.height(), kBGRA_8888_SkColorType, kPremul_SkAlphaType),
        image.bits(), size_t(image.bytesPerLine()));
    if (!surface)
        return {};
    p->paint(*surface->getCanvas());
    return image;
}

QImage sceneBaseMatte(QSize size, Rgba dark)
{
    QImage out(size, QImage::Format_RGB32);
    const int w = size.width(), h = size.height();
    for (int y = 0; y < h; ++y) {
        const float fy = h > 1 ? float(y) / float(h - 1) : 0.f;
        auto *row = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const float fx = w > 1 ? float(x) / float(w - 1) : 0.f;
            const auto ch = [&](int c) { return int(qBound(0.f, float(c) + 7.2f * fx + 3.2f * (1.f - fy), 255.f)); };
            row[x] = qRgb(ch(dark.r), ch(dark.g), ch(dark.b));
        }
    }
    return out;
}

QImage compositeOverMatte(const QImage &frame, const QImage &matte)
{
    QImage out = matte.convertToFormat(QImage::Format_RGB32);
    const QImage src = frame.convertToFormat(QImage::Format_ARGB32_Premultiplied);
    for (int y = 0; y < qMin(out.height(), src.height()); ++y) {
        const auto *s = reinterpret_cast<const QRgb *>(src.constScanLine(y));
        auto *d = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < qMin(out.width(), src.width()); ++x) {
            const int A = qAlpha(s[x]);
            if (A == 0)
                continue;
            const float a = A / 255.f;
            // premultiplied in, so src·a is the stored channel itself
            const auto mixc = [&](int dst, int pre) { return int(qBound(0.f, dst * (1.f - a) + float(pre), 255.f)); };
            d[x] = qRgb(mixc(qRed(d[x]), qRed(s[x])), mixc(qGreen(d[x]), qGreen(s[x])), mixc(qBlue(d[x]), qBlue(s[x])));
        }
    }
    return out;
}

QHash<QString, QRectF> sceneRestBoxes(const SceneRenderer &renderer)
{
    QHash<QString, QRectF> boxes;
    const Channels rest;
    for (const Actor &a : renderer.scene.actors) {
        if (a.type == QLatin1String("text")) {
            boxes.insert(a.id, renderer.texts.value(a.id).ink);
        } else if (a.hasBox) {
            boxes.insert(a.id, scene::currentBox(a, rest));
        } else if (a.type == QLatin1String("line") || a.type == QLatin1String("route")) {
            QList<QPointF> pts = a.type == QLatin1String("line") ? QList<QPointF>{a.from, a.to} : a.points;
            QRectF r(pts.first(), QSizeF(0, 0));
            for (const QPointF &p : pts)
                r = r.united(QRectF(p, QSizeF(0, 0)));
            boxes.insert(a.id, r.adjusted(-a.width, -a.width, a.width + 1, a.width + 1));
        }
    }
    return boxes;
}

} // namespace drift::skia
