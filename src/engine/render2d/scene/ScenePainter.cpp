#include "engine/render2d/scene/ScenePainter.h"

#include "core/scene/SceneKeycaps.h"
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
#include "include/core/SkSurfaceProps.h"
#include "include/core/SkTypeface.h"
#if defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
#include "include/ports/SkFontMgr_empty.h"
#endif
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

namespace {

constexpr int kSS = 2; // sprite2x supersampling (a flattened group may draw its sprite at its own "ss")

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
    std::vector<SkPoint> pos; // glyph positions relative to the text's origin (pen + offset)
    SkScalar end = 0;         // pen position after the run's last advance
    std::vector<SkPoint> offsets; // HarfBuzz glyph offsets (marks); folded into pos by the shaper
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
        GlyphRun run{info.fFont, std::vector<SkGlyphID>(info.glyphCount), std::vector<SkPoint>(info.glyphCount), 0,
                     std::vector<SkPoint>(info.glyphCount)};
        runs.push_back(std::move(run));
        // With an offsets buffer SkShaper writes the bare pen positions and the offsets apart, so the
        // advances can be read back from consecutive pens (quantizeLikeHbFt).
        return {runs.back().glyphs.data(), runs.back().pos.data(), runs.back().offsets.data(), nullptr, {x, 0}};
    }
    void commitRunBuffer(const RunInfo &info) override
    {
        x += info.fAdvance.fX;
        runs.back().end = x;
    }
    void commitLine() override {}
};

// The scene fonts as FreeType faces on every platform. The reference measures and draws with PIL,
// which is FreeType; the platform manager would be DirectWrite on Windows and CoreText on macOS,
// whose hinting and glyph boxes are not PIL's (13C U3 moved 0.501 px on Windows only). Builds
// without Skia's FreeType typeface (no SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE) keep the platform one.
sk_sp<SkFontMgr> sceneFontMgr()
{
#if defined(SK_FONTMGR_FREETYPE_EMPTY_AVAILABLE)
    static const sk_sp<SkFontMgr> mgr = SkFontMgr_New_Custom_Empty();
    if (mgr)
        return mgr;
#endif
    return systemFontMgr();
}

SkFont pixelFont(const sk_sp<SkTypeface> &tf, double size)
{
    SkFont f(tf, SkScalar(size));
    f.setHinting(SkFontHinting::kNormal);
    f.setSubpixel(false);
    // PIL lays glyphs out with unhinted (linear) advances, in 26.6 (quantizeLikeHbFt), and puts
    // each one on a whole pixel.
    f.setLinearMetrics(true);
    f.setEdging(SkFont::Edging::kAntiAlias);
    f.setBaselineSnap(true);
    return f;
}

// PIL's Raqm layout keeps every advance in FreeType's 26.6: HarfBuzz runs on hb-ft, so a glyph
// advances by its linear advance rounded to 1/64 px, (v + 512) >> 10 of the 16.16 value, plus its
// GPOS adjustment scaled to 26.6 by HarfBuzz (em_mult: (units * mult + 32768) >> 16, mult =
// (px * 64 << 16) / upem). The pens are summed as integers and put on whole pixels with PIXEL().
// SkShaper keeps 16.16 instead, and the difference decides ties: ALT at 43 px ends at 72.498 px in
// Skia (key 162 px) but at 4640/64 = 72.5 px in PIL, PIXEL() 73 (key 164 px, 13A relayout). So the
// shaped run is put back on PIL's grid: the unkerned advance is Skia's linear width (FreeType's
// linearHoriAdvance), the kerning is whatever the shaper added to it, in font units.
// Returns the pen after the run, relative to `atX`, in 26.6.
qint64 quantizeLikeHbFt(GlyphRun &r, const SkFont &shapedWith, SkScalar atX, qint64 pen26)
{
    const size_t n = r.glyphs.size();
    if (n == 0)
        return pen26;
    const double size = double(shapedWith.getSize());
    const int upem = shapedWith.getTypeface() ? shapedWith.getTypeface()->getUnitsPerEm() : 0;
    if (size <= 0 || upem <= 0)
        return std::llround(double(r.end - atX) * 64.0); // nothing to scale with: leave the run as shaped
    std::vector<SkScalar> widths(n);
    shapedWith.getWidths({r.glyphs.data(), n}, {widths.data(), n});
    const qint64 mult = (qint64(std::llround(size * 64.0)) << 16) / upem;
    const auto emMult = [mult](qint64 units) { return (units * mult + 32768) >> 16; };
    const auto units = [&](double px) { return qint64(std::llround(px * upem / size)); };
    for (size_t i = 0; i < n; ++i) {
        const double shaped = double((i + 1 < n ? r.pos[i + 1].fX : r.end) - r.pos[i].fX);
        const qint64 linear16 = std::llround(double(widths[i]) * 65536.0);
        const qint64 adv26 = ((linear16 + 512) >> 10) + emMult(units(shaped - double(widths[i])));
        const qint64 off26 = emMult(units(double(r.offsets[i].fX)));
        r.pos[i].fX = atX + SkScalar(double(pen26 + off26) / 64.0);
        r.pos[i].fY += r.offsets[i].fY;
        pen26 += adv26;
    }
    r.end = atX + SkScalar(double(pen26) / 64.0);
    return pen26;
}

