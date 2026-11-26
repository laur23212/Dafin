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
        inks_ = bm;
        for (const auto &px : parts)
            for (int q : px)
                inks_.p[size_t(q)] = 255; // the solid form: counters filled
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
                        stroke(lines, c[size_t(k - 1)], c[size_t(k)]);
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
    // cv2.polylines(…, 2, LINE_AA): a 2 px anti-aliased segment, max into the layer.
    static void stroke(MF &m, std::pair<int, int> p, std::pair<int, int> q)
    {
        const double x0 = p.first, y0 = p.second, x1 = q.first, y1 = q.second;
        const double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
        for (int y = int(std::min(y0, y1)) - 3; y <= int(std::max(y0, y1)) + 3; ++y)
            for (int x = int(std::min(x0, x1)) - 3; x <= int(std::max(x0, x1)) + 3; ++x) {
                if (x < 0 || y < 0 || x >= m.w || y >= m.h)
                    continue;
                const double u = len2 > 0 ? std::clamp(((x - x0) * dx + (y - y0) * dy) / len2, 0.0, 1.0) : 0.0;
                const double d = std::hypot(x - (x0 + u * dx), y - (y0 + u * dy));
                const float v = float(clamp01(2.2 - d)); // cv2 2 px AA: ~3.4 px across
                m.at(x, y) = std::max(m.at(x, y), v);
            }
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

} // namespace detail

inline const QStringList &goldKinds()
{
    static const QStringList k{QStringLiteral("contourMorph"), QStringLiteral("elasticWarp"), QStringLiteral("arcToBaseline"),
                               QStringLiteral("perspectivePlane"), QStringLiteral("widthBloom"), QStringLiteral("weightMorph"),
                               QStringLiteral("spectralInk"), QStringLiteral("glyphPivot"), QStringLiteral("mirrorUnfold"),
                               QStringLiteral("slantMorph"), QStringLiteral("counterRelease"), QStringLiteral("inverseTransit"),
                               QStringLiteral("depthCast"), QStringLiteral("pleatUnfold"), QStringLiteral("particleCoalesce"),
                               QStringLiteral("contourTrace"), QStringLiteral("characterReel"), QStringLiteral("isolineCollapse")};
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
    return nullptr;
}

} // namespace drift::scene::gold
