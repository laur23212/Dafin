// SceneGoldEffects.h — the 18 Golden Run effects of the gold1x scene profile
// (docs/scene-titles.md "gold1x"; reference: tools/scene-ref/golden.py, which follows
// render_stage25.py … render_stage30.py step for step).
//
// Each effect draws light onto a black Frame, as the proofs did; ScenePainter turns the frame into
// a black-keyed layer (alpha = the brightest channel). Text comes through GoldText, so the same
// code runs on Skia in Dafin and on FreeType in a stand-alone check.
#pragma once

#include "SceneGoldOps.h"

#include <QJsonArray>
#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>

namespace drift::scene::gold {

// PIL's getbbox(text, anchor='ls'): pen at 0, baseline at 0.
struct TextBox
{
    int left = 0, top = 0, right = 0, bottom = 0;
};

class GoldText
{
public:
    virtual ~GoldText() = default;
    virtual bool has(const QString &font) const = 0;
    virtual TextBox bbox(const QString &font, int px, const QString &text) const = 0;
    virtual double length(const QString &font, int px, const QString &text) const = 0; // getlength
    virtual int ascender(const QString &font, int px) const = 0; // PIL's anchor 'la' offset to the baseline
    // ImageDraw.text((x, y), text, fill=255, anchor='ls') into an 8-bit mask
    virtual void draw(M8 &into, const QString &font, int px, const QString &text, double x, double y) const = 0;
};

class GoldEffect
{
public:
    virtual ~GoldEffect() = default;
    virtual void draw(Frame &f, double t) const = 0;
};

struct GoldActor
{
    QString font;
    int size = 0;
    QString text;
    double color[3] = {255, 255, 255};
    QJsonObject effect;
};

namespace detail {

inline double num(const QJsonObject &e, const char *k, int i = -1)
{
    const QJsonValue v = e.value(QLatin1String(k));
    return i < 0 ? v.toDouble() : v.toArray().at(i).toDouble();
}
inline void rgb(const QJsonObject &e, const char *k, double out[3])
{
    const QJsonArray a = e.value(QLatin1String(k)).toArray();
    for (int i = 0; i < 3; ++i)
        out[i] = a.at(i).toDouble();
}
inline double phase(const QJsonObject &e, const char *k, double t, double (*ease)(double) = smooth)
{
    return ease((t - num(e, k, 0)) / num(e, k, 1));
}
inline QStringList chars(const QString &s)
{
    QStringList out;
    for (int i = 0; i < s.size(); ++i) {
        const int n = (s.at(i).isHighSurrogate() && i + 1 < s.size()) ? 2 : 1;
        out << s.mid(i, n);
        i += n - 1;
    }
    return out;
}
inline QString joined(const QStringList &l, int n)
{
    QString s;
    for (int i = 0; i < n && i < l.size(); ++i)
        s += l.at(i);
    return s;
}

// The scripts' text masks: PIL draws, then a crop around the ink.
// plain: (bbox + 2 pad), text at (pad − left, pad − top); ceil sizes as the script did.
inline M8 maskPlain(const GoldText &g, const QString &f, int px, const QString &t, int pad, int before, int after, int thr = 0)
{
    const TextBox b = g.bbox(f, px, t);
    M8 m(b.right - b.left + 2 * pad, b.bottom - b.top + 2 * pad);
    g.draw(m, f, px, t, pad - b.left, pad - b.top);
    return cropInk(m, before, after, thr);
}
// render_stage25.textmask() with tracking: whole-word box height, pens at round(x).
inline M8 maskTracked25(const GoldText &g, const QString &f, int px, const QString &t, double trk, int pad)
{
    const QStringList cs = chars(t);
    const TextBox b = g.bbox(f, px, t);
    double sum = 0;
    for (const QString &c : cs)
        sum += g.length(f, px, c);
    M8 m(int(std::ceil(sum + (cs.size() - 1) * trk)) + 2 * pad, std::max(1, b.bottom - b.top + 2 * pad));
    double x = pad;
    for (const QString &c : cs) {
        g.draw(m, f, px, c, roundEven(x), roundEven(pad - b.top));
        x += g.length(f, px, c) + trk;
    }
    return cropInk(m, 4, 5);
}
// render_stage27/28/29 masktxt() with tracking: ascent/descent from each character's box.
inline M8 maskTrackedAsc(const GoldText &g, const QString &f, int px, const QString &t, double trk, int pad, int before, int after)
{
    const QStringList cs = chars(t);
    double sum = 0;
    int asc = 0, desc = 0;
    for (const QString &c : cs) {
        sum += g.length(f, px, c);
        const TextBox b = g.bbox(f, px, c);
        asc = std::max(asc, -b.top);
        desc = std::max(desc, b.bottom);
    }
    M8 m(int(std::ceil(sum + trk * (cs.size() - 1) + 2 * pad)), asc + desc + 2 * pad);
    double p = pad;
    for (const QString &c : cs) {
        g.draw(m, f, px, c, roundEven(p), pad + asc);
        p += g.length(f, px, c) + trk;
    }
    return cropInk(m, before, after);
}
inline M8 maxWidth(const M8 &m, double maxw, bool roundH)
{
    if (m.w <= maxw)
        return m;
    const int nh = roundH ? int(roundEven(m.h * maxw / m.w)) : int(m.h * maxw / m.w);
    return resize8(m, int(maxw), nh, Interp::Area);
}
inline void blendRgbMap(Frame &f, const M8 &m, int x, int y, const std::function<void(int, float *)> &rgbAt, double opacity)
{
    const float k = float(clamp01(opacity) / 255.0);
    for (int j = std::max(0, y); j < std::min(FH, y + m.h); ++j)
        for (int i = std::max(0, x); i < std::min(FW, x + m.w); ++i) {
            const float a = float(m.at(i - x, j - y)) * k;
            if (a <= 0.0f)
                continue;
            float c[3];
            rgbAt(i - x, c);
            std::uint8_t *p = f.px(i, j);
            for (int q = 0; q < 3; ++q)
                p[q] = sat8(double(float(p[q]) * (1.0f - a) + std::clamp(c[q], 0.0f, 255.0f) * a));
        }
}

// ---- 25A Become: per-glyph distance-field morph between two words ------------------------------
class ContourMorph : public GoldEffect
{
public:
    ContourMorph(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        QStringList src = chars(a.text), dst = chars(e.value(QStringLiteral("to")).toString());
        n_ = std::max(src.size(), dst.size());
        while (src.size() < n_) src << QStringLiteral(" ");
        while (dst.size() < n_) dst << QStringLiteral(" ");
        const int gw = int(num(e, "cell", 0)), gh = int(num(e, "cell", 1)), sw = int(num(e, "glyph", 0)), sh = int(num(e, "glyph", 1));
        const int ox = int(num(e, "glyphAt", 0)), oy = int(num(e, "glyphAt", 1));
        const QString fTo = e.value(QStringLiteral("fontTo")).toString();
        const int sTo = int(num(e, "sizeTo"));
        for (int i = 0; i < n_; ++i) {
            M8 pa(gw, gh), pb(gw, gh);
            const auto place = [&](M8 &into, const M8 &m) {
                const M8 s = resize8(m, sw, sh, (sw < m.w || sh < m.h) ? Interp::Area : Interp::Cubic);
                for (int y = 0; y < s.h; ++y)
                    for (int x = 0; x < s.w; ++x)
                        into.at(ox + x, oy + y) = s.at(x, y);
            };
            place(pa, maskPlain(g, a.font, a.size, src.at(i), 20, 4, 5));
            place(pb, maskPlain(g, fTo, sTo, dst.at(i), 20, 4, 5));
            fields_.push_back({signedField(pa, 127, 3), signedField(pb, 127, 3)});
        }
        x0_ = (FW - (gw * n_ + int(num(e, "gap")) * (n_ - 1))) / 2;
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const double ent = phase(e, "in", t), morph = phase(e, "morph", t), leave = 1 - phase(e, "out", t);
        double to[3], col[3];
        rgb(e, "colorTo", to);
        for (int k = 0; k < 3; ++k)
            col[k] = std::trunc((1 - morph) * a_.color[k] + morph * to[k]);
        const int gw = int(num(e, "cell", 0)), gap = int(num(e, "gap"));
        for (int i = 0; i < n_; ++i) {
            const auto &[o, nw] = fields_[size_t(i)];
            M8 m(o.w, o.h);
            for (size_t q = 0; q < m.p.size(); ++q)
                m.p[q] = std::uint8_t(255.0 * clamp01((o.p[q] * (1 - morph) + nw.p[q] * morph + 0.65) / 1.5));
            putMax(f, m, x0_ + i * (gw + gap), num(e, "y"), col, ent * leave);
        }
    }

private:
    GoldActor a_;
    int n_ = 0, x0_ = 0;
    std::vector<std::pair<MF, MF>> fields_;
};

// ---- 25B Unbound: the word on a waving remap grid, tinted left to right ------------------------
class ElasticWarp : public GoldEffect
{
public:
    ElasticWarp(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        M8 m = maskTracked25(g, a.font, a.size, a.text, num(e, "tracking"), 20);
        const int w = int(num(e, "width"));
        m = resize8(m, w, int(roundEven(m.h * double(w) / m.w)), (w < m.w) ? Interp::Area : Interp::Cubic);
        base_ = border(m, int(num(e, "margin", 1)), int(num(e, "margin", 1)), int(num(e, "margin", 0)), int(num(e, "margin", 0)));
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int bw = base_.w, bh = base_.h, bx = (FW - bw) / 2, by = (FH - bh) / 2 + int(num(e, "dy"));
        const double arrival = 1 - phase(e, "settle", t), departure = phase(e, "curl", t);
        const double amp = num(e, "amp", 0) * arrival + num(e, "amp", 1) * departure;
        const double ph = num(e, "phase", 0) + num(e, "phase", 1) * t;
        const double f0 = num(e, "freq", 0), f1 = num(e, "freq", 1), shear = num(e, "shear");
        const double op = phase(e, "in", t) * (1 - phase(e, "out", t));
        if (op <= 0)
            return;
        const double pi = 3.14159265358979323846;
        const MF m = remapLinear(base_, bw, bh, [&](int x, int y, double &sx, double &sy) {
            sy = y - amp * std::sin(2 * pi * (double(x) / bw) * f0 - ph);
            sx = x - (amp * shear) * std::sin(2 * pi * (double(y) / bh) * f1 + ph + 1.0);
        });
        double to[3];
        rgb(e, "colorTo", to);
        for (int y = std::max(0, by); y < std::min(FH, by + bh); ++y)
            for (int x = std::max(0, bx); x < std::min(FW, bx + bw); ++x) {
                const double u = bw > 1 ? double(x - bx) / (bw - 1) : 0.0;
                const double aa = std::round(m.at(x - bx, y - by)) / 255.0 * op;
                std::uint8_t *p = f.px(x, y);
                for (int k = 0; k < 3; ++k)
                    p[k] = sat8(aa * (a_.color[k] + (to[k] - a_.color[k]) * u));
            }
    }

private:
    GoldActor a_;
    M8 base_;
};

// ---- 25C Revolve: letters on an arc swing down onto a straight baseline ------------------------
class ArcToBaseline : public GoldEffect
{
public:
    ArcToBaseline(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const QStringList cs = chars(a.text);
        const int n = int(num(e, "cell")), c0 = n / 2;
        const double trk = num(e, "tracking");
        const double total = g.length(a.font, a.size, a.text) + trk * (cs.size() - 1), left = (FW - total) * 0.5;
        const double pi = 3.14159265358979323846;
        for (int i = 0; i < cs.size(); ++i) {
            const TextBox b = g.bbox(a.font, a.size, cs.at(i));
            Glyph gl;
            gl.m = M8(n, n);
            g.draw(gl.m, a.font, a.size, cs.at(i), c0 - (b.right - b.left) * 0.5 - b.left, c0 - (b.bottom - b.top) * 0.5 - b.top);
            gl.cx = left + g.length(a.font, a.size, joined(cs, i)) + trk * i + (b.left + b.right) * 0.5;
            gl.cy = num(e, "baseline") + (b.top + b.bottom) * 0.5;
            const double ix = i - (cs.size() - 1) * 0.5;
            gl.rx = FW * 0.5 + (gl.cx - FW * 0.5) * num(e, "arcScale");
            gl.ry = gl.cy - num(e, "arcHeight") * std::sin(pi * (i + 0.5) / cs.size()) + ix * num(e, "arcTilt");
            gl.ang = -ix * num(e, "angle");
            glyphs_.push_back(std::move(gl));
        }
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int n = int(num(e, "cell")), c0 = n / 2;
        for (size_t k = 0; k < glyphs_.size(); ++k) {
            const Glyph &gl = glyphs_[k];
            const double i = double(k);
            const double u = outc((t - (num(e, "land", 0) + i * num(e, "land", 2))) / num(e, "land", 1));
            const double ex = smooth((t - (num(e, "rise", 0) + i * num(e, "rise", 2))) / num(e, "rise", 1));
            const double x = gl.rx * (1 - u) + gl.cx * u, y = gl.ry * (1 - u) + gl.cy * u - num(e, "riseBy") * ex;
            const double rot = gl.ang * (1 - u);
            const double aa = smooth((t - (num(e, "in", 0) + i * num(e, "in", 2))) / num(e, "in", 1))
                              * (1 - smooth((t - (num(e, "out", 0) + i * num(e, "out", 2))) / num(e, "out", 1)));
            if (aa <= .001)
                continue;
            if (std::fabs(rot) < .4)
                putMax(f, gl.m, x - c0, y - c0, a_.color, aa);
            else
                putMax(f, warpAffine(gl.m, rotationMatrix(c0, c0, rot), n, n, false), x - c0, y - c0, a_.color, aa);
        }
    }

private:
    struct Glyph
    {
        M8 m;
        double cx = 0, cy = 0, rx = 0, ry = 0, ang = 0;
    };
    GoldActor a_;
    std::vector<Glyph> glyphs_;
};

// ---- 26A Beyond: the word on a perspective plane that swings flat ------------------------------
class PerspectivePlane : public GoldEffect
{
public:
    PerspectivePlane(const GoldText &g, const GoldActor &a) : a_(a)
    {
        m_ = maxWidth(maskPlain(g, a.font, a.size, a.text, 35, 3, 4), num(a.effect, "maxWidth"), true);
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int AH = m_.h, AW = m_.w, AX = (FW - AW) / 2, AY = (FH - AH) / 2 + int(num(e, "dy"));
        const double u = phase(e, "swing", t, outc);
        const double alpha = phase(e, "in", t) * (1 - phase(e, "out", t));
        const double left = AX + (1 - u) * num(e, "corners", 0), right = AX + AW - (1 - u) * num(e, "corners", 1);
        const double tl = AY + (1 - u) * num(e, "corners", 2), bl = AY + AH - (1 - u) * num(e, "corners", 3);
        const double tr = AY - (1 - u) * num(e, "corners", 4), br = AY + AH + (1 - u) * num(e, "corners", 5);
        const double src[8] = {0, 0, double(AW - 1), 0, double(AW - 1), double(AH - 1), 0, double(AH - 1)};
        const double dst[8] = {left, tl, right, tr, right, br, left, bl};
        const M8 warped = warpPerspectiveToFrame(m_, src, dst);
        double from[3], col[3];
        rgb(e, "colorFrom", from);
        for (int k = 0; k < 3; ++k)
            col[k] = roundEven(from[k] * (1 - u) + a_.color[k] * u);
        blendOver(f, warped, 0, 0, col, alpha);
    }

private:
    GoldActor a_;
    M8 m_;
};

// ---- 26B Kinetic: each letter grows from a sliver to full width, then compresses away ----------
class WidthBloom : public GoldEffect
{
public:
    WidthBloom(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const QStringList cs = chars(a.text);
        const double trk = num(e, "tracking");
        for (int i = 0; i < cs.size(); ++i) {
            Glyph gl;
            gl.b = g.bbox(a.font, a.size, cs.at(i));
            const int pad = 14;
            M8 m(std::max(1, gl.b.right - gl.b.left + 2 * pad), std::max(1, gl.b.bottom - gl.b.top + 2 * pad));
            g.draw(m, a.font, a.size, cs.at(i), pad - gl.b.left, pad - gl.b.top);
            gl.m = cropInk(m, 2, 3);
            gl.adv = g.length(a.font, a.size, joined(cs, i)) + trk * i;
            glyphs_.push_back(std::move(gl));
        }
        bx_ = (FW - (g.length(a.font, a.size, a.text) + trk * (cs.size() - 1))) / 2;
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const double exit_ = phase(e, "out", t), pi = 3.14159265358979323846;
        double from[3];
        rgb(e, "colorFrom", from);
        for (size_t i = 0; i < glyphs_.size(); ++i) {
            const Glyph &gl = glyphs_[i];
            const double start = num(e, "grow", 0) + num(e, "grow", 2) * double(i);
            const double p = outc((t - start) / num(e, "grow", 1));
            const double s1 = std::sin(pi * clamp01((t - start) / 1.0));
            const double sx = num(e, "from") + (1 - num(e, "from")) * p + num(e, "overshoot") * s1 * s1 * (1 - p);
            M8 scaled = resize8(gl.m, int(roundEven(gl.m.w * sx)), gl.m.h, Interp::Linear);
            const double cx = bx_ + gl.adv + gl.b.left + (gl.b.right - gl.b.left) / 2.0;
            double x = cx - scaled.w / 2.0;
            const double y = num(e, "baseline") + gl.b.top - 2;
            double col[3];
            for (int k = 0; k < 3; ++k)
                col[k] = std::trunc(from[k] * (1 - p) + a_.color[k] * p);
            const double op = smooth((t - start + num(e, "in", 0)) / num(e, "in", 1)) * (1 - exit_);
            if (exit_ > 0) {
                const double sx2 = std::max(num(e, "squeeze", 1), 1 - num(e, "squeeze", 0) * exit_);
                scaled = resize8(scaled, int(roundEven(scaled.w * sx2)), scaled.h, Interp::Linear);
                x = cx - scaled.w / 2.0;
            }
            blendOver(f, scaled, x, y, col, op);
        }
    }

private:
    struct Glyph
    {
        M8 m;
        TextBox b;
        double adv = 0;
    };
    GoldActor a_;
    std::vector<Glyph> glyphs_;
    double bx_ = 0;
};

// ---- 26C Awaken: each glyph's weight morphs Light → Bold through its distance field ------------
class WeightMorph : public GoldEffect
{
public:
    WeightMorph(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const QString bold = e.value(QStringLiteral("fontTo")).toString();
        const QStringList cs = chars(a.text);
        const double trk = num(e, "tracking");
        const double total = g.length(bold, a.size, a.text) + trk * (cs.size() - 1);
        const double cx0 = std::floor((FW - total) / 2);
        const int height = int(num(e, "cellHeight")), pen = int(num(e, "pen"));
        for (int i = 0; i < cs.size(); ++i) {
            const int width = int(std::ceil(g.length(bold, a.size, cs.at(i)))) + 34;
            MF fl[2];
            for (int k = 0; k < 2; ++k) {
                const QString &font = k ? bold : a.font;
                const TextBox b = g.bbox(font, a.size, cs.at(i));
                M8 c(width, height);
                g.draw(c, font, a.size, cs.at(i), (width - (b.right - b.left)) / 2.0 - b.left, pen);
                fl[k] = signedField(c, 125, 3);
            }
            glyphs_.push_back({fl[0], fl[1], cx0 + g.length(bold, a.size, joined(cs, i)) + trk * i - 17});
        }
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const double p = phase(e, "morph", t, outc), ent = phase(e, "in", t), leave = 1 - phase(e, "out", t);
        double from[3], col[3];
        rgb(e, "colorFrom", from);
        for (int k = 0; k < 3; ++k)
            col[k] = roundEven(from[k] * (1 - p) + a_.color[k] * p);
        for (const Glyph &gl : glyphs_) {
            M8 m(gl.f0.w, gl.f0.h);
            for (size_t q = 0; q < m.p.size(); ++q)
                m.p[q] = std::uint8_t(255.0 * clamp01((gl.f0.p[q] * (1 - p) + gl.f1.p[q] * p + .65) / 1.3));
            blendOver(f, m, gl.x, num(e, "baseline") - num(e, "pen"), col, ent * leave);
        }
    }

private:
    struct Glyph
    {
        MF f0, f1;
        double x = 0;
    };
    GoldActor a_;
    std::vector<Glyph> glyphs_;
};

// ---- 27A Spectra: a prism of colour runs through the letters as they uncover --------------------
class SpectralInk : public GoldEffect
{
public:
    SpectralInk(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking");
        M8 m = trk != 0 ? maskTrackedAsc(g, a.font, a.size, a.text, trk, 24, 2, 3) : maskPlain(g, a.font, a.size, a.text, 24, 2, 3);
        m_ = maxWidth(m, num(e, "maxWidth"), false);
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int AW = m_.w, AX = (FW - AW) / 2, AY = int((FH - m_.h) / 2 + num(e, "dy"));
        const double reveal = phase(e, "reveal", t), departure = 1 - phase(e, "out", t);
        const double travel = phase(e, "travel", t, outc);
        const double center = (num(e, "pass", 0) + num(e, "pass", 1) * travel) * AW, wid = AW * num(e, "pass", 2);
        const double boundary = reveal * (AW + num(e, "edge", 0)) - num(e, "edge", 1), soft = num(e, "edge", 2);
        const QJsonArray bands = e.value(QStringLiteral("bands")).toArray(), tints = e.value(QStringLiteral("tints")).toArray();
        M8 mask(m_.w, m_.h);
        for (int y = 0; y < m_.h; ++y)
            for (int x = 0; x < m_.w; ++x)
                mask.at(x, y) = std::uint8_t(m_.at(x, y) * clamp01((boundary - x) / soft));
        const int y = AY + int(roundEven((1 - outc((t - num(e, "rise", 0)) / num(e, "rise", 1))) * num(e, "rise", 2)));
        blendRgbMap(f, mask, AX, y, [&](int x, float *c) {
            const double d = (x - center) / wid;
            double v[3] = {a_.color[0], a_.color[1], a_.color[2]};
            for (int b = 0; b < bands.size() && b < tints.size(); ++b) {
                const QJsonArray bd = bands.at(b).toArray(), tn = tints.at(b).toArray();
                const double k = std::exp(-std::pow((d + bd.at(0).toDouble()) / bd.at(1).toDouble(), 2.0));
                for (int q = 0; q < 3; ++q)
                    v[q] += k * tn.at(q).toDouble();
            }
            for (int q = 0; q < 3; ++q)
                c[q] = float(v[q]);
        }, departure);
    }

private:
    GoldActor a_;
    M8 m_;
};

// ---- 27B Pivot: each glyph turns about its own centre and settles ------------------------------
class GlyphPivot : public GoldEffect
{
public:
    GlyphPivot(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QStringList cs = chars(a.text);
        const int pad = int(num(a.effect, "glyphPad"));
        for (int i = 0; i < cs.size(); ++i)
            glyphs_.push_back({maskPlain(g, a.font, a.size, cs.at(i), pad, 2, 3), g.length(a.font, a.size, joined(cs, i))});
        bx_ = (FW - g.length(a.font, a.size, a.text)) / 2;
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const double leave = 1 - phase(e, "out", t);
        double from[3];
        rgb(e, "colorFrom", from);
        const int pad = int(num(e, "pad"));
        for (size_t i = 0; i < glyphs_.size(); ++i) {
            const double onset = num(e, "turn", 0) + double(i) * num(e, "turn", 2);
            const double p = outc((t - onset) / num(e, "turn", 1));
            const bool even = i % 2 == 0;
            const double angle = (even ? num(e, "angle", 0) : num(e, "angle", 1)) * (1 - p);
            const double shift = (even ? num(e, "shift", 0) : num(e, "shift", 1)) * (1 - p);
            const M8 &m = glyphs_[i].m;
            std::array<double, 6> M = rotationMatrix(m.w / 2.0, m.h / 2.0, angle);
            M[2] += pad;
            M[5] += pad;
            const M8 warped = warpAffine(m, M, m.w + 2 * pad, m.h + 2 * pad, true);
            double col[3];
            for (int k = 0; k < 3; ++k)
                col[k] = roundEven((1 - p) * from[k] + p * a_.color[k]);
            blendOver(f, warped, bx_ + glyphs_[i].adv + num(e, "nudge", 0) - pad, num(e, "y") + num(e, "nudge", 1) + shift - pad, col,
                      smooth((t - onset) / num(e, "in", 1)) * leave);
        }
    }

private:
    struct Glyph
    {
        M8 m;
        double adv = 0;
    };
    GoldActor a_;
    std::vector<Glyph> glyphs_;
    double bx_ = 0;
};

// ---- 27C Mirror: the word unfolds upward from its reflection -------------------------------------
class MirrorUnfold : public GoldEffect
{
public:
    MirrorUnfold(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking");
        m_ = maxWidth(trk != 0 ? maskTrackedAsc(g, a.font, a.size, a.text, trk, 24, 2, 3) : maskPlain(g, a.font, a.size, a.text, 24, 2, 3),
                      num(e, "maxWidth"), false);
        ref_ = flipV(m_);
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int CH = m_.h, CW = m_.w, CX = (FW - CW) / 2, CY = int(num(e, "y"));
        const double unfold = phase(e, "unfold", t, outc), leave = 1 - phase(e, "out", t);
        const int h = std::max(1, int(roundEven(CH * (num(e, "from") + (1 - num(e, "from")) * unfold))));
        const M8 top = resize8(m_, CW, h, h < CH ? Interp::Area : Interp::Linear);
        const double reflP = std::pow(1 - unfold, 1.6);
        if (reflP > .008) {
            const int rh = std::max(1, int(roundEven(CH * num(e, "reflection") * reflP)));
            M8 refl = resize8(ref_, CW, rh, Interp::Area);
            for (int y = 0; y < rh; ++y) {
                const double v = std::pow(rh > 1 ? 1.0 - double(y) / (rh - 1) : 1.0, 1.5);
                for (int x = 0; x < CW; ++x)
                    refl.at(x, y) = sat8(refl.at(x, y) * v);
            }
            double rc[3];
            rgb(e, "reflectionColor", rc);
            blendOver(f, refl, CX, CY + CH + 9, rc, num(e, "reflectionAlpha") * reflP * leave * phase(e, "rin", t));
        }
        double from[3], col[3];
        rgb(e, "colorFrom", from);
        for (int k = 0; k < 3; ++k)
            col[k] = roundEven((1 - unfold) * from[k] + unfold * a_.color[k]);
        blendOver(f, top, CX, CY + CH - h, col, phase(e, "in", t) * leave);
    }

private:
    GoldActor a_;
    M8 m_, ref_;
};

// ---- 28A Incline: upright → italic through the two masters' exact distance fields ---------------
class SlantMorph : public GoldEffect
{
public:
    SlantMorph(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double mw = num(e, "maxWidth");
        const M8 reg = maxWidth(maskPlain(g, a.font, a.size, a.text, 54, 4, 5), mw, true);
        const M8 ita = maxWidth(maskPlain(g, e.value(QStringLiteral("fontTo")).toString(), a.size, a.text, 54, 4, 5), mw, true);
        const int ah = std::max(reg.h, ita.h) + 48, aw = std::max(reg.w, ita.w) + 60;
        m0_ = centred(reg, aw, ah);
        m1_ = centred(ita, aw, ah);
        s0_ = signedField(m0_, 127, 0);
        s1_ = signedField(m1_, 127, 0);
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int ah = m0_.h, aw = m0_.w, ax = (FW - aw) / 2, ay = (FH - ah) / 2 + int(num(e, "dy"));
        const double pr = phase(e, "morph", t);
        M8 mask = pr <= 0 ? m0_ : (pr >= 1 ? m1_ : M8(aw, ah));
        if (pr > 0 && pr < 1)
            for (size_t q = 0; q < mask.p.size(); ++q)
                mask.p[q] = sat8((s0_.p[q] * (1 - pr) + s1_.p[q] * pr + 1.05) * 255 / 2.1);
        const double entry = phase(e, "in", t);
        if (entry < 1)
            for (int y = 0; y < ah; ++y) {
                const double k = clamp01((entry * (ah + 24) - y) / 18);
                for (int x = 0; x < aw; ++x)
                    mask.at(x, y) = std::uint8_t(mask.at(x, y) * k);
            }
        double to[3], tint[3];
        rgb(e, "colorTo", to);
        for (int k = 0; k < 3; ++k)
            tint[k] = roundEven(a_.color[k] + (to[k] - a_.color[k]) * pr);
        blendOver(f, mask, ax, ay, tint, 1 - phase(e, "out", t));
    }

private:
    GoldActor a_;
    M8 m0_, m1_;
    MF s0_, s1_;
};

// ---- 28B Aperture: the counters open from solid forms, each from its own centre -----------------
// The script found the counters as the odd-depth contours of the ink; here they are the parts of
// the background that do not reach the mask's edge (the same regions for letters).
class CounterRelease : public GoldEffect
{
public:
    CounterRelease(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking");
        const M8 bm = maxWidth(trk != 0 ? maskTrackedAsc(g, a.font, a.size, a.text, trk, 54, 4, 5) : maskPlain(g, a.font, a.size, a.text, 54, 4, 5),
                               num(e, "maxWidth"), true);
        const int w = bm.w, h = bm.h;
        Img<int> label(w, h, -1);
        std::vector<int> stack;
        int next = 0;
        std::vector<std::vector<int>> parts;
        for (int y0 = 0; y0 < h; ++y0)
            for (int x0 = 0; x0 < w; ++x0) {
                if (bm.at(x0, y0) >= 120 || label.at(x0, y0) >= 0)
                    continue;
                std::vector<int> px;
                bool edge = false;
                label.at(x0, y0) = next;
                stack.assign(1, y0 * w + x0);
                while (!stack.empty()) {
                    const int q = stack.back();
                    stack.pop_back();
                    px.push_back(q);
                    const int x = q % w, y = q / w;
                    edge = edge || x == 0 || y == 0 || x == w - 1 || y == h - 1;
                    const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                    for (const auto &d : nb) {
                        const int nx = x + d[0], ny = y + d[1];
                        if (nx < 0 || ny < 0 || nx >= w || ny >= h || bm.at(nx, ny) >= 120 || label.at(nx, ny) >= 0)
                            continue;
                        label.at(nx, ny) = next;
                        stack.push_back(ny * w + nx);
                    }
                }
                ++next;
                if (!edge)
                    parts.push_back(std::move(px));
            }
        // cv2.drawContours fills each outer contour, so every pixel >= 120 and every counter is solid.
        inks_ = bm;
        for (auto &v : inks_.p)
            if (v >= 120)
                v = 255;
        Img<int> seen(w, h, -1);
        for (size_t k = 0; k < parts.size(); ++k) {
            auto &px = parts[k];
            for (int q : px)
                inks_.p[size_t(q)] = 255;
            // A filled hole contour runs through the ink pixels around the counter (4-adjacent): include them.
            const size_t n0 = px.size();
            for (size_t i = 0; i < n0; ++i) {
                const int x = px[i] % w, y = px[i] / w;
                const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                for (const auto &d : nb) {
                    const int nx = x + d[0], ny = y + d[1];
                    if (nx < 0 || ny < 0 || nx >= w || ny >= h || bm.at(nx, ny) < 120 || seen.at(nx, ny) == int(k))
                        continue;
                    seen.at(nx, ny) = int(k);
                    px.push_back(ny * w + nx);
                }
            }
        }
        for (const auto &px : parts) {
            if (px.size() < 25)
                continue;
            Hole hole;
            double sx = 0, sy = 0;
            for (int q : px) {
                sx += q % w;
                sy += q / w;
            }
            hole.cx = sx / px.size();
            hole.cy = sy / px.size();
            double r = 0;
            for (int q : px)
                r = std::max(r, std::hypot(q % w - hole.cx, q / w - hole.cy));
            hole.r = std::max(r, .001);
            hole.px = px;
            holes_.push_back(std::move(hole));
        }
        std::sort(holes_.begin(), holes_.end(), [](const Hole &p, const Hole &q) { return p.cx < q.cx; });
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int w = inks_.w;
        M8 mask = inks_;
        for (size_t j = 0; j < holes_.size(); ++j) {
            const Hole &hole = holes_[j];
            const double prog = outc((t - (num(e, "open", 0) + num(e, "open", 2) * double(j))) / num(e, "open", 1));
            for (int q : hole.px) {
                const double d = std::hypot(q % w - hole.cx, q / w - hole.cy) / hole.r;
                mask.p[size_t(q)] = std::uint8_t(mask.p[size_t(q)] * (1 - clamp01((prog - d) / .20)));
            }
        }
        blendOver(f, mask, (FW - mask.w) / 2, (FH - mask.h) / 2, a_.color, phase(e, "in", t) * (1 - phase(e, "out", t)));
    }

private:
    struct Hole
    {
        double cx = 0, cy = 0, r = 1;
        std::vector<int> px;
    };
    GoldActor a_;
    M8 inks_;
    std::vector<Hole> holes_;
};

// ---- 28C Phase: an ivory swath crosses the word, letters black inside it --------------------------
class InverseTransit : public GoldEffect
{
public:
    InverseTransit(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking");
        m_ = maxWidth(trk != 0 ? maskTrackedAsc(g, a.font, a.size, a.text, trk, 54, 4, 5) : maskPlain(g, a.font, a.size, a.text, 54, 4, 5),
                      num(e, "maxWidth"), true);
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        if (t < num(e, "visible", 0) || t >= num(e, "visible", 1))
            return;
        const int ch = m_.h, cw = m_.w, cx = (FW - cw) / 2, cy = (FH - ch) / 2;
        const bool exiting = t > num(e, "exit", 0);
        const double prog = exiting ? phase(e, "exit", t, outc) : phase(e, "enter", t, outc);
        const double front = exiting ? cw + 315 - prog * (cw + 650) : -310 + prog * (cw + 590);
        const double left = front - num(e, "slab");
        for (int y = std::max(0, cy - 13); y < std::min(FH, cy + ch + 13); ++y)
            for (int x = std::max(0, cx); x < std::min(FW, cx + cw); ++x) {
                const double c = x - cx;
                const float band = float(clamp01((c - left) / 6) * clamp01((front - c) / 6));
                const float unc = float(clamp01((front - c) / 20));
                std::uint8_t *p = f.px(x, y);
                for (int k = 0; k < 3; ++k)
                    p[k] = std::uint8_t(band * float(a_.color[k]));
                const int my = y - cy;
                if (my < 0 || my >= ch)
                    continue;
                const float m = m_.at(x - cx, my) / 255.0f;
                for (int k = 0; k < 3; ++k)
                    p[k] = sat8(double(float(p[k]) * (1 - m) + m * ((1 - band) * unc * float(a_.color[k]))));
            }
    }

private:
    GoldActor a_;
    M8 m_;
};

// ---- 29A Gravity: an extruded cast of the word retracts into a crisp front face -------------------
class DepthCast : public GoldEffect
{
public:
    DepthCast(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking");
        m_ = maxWidth(trk != 0 ? maskTrackedAsc(g, a.font, a.size, a.text, trk, 35, 4, 5) : maskPlain(g, a.font, a.size, a.text, 35, 4, 5),
                      num(e, "maxWidth"), true);
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        if (t < num(e, "visible", 0) || t >= num(e, "visible", 1))
            return;
        const int ah = m_.h, aw = m_.w, ax = (FW - aw) / 2, ay = (FH - ah) / 2 + int(num(e, "dy"));
        const double inc = phase(e, "in", t), retract = phase(e, "retract", t), expand = phase(e, "expand", t);
        const double depth = (1 - retract) + expand * num(e, "expand", 2);
        const int n = int(num(e, "layers"));
        double cast[3];
        rgb(e, "castColor", cast);
        if (depth > .005)
            for (int j = n; j >= 1; --j) {
                const double u = j / double(n);
                const int dx = -int(roundEven(u * num(e, "extrude", 0) * depth)), dy = int(roundEven(u * num(e, "extrude", 1) * depth));
                double col[3];
                for (int k = 0; k < 3; ++k)
                    col[k] = std::trunc(cast[k] + num(e, "castLift") * (1 - u));
                blendOver(f, m_, ax + dx, ay + dy, col, inc * (1 - phase(e, "castOut", t)));
            }
        const double front = inc * (1 - phase(e, "out", t));
        if (front > 0) {
            const double p = phase(e, "light", t);
            M8 lit(aw, ah);
            for (int x = 0; x < aw; ++x) {
                const double wave = clamp01((p * (aw + 300) - x + 25) / 160);
                const float tint = float(std::max(.45, std::min(1.0, .46 + .54 * wave)));
                for (int y = 0; y < ah; ++y)
                    lit.at(x, y) = std::uint8_t(float(m_.at(x, y)) * tint);
            }
            blendOver(f, lit, ax, ay, a_.color, front);
        }
    }

private:
    GoldActor a_;
    M8 m_;
};

// ---- 29B Unfold: the word opens like a pleated sheet -----------------------------------------------
class PleatUnfold : public GoldEffect
{
public:
    PleatUnfold(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking");
        m_ = maxWidth(trk != 0 ? maskTrackedAsc(g, a.font, a.size, a.text, trk, 35, 4, 5) : maskPlain(g, a.font, a.size, a.text, 35, 4, 5),
                      num(e, "maxWidth"), true);
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        if (t < num(e, "visible", 0) || t > num(e, "visible", 1))
            return;
        const int bh = m_.h, bw = m_.w, by = (FH - bh) / 2;
        const double u = phase(e, "open", t, outc), foldout = phase(e, "close", t);
        const double flat = u * (1 - foldout), scale = num(e, "from") + (1 - num(e, "from")) * flat;
        const int nw = std::max(8, int(roundEven(bw * scale)));
        const MF sh = resizeAxisLinear(m_, nw, bh);
        const double pi = 3.14159265358979323846;
        const MF warped = remapLinear(sh, nw, bh, [&](int x, int y, double &sx, double &sy) {
            const double bend = std::sin(double(x) / std::max(1, nw - 1) * 4 * pi) * (1 - flat);
            sx = x + 9 * bend;
            sy = y + 18 * bend;
        });
        M8 out(nw, bh);
        for (int x = 0; x < nw; ++x) {
            const double ph = double(x) / std::max(1, nw - 1) * 4 * pi;
            const double light = (.77 + .23 * std::cos(ph + pi * .22)) * (1 - flat) + flat;
            for (int y = 0; y < bh; ++y)
                out.at(x, y) = sat8(warped.at(x, y) * light);
        }
        blendOver(f, out, (FW - nw) / 2, by, a_.color, phase(e, "in", t) * (1 - phase(e, "out", t)));
    }

private:
    GoldActor a_;
    M8 m_;
};

// ---- 29C Coalesce: a seeded dust cloud condenses onto the word, then the fill takes over ----------
class ParticleCoalesce : public GoldEffect
{
public:
    ParticleCoalesce(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking");
        m_ = maxWidth(trk != 0 ? maskTrackedAsc(g, a.font, a.size, a.text, trk, 35, 4, 5) : maskPlain(g, a.font, a.size, a.text, 35, 4, 5),
                      num(e, "maxWidth"), true);
        std::vector<std::pair<int, int>> valid; // np.column_stack(np.where(m > 180)): row-major
        for (int y = 0; y < m_.h; ++y)
            for (int x = 0; x < m_.w; ++x)
                if (m_.at(x, y) > 180)
                    valid.push_back({y, x});
        const int n = int(num(e, "count"));
        if (valid.empty() || n <= 0)
            return;
        NumpyRng rng(std::uint64_t(num(e, "seed")));
        const std::vector<std::int64_t> pick = rng.integers(0, std::int64_t(valid.size()), n);
        const std::vector<double> ang = rng.uniform(0, 2 * 3.14159265358979323846, n);
        const std::vector<double> rad = rng.uniform(num(e, "radius", 0), num(e, "radius", 1), n);
        const std::vector<std::int64_t> rr = rng.integers(0, 4, n);
        const std::vector<std::int64_t> shade = rng.integers(std::int64_t(num(e, "shade", 0)), std::int64_t(num(e, "shade", 1)), n);
        const float squash = float(num(e, "squash"));
        for (int k = 0; k < n; ++k) {
            Dot d;
            d.ey = float(valid[size_t(pick[size_t(k)])].first);
            d.ex = float(valid[size_t(pick[size_t(k)])].second);
            const float an = float(ang[size_t(k)]), ra = float(rad[size_t(k)]);
            d.sx = d.ex + std::cos(an) * ra;
            d.sy = d.ey + std::sin(an) * ra * squash;
            d.r = rr[size_t(k)] == 3 ? 2 : 1; // rng.choice([1, 1, 1, 2])
            d.shade = int(shade[size_t(k)]);
            dots_.push_back(d);
        }
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int ch = m_.h, cw = m_.w, cx = (FW - cw) / 2, cy = (FH - ch) / 2 + int(num(e, "dy"));
        if (t < .20)
            return;
        double drift, dot, fill;
        if (t <= 2.70) {
            drift = 1 - smooth((t - .45) / 1.82);
            dot = (1 - smooth((t - 1.70) / .67)) * smooth((t - .20) / .24);
            fill = smooth((t - 1.54) / .95);
        } else if (t < 4.43) {
            drift = 0;
            dot = 0;
            fill = 1;
        } else {
            drift = smooth((t - 4.43) / .88);
            dot = smooth((t - 4.48) / .25) * (1 - smooth((t - 5.18) / .54));
            fill = 1 - smooth((t - 4.44) / .36);
        }
        if (dot > .004)
            for (const Dot &d : dots_) {
                const int x = int(std::nearbyint(d.ex + float(drift) * (d.sx - d.ex))) + cx;
                const int y = int(std::nearbyint(d.ey + float(drift) * (d.sy - d.ey))) + cy;
                if (x < 0 || x >= FW || y < 0 || y >= FH)
                    continue;
                const int v = int(d.shade * dot);
                // cv2.circle(LINE_AA) of radius 1 or 2, as cv2 draws it (coverage / 200)
                static const int r1[][3] = {{-1, -1, 54}, {0, -1, 200}, {1, -1, 58}, {-2, 0, 54}, {-1, 0, 200}, {0, 0, 200}, {1, 0, 200},
                                            {2, 0, 56}, {-2, 1, 4}, {-1, 1, 58}, {0, 1, 200}, {1, 1, 60}, {2, 1, 4}, {-1, 2, 4}, {1, 2, 4}};
                static const int r2[][3] = {{-1, -2, 54}, {0, -2, 200}, {1, -2, 58}, {-2, -1, 56}, {-1, -1, 200}, {0, -1, 200}, {1, -1, 200},
                                            {2, -1, 56}, {-3, 0, 54}, {-2, 0, 200}, {-1, 0, 200}, {0, 0, 200}, {1, 0, 200}, {2, 0, 200},
                                            {3, 0, 56}, {-3, 1, 4}, {-2, 1, 62}, {-1, 1, 200}, {0, 1, 200}, {1, 1, 200}, {2, 1, 63},
                                            {3, 1, 4}, {-1, 2, 58}, {0, 2, 200}, {1, 2, 58}, {-1, 3, 4}, {1, 3, 4}};
                const auto stamp = [&](const int (*st)[3], size_t n) {
                    for (size_t q = 0; q < n; ++q) {
                        const int i = x + st[q][0], j = y + st[q][1];
                        if (i < 0 || j < 0 || i >= FW || j >= FH)
                            continue;
                        const double cov = st[q][2] / 200.0;
                        std::uint8_t *p = f.px(i, j);
                        for (int k = 0; k < 3; ++k)
                            p[k] = sat8r(p[k] * (1 - cov) + v * cov);
                    }
                };
                if (d.r == 2)
                    stamp(r2, sizeof(r2) / sizeof(r2[0]));
                else
                    stamp(r1, sizeof(r1) / sizeof(r1[0]));
            }
        if (fill > 0)
            blendOver(f, m_, cx, cy, a_.color, fill);
    }

private:
    struct Dot
    {
        float ex = 0, ey = 0, sx = 0, sy = 0;
        int r = 1, shade = 0;
    };
    GoldActor a_;
    M8 m_;
    std::vector<Dot> dots_;
};

// ---- Stage30: a title region (x, y, w, h) composed as alpha layers on black ----------------------
struct Region30
{
    int x = 130, y = 315, w = 1660, h = 450;
};
inline Region30 region30(const QJsonObject &e)
{
    return {int(num(e, "region", 0)), int(num(e, "region", 1)), int(num(e, "region", 2)), int(num(e, "region", 3))};
}
// mask_text(): each character drawn alone across the region, pens at round(x), the word centred.
inline std::vector<M8> glyphs30(const GoldText &g, const GoldActor &a, const Region30 &r, double trk, double baseline, double *startX = nullptr)
{
    const QStringList cs = chars(a.text);
    const double xx = (r.w - (g.length(a.font, a.size, a.text) + std::max(0, int(cs.size()) - 1) * trk)) / 2;
    if (startX)
        *startX = xx;
    std::vector<M8> out;
    for (int i = 0; i < cs.size(); ++i) {
        M8 m(r.w, r.h);
        g.draw(m, a.font, a.size, cs.at(i), roundEven(xx + g.length(a.font, a.size, joined(cs, i)) + trk * i), baseline);
        out.push_back(std::move(m));
    }
    return out;
}
inline void comp30(Frame &f, const Region30 &r, const std::vector<std::pair<const MF *, const double *>> &layers)
{
    for (int y = 0; y < r.h; ++y)
        for (int x = 0; x < r.w; ++x) {
            float c[3] = {0, 0, 0};
            for (const auto &[a, col] : layers) {
                const float v = std::clamp(a->at(x, y), 0.0f, 1.0f);
                for (int k = 0; k < 3; ++k)
                    c[k] = c[k] * (1 - v) + v * float(col[k]);
            }
            if (r.x + x < 0 || r.y + y < 0 || r.x + x >= FW || r.y + y >= FH)
                continue;
            std::uint8_t *p = f.px(r.x + x, r.y + y);
            for (int k = 0; k < 3; ++k)
                p[k] = sat8(double(c[k] + 0.5f));
        }
}

// 30A Etched: each glyph's outline traced in copper, then the fill comes in. The script drew the
// first part of each cv2 contour; here the contours are the glyph's boundary pixels, chained.
class ContourTrace : public GoldEffect
{
public:
    ContourTrace(const GoldText &g, const GoldActor &a) : a_(a), r_(region30(a.effect))
    {
        const QJsonObject &e = a.effect;
        glyphs_ = glyphs30(g, a, r_, num(e, "tracking"), num(e, "baseline"));
        for (const M8 &gm : glyphs_)
            chains_.push_back(chainsOf(gm));
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        MF fill(r_.w, r_.h), lines(r_.w, r_.h);
        const double exitA = phase(e, "out", t);
        for (size_t i = 0; i < glyphs_.size(); ++i) {
            const double start = num(e, "trace", 0) + num(e, "trace", 2) * double(i);
            const double outline = quint((t - start) / num(e, "trace", 1));
            const double fillP = smooth((t - start - num(e, "fill", 0)) / num(e, "fill", 1));
            if (fillP > 0)
                for (size_t q = 0; q < fill.p.size(); ++q)
                    fill.p[q] = std::max(fill.p[q], float(glyphs_[i].p[q] / 255.0 * fillP * (1 - exitA)));
            if (outline > 0 && fillP < .98)
                for (const auto &c : chains_[i]) {
                    const int count = std::max(1, int(c.size() * outline));
                    for (int k = 1; k < count; ++k)
                        strokeAA(lines, c[size_t(k - 1)].first, c[size_t(k - 1)].second, c[size_t(k)].first, c[size_t(k)].second);
                }
        }
        const float k = float((1 - phase(e, "tracer", t)) * (1 - exitA));
        for (float &v : lines.p)
            v *= k;
        double tc[3];
        rgb(e, "tracerColor", tc);
        comp30(f, r_, {{&fill, a_.color}, {&lines, tc}});
    }

private:
    using Chain = std::vector<std::pair<int, int>>;
    // The glyph's borders as cv2.findContours lists them: each outer border from its top-left
    // pixel, down the left side first (Moore tracing, counter-clockwise on screen), and each
    // counter's border the same way. Longest first.
    static std::vector<Chain> chainsOf(const M8 &gm)
    {
        const int w = gm.w, h = gm.h;
        const auto inside = [&](const Img<std::uint8_t> &m, int x, int y) { return x >= 0 && y >= 0 && x < w && y < h && m.at(x, y); };
        Img<std::uint8_t> fg(w, h), seen(w, h);
        for (size_t q = 0; q < fg.p.size(); ++q)
            fg.p[q] = gm.p[q] > 50;
        // the counters: background not reaching the edge (4-connected)
        Img<std::uint8_t> hole(w, h, 1);
        std::vector<int> stack;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                if ((x == 0 || y == 0 || x == w - 1 || y == h - 1) && !fg.at(x, y) && hole.at(x, y)) {
                    hole.at(x, y) = 0;
                    stack.push_back(y * w + x);
                    while (!stack.empty()) {
                        const int q = stack.back();
                        stack.pop_back();
                        const int qx = q % w, qy = q / w;
                        const int nb[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
                        for (const auto &d : nb) {
                            const int nx = qx + d[0], ny = qy + d[1];
                            if (nx >= 0 && ny >= 0 && nx < w && ny < h && !fg.at(nx, ny) && hole.at(nx, ny)) {
                                hole.at(nx, ny) = 0;
                                stack.push_back(ny * w + nx);
                            }
                        }
                    }
                }
        for (size_t q = 0; q < hole.p.size(); ++q)
            hole.p[q] = hole.p[q] && !fg.p[q];
        // counter-clockwise on screen (y down): E, NE, N, NW, W, SW, S, SE
        static const int dirs[8][2] = {{1, 0}, {1, -1}, {0, -1}, {-1, -1}, {-1, 0}, {-1, 1}, {0, 1}, {1, 1}};
        std::vector<Chain> out;
        for (const Img<std::uint8_t> *m : {&fg, &hole}) {
            for (int y0 = 0; y0 < h; ++y0)
                for (int x0 = 0; x0 < w; ++x0) {
                    if (!m->at(x0, y0) || seen.at(x0, y0) || inside(*m, x0, y0 - 1))
                        continue; // a border pixel with nothing above it starts a border
                    Chain c;
                    int x = x0, y = y0, from = 2; // came looking from the north
                    const size_t limit = size_t(w) * size_t(h) * 4;
                    for (size_t steps = 0; steps < limit; ++steps) {
                        c.push_back({x, y});
                        seen.at(x, y) = 1;
                        int k = 0;
                        for (; k < 8; ++k) {
                            const int d = (from + 1 + k) % 8;
                            if (inside(*m, x + dirs[d][0], y + dirs[d][1])) {
                                x += dirs[d][0];
                                y += dirs[d][1];
                                from = (d + 4) % 8;
                                break;
                            }
                        }
                        if (k == 8 || (x == x0 && y == y0))
                            break;
                    }
                    if (c.size() > 1)
                        out.push_back(std::move(c));
                }
        }
        std::sort(out.begin(), out.end(), [](const Chain &p, const Chain &q) { return p.size() > q.size(); });
        return out;
    }
    GoldActor a_;
    Region30 r_;
    std::vector<M8> glyphs_;
    std::vector<std::vector<Chain>> chains_;
};

// 30B Recode: each letter rolls through alternates like a character drum.
class CharacterReel : public GoldEffect
{
public:
    CharacterReel(const GoldText &g, const GoldActor &a) : a_(a), r_(region30(a.effect))
    {
        const QJsonObject &e = a.effect;
        const double trk = num(e, "tracking"), base = num(e, "baseline");
        double xx = 0;
        finals_ = glyphs30(g, a, r_, trk, base, &xx);
        const QStringList cs = chars(a.text);
        const QJsonArray reels = e.value(QStringLiteral("reels")).toArray();
        for (int i = 0; i < cs.size(); ++i) {
            QStringList alts;
            if (i < reels.size())
                for (const QJsonValue &v : reels.at(i).toArray())
                    alts << v.toString();
            else
                alts << cs.at(i) << cs.at(i) << cs.at(i);
            const double x = xx + g.length(a.font, a.size, joined(cs, i)) + trk * i;
            std::vector<M8> row;
            for (const QString &c : alts) {
                M8 m(r_.w, r_.h);
                g.draw(m, a.font, a.size, c, roundEven(x), base);
                row.push_back(std::move(m));
            }
            reels_.push_back(std::move(row));
        }
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        MF layer(r_.w, r_.h);
        const double exitP = phase(e, "out", t), drum = num(e, "drum");
        for (size_t i = 0; i < finals_.size(); ++i) {
            const double start = num(e, "roll", 0) + double(i) * num(e, "roll", 2);
            const double p = clamp01((t - start) / num(e, "roll", 1));
            if (p <= 0)
                continue;
            const double ph = p * 3;
            const int step = std::min(int(ph), 2);
            const double frac = p < 1 ? ph - step : 1.0;
            const double change = smooth((frac - .16) / .7);
            std::vector<const M8 *> seq;
            for (const M8 &m : reels_[i])
                seq.push_back(&m);
            seq.push_back(&finals_[i]);
            if (size_t(step + 1) >= seq.size())
                continue;
            const int dOld = int(roundEven(-drum * change)), dNew = int(roundEven(drum * (1 - change)));
            const double inten = smooth((t - start) / .14) * (1 - exitP);
            for (int y = 0; y < r_.h; ++y)
                for (int x = 0; x < r_.w; ++x) {
                    const int yo = y - dOld, yn = y - dNew;
                    const double o = (yo >= 0 && yo < r_.h) ? seq[size_t(step)]->at(x, yo) : 0;
                    const double n = (yn >= 0 && yn < r_.h) ? seq[size_t(step + 1)]->at(x, yn) : 0;
                    const float v = float((o * (1 - change) + n * change) / 255.0 * inten);
                    layer.at(x, y) = std::max(layer.at(x, y), v);
                }
        }
        comp30(f, r_, {{&layer, a_.color}});
    }

private:
    GoldActor a_;
    Region30 r_;
    std::vector<M8> finals_;
    std::vector<std::vector<M8>> reels_;
};

// 30C Contour: iso-distance rings collapse onto the glyph edges, then the ink fills.
class IsolineCollapse : public GoldEffect
{
public:
    IsolineCollapse(const GoldText &g, const GoldActor &a) : a_(a), r_(region30(a.effect))
    {
        const QJsonObject &e = a.effect;
        mask_ = M8(r_.w, r_.h);
        for (const M8 &gm : glyphs30(g, a, r_, num(e, "tracking"), num(e, "baseline")))
            for (size_t q = 0; q < mask_.p.size(); ++q)
                mask_.p[q] = std::max(mask_.p[q], gm.p[q]);
        M8 in(r_.w, r_.h), out(r_.w, r_.h);
        for (size_t q = 0; q < in.p.size(); ++q) {
            in.p[q] = mask_.p[q] > 45 ? 1 : 0;
            out.p[q] = 1 - in.p[q];
        }
        const MF o = chamfer(out, 5), i = chamfer(in, 5);
        signed_ = MF(r_.w, r_.h);
        for (size_t q = 0; q < signed_.p.size(); ++q)
            signed_.p[q] = o.p[q] - i.p[q];
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const double entry = smooth((t - .13) / 1.45), leave = smooth((t - 4.48) / 1.28);
        const double solid = smooth((t - 1.02) / .83) * (1 - smooth((t - 4.56) / .55));
        MF fill(r_.w, r_.h), rings(r_.w, r_.h);
        for (size_t q = 0; q < fill.p.size(); ++q)
            fill.p[q] = float(mask_.p[q] / 255.0 * solid);
        const double rin = smooth((t - .11) / .16) * (1 - smooth((t - 1.48) / .58));
        const double rout = smooth((t - 4.52) / .24) * (1 - smooth((t - 5.45) / .36));
        if (rin > .001 || rout > .001)
            for (const QJsonValue &rv : e.value(QStringLiteral("rings")).toArray()) {
                const double off = rv.toArray().at(0).toDouble(), wgt = rv.toArray().at(1).toDouble();
                const double ri = 1.4 + off * (1 - entry), ro = 1.4 + off * leave;
                for (size_t q = 0; q < rings.p.size(); ++q) {
                    const double s = signed_.p[q];
                    if (rin > .001)
                        rings.p[q] = std::max(rings.p[q], float(std::exp(-.5 * std::pow((s - ri) / 1.35, 2.0)) * wgt * rin));
                    if (rout > .001)
                        rings.p[q] = std::max(rings.p[q], float(std::exp(-.5 * std::pow((s - ro) / 1.35, 2.0)) * wgt * rout));
                }
            }
        double rc[3];
        rgb(e, "ringColor", rc);
        comp30(f, r_, {{&rings, rc}, {&fill, a_.color}});
    }

private:
    GoldActor a_;
    Region30 r_;
    M8 mask_;
    MF signed_;
};

// =================================================================================================
// The later stages (render_stage32.py … render_stage49.py). Their numbers are fixed in the code as
// the scripts had them; word, font, size, tracking and colours come from the actor.
// =================================================================================================
inline double cycNum(const QJsonObject &e, const char *k, int i)
{
    const QJsonArray a = e.value(QLatin1String(k)).toArray();
    return a.isEmpty() ? 0.0 : a.at(i % a.size()).toDouble();
}
// PIL Image.getbbox()-based crop: [x0 − before, x1 + after) where x1 is one past the ink.
inline M8 cropBox(const M8 &m, int before, int afterExclusive) { return cropInk(m, before, afterExclusive + 1); }

// 32: each character drawn alone on the frame (anchor 'la' at (round(x), round(H/2 − 0.43 size))),
// cropped 90/100 px around its ink.
struct Glyph32
{
    M8 g;
    int x0 = 0, y0 = 0, j = 0;
};
inline std::vector<Glyph32> glyphs32(const GoldText &gt, const GoldActor &a, double trk)
{
    const QStringList cs = chars(a.text);
    double total = 0;
    for (const QString &c : cs)
        total += gt.length(a.font, a.size, c);
    total += (cs.size() - 1) * trk;
    double x = (FW - total) / 2;
    const int asc = gt.ascender(a.font, a.size);
    std::vector<Glyph32> out;
    for (int j = 0; j < cs.size(); ++j) {
        M8 im(FW, FH);
        gt.draw(im, a.font, a.size, cs.at(j), roundEven(x), roundEven(FH / 2.0 - a.size * .43) + asc);
        int x0 = FW, y0 = FH, x1 = -1, y1 = -1;
        for (int y = 0; y < FH; ++y)
            for (int xx = 0; xx < FW; ++xx)
                if (im.at(xx, y)) {
                    x0 = std::min(x0, xx); x1 = std::max(x1, xx);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
        if (x1 >= 0) {
            Glyph32 g;
            g.x0 = std::max(x0 - 90, 0);
            g.y0 = std::max(y0 - 100, 0);
            const int ex = std::min(x1 + 91, FW), ey = std::min(y1 + 101, FH);
            g.g = M8(ex - g.x0, ey - g.y0);
            for (int y = 0; y < g.g.h; ++y)
                for (int xx = 0; xx < g.g.w; ++xx)
                    g.g.at(xx, y) = im.at(g.x0 + xx, g.y0 + y);
            g.j = j;
            out.push_back(std::move(g));
        }
        x += gt.length(a.font, a.size, cs.at(j)) + trk;
    }
    return out;
}
inline void maxInto(Frame &f, int x0, int y0, int w, int h, const std::function<void(int, int, float *)> &rgb)
{
    for (int y = std::max(0, y0); y < std::min(FH, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(FW, x0 + w); ++x) {
            float c[3];
            rgb(x - x0, y - y0, c);
            std::uint8_t *p = f.px(x, y);
            for (int k = 0; k < 3; ++k)
                p[k] = std::max(p[k], sat8(double(c[k])));
        }
}
inline void setInto(Frame &f, int x0, int y0, int w, int h, const std::function<void(int, int, float *)> &rgb)
{
    for (int y = std::max(0, y0); y < std::min(FH, y0 + h); ++y)
        for (int x = std::max(0, x0); x < std::min(FW, x0 + w); ++x) {
            float c[3];
            rgb(x - x0, y - y0, c);
            std::uint8_t *p = f.px(x, y);
            for (int k = 0; k < 3; ++k)
                p[k] = sat8(double(c[k]));
        }
}
inline double ssm(double t) { return smooth(t); }

class SplitRegister : public GoldEffect
{
public:
    SplitRegister(const GoldText &g, const GoldActor &a) : a_(a), gl_(glyphs32(g, a, num(a.effect, "tracking"))) {}
    void draw(Frame &f, double t) const override
    {
        const double fade = ssm((t - .06) / .35) * (1 - ssm((t - 5.27) / .52));
        for (const Glyph32 &gl : gl_) {
            const int hh = gl.g.h, ww = gl.g.w;
            const double lock = ssm((t - .21 - gl.j * .083) / 1.15), depart = ssm((t - 4.37 - gl.j * .043) / .86);
            const double aa = 1 - lock + depart, d = 51 * aa, dx = 21 * aa;
            const auto half = [&](bool upper, double sx, double sy) {
                // remap of g·mask: the half kept is where (y < line) in source coordinates
                const double x0 = sx, y0 = sy;
                const int ix = int(std::floor(x0)), iy = int(std::floor(y0));
                const double fx = x0 - ix, fy = y0 - iy;
                double v = 0;
                for (int j = 0; j < 2; ++j)
                    for (int i = 0; i < 2; ++i) {
                        const int px = ix + i, py = iy + j;
                        if (px < 0 || py < 0 || px >= ww || py >= hh)
                            continue;
                        const bool up = py < hh * .50 + (px - ww * .5) * .17;
                        if (up != upper)
                            continue;
                        v += gl.g.at(px, py) * (i ? fx : 1 - fx) * (j ? fy : 1 - fy);
                    }
                return v;
            };
            maxInto(f, gl.x0, gl.y0, ww, hh, [&](int x, int y, float *c) {
                const double u = half(true, x - dx, y + d), l = half(false, x + dx, y - d);
                const double al = std::max(u, l) * fade;
                for (int k = 0; k < 3; ++k)
                    c[k] = float(std::trunc(al / 255 * a_.color[k]));
            });
        }
    }

private:
    GoldActor a_;
    std::vector<Glyph32> gl_;
};

class GlyphTorsion : public GoldEffect
{
public:
    GlyphTorsion(const GoldText &g, const GoldActor &a) : a_(a), gl_(glyphs32(g, a, num(a.effect, "tracking"))) {}
    void draw(Frame &f, double t) const override
    {
        const double fade = ssm((t - .04) / .38) * (1 - ssm((t - 5.28) / .5)), pi = 3.14159265358979323846;
        for (const Glyph32 &gl : gl_) {
            const int hh = gl.g.h, ww = gl.g.w;
            const double cx = ww / 2., cy = hh / 2.;
            const double aa = (1 - ssm((t - .17 - gl.j * .075) / 1.36)) + ssm((t - 4.45 - gl.j * .038) / .82);
            maxInto(f, gl.x0, gl.y0, ww, hh, [&](int x, int y, float *c) {
                const double nrm = (y - cy) / std::max(hh, 1);
                const double warp = aa * (36 * std::sin(nrm * pi * 2.05 + gl.j * .34) * (0.68 + 0.32 * std::cos((x - cx) / std::max(ww, 1) * pi)));
                const double mx = (x - cx - warp) / (1 - 0.18 * aa) + cx, my = y - aa * 14 * std::sin((x - cx) / std::max(ww, 1) * pi * 1.3);
                const double al = std::round(bilinear(gl.g, mx, my)) * fade / 255.;
                const double ridge = clamp01((std::cos(nrm * pi * 2.05 + gl.j * .34) + 1) * .5);
                const double add[3] = {10, 7, 4};
                for (int k = 0; k < 3; ++k)
                    c[k] = float(std::trunc(al * std::trunc(a_.color[k] + add[k] * ridge * aa)));
            });
        }
    }

private:
    GoldActor a_;
    std::vector<Glyph32> gl_;
};

// 33A/34A/35: the word with its pens at round(x), the ink cropped by PIL's getbbox.
inline M8 wordMask(const GoldText &g, const GoldActor &a, double trk, int canvasW, int canvasH, double startX, double baseline, int before,
                   int afterExclusive)
{
    const QStringList cs = chars(a.text);
    M8 im(canvasW, canvasH);
    double x = startX;
    for (const QString &c : cs) {
        g.draw(im, a.font, a.size, c, roundEven(x), baseline);
        x += g.length(a.font, a.size, c) + trk;
    }
    return cropBox(im, before, afterExclusive);
}
inline double sumAdv(const GoldText &g, const GoldActor &a, double trk)
{
    const QStringList cs = chars(a.text);
    double s = 0;
    for (const QString &c : cs)
        s += g.length(a.font, a.size, c);
    return s + trk * (cs.size() - 1);
}

class ImprintPress : public GoldEffect
{
public:
    ImprintPress(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const double trk = num(a.effect, "tracking");
        m_ = wordMask(g, a, trk, int(std::ceil(sumAdv(g, a, trk))) + 200, a.size + 250, 100, a.size + 60, 22, 22);
        M8 bin(m_.w, m_.h);
        for (size_t q = 0; q < bin.p.size(); ++q)
            bin.p[q] = m_.p[q] > 100;
        dist_ = chamfer(bin, 5);
        MF gx, gy;
        gradient(gaussian(dist_, 2.8), gx, gy);
        rim_ = MF(m_.w, m_.h);
        shade_ = MF(m_.w, m_.h);
        for (size_t q = 0; q < rim_.p.size(); ++q) {
            const double len = std::max(.001, std::sqrt(double(gx.p[q]) * gx.p[q] + double(gy.p[q]) * gy.p[q]));
            rim_.p[q] = float(clamp01((14 - dist_.p[q]) / 13.0) * clamp01(dist_.p[q] / 1.5));
            shade_.p[q] = float(clamp01(gx.p[q] / len * .68 + gy.p[q] / len * (-.72) + .58));
        }
    }
    void draw(Frame &f, double t) const override
    {
        const int w = m_.w, h = m_.h, x0 = (FW - w) / 2, y0 = (FH - h) / 2;
        const double imp = ssm((t - .12) / .64) * (1 - ssm((t - 5.10) / .7));
        const double travel = (clamp01((t - .50) / .86) * 1.45 - .22) * w, gfade = 1 - ssm((t - 1.45) / .40);
        double inc = ssm((t - .91) / .72);
        if (t >= 1.70)
            inc = 1;
        const double ink = inc * (1 - ssm((t - 4.55) / .56));
        const double base[3] = {51, 57, 64}, gain[3] = {148, 154, 162}, gl[3] = {92, 90, 74};
        setInto(f, x0, y0, w, h, [&](int x, int y, float *c) {
            const size_t q = size_t(y) * size_t(w) + size_t(x);
            const double body = m_.p[q] / 255.0;
            if (ink >= .999) {
                for (int k = 0; k < 3; ++k)
                    c[k] = float(body * a_.color[k]);
                return;
            }
            const double delay = clamp01(1 - dist_.p[q] / 13.5) * .18;
            double grow = clamp01((ink - delay) / std::max(.001, 1 - delay));
            grow = grow * grow * (3 - 2 * grow);
            const double gleam = std::exp(-std::pow((x - travel) / 63, 2.0)) * rim_.p[q] * gfade;
            for (int k = 0; k < 3; ++k) {
                const double mat = std::clamp(base[k] + gain[k] * rim_.p[q] * (.48 + .52 * shade_.p[q]), 0.0, 255.0) + gleam * gl[k];
                c[k] = float(body * imp * (1 - ink) * clamp01(mat / 255) * 255 + body * grow * a_.color[k]);
            }
        });
    }

private:
    GoldActor a_;
    M8 m_;
    MF dist_, rim_, shade_;
};

class PairKerning : public GoldEffect
{
public:
    PairKerning(const GoldText &g, const GoldActor &a) : a_(a)
    {
        for (const QString &c : chars(a.text)) {
            const TextBox b = g.bbox(a.font, a.size, c);
            const int pad = 10;
            M8 m(std::max(1, b.right - b.left + 2 * pad), std::max(1, b.bottom - b.top + 2 * pad));
            g.draw(m, a.font, a.size, c, pad - b.left, pad - b.top);
            gl_.push_back({m, g.length(a.font, a.size, c), double(b.left - pad), double(b.top - pad)});
        }
        const int n = int(gl_.size());
        QList<int> order;
        for (const QJsonValue &v : a.effect.value(QStringLiteral("order")).toArray())
            if (v.toInt() < n - 1)
                order << v.toInt();
        for (int i = 0; i < n - 1; ++i)
            if (!order.contains(i))
                order << i;
        in_.assign(size_t(std::max(0, n - 1)), 0.0);
        out_.assign(size_t(std::max(0, n - 1)), 0.0);
        for (int r = 0; r < order.size(); ++r) {
            in_[size_t(order[r])] = .31 + r * .172;
            out_[size_t(order[order.size() - 1 - r])] = 4.63 + r * .155;
        }
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const int n = int(gl_.size());
        const double op = ssm((t - .10) / .25) * (1 - ssm((t - 5.60) / .32));
        std::vector<float> gaps(size_t(std::max(0, n - 1)));
        double total = 0;
        for (const Glyph &g : gl_)
            total += float(g.adv);
        for (int i = 0; i < n - 1; ++i) {
            const double target = cycNum(e, "target", i), wide = cycNum(e, "wide", i);
            const double lock = outc((t - in_[size_t(i)]) / .40), rel = ssm((t - out_[size_t(i)]) / .38);
            gaps[size_t(i)] = float(target + (wide - target) * (1 - lock + rel));
            total += gaps[size_t(i)];
        }
        double x = (FW - total) / 2;
        for (int i = 0; i < n; ++i) {
            blendOver(f, gl_[size_t(i)].m, x + gl_[size_t(i)].xoff, FH / 2 + 94 + gl_[size_t(i)].yoff, a_.color, op);
            x += float(gl_[size_t(i)].adv) + (i < n - 1 ? gaps[size_t(i)] : 0.f);
        }
    }

private:
    struct Glyph
    {
        M8 m;
        double adv = 0, xoff = 0, yoff = 0;
    };
    GoldActor a_;
    std::vector<Glyph> gl_;
    std::vector<double> in_, out_;
};

class SolidExtract : public GoldEffect
{
public:
    SolidExtract(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const double trk = num(a.effect, "tracking");
        const int pad = 100;
        const M8 src = wordMask(g, a, trk, int(std::ceil(sumAdv(g, a, trk) + 2 * pad)), a.size + 2 * pad, pad, a.size + pad * .5, 55, 55);
        alpha_ = border(src, 45, 45, 92, 92);
        M8 in(alpha_.w, alpha_.h);
        for (size_t q = 0; q < in.p.size(); ++q)
            in.p[q] = alpha_.p[q] > 127;
        sdf_ = signedField(alpha_, 127, 0);
        for (float &v : sdf_.p)
            v = -v; // d_out − d_in
        int x0 = in.w, y0 = in.h, x1 = -1, y1 = -1;
        for (int y = 0; y < in.h; ++y)
            for (int x = 0; x < in.w; ++x)
                if (in.at(x, y)) {
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
        gb_ = {x0, y0, x1, y1};
        cx_ = (x0 + x1) * .5;
        cy_ = (y0 + y1) * .5;
        rw_ = (x1 - x0) * .5 + 12;
        rh_ = (y1 - y0) * .5 + 18;
    }
    void draw(Frame &f, double t) const override
    {
        const int w = alpha_.w, h = alpha_.h;
        const double toSil = ssm((t - .48) / .43), toInk = ssm((t - .91) / .63), rev = ssm((t - 4.43) / .83);
        const double grow = t < .48 ? 1 - std::pow(1 - clamp01((t - .10) / .40), 5.0) : (t > 5.26 ? 1 - ssm((t - 5.26) / .58) : 1.0);
        const double ghw = w * .48 * grow;
        const double finalInk = (1 - rev) * toSil * toInk, active = finalInk * (1 - finalInk);
        double k = 1;
        if (t < .22)
            k *= ssm((t - .06) / .16);
        if (t > 5.67)
            k *= 1 - ssm((t - 5.67) / .28);
        double oy[3];
        rgb(a_.effect, "slabColor", oy);
        const auto box = [](double qx, double qy, double r) { return std::hypot(std::max(qx, 0.0), std::max(qy, 0.0)) + std::min(std::max(qx, qy), 0.0) - r; };
        setInto(f, (FW - w) / 2, (FH - h) / 2, w, h, [&](int x, int y, float *c) {
            const size_t q = size_t(y) * size_t(w) + size_t(x);
            const double slab0 = box(std::fabs(x - cx_) - (rw_ - 23), std::fabs(y - cy_) - (rh_ - 23), 23);
            const double boxNow = box(std::fabs(x - cx_) - (ghw - 16), std::fabs(y - cy_) - ((gb_[3] - gb_[1]) * .5 + 19 - 16), 16);
            const double slab = (t < .48 || t > 5.26) ? boxNow : slab0, sdf = sdf_.p[q];
            double sg = ((1 - toSil) * slab + toSil * (sdf - 19.0)) * (1 - toInk) + sdf * toInk;
            if (t > 4.43)
                sg = (1 - rev) * sdf + rev * slab0;
            if (t > 5.26)
                sg = boxNow;
            const double body = finalInk >= .999 ? alpha_.p[q] / 255. : clamp01(.5 - sg);
            const double yrel = std::clamp((y - cy_) / std::max(rh_, 1.0), -1.0, 1.0);
            const double hl = std::exp(-std::pow(sg / 5.0, 2.0)) * active * 26 * clamp01(.48 - .45 * yrel);
            for (int i = 0; i < 3; ++i)
                c[i] = float(std::clamp(body * (oy[i] * (1 - finalInk) + a_.color[i] * finalInk + hl), 0.0, 255.0) * k);
        });
    }

private:
    GoldActor a_;
    M8 alpha_;
    MF sdf_;
    std::array<int, 4> gb_{};
    double cx_ = 0, cy_ = 0, rw_ = 0, rh_ = 0;
};

class PrintRegister : public GoldEffect
{
public:
    PrintRegister(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const double trk = num(a.effect, "tracking");
        double x = 0;
        for (const QString &c : chars(a.text)) {
            const TextBox b = g.bbox(a.font, a.size, c);
            const int pad = 20;
            M8 m(b.right - b.left + 2 * pad, b.bottom - b.top + 2 * pad);
            g.draw(m, a.font, a.size, c, pad - b.left, pad - b.top);
            gl_.push_back({m, x + b.left - pad, double(b.top - pad)});
            x += g.length(a.font, a.size, c) + trk;
        }
        startX_ = (FW - (x - trk)) / 2;
    }
    void draw(Frame &f, double t) const override
    {
        const QJsonObject &e = a_.effect;
        const double appear = ssm((t - .10) / .26) * (1 - ssm((t - 5.68) / .26));
        if (appear < .001)
            return;
        double warm[3], blue[3];
        rgb(e, "warm", warm);
        rgb(e, "blue", blue);
        const QJsonArray outward = e.value(QStringLiteral("outward")).toArray();
        const int n = int(gl_.size());
        for (int i = 0; i < n; ++i) {
            const Glyph &g = gl_[size_t(i)];
            const double reg = ssm((t - (.24 + i * .064)) / .90), sep = ssm((t - (4.52 + (n - 1 - i) * .045)) / .72);
            const double loose = clamp01(1 - reg + sep);
            const double ow = outward.isEmpty() ? 1.0 : outward.at(std::min(i, int(outward.size()) - 1)).toDouble();
            const double dx = double(28.0f * float(ow)) * loose, dy = 13 * loose;
            const int W2 = g.m.w + 100, H2 = g.m.h + 60;
            const auto at = [&](double sx, double sy) { return bilinear(g.m, sx - 50, sy - 30); };
            maxInto(f, int(roundEven(startX_ + g.x - 50)), int(roundEven(FH / 2 + 77 + g.y - 30)), W2, H2, [&](int x, int y, float *c) {
                if (loose < .0001) {
                    const double v = g.m.get(x - 50, y - 30) / 255.0;
                    for (int k = 0; k < 3; ++k)
                        c[k] = float(v * a_.color[k] * appear);
                    return;
                }
                const double A = std::round(at(x + dx, y + dy)) / 255, B = std::round(at(x - dx, y - dy)) / 255;
                const double ov = std::min(A, B);
                for (int k = 0; k < 3; ++k)
                    c[k] = float(((A - ov) * warm[k] + (B - ov) * blue[k] + ov * a_.color[k]) * appear);
            });
        }
    }

private:
    struct Glyph
    {
        M8 m;
        double x = 0, y = 0;
    };
    GoldActor a_;
    std::vector<Glyph> gl_;
    double startX_ = 0;
};

inline M8 word35(const GoldText &g, const GoldActor &a)
{
    const double trk = num(a.effect, "tracking");
    const int margin = int(num(a.effect, "margin"));
    return wordMask(g, a, trk, int(std::ceil(sumAdv(g, a, trk) + 2 * margin)), a.size + 2 * margin, margin, a.size + margin * .5, 48, 49);
}

class TidalInk : public GoldEffect
{
public:
    TidalInk(const GoldText &g, const GoldActor &a) : a_(a), m_(word35(g, a))
    {
        int top = m_.h, bottom = -1;
        for (int y = 0; y < m_.h; ++y)
            for (int x = 0; x < m_.w; ++x)
                if (m_.at(x, y) / 255.0 > .1) {
                    top = std::min(top, y);
                    bottom = std::max(bottom, y);
                }
        top_ = top;
        bottom_ = bottom;
        vessel_ = morphGradient5(m_);
    }
    void draw(Frame &f, double t) const override
    {
        const double p = t <= 4.38 ? ssm((t - .20) / 1.33) : 1 - ssm((t - 4.38) / 1.30);
        if (p <= 0)
            return;
        const int w = m_.w, h = m_.h;
        const double pi = 3.14159265358979323846, inkH = bottom_ - top_;
        const double amp = 20.0 * (1 - ssm((p - .24) / .76));
        const double active = 1 - ssm((p - .70) / .30), dry = ssm((p - .64) / .36);
        const double vo = (1 - ssm((p - .68) / .31)) * ssm((t - .20) / .19);
        double wet[3];
        rgb(a_.effect, "wet", wet);
        double k = 1;
        if (t < .28)
            k *= ssm((t - .20) / .10);
        if (t > 5.63)
            k *= 1 - ssm((t - 5.63) / .28);
        const double vc[3] = {81., 106., 114.};
        setInto(f, (FW - w) / 2, (FH - h) / 2, w, h, [&](int x, int y, float *c) {
            const double ph = 2 * pi * x / (w * .74), cap = 2 * pi * x / 138;
            const double surf = bottom_ + 28.0 - (inkH + 63.0) * p + amp * (0.82 * std::sin(ph + 2.5 * t) + 0.18 * std::sin(cap - 3.5 * t));
            const double av = m_.at(x, y) / 255.0;
            const double ink = p > .999 ? av : av * clamp01((y - surf + 1.2) / 3.0);
            const double crest = std::exp(-std::pow((y - surf) / 2.5, 2.0));
            const double refl = (1 - dry) * 7.0 * std::sin(ph * .45 + 1.1 * t);
            const double inside = (1 - dry) * 8.0 * clamp01((y - surf) / std::max(inkH, 1.0));
            for (int q = 0; q < 3; ++q) {
                const double col = wet[q] * (1 - dry) + a_.color[q] * dry;
                const double v = std::max(ink * std::clamp(col + refl + inside + crest * active * 35, 0.0, 255.0), vessel_.at(x, y) / 255.0 * vc[q] * vo);
                c[q] = float(v * k);
            }
        });
    }

private:
    GoldActor a_;
    M8 m_, vessel_;
    double top_ = 0, bottom_ = 0;
};

class HatchEngrave : public GoldEffect
{
public:
    HatchEngrave(const GoldText &g, const GoldActor &a) : a_(a), m_(word35(g, a)) {}
    void draw(Frame &f, double t) const override
    {
        const double p = t <= 4.42 ? ssm((t - .13) / 1.50) : 1 - ssm((t - 4.42) / 1.34);
        if (p <= 0)
            return;
        const int w = m_.w, h = m_.h;
        const double pa = 16.2, pb = 17.1, pi = 3.14159265358979323846;
        const double A = 1 - std::pow(1 - clamp01((p - .025) / .85), 4.0), B = 1 - std::pow(1 - clamp01((p - .28) / .62), 4.0);
        const double wa = .45 + 8.35 * ssm((p - .12) / .84), wb = .35 + 9.15 * ssm((p - .34) / .62), solid = ssm((p - .77) / .23);
        double line[3], cross[3];
        rgb(a_.effect, "line", line);
        rgb(a_.effect, "cross", cross);
        double k = 1;
        if (t < .21)
            k *= ssm((t - .13) / .08);
        if (t > 5.69)
            k *= 1 - ssm((t - 5.69) / .24);
        const auto pmod = [](double v, double m) { const double r = std::fmod(v, m); return r < 0 ? r + m : r; };
        setInto(f, (FW - w) / 2, (FH - h) / 2, w, h, [&](int x, int y, float *c) {
            const double av = m_.at(x, y) / 255.0;
            const double da = std::fabs(pmod((x + .64 * y) + pa * .5, pa) - pa * .5), db = std::fabs(pmod((x - .78 * y) + pb * .5, pb) - pb * .5);
            const double la = clamp01((wa - da + .85) / 1.15) * A, lb = clamp01((wb - db + .85) / 1.15) * B;
            double cov = (1 - (1 - la) * (1 - lb)) * av;
            cov = solid >= .999 ? av : cov * (1 - solid) + av * solid;
            const double inter = std::min(la, lb) * (1 - solid) * av, hl = 3.5 * std::sin(2 * pi * (x + .64 * y) / pa) * (1 - solid) * av;
            for (int q = 0; q < 3; ++q)
                c[q] = float(std::clamp(cov * (line[q] * (1 - solid) + a_.color[q] * solid) + (cross[q] - line[q]) * inter * .30 + hl, 0.0, 255.0) * k);
        });
    }

private:
    GoldActor a_;
    M8 m_;
};

class GridReflow : public GoldEffect
{
public:
    GridReflow(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QStringList cs = chars(a.text);
        const double trk = num(a.effect, "tracking");
        double start = FW * .5 - sumAdv(g, a, trk) * .5;
        const int half = (int(cs.size()) + 1) / 2;
        for (int i = 0; i < cs.size(); ++i) {
            M8 im(400, 410);
            g.draw(im, a.font, a.size, cs.at(i), 80, 326);
            int x0 = 400, y0 = 410, x1 = -1, y1 = -1;
            for (int y = 0; y < 410; ++y)
                for (int x = 0; x < 400; ++x)
                    if (im.at(x, y)) {
                        x0 = std::min(x0, x); x1 = std::max(x1, x);
                        y0 = std::min(y0, y); y1 = std::max(y1, y);
                    }
            const double adv = g.length(a.font, a.size, cs.at(i));
            if (x1 < 0) {
                start += adv;
                continue;
            }
            Part p;
            const int cx0 = std::max(0, x0 - 5), cy0 = std::max(0, y0 - 5);
            p.m = M8(std::min(400, x1 + 6) - cx0, std::min(410, y1 + 6) - cy0);
            for (int y = 0; y < p.m.h; ++y)
                for (int x = 0; x < p.m.w; ++x)
                    p.m.at(x, y) = im.at(cx0 + x, cy0 + y);
            p.fx = start + ((x0 + x1) * .5 - 80) + trk * i;
            p.fy = FH * .5 + (((y0 + y1) * .5 - 326) + 106);
            p.ix = FW * .5 + ((i % std::max(1, half)) - (half - 1) / 2.0) * 255;
            p.iy = FH * .5 + (i < half ? -151 : 151);
            p.i = i;
            p.top = i < half;
            parts_.push_back(std::move(p));
            start += adv;
        }
        double lx = 1e9, rx = -1e9;
        for (const Part &p : parts_) {
            lx = std::min(lx, p.fx - p.m.w / 2.0);
            rx = std::max(rx, p.fx + p.m.w / 2.0);
        }
        for (Part &p : parts_)
            p.fx += FW * .5 - (lx + rx) * .5;
    }
    void draw(Frame &f, double t) const override
    {
        if (t < .10 || t >= 5.92)
            return;
        const double fade = ssm((t - .10) / .2) * (1 - ssm((t - 5.63) / .27));
        double px, py;
        if (t < 4.5) {
            px = ssm((t - .18) / .43);
            py = ssm((t - .57) / .33);
        } else {
            py = 1 - ssm((t - 4.49) / .35);
            px = 1 - ssm((t - 4.91) / .52);
        }
        double lower[3];
        rgb(a_.effect, "lower", lower);
        for (const Part &p : parts_) {
            const double x = p.ix * (1 - px) + p.fx * px, y = p.iy * (1 - py) + p.fy * py;
            const double theta = (p.i % 2 ? 2.8 : -3.2) * (1 - py);
            const M8 small = std::fabs(theta) > .06 ? warpAffine(p.m, rotationMatrix(p.m.w / 2.0, p.m.h / 2.0, theta), p.m.w, p.m.h, false) : p.m;
            double tone[3];
            for (int k = 0; k < 3; ++k)
                tone[k] = p.top ? a_.color[k] : lower[k] * (1 - py) + a_.color[k] * py;
            maxInto(f, int(roundEven(x - p.m.w / 2.0)), int(roundEven(y - p.m.h / 2.0)), p.m.w, p.m.h, [&](int xx, int yy, float *c) {
                const double al = small.at(xx, yy) / 255. * fade;
                for (int k = 0; k < 3; ++k)
                    c[k] = float(std::trunc(al * tone[k]));
            });
        }
    }

private:
    struct Part
    {
        M8 m;
        double fx = 0, fy = 0, ix = 0, iy = 0;
        int i = 0;
        bool top = true;
    };
    GoldActor a_;
    std::vector<Part> parts_;
};

class RippleWave : public GoldEffect
{
public:
    RippleWave(const GoldText &g, const GoldActor &a) : a_(a)
    {
        // textbbox((0, 0), anchor 'la') and the word drawn there, centred on the frame
        const TextBox b = g.bbox(a.font, a.size, a.text);
        const int asc = g.ascender(a.font, a.size);
        const double top = b.top + asc, bottom = b.bottom + asc;
        M8 full(FW, FH);
        g.draw(full, a.font, a.size, a.text, (FW - (b.right - b.left)) / 2.0 - b.left, (FH - (bottom - top)) / 2.0 - top + asc);
        int x0 = FW, y0 = FH, x1 = -1, y1 = -1;
        for (int y = 0; y < FH; ++y)
            for (int x = 0; x < FW; ++x)
                if (full.at(x, y) > 1) {
                    x0 = std::min(x0, x); x1 = std::max(x1, x);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
        if (x1 < 0)
            return;
        bx_ = std::max(0, x0 - 125);
        by_ = std::max(0, y0 - 130);
        m_ = M8(std::min(FW, x1 + 126) - bx_, std::min(FH, y1 + 131) - by_);
        for (int y = 0; y < m_.h; ++y)
            for (int x = 0; x < m_.w; ++x)
                m_.at(x, y) = full.at(bx_ + x, by_ + y);
        cx_ = (FW / 2.0 - bx_) + 30.0;
        cy_ = (FH / 2.0 - by_) + 1.0;
    }
    void draw(Frame &f, double t) const override
    {
        if (m_.empty())
            return;
        double acc[3];
        rgb(a_.effect, "accent", acc);
        const double p = (t - 0.42) / 0.75, k = ssm((t - 0.06) / 0.25) * (1 - ssm((t - 5.38) / 0.49));
        const bool on = p >= 0 && p <= 1;
        const double ign = on ? ssm(p / 0.065) : 0, init = t < 0.52 ? std::max(0.0, 1 - std::fabs(t - 0.47) / 0.052) : 0;
        setInto(f, bx_, by_, m_.w, m_.h, [&](int x, int y, float *c) {
            const double vx = x - cx_, vy = y - cy_, dist = std::sqrt(vx * vx + vy * vy + 1e-3);
            double displ = 0, halo = 0;
            if (on) {
                const double band = std::exp(-std::pow((dist - 1100 * p) / 100.0, 2.0));
                displ = 9.0 * band * ign;
                halo = 0.48 * band * ign + 0.18 * init * std::exp(-std::pow(dist / 95.0, 2.0));
            }
            const double wv = std::round(bilinear(m_, x - vx / dist * displ, y - vy / dist * displ * 0.49)) / 255.0;
            const double mix = clamp01(halo);
            for (int q = 0; q < 3; ++q)
                c[q] = float((a_.color[q] * (1 - mix) + acc[q] * mix) * wv * k);
        });
    }

private:
    GoldActor a_;
    M8 m_;
    int bx_ = 0, by_ = 0;
    double cx_ = 0, cy_ = 0;
};

class SignalWave : public GoldEffect
{
public:
    SignalWave(const GoldText &g, const GoldActor &a) : a_(a)
    {
        const QStringList cs = chars(a.text);
        const double trk = num(a.effect, "tracking"), total = sumAdv(g, a, trk);
        const int asc = g.ascender(a.font, a.size);
        M8 im(FW, FH);
        double x = (FW - total) * 0.5;
        for (const QString &c : cs) {
            g.draw(im, a.font, a.size, c, roundEven(x), 360 + asc);
            x += g.length(a.font, a.size, c) + trk;
        }
        int ymin = FH, ymax = -1;
        for (int y = 0; y < FH; ++y)
            for (int xx = 0; xx < FW; ++xx)
                if (im.at(xx, y)) {
                    ymin = std::min(ymin, y);
                    ymax = std::max(ymax, y);
                }
        if (ymax < 0)
            return;
        const int dy = int(roundEven(FH / 2.0 - (ymin + ymax) / 2.0));
        M8 rolled(FW, FH);
        for (int y = 0; y < FH; ++y)
            for (int xx = 0; xx < FW; ++xx)
                rolled.at(xx, ((y + dy) % FH + FH) % FH) = im.at(xx, y);
        int x0 = FW, y0 = FH, x1 = -1, y1 = -1;
        for (int y = 0; y < FH; ++y)
            for (int xx = 0; xx < FW; ++xx)
                if (rolled.at(xx, y)) {
                    x0 = std::min(x0, xx); x1 = std::max(x1, xx);
                    y0 = std::min(y0, y); y1 = std::max(y1, y);
                }
        box_ = {std::max(x0 - 95, 0), std::max(y0 - 120, 0), std::min(x1 + 96, FW), std::min(y1 + 121, FH)};
        m_ = M8(box_[2] - box_[0], box_[3] - box_[1]);
        for (int y = 0; y < m_.h; ++y)
            for (int xx = 0; xx < m_.w; ++xx)
                m_.at(xx, y) = rolled.at(box_[0] + xx, box_[1] + y);
        x = (FW - total) * 0.5;
        for (const QString &c : cs) {
            const double wd = g.length(a.font, a.size, c);
            centers_.push_back(x + wd / 2 - box_[0]);
            x += wd + trk;
        }
        int ax0 = m_.w, ax1 = -1, ay0 = m_.h, ay1 = -1;
        for (int y = 0; y < m_.h; ++y)
            for (int xx = 0; xx < m_.w; ++xx)
                if (m_.at(xx, y) > 10) {
                    ax0 = std::min(ax0, xx); ax1 = std::max(ax1, xx);
                    ay0 = std::min(ay0, y); ay1 = std::max(ay1, y);
                }
        xmin_ = ax0;
        xmax_ = ax1;
        yc_ = (ay0 + ay1) * 0.5;
    }
    void draw(Frame &f, double t) const override
    {
        if (m_.empty())
            return;
        const auto ramp = [t](double s, double e) { return ssm((t - s) / (e - s)); };
        const double word = ramp(.43, .80) * (1 - ramp(4.84, 5.25));
        const double bars = ramp(.24, .43) * (1 - ramp(.50, .78)) + ramp(5.03, 5.25) * (1 - ramp(5.39, 5.67));
        const double wave = ramp(.06, .19) * (1 - ramp(.25, .43)) + ramp(5.44, 5.65) * (1 - ramp(5.73, 5.93));
        double ink[3];
        rgb(a_.effect, "ink", ink);
        M8 barM(m_.w, m_.h);
        const int cy = int(roundEven(yc_));
        if (bars > 0.005)
            for (size_t j = 0; j < centers_.size(); ++j) {
                const double pulse = 1 + 0.10 * std::sin(12 * t + j * 1.25);
                const int hh = int(roundEven((56 + 108 * cycNum(a_.effect, "bars", int(j)) * pulse) * std::min(1.0, 0.75 + 0.25 * bars)));
                const int xc = int(roundEven(centers_[j]));
                for (int y = std::max(0, cy - hh / 2); y <= std::min(m_.h - 1, cy + hh / 2); ++y)
                    for (int x = std::max(0, xc - 8); x <= std::min(m_.w - 1, xc + 8); ++x)
                        barM.at(x, y) = 255;
            }
        MF waveM(m_.w, m_.h);
        if (wave > 0.005) {
            const double pi = 3.14159265358979323846;
            double px = 0, py = 0;
            bool first = true;
            for (int xc = xmin_ - 50; xc < xmax_ + 51; xc += 2) {
                const double u = (xc - xmin_) / std::max(1.0, double(xmax_ - xmin_));
                const double env = std::pow(std::max(0.0, std::sin(pi * u)), 0.85);
                const double osc = (46 * std::sin(2 * pi * (3.25 * u + 0.07)) + 18 * std::sin(2 * pi * (8.1 * u + 0.14)) + 6 * std::sin(2 * pi * 16 * u)) * env;
                const double y = std::nearbyint(yc_ + osc);
                if (!first)
                    strokeAA(waveM, px, py, xc, y);
                px = xc;
                py = y;
                first = false;
            }
        }
        setInto(f, box_[0], box_[1], m_.w, m_.h, [&](int x, int y, float *c) {
            const double mv = m_.at(x, y) / 255. * word, bv = barM.at(x, y) / 255. * bars, wv = waveM.at(x, y) * wave;
            for (int q = 0; q < 3; ++q)
                c[q] = float(std::max({mv * a_.color[q], bv * (ink[q] * .94 + a_.color[q] * .06), wv * ink[q]}));
        });
    }

private:
    GoldActor a_;
    M8 m_;
    std::array<int, 4> box_{};
    std::vector<double> centers_;
    int xmin_ = 0, xmax_ = 0;
    double yc_ = 0;
};

} // namespace detail

inline const QStringList &goldKinds()
{
    static const QStringList k{QStringLiteral("contourMorph"), QStringLiteral("elasticWarp"), QStringLiteral("arcToBaseline"),
                               QStringLiteral("perspectivePlane"), QStringLiteral("widthBloom"), QStringLiteral("weightMorph"),
                               QStringLiteral("spectralInk"), QStringLiteral("glyphPivot"), QStringLiteral("mirrorUnfold"),
                               QStringLiteral("slantMorph"), QStringLiteral("counterRelease"), QStringLiteral("inverseTransit"),
                               QStringLiteral("depthCast"), QStringLiteral("pleatUnfold"), QStringLiteral("particleCoalesce"),
                               QStringLiteral("contourTrace"), QStringLiteral("characterReel"), QStringLiteral("isolineCollapse"),
                               QStringLiteral("splitRegister"), QStringLiteral("glyphTorsion"), QStringLiteral("imprintPress"),
                               QStringLiteral("pairKerning"), QStringLiteral("solidExtract"), QStringLiteral("printRegister"),
                               QStringLiteral("tidalInk"), QStringLiteral("hatchEngrave"), QStringLiteral("gridReflow"),
                               QStringLiteral("rippleWave"), QStringLiteral("signalWave")};
    return k;
}

inline std::unique_ptr<GoldEffect> makeGoldEffect(const GoldText &g, const GoldActor &a)
{
    using namespace detail;
    const QString k = a.effect.value(QStringLiteral("type")).toString();
    if (k == QLatin1String("contourMorph")) return std::make_unique<ContourMorph>(g, a);
    if (k == QLatin1String("elasticWarp")) return std::make_unique<ElasticWarp>(g, a);
    if (k == QLatin1String("arcToBaseline")) return std::make_unique<ArcToBaseline>(g, a);
    if (k == QLatin1String("perspectivePlane")) return std::make_unique<PerspectivePlane>(g, a);
    if (k == QLatin1String("widthBloom")) return std::make_unique<WidthBloom>(g, a);
    if (k == QLatin1String("weightMorph")) return std::make_unique<WeightMorph>(g, a);
    if (k == QLatin1String("spectralInk")) return std::make_unique<SpectralInk>(g, a);
    if (k == QLatin1String("glyphPivot")) return std::make_unique<GlyphPivot>(g, a);
    if (k == QLatin1String("mirrorUnfold")) return std::make_unique<MirrorUnfold>(g, a);
    if (k == QLatin1String("slantMorph")) return std::make_unique<SlantMorph>(g, a);
    if (k == QLatin1String("counterRelease")) return std::make_unique<CounterRelease>(g, a);
    if (k == QLatin1String("inverseTransit")) return std::make_unique<InverseTransit>(g, a);
    if (k == QLatin1String("depthCast")) return std::make_unique<DepthCast>(g, a);
    if (k == QLatin1String("pleatUnfold")) return std::make_unique<PleatUnfold>(g, a);
    if (k == QLatin1String("particleCoalesce")) return std::make_unique<ParticleCoalesce>(g, a);
    if (k == QLatin1String("contourTrace")) return std::make_unique<ContourTrace>(g, a);
    if (k == QLatin1String("characterReel")) return std::make_unique<CharacterReel>(g, a);
    if (k == QLatin1String("isolineCollapse")) return std::make_unique<IsolineCollapse>(g, a);
    if (k == QLatin1String("splitRegister")) return std::make_unique<SplitRegister>(g, a);
    if (k == QLatin1String("glyphTorsion")) return std::make_unique<GlyphTorsion>(g, a);
    if (k == QLatin1String("imprintPress")) return std::make_unique<ImprintPress>(g, a);
    if (k == QLatin1String("pairKerning")) return std::make_unique<PairKerning>(g, a);
    if (k == QLatin1String("solidExtract")) return std::make_unique<SolidExtract>(g, a);
    if (k == QLatin1String("printRegister")) return std::make_unique<PrintRegister>(g, a);
    if (k == QLatin1String("tidalInk")) return std::make_unique<TidalInk>(g, a);
    if (k == QLatin1String("hatchEngrave")) return std::make_unique<HatchEngrave>(g, a);
    if (k == QLatin1String("gridReflow")) return std::make_unique<GridReflow>(g, a);
    if (k == QLatin1String("rippleWave")) return std::make_unique<RippleWave>(g, a);
    if (k == QLatin1String("signalWave")) return std::make_unique<SignalWave>(g, a);
    return nullptr;
}

} // namespace drift::scene::gold