// Both profiles shape with HarfBuzz and the font's linear advances, on PIL's 26.6 grid
// (quantizeLikeHbFt). sprite2x: cmap glyphs and pair kerning, no ligatures or contextual forms, so
// those features are off. mask1x is RAQM: HarfBuzz's default features.
// letterSpacing draws one character at a time, as the proofs did.
std::vector<GlyphRun> shapedLayout(const SkFont &font, const QString &text, double letterSpacing, bool ligatures,
                                   double *advance = nullptr)
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
        qint64 pen26 = 0;
        for (GlyphRun &r : h.runs) {
            pen26 = quantizeLikeHbFt(r, unhinted, SkScalar(atX), pen26);
            r.font = font;
            out.push_back(std::move(r));
        }
        return h.runs.empty() ? double(h.x) : atX + double(pen26) / 64.0;
    };
    double end = 0.0;
    if (letterSpacing == 0.0) {
        end = shapeOne(text, 0.0);
    } else {
        double x = 0.0;
        for (const QChar c : text)
            end = x = shapeOne(QString(c), x) + letterSpacing;
    }
    if (advance)
        *advance = end;
    return out;
}

std::vector<GlyphRun> basicLayout(const SkFont &font, const QString &text, double *advance = nullptr)
{
    return shapedLayout(font, text, 0.0, false, advance);
}

// PIL's textbbox / getbbox of a basic layout, pen at 0 and the baseline at 0 (anchor "ls"): the
// glyphs' boxes (FT_GLYPH_BBOX_PIXELS), widened to the pen on the left, to the rounded advance on
// the right and to the baseline below/above.
// It is not the ink: "File" is 0..135 at 84 px although its ink is 7..133. render.py measures
// fitBox and centres group texts with it, so the native layout must too.
struct PilBox
{
    int left = 0, top = 0, right = 0, bottom = 0;
    double advance = 0.0;
};

// The layout's pen positions and total advance in FreeType's 26.6 units, as PIL sums them: every
// glyph advance is a whole 1/64 px (HarfBuzz at a 64× scale), Skia's shaper keeps 1/65536. Only
// the measurements use this; the drawn glyphs keep Skia's positions. ("Export settings and more"
// at 126 px: 1479.50 in PIL, 1479.49 unquantised, which rounds the other way.)
double quantisedPens(const std::vector<GlyphRun> &runs, double end, std::vector<double> *pens)
{
    std::vector<double> x;
    for (const GlyphRun &r : runs)
        for (const SkPoint &p : r.pos)
            x.push_back(p.fX);
    double q = 0.0;
    for (size_t i = 0; i < x.size(); ++i) {
        if (pens)
            pens->push_back(q);
        const double next = i + 1 < x.size() ? x[i + 1] : end;
        q += std::round((next - x[i]) * 64.0) / 64.0;
    }
    return x.empty() ? end : q;
}

double pilAdvance(const SkFont &font, const QString &text)
{
    double end = 0.0;
    const std::vector<GlyphRun> runs = basicLayout(font, text, &end);
    return quantisedPens(runs, end, nullptr);
}

PilBox pilBBox(const SkFont &font, const QString &text)
{
    PilBox box;
    double end = 0.0;
    const std::vector<GlyphRun> runs = basicLayout(font, text, &end);
    std::vector<double> pens;
    box.advance = quantisedPens(runs, end, &pens);
    SkRect u = SkRect::MakeEmpty();
    size_t k = 0;
    for (const GlyphRun &r : runs) {
        std::vector<SkRect> b(r.glyphs.size());
        r.font.getBounds({r.glyphs.data(), r.glyphs.size()}, {b.data(), b.size()}, nullptr);
        for (size_t i = 0; i < b.size(); ++i, ++k)
            if (!b[i].isEmpty())
                u.join(b[i].makeOffset(std::floor(pens[k] + 0.5), std::floor(r.pos[i].fY + 0.5f)));
    }
    const SkIRect g = u.isEmpty() ? SkIRect::MakeEmpty() : u.roundOut();
    box.left = qMin(0, g.left());
    box.right = qMax(int(std::floor(box.advance + 0.5)), g.isEmpty() ? 0 : g.right()); // PIXEL(): half up
    // Vertically the box always holds the baseline ("-" is -40..0 at 126 px, "." -19..1).
    box.top = g.isEmpty() ? 0 : qMin(0, g.top());
    box.bottom = g.isEmpty() ? 0 : qMax(0, g.bottom());
    return box;
}

// FreeType's size ascender (PIL's getmetrics()[0]): hhea ascender scaled, rounded up.
int pilAscender(const SkFont &font)
{
    const sk_sp<SkTypeface> tf = font.refTypeface();
    const int upem = tf ? tf->getUnitsPerEm() : 0;
    quint8 hhea[8] = {};
    if (upem > 0 && tf->getTableData(SkSetFourByteTag('h', 'h', 'e', 'a'), 0, sizeof hhea, hhea) == sizeof hhea) {
        const int asc = qint16(quint16(hhea[4]) << 8 | hhea[5]);
        return int(std::ceil(asc * double(font.getSize()) / upem - 1e-9));
    }
    SkFontMetrics m;
    font.getMetrics(&m);
    return int(std::ceil(-m.fAscent - 1e-9));
}

// Coverage of a glyph layout drawn with its pen at `origin` on an A8 surface the size of `bounds`
// (integer px). Pen positions are put on whole pixels (FreeType's PIXEL()), as PIL does.
SkBitmap coverage(const std::vector<GlyphRun> &runs, SkPoint origin, const SkIRect &bounds)
{
    SkBitmap bm;
    bm.allocPixels(SkImageInfo::MakeA8(qMax(1, bounds.width()), qMax(1, bounds.height())));
    bm.eraseColor(SK_ColorTRANSPARENT);
    // Linear coverage, as FreeType renders it for PIL: no text contrast and gamma 1. Skia's default
    // (contrast 0.5, sRGB, applied to A8 masks) left every proof text ~3.5 % lighter, which
    // moved the centroid of 15_B's definition line by 0.55 px (gate 0.5).
    const SkSurfaceProps linear(0, kUnknown_SkPixelGeometry, 0.0f, 1.0f);
    SkCanvas canvas(bm, linear);
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

// sprite2x text: drawn at f× (2, or a flattened group's 3) with 7/8 px (1x) of padding, reduced
// with Lanczos. The padding is chosen so that the pen lands exactly on the actor's baseline
// anchor. `exactGrid` keeps the f× sprite a whole multiple of f: a flattened group reduces its
// whole f× image to exactly 1/f (render.py group_sprite), a lone text sprite by width // 2.
TextArt spriteText(const SkFont &fontF, const QString &text, const Rgba &color, QPointF pen, int f = kSS,
                   bool exactGrid = false)
{
    TextArt art;
    const std::vector<GlyphRun> runs = basicLayout(fontF, text);
    const SkIRect bounds = layoutBounds(runs, {0, 0});
    const SkBitmap cov = coverage(runs, {0, 0}, bounds);
    // The sprite is sized by PIL's getbbox (render.py text_sprite), which spans the pen origin and
    // the advance, not by the drawn ink: with the ink, an odd width made (w + 14 ss) / ss truncate,
    // the reduction was no longer exactly ss : 1 and a long line drifted (15_B line1, 0.55 px).
    const PilBox pb = pilBBox(fontF, text);
    SkIRect ink = SkIRect::MakeLTRB(pb.left, pb.top, pb.right, pb.bottom);
    if (ink.isEmpty())
        ink = SkIRect::MakeXYWH(0, 0, 1, 1);
    // L, T: ink box relative to the pen at f× (PIL's getbbox, ascender cancelled out).
    const int L = ink.left(), T = ink.top();
    const double penF = pen.x() * f, baseF = pen.y() * f;
    // Sprite top-left at f× = penF - (padL - L); it must be a multiple of f so the 1x sprite is
    // whole: the smallest pad from `base` up that makes it so (f = 2: 14 or 15, 16 or 17).
    const auto padFor = [f](long long p, int inkOffset, int base) {
        return base + int(((p + inkOffset - base) % f + f) % f);
    };
    const int padL = padFor(std::llround(penF), L, 7 * f);
    const int padT = padFor(std::llround(baseF), T, 8 * f);
    int sprW = ink.width() + 14 * f, sprH = ink.height() + 16 * f;
    if (exactGrid) {
        sprW = (sprW + f - 1) / f * f;
        sprH = (sprH + f - 1) / f * f;
    }
    Img spr{sprW, sprH, {}};
    spr.px.assign(size_t(spr.w) * spr.h * 4, 0);
    for (int y = 0; y < ink.height(); ++y)
        for (int x = 0; x < ink.width(); ++x) {
            const int cx = ink.left() - bounds.left() + x, cy = ink.top() - bounds.top() + y;
            if (cx < 0 || cy < 0 || cx >= cov.width() || cy >= cov.height())
                continue; // inside PIL's box but outside every glyph: blank, as in PIL
            const quint8 a = *static_cast<const quint8 *>(cov.getAddr(cx, cy));
            quint8 *p = spr.at(x + padL, y + padT);
            p[0] = quint8(color.r);
            p[1] = quint8(color.g);
            p[2] = quint8(color.b);
            p[3] = a;
        }
    // Outside the ink PIL leaves (0,0,0,0); inside, colour with coverage alpha (zero alpha keeps
    // the colour, which premultiplication erases anyway).
    const Img small = pilResizeRgba(std::move(spr), sprW / f, sprH / f);
    art.image = premulImage(small);
    art.origin = QPointF((std::llround(penF) - (padL - L)) / double(f), (std::llround(baseF) - (padT - T)) / double(f));
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

// sprite1x text ("ss": 1, the Stage11/12 Label): drawn once at 1x, no reduction. The sprite is
// PIL's getbbox (anchor "la") plus 4 px each side and 2 below, the pen 4 px in on the ascender
// line, laid at (bx − 4, by − ascender). What falls outside the sprite is cut, as in PIL.
TextArt sprite1xText(const SkFont &font1x, const QString &text, const Rgba &color, QPointF pen)
{
    constexpr int kPad = 4;
    TextArt art;
    const std::vector<GlyphRun> runs = basicLayout(font1x, text);
    const PilBox box = pilBBox(font1x, text);
    const int asc = pilAscender(font1x);
    const int w = qMax(1, box.right + 2 * kPad), h = qMax(1, box.bottom + asc + 2);
    const SkBitmap cov = coverage(runs, {SkScalar(kPad), SkScalar(asc)}, SkIRect::MakeWH(w, h));
    Img spr{w, h, std::vector<quint8>(size_t(w) * h * 4, 0)};
    int l = w, t = h, r = -1, b = -1;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const quint8 a = *static_cast<const quint8 *>(cov.getAddr(x, y));
            if (!a)
                continue;
            quint8 *p = spr.at(x, y);
            p[0] = quint8(color.r);
            p[1] = quint8(color.g);
            p[2] = quint8(color.b);
            p[3] = a;
            l = qMin(l, x);
            r = qMax(r, x);
            t = qMin(t, y);
            b = qMax(b, y);
        }
    art.image = premulImage(spr);
    art.origin = QPointF(pen.x() - kPad, pen.y() - asc);
    art.ink = r < 0 ? QRectF(art.origin, QSizeF(0, 0)) : QRectF(art.origin + QPointF(l, t), QSizeF(r + 1 - l, b + 1 - t));
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
            sk_sp<SkTypeface> tf = sceneFontMgr()->makeFromFile(QFile::encodeName(path).constData());
            if (!tf) {
                *error = QStringLiteral("font %1 could not be read").arg(path);
                return;
            }
            faces.insert(it.key(), tf);
        }
        // Smart Keycaps, phase B (docs/scene-keycaps.md): widths, rows and anchors from the same ink
        // the sprites are placed by, before any text art is made from those anchors.
        if (scene.keycaps) {
            QHash<QString, scene::KeycapInk> measured;
            const scene::KeycapMeasure measure = [&](const QString &font, int px,
                                                     const QString &text) -> std::optional<scene::KeycapInk> {
                const sk_sp<SkTypeface> tf = faces.value(font);
                if (!tf || px <= 0)
                    return std::nullopt;
                const QString key = font + QLatin1Char('\n') + QString::number(px) + QLatin1Char('\n') + text;
                if (const auto it = measured.constFind(key); it != measured.constEnd())
                    return it.value();
                const PilBox b = pilBBox(pixelFont(tf, px), text);
                const scene::KeycapInk ink{b.left, b.top, b.right, b.bottom};
                measured.insert(key, ink);
                return ink;
            };
            if (!scene::layoutKeycaps(scene, measure, error))
                return;
        }
        // Measured layout first (fitBox, after, fillGroup): it moves pens and sizes groups, so
        // the texts below are drawn where they end up. render.py does it in Sprite2x only.
        if (scene.profile == Profile::Sprite2x) {
            scene::LayoutFns fns;
            fns.inkWidth = [&faces](const Actor &t, int ss) -> std::optional<double> {
                const sk_sp<SkTypeface> tf = faces.value(t.font);
                if (!tf)
                    return std::nullopt;
                const PilBox b = pilBBox(pixelFont(tf, int(t.size) * ss), t.text);
                return double(b.right - b.left);
            };
            fns.advance = [&faces](const Actor &t, int ss) -> std::optional<double> {
                const sk_sp<SkTypeface> tf = faces.value(t.font);
                if (!tf)
                    return std::nullopt;
                return pilAdvance(pixelFont(tf, int(t.size) * ss), t.text);
            };
            if (!scene::resolveLayout(scene, fns, error))
                return;
        }
        for (const Actor &a : std::as_const(scene.actors)) {
            if (a.type != QLatin1String("text"))
                continue;
            const sk_sp<SkTypeface> tf = faces.value(a.font);
            const Actor *flat = flatGroupOf(a);
            if (scene.profile == Profile::Sprite2x || flat) {
                QPointF pen = a.baseline;
                if (a.anchorKind == QLatin1String("sprite")) {
                    *error = QStringLiteral("%1: at.sprite is a conversion-only anchor; use at.baseline").arg(a.id);
                    return;
                }
                // Inside a flattened group the group's factor draws it (its own "ss" is unused).
                const int f = flat ? flat->ss : a.ss;
                if (f == 1) {
                    texts.insert(a.id, sprite1xText(pixelFont(tf, int(a.size)), a.text, a.color, pen));
                    continue;
                }
                const SkFont ff = pixelFont(tf, int(a.size) * f);
                if (a.centerInGroup) {
                    // render.py group_sprite: centred on PIL's textbbox (not the ink), at f×.
                    const Actor *g = scene.actor(a.parent);
                    const PilBox bb = pilBBox(ff, a.text);
                    const double gw = g ? g->groupW : 0, gh = g ? g->groupH : 0;
                    pen = QPointF(((gw * f - (bb.right - bb.left)) / 2.0 - bb.left) / f,
                                  a.alignTop ? (a.centerTop * f - bb.top) / f
                                             : ((gh * f - (bb.bottom - bb.top)) / 2.0 - bb.top) / f + a.centerDy);
                }
                texts.insert(a.id, spriteText(ff, a.text, a.color, pen, f, flat != nullptr));
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

    // The nearest flattened group around `a` (which then draws it), or null.
    const Actor *flatGroupOf(const Actor &a) const
    {
        for (QString p = a.parent; !p.isEmpty();) {
            const Actor *g = scene.actor(p);
            if (!g)
                return nullptr;
            if (g->flatten)
                return g;
            p = g->parent;
        }
        return nullptr;
    }
};

namespace {

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

// PIL shapes drawn at f× with inclusive corners: the last f× pixel ends 1/f px past the 1x corner.
// `replace`: inside a flattened group, as render.py's ImageDraw on the group's RGBA sprite, a shape
// replaces what is under it instead of blending over it (a stroke over its own fill, a line over a
// plate). kSrc does that, and its coverage lerp stands in for the f× edge after the Lanczos.
void drawBoxShape(SkCanvas &c, const Actor &a, const Channels &ch, bool snap, double ox, double oy, int f = kSS,
                  bool replace = false)
{
    const double dx = (snap ? roundHalfEven(ch.dx) : ch.dx) + ox;
    const double dy = (snap ? roundHalfEven(ch.dy) : ch.dy) + oy;
    const double end = 1.0 / qMax(1, f);
    const SkRect r = SkRect::MakeLTRB(SkScalar(a.box[0] + dx), SkScalar(a.box[1] + dy),
                                      SkScalar(a.box[0] + a.boxWidth() + end + dx), SkScalar(a.box[3] + end + dy));
    c.save();
    if (ch.wipe < 1.0)
        c.clipRect(SkRect::MakeLTRB(r.left() - 8, r.top() - 8, SkScalar(r.left() + roundHalfEven(r.width() * qMax(0.0, ch.wipe))), r.bottom() + 8));
    const bool ellipse = a.type == QLatin1String("ellipse") || a.type == QLatin1String("ring");
    const SkRRect outer = ellipse ? SkRRect::MakeOval(r) : SkRRect::MakeRectXY(r, SkScalar(a.radius), SkScalar(a.radius));
    SkPaint paint;
    paint.setAntiAlias(true);
    if (replace)
        paint.setBlendMode(SkBlendMode::kSrc);
    if (a.hasFill && a.type != QLatin1String("ring")) {
        paint.setColor(skColor(a.fill, ch.opacity));
        c.drawRRect(outer, paint);
    }
    if (a.hasStroke && a.stroke > 0) {
        // PIL's outline is int(stroke·f) f× px, inside the box.
        const SkScalar w = SkScalar(std::trunc(a.stroke * f) / f);
        SkRRect inner;
        outer.inset(w, w, &inner);
        paint.setColor(skColor(a.strokeColor, ch.opacity));
        c.drawDRRect(outer, inner, paint);
    }
    c.restore();
}

// cv2.circle(..., LINE_AA) on the alpha layer: integer centre on a pixel centre. Measured against
// OpenCV with an area-coverage model (tools/scene-ref: see docs, "Native mirror (C10n)"): a disc
// of radius r covers up to r + 0.5; a ring of thickness t is centred on r and 1.3 px wide for
// t = 1, else 2·ceil(t/2) + 1.7 (r = 22, t = 2: 3.7 px; SSIM 0.997 against cv2). Opacity scales
// the coverage, as render.py's draw_alpha does ("mixWith" / "mix" only matter on an opaque frame).
void drawCircle(SkCanvas &c, const Actor &a, const Channels &ch, bool snap)
{
    const double op = qMin(1.0, ch.opacity);
    if (op <= 0.0)
        return;
    const double dx = snap ? roundHalfEven(ch.dx) : ch.dx, dy = snap ? roundHalfEven(ch.dy) : ch.dy;
    const SkScalar cx = SkScalar(a.center.x() + dx + 0.5), cy = SkScalar(a.center.y() + dy + 0.5);
    SkPaint paint;
    paint.setAntiAlias(true);
    paint.setColor(skColor(a.color, op));
    if (a.hasFill) {
        c.drawCircle(cx, cy, SkScalar(a.radius + 0.5), paint);
        return;
    }
    const int t = qMax(1, int(std::trunc(a.width)));
    paint.setStyle(SkPaint::kStroke_Style);
    paint.setStrokeWidth(SkScalar(t == 1 ? 1.3 : 2 * ((t + 1) / 2) + 1.7));
    c.drawCircle(cx, cy, SkScalar(a.radius), paint);
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
        const bool sprite = sc.profile == Profile::Sprite2x;
        for (const Actor &a : sc.actors) {
            if (a.hidden) // Smart Keycaps: a "+" that would start a wrapped row
                continue;
            bool skip = false;
            Channels ch = scene::drawChannels(sc, a, t, &skip);
            if (skip)
                continue;
            if (a.snapTrunc) { // "snap": "trunc", as Stage13 A: int(dx), int(dy)
                ch.dx = std::trunc(ch.dx);
                ch.dy = std::trunc(ch.dy);
            }
            if (a.type == QLatin1String("group")) {
                if (a.flatten)
                    drawFlatGroup(c, a, ch, sampling);
                continue;
            }
            if (a.type == QLatin1String("text"))
                sprite ? drawSpriteText(c, a, ch, sampling, 0, 0) : drawMaskText(c, a, ch);
            else if (a.type == QLatin1String("line") || a.type == QLatin1String("route"))
                drawLine(c, a, ch, sprite);
            else if (a.type == QLatin1String("circle"))
                drawCircle(c, a, ch, sprite);
            else if (a.type == QLatin1String("rect"))
                sprite ? drawSpriteRect(c, a, ch, 0, 0) : drawReplaceRect(c, a, ch);
            else if (a.hasBox)
                drawBoxShape(c, a, ch, sprite, 0, 0);
        }
        c.restore();
    }

private:
    void drawSpriteText(SkCanvas &c, const Actor &a, const Channels &ch, const SkSamplingOptions &sampling,
                        double ox, double oy) const
    {
        const TextArt art = r->texts.value(a.id);
        if (!art.image || ch.opacity <= 0.0 || ch.wipe <= 0.0)
            return;
        const double x = roundHalfEven(art.origin.x() + ch.dx) + ox;
        const double y = roundHalfEven(art.origin.y() + ch.dy) + oy;
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
        const double x = roundHalfEven(a.box[0] + ch.dx) + ox, y = roundHalfEven(a.box[1] + ch.dy) + oy;
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

    // A line in a flattened group is render.py's ImageDraw.line at the group's ss: `width` * ss whole
    // pixels, rows Y - (W-1)/2 .. Y + W/2 (integer halves) and columns x0 * ss .. x1 * ss inclusive, in `color` with
    // its own `alpha`, replacing the plate under it. Horizontal and vertical lines only; false for
    // any other, which keeps the cv2 line.
    static bool drawGroupLine(SkCanvas &c, const Actor &k, double x, double y, int ss)
    {
        const bool horizontal = k.from.y() == k.to.y(), vertical = k.from.x() == k.to.x();
        if (!horizontal && !vertical)
            return false;
        ss = qMax(1, ss);
        const int W = qMax(1, int(k.width * ss));
        const auto across = [&](double v) { // v in group px -> [first, last + 1) in group px
            const int Y = int(std::llround(v * ss));
            return std::pair<double, double>{double(Y - (W - 1) / 2) / ss, double(Y + W / 2 + 1) / ss};
        };
        const auto along = [&](double a, double b) {
            const int A = int(std::llround(qMin(a, b) * ss)), B = int(std::llround(qMax(a, b) * ss));
            return std::pair<double, double>{double(A) / ss, double(B + 1) / ss};
        };
        const auto [a0, a1] = horizontal ? along(k.from.x(), k.to.x()) : across(k.from.x());
        const auto [b0, b1] = horizontal ? across(k.from.y()) : along(k.from.y(), k.to.y());
        SkPaint paint;
        paint.setAntiAlias(true);
        paint.setBlendMode(SkBlendMode::kSrc);
        paint.setColor(SkColorSetARGB(U8CPU(k.alpha), U8CPU(k.color.r), U8CPU(k.color.g), U8CPU(k.color.b)));
        c.drawRect(SkRect::MakeLTRB(SkScalar(x + a0), SkScalar(y + b0), SkScalar(x + a1), SkScalar(y + b1)), paint);
        return true;
    }

    // A flattened group is one sprite: its children at rest, then the group's own channels.
    void drawFlatGroup(SkCanvas &c, const Actor &g, const Channels &ch, const SkSamplingOptions &sampling) const
    {
        if (ch.opacity <= 0.0 || ch.wipe <= 0.0)
            return;
        const double x = roundHalfEven(g.groupAt.x() + ch.dx), y = roundHalfEven(g.groupAt.y() + ch.dy);
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
                drawBoxShape(c, k, rest, true, x, y, g.ss, true);
            else if (k.type == QLatin1String("line") && !drawGroupLine(c, k, x, y, g.ss)) {
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
        for (double v : {ch.opacity, ch.wipe, ch.progress, ch.scale, ch.dx, ch.dy})
            mix(v);
    }
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
    const Scene &sc = renderer.scene;
    for (const Actor &a : sc.actors) {
        // As render.py rest_boxes: a hidden actor has none (Smart Keycaps), a flattened group is one
        // box and its children are inside it. A plain group only moves its children by its "at"
        // (lines, routes and circles are in group coordinates).
        if (a.hidden || renderer.flatGroupOf(a))
            continue;
        QPointF off;
        for (const Actor *g = sc.actor(a.parent); g; g = sc.actor(g->parent))
            off += g->groupAt;
        if (a.type == QLatin1String("group")) {
            if (a.flatten)
                boxes.insert(a.id, QRectF(a.groupAt + off, QSizeF(a.groupW, a.groupH)));
        } else if (a.type == QLatin1String("text")) {
            boxes.insert(a.id, renderer.texts.value(a.id).ink.translated(off));
        } else if (a.hasBox) {
            boxes.insert(a.id, scene::currentBox(a, rest).translated(off));
        } else if (a.type == QLatin1String("line") || a.type == QLatin1String("route")) {
            QList<QPointF> pts = a.type == QLatin1String("line") ? QList<QPointF>{a.from, a.to} : a.points;
            QRectF r(pts.first(), QSizeF(0, 0));
            for (const QPointF &p : pts)
                r = r.united(QRectF(p, QSizeF(0, 0)));
            boxes.insert(a.id, r.adjusted(-a.width, -a.width, a.width + 1, a.width + 1).translated(off));
        } else if (a.type == QLatin1String("circle")) {
            const double r = a.radius + 2;
            boxes.insert(a.id, QRectF(a.center + off - QPointF(r, r), QSizeF(2 * r + 1, 2 * r + 1)));
        }
    }
    return boxes;
}

QImage scenePreviewMatte(const scene::Scene &scene, QSize size)
{
    if (scene.previewKind == QLatin1String("solid")) {
        QImage out(size, QImage::Format_RGB32);
        out.fill(qRgb(scene.previewColor.r, scene.previewColor.g, scene.previewColor.b));
        return out;
    }
    return sceneBaseMatte(size, scene.mixColor);
}

} // namespace drift::skia
