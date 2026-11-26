// SceneDraftEffects.h — the draft1x titles (docs/scene-titles.md "draft1x"): the Stage 54–67
// library and the creator's favourite Filmora styles. Each class is its script's prepare()/render()
// (tools/scene-ref/drafts/d<asset>.py) in C++, with the script's numbers; the two editable strings
// come from the scene. A string equal to the script's default counts as not given (None), as the
// script's own command line passes it, because a few scripts draw their default differently.
#pragma once

#include "SceneDraftOps.h"

#include <QHash>
#include <QRegularExpression>
#include <QString>
#include <QStringList>

#include <functional>
#include <memory>
#include <optional>

namespace drift::scene::draft {

struct DraftActor
{
    QString kind;
    QString font; // the scene's font key, for the favourites (one font each)
    std::optional<QString> text, secondary;
};

class DraftEffect
{
public:
    virtual ~DraftEffect() = default;
    virtual void draw(Rgba &f, double t) const = 0;
    // Scripts written after Stage 60 froze their hold on its first frame.
    double holdA = -1, holdB = -1, holdT = 0;
    void freeze(double a, double b)
    {
        holdA = a, holdB = b;
        int n = 0;
        while (n / 25.0 < a)
            ++n;
        holdT = n / 25.0;
    }
    double timeFor(double t) const { return holdA >= 0 && t >= holdA && t <= holdB ? holdT : t; }
};

namespace d {

inline QString val(const std::optional<QString> &v, const char *def) { return v && !v->isEmpty() ? *v : QString::fromUtf8(def); }

// Words as Python's str.split().
inline QStringList words(const QString &s) { return s.split(QRegularExpression(QStringLiteral("\\s+")), Qt::SkipEmptyParts); }

inline Rgba solid(int w, int h, Rgb c, int a = 255)
{
    Rgba out(w, h);
    for (size_t i = 0; i < out.p.size(); i += 4)
        out.p[i] = std::uint8_t(c.r), out.p[i + 1] = std::uint8_t(c.g), out.p[i + 2] = std::uint8_t(c.b), out.p[i + 3] = std::uint8_t(a);
    return out;
}

// ---- 68 Art 30 / Art 27: a static two-layer word (back: offset and/or stroked; face on top) ---------
struct ArtStyle
{
    double size, tracking, cx, cy, offX, offY, stroke;
    Rgb face, back;
};

class Art : public DraftEffect
{
public:
    Art(const GoldText &g, const QString &font, const QString &text, const ArtStyle &P)
    {
        const double S = FW / 288.0;
        double scale = 1.0;
        const QList<uint> cps = text.toUcs4();
        struct Run { QString t; double x; };
        std::vector<Run> runs;
        int px = 0;
        double box[4] = {0, 0, 0, 0};
        const int asc0 = 0;
        (void)asc0;
        auto masks = [&](double factor) {
            px = std::max(1, pyRound(P.size * S * factor));
            const double track = P.tracking * S * factor;
            runs.clear();
            if (std::abs(track) < 1e-7)
                runs.push_back({text, 0.0});
            else
                for (int i = 0; i < cps.size(); ++i) {
                    const QString pre = QString::fromUcs4(reinterpret_cast<const char32_t *>(cps.constData()), i);
                    runs.push_back({QString::fromUcs4(reinterpret_cast<const char32_t *>(&cps[i]), 1), g.length(font, px, pre) + i * track});
                }
            const int asc = g.ascender(font, px);
            bool first = true;
            for (const Run &r : runs) {
                const TextBox b = g.bbox(font, px, r.t); // getbbox(t): anchor 'la'
                const double q[4] = {r.x + b.left, double(b.top + asc), r.x + b.right, double(b.bottom + asc)};
                if (first)
                    std::copy(q, q + 4, box), first = false;
                else
                    box[0] = std::min(box[0], q[0]), box[1] = std::min(box[1], q[1]), box[2] = std::max(box[2], q[2]), box[3] = std::max(box[3], q[3]);
            }
        };
        masks(scale);
        for (int i = 0; i < 5; ++i) {
            const double span = box[2] - box[0] + 2 * (P.stroke + std::abs(P.offX)) * S * scale;
            if (span <= FW * 0.9)
                break;
            scale *= FW * 0.9 / span;
            masks(scale);
        }
        const double cx = P.cx * S, cy = P.cy * S;
        const int asc = g.ascender(font, px);
        auto mask = [&](double stroke, double dx, double dy) {
            gold::M8 m(FW, FH);
            for (const Run &r : runs)
                g.draw(m, font, px, r.t, pyRound(cx + r.x - (box[0] + box[2]) / 2 + dx), pyRound(cy - (box[1] + box[3]) / 2 + dy) + asc,
                       pyRound(stroke));
            return m;
        };
        const gold::M8 front = mask(0, 0, 0), back = mask(P.stroke * S * scale, P.offX * S * scale, P.offY * S * scale);
        im_ = Rgba(FW, FH);
        const std::pair<Rgb, const gold::M8 *> layers[2] = {{P.back, &back}, {P.face, &front}};
        for (const auto &l : layers) {
            Rgba layer = solid(FW, FH, l.first);
            for (size_t i = 0; i < l.second->p.size(); ++i)
                layer.p[i * 4 + 3] = l.second->p[i];
            composite(im_, layer, 0, 0);
        }
    }
    void draw(Rgba &f, double) const override { composite(f, im_, 0, 0); }

private:
    Rgba im_;
};

// ---- 55A Keyphrase: a marker travels word to word behind one shaped line ------------------------
class Keyphrase : public DraftEffect
{
public:
    Keyphrase(const GoldText &g, const DraftActor &a)
    {
        const QString text = val(a.text, "Make every moment count");
        const QStringList w = words(text);
        if (w.size() < 2 || w.size() > 8)
            return;
        int size = 114;
        std::vector<double> widths;
        double total = 0;
        while (true) {
            widths.clear();
            for (const QString &s : w)
                widths.push_back(g.length(QStringLiteral("bold"), size, s));
            total = 0;
            for (double v : widths)
                total += v;
            total += 34.0 * (w.size() - 1);
            if (total <= 1680 || size == 56)
                break;
            --size;
        }
        if (total > 1680)
            return;
        double x = (FW - total) / 2;
        std::vector<int> xs;
        std::vector<TextBox> bbs;
        for (int i = 0; i < w.size(); ++i) {
            xs.push_back(pyRound(x));
            bbs.push_back(g.bbox(QStringLiteral("bold"), size, w[i]));
            x += widths[size_t(i)] + 34;
        }
        int top = 1 << 30, bottom = -(1 << 30);
        for (const TextBox &b : bbs)
            top = std::min(top, 592 + b.top), bottom = std::max(bottom, 592 + b.bottom);
        top -= 17, bottom += 17;
        for (int i = 0; i < w.size(); ++i)
            bounds_.push_back({double(xs[size_t(i)] + bbs[size_t(i)].left - 22), double(xs[size_t(i)] + bbs[size_t(i)].right + 22)});
        double bl = 1e9, br = -1e9;
        for (const auto &b : bounds_)
            bl = std::min(bl, b.first), br = std::max(br, b.second);
        x0_ = std::max(0, int(bl) - 30), y0_ = std::max(0, top - 35);
        const int x1 = std::min(FW, int(br) + 30), y1 = std::min(FH, bottom + 35);
        top_ = top, bottom_ = bottom;
        glyph_ = gold::M8(x1 - x0_, y1 - y0_);
        for (int i = 0; i < w.size(); ++i)
            g.draw(glyph_, QStringLiteral("bold"), size, w[i], xs[size_t(i)] - x0_, 592 - y0_);
        for (int i = 0; i < 4; ++i)
            idx_[i] = pyRound(i * (w.size() - 1) / 3.0);
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double e = ease(t, 0.12, 0.78), o = ease(t, 4.96, 5.76), a = e * (1 - o);
        const int dy = pyRound(20 * (1 - e) - 16 * o);
        double left = bounds_[size_t(idx_[0])].first, right = bounds_[size_t(idx_[0])].second;
        const double cues[3][2] = {{1.10, 1.55}, {1.95, 2.40}, {2.78, 3.23}};
        for (int i = 0; i < 3; ++i) {
            const double p = ease(t, cues[i][0], cues[i][1]);
            const auto &b = bounds_[size_t(idx_[i + 1])];
            left = left + (b.first - left) * p, right = right + (b.second - right) * p;
        }
        const double mi = ease(t, 0.72, 1.08), c = (left + right) / 2;
        left = c + (left - c) * mi, right = c + (right - c) * mi;
        const int bw = glyph_.w, bh = glyph_.h;
        gold::M8 marker(bw, bh);
        if (mi > 0) {
            const int L = pyRound(left - x0_), T = top_ - y0_, R = pyRound(right - x0_), B = bottom_ - y0_;
            if (R >= L && B >= T) {
                const Rgba r = rect(R - L + 1, B - T + 1, {255, 255, 255}, std::min(18, pyRound((right - left) / 2)));
                const int v = pyRound(255 * mi);
                for (int y = 0; y < r.h; ++y)
                    for (int x = 0; x < r.w; ++x)
                        if (r.px(x, y)[3] && L + x >= 0 && L + x < bw && T + y >= 0 && T + y < bh)
                            marker.at(L + x, T + y) = std::uint8_t(v);
            }
        }
        const double INK[3] = {241, 240, 234}, ACT[3] = {18, 26, 24}, MARK[3] = {168, 224, 186};
        for (int y = 0; y < bh; ++y) {
            const int fy = y0_ + y + dy;
            if (fy < 0 || fy >= FH)
                continue;
            for (int x = 0; x < bw; ++x) {
                const float ma = marker.at(x, y) / 255.f, gg = glyph_.at(x, y) / 255.f;
                const float al = gg + ma * (1 - gg);
                std::uint8_t *q = f.px(x0_ + x, fy);
                std::uint8_t px[4];
                for (int k = 0; k < 3; ++k) {
                    const double ink = INK[k] * (1 - double(ma)) + ACT[k] * double(ma);
                    const double v = (ink * gg + MARK[k] * double(ma) * double(1 - gg)) / std::max(double(al), 1e-8);
                    px[k] = std::uint8_t(std::clamp(std::nearbyint(v), 0.0, 255.0));
                }
                px[3] = std::uint8_t(std::nearbyint(double(al) * 255 * a));
                std::copy_n(px, 4, q); // the script writes this box over the frame
            }
        }
    }

private:
    bool ok_ = false;
    std::vector<std::pair<double, double>> bounds_;
    int idx_[4] = {0, 0, 0, 0};
    int x0_ = 0, y0_ = 0, top_ = 0, bottom_ = 0;
    gold::M8 glyph_;
};

// ---- 55B Linebreak: one line parts into two centred lines ----------------------------------------
class Linebreak : public DraftEffect
{
public:
    Linebreak(const GoldText &g, const DraftActor &a)
    {
        const QStringList w = words(val(a.text, "Make every moment count"));
        if (w.size() < 2 || w.size() > 12)
            return;
        const int split = (int(w.size()) + 1) / 2;
        const QString groups[2] = {w.mid(0, split).join(QLatin1Char(' ')), w.mid(split).join(QLatin1Char(' '))};
        int size = 116, widths[2] = {0, 0};
        while (true) {
            for (int i = 0; i < 2; ++i)
                b_[i] = g.bbox(QStringLiteral("bold"), size, groups[i]), widths[i] = b_[i].right - b_[i].left;
            if (widths[0] + widths[1] + 36 <= 1680 || size == 54)
                break;
            --size;
        }
        if (widths[0] + widths[1] + 36 > 1680)
            return;
        const int first = pyRound(960 - (widths[0] + widths[1] + 36) / 2.0);
        start_[0] = first, start_[1] = first + widths[0] + 36;
        const Rgb cols[2] = {{244, 242, 234}, {165, 201, 232}};
        for (int i = 0; i < 2; ++i) {
            finish_[i] = pyRound(960 - widths[i] / 2.0);
            gold::M8 m(b_[i].right - b_[i].left + 4, b_[i].bottom - b_[i].top + 4);
            g.draw(m, QStringLiteral("bold"), size, groups[i], 2 - b_[i].left, 2 - b_[i].top);
            tiles_[i] = tint(m, cols[i]);
            for (int y = 0; y < m.h; ++y) // rgb is the colour everywhere, alpha the mask
                for (int x = 0; x < m.w; ++x) {
                    std::uint8_t *q = tiles_[i].px(x, y);
                    q[0] = std::uint8_t(cols[i].r), q[1] = std::uint8_t(cols[i].g), q[2] = std::uint8_t(cols[i].b);
                }
        }
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double e = ease(t, 0.10, 0.74), v = ease(t, 1.00, 1.52), h = ease(t, 1.30, 2.13), o = ease(t, 4.92, 5.72), op = e * (1 - o);
        for (int i = 0; i < 2; ++i) {
            const int sign = i == 0 ? -1 : 1;
            const int xx = pyRound(start_[i] + (finish_[i] - start_[i]) * h) - 2;
            const int base = pyRound(594 + sign * 94 * v + 20 * (1 - e) + sign * 32 * o);
            Rgba l = tiles_[i];
            for (size_t k = 3; k < l.p.size(); k += 4)
                l.p[k] = std::uint8_t(std::nearbyint(l.p[k] * op));
            composite(f, l, xx, base + b_[i].top - 2);
        }
    }

private:
    bool ok_ = false;
    TextBox b_[2];
    int start_[2] = {0, 0}, finish_[2] = {0, 0};
    Rgba tiles_[2];
};

// ---- 56B Pullquote: lines rise through stationary apertures, then the credit ---------------------
class Pullquote : public DraftEffect
{
public:
    Pullquote(const GoldText &g, const DraftActor &a)
    {
        const QString text = val(a.text, "The best stories\nmake us look closer.");
        const QString credit = val(a.secondary, "FROM THE FIELD NOTES");
        const QStringList lines = text.split(QLatin1Char('\n'));
        if (lines.isEmpty() || lines.size() > 3)
            return;
        for (const QString &l : lines)
            tiles_.push_back(textTile(g, QStringLiteral("serif"), l, 112, INK, 1330));
        quote_ = textTile(g, QStringLiteral("serif"), QStringLiteral("\u201C"), 140, ACC, 180);
        credit_ = textTile(g, QStringLiteral("medium"), credit, 33, ACC, 1240);
        cy_ = 330 + int(lines.size()) * 152 + 46;
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double o = ease(t, 4.94, 5.76), a = 1 - o, e = ease(t, 0.12, 0.80);
        const int dy = pyRound(-14 * o);
        paste(f, quote_.im, 286 - 105, 330 - 26 + dy, e * a);
        for (size_t i = 0; i < tiles_.size(); ++i) {
            const Rgba &im = tiles_[i].im;
            const double s = 0.12 + i * 0.32, p = ease(t, s, s + 0.76);
            const int offset = pyRound((im.h + 10) * (1 - p)), vis = std::max(0, im.h - offset);
            if (vis)
                paste(f, crop(im, 0, 0, im.w, vis), 286, 330 + int(i) * 152 + offset + dy, a);
        }
        const double p = ease(t, 1.24, 1.94);
        Rgba rule(58, 3);
        for (int y = 0; y < 3; ++y)
            for (int x = 0; x <= std::min(57, pyRound(57 * p)); ++x) {
                std::uint8_t *q = rule.px(x, y);
                q[0] = std::uint8_t(ACC.r), q[1] = std::uint8_t(ACC.g), q[2] = std::uint8_t(ACC.b), q[3] = std::uint8_t(pyRound(255 * p));
            }
        paste(f, rule, 286, cy_ + credit_.height() / 2 + dy, a);
        paste(f, credit_.im, 286 + 84, cy_ + dy + pyRound(10 * (1 - p)), p * a);
    }

private:
    static constexpr Rgb INK{245, 240, 229}, ACC{190, 209, 190};
    bool ok_ = false;
    std::vector<Tile> tiles_;
    Tile quote_, credit_;
    int cy_ = 0;
};

// ---- 57A Switchword: one word rolls into another, the rest follows its width -----------------------
class Switchword : public DraftEffect
{
public:
    Switchword(const GoldText &g, const DraftActor &a)
    {
        const Rgb ACC{246, 186, 116}, INK{246, 244, 236};
        a_ = textTile(g, QStringLiteral("bold"), a.text ? *a.text : QStringLiteral("Make"), 132, ACC, 650);
        b_ = textTile(g, QStringLiteral("bold"), a.text ? *a.text : QStringLiteral("Let"), 132, ACC, 650);
        s_ = textTile(g, QStringLiteral("bold"), val(a.secondary, "it happen."), 132, INK, 900);
        ya_ = 580 + a_.box.top - 2, yb_ = 580 + b_.box.top - 2, ys_ = 580 + s_.box.top - 2;
        ax_ = 250 + a_.advance + 25, bx_ = 250 + b_.advance + 25;
    }
    void draw(Rgba &f, double t) const override
    {
        const double e = ease(t, 0.14, 0.72), p = ease(t, 1.18, 1.96), o = ease(t, 4.84, 5.76), v = e * (1 - o);
        const int dy = pyRound(14 * (1 - e) - 10 * o);
        const double sx = mix(ax_, bx_, p);
        const double sl = 250 - 4, st = 580 - 152 + dy, sr = sx - 8, sb = 580 + 37 + dy;
        clip(f, a_.im, 250, ya_ + dy - 180 * p, sl, st, sr, sb, v);
        clip(f, b_.im, 250, yb_ + dy + 180 * (1 - p), sl, st, sr, sb, v);
        paste(f, s_.im, sx, ys_ + dy, v);
    }

private:
    Tile a_, b_, s_;
    double ya_ = 0, yb_ = 0, ys_ = 0, ax_ = 0, bx_ = 0;
};

// ---- 57B Nameplate: an adaptive plate opens, name and role slide in ---------------------------------
class Nameplate : public DraftEffect
{
public:
    Nameplate(const GoldText &g, const DraftActor &a)
    {
        const Rgb INK{24, 32, 33};
        a_ = textTile(g, QStringLiteral("bold"), val(a.text, "Alex Morgan"), 65, INK, 1100);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "Creative director"), 31, INK, 1100);
        w_ = std::max(a_.width(), b_.width()) + 2 * 30 + 9;
        plate_ = rect(w_, 148, {235, 232, 222}, 3);
        stripe_ = rect(8, 148, {35, 111, 101});
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, 0.16, 0.84), q = ease(t, 0.43, 1.10), o = ease(t, 4.85, 5.78);
        const int X = 132, Y = 790, end = X + pyRound(w_ * p * (1 - o));
        const int nx = X + 30 + 8, ny = Y + 18, ry = Y + 100;
        clip(f, plate_, X, Y, X, Y, end, Y + 148);
        clip(f, stripe_, X, Y, X, Y, end, Y + 148);
        clip(f, a_.im, nx - 26 * (1 - q), ny, X, Y, end, Y + 148, q * (1 - o));
        clip(f, b_.im, nx - 19 * (1 - q), ry, X, Y, end, Y + 148, q * (1 - o));
    }

private:
    Tile a_, b_;
    int w_ = 0;
    Rgba plate_, stripe_;
};

// ---- 57D Section: spine, tab and index, then the heading slides out of its clip --------------------
class Section : public DraftEffect
{
public:
    Section(const GoldText &g, const DraftActor &a)
    {
        const Rgb INK{247, 244, 236}, ACC{224, 169, 104};
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "A new perspective"), 99, INK, 1290);
        num_ = textTile(g, QStringLiteral("bold"), val(a.secondary, "02"), 58, ACC, 130);
        tag_ = textTile(g, QStringLiteral("medium"), QStringLiteral("CHAPTER"), 26, ACC);
        sp_ = rect(6, 310, {90, 99, 108});
        tab_ = rect(14, 79, ACC, 2);
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 225, Y = 385, tx = X + 215, ty = Y + 92, nx = X + 42, ny = Y + 13, ly = Y + 91;
        const double p = ease(t, 0.13, 0.92), q = ease(t, 0.50, 1.27), v = 1 - ease(t, 4.83, 5.76);
        const int h = pyRound(310 * p);
        clip(f, sp_, X, Y, X, Y, X + 6, Y + h, v);
        paste(f, tab_, X - 4, Y + 146 * p, p * v);
        paste(f, num_.im, nx, ny + 20 * (1 - p), p * v);
        paste(f, tag_.im, nx, ly + 15 * (1 - p), p * v);
        clip(f, a_.im, tx - 80 * (1 - q), ty, tx, Y - 20, FW - 100, Y + 310, q * v);
    }

private:
    Tile a_, num_, tag_;
    Rgba sp_, tab_;
};


// ---- 57C Checklist: three rows, each ticked in turn -----------------------------------------------
class Checklist : public DraftEffect
{
public:
    Checklist(const GoldText &g, const DraftActor &a)
    {
        const QString rows[3] = {val(a.text, "Plan the story"), val(a.secondary, "Review the cut"), QStringLiteral("Publish with confidence")};
        for (const QString &r : rows)
            tiles_.push_back(textTile(g, QStringLiteral("medium"), r, 62, {247, 244, 236}, 1250));
        done_ = tick(1);
    }
    // The 64 px box: a rounded outline easing from dim to accent, and the check drawn by arc length.
    static Rgba tick(double p)
    {
        const double DIM[3] = {107, 124, 127}, ACC[3] = {141, 221, 190};
        const Rgb col{pyRound(mix(DIM[0], ACC[0], p)), pyRound(mix(DIM[1], ACC[1], p)), pyRound(mix(DIM[2], ACC[2], p))};
        Rgba im(64, 64);
        for (int y = 0; y < 64; ++y)
            for (int x = 0; x < 64; ++x) {
                const double c = rrectCoverage(x + .5, y + .5, 2, 2, 60 + 1 / 3.0, 60 + 1 / 3.0, 12)
                                 - rrectCoverage(x + .5, y + .5, 5, 5, 57 + 1 / 3.0, 57 + 1 / 3.0, 9);
                if (c <= 0)
                    continue;
                std::uint8_t *q = im.px(x, y);
                q[0] = std::uint8_t(col.r), q[1] = std::uint8_t(col.g), q[2] = std::uint8_t(col.b), q[3] = std::uint8_t(pyRound(c * 255));
            }
        if (p > 0) {
            const std::pair<double, double> pts[3] = {{16, 30}, {27, 41}, {46, 18}};
            const double ls[2] = {std::hypot(11, 11), std::hypot(19, 23)};
            double dist = (ls[0] + ls[1]) * p;
            std::vector<std::pair<double, double>> path{pts[0]};
            for (int i = 0; i < 2; ++i) {
                if (dist <= ls[i]) {
                    const double f = dist / ls[i];
                    path.push_back({mix(pts[i].first, pts[i + 1].first, f), mix(pts[i].second, pts[i + 1].second, f)});
                    break;
                }
                path.push_back(pts[i + 1]);
                dist -= ls[i];
            }
            const Layer l = lineLayer(path, 5, {141, 221, 190});
            composite(im, l.im, l.x, l.y);
        }
        return im;
    }
    void draw(Rgba &f, double t) const override
    {
        const double v = ease(t, 0.12, 0.68) * (1 - ease(t, 4.82, 5.76));
        const double cues[3][2] = {{0.44, 0.97}, {1.08, 1.61}, {1.72, 2.25}};
        for (int i = 0; i < 3; ++i) {
            const double p = ease(t, cues[i][0], cues[i][1]);
            const int y = 310 + i * 143;
            paste(f, p == 1 ? done_ : tick(p), 280, y + 4, v);
            paste(f, tiles_[size_t(i)].im, 280 + 102, y + 12 * (1 - p), v * (.5 + .5 * p));
        }
    }

private:
    std::vector<Tile> tiles_;
    Rgba done_;
};

// ---- 57E Dualcaption: a plate rises with two languages ----------------------------------------------
class Dualcaption : public DraftEffect
{
public:
    Dualcaption(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("medium"), val(a.text, "Every journey begins somewhere."), 55, {249, 246, 236}, 1580);
        b_ = textTile(g, QStringLiteral("regular"), val(a.secondary, "Orice c\xC4\x83""l\xC4\x83""torie \xC3\xAE""ncepe de undeva."), 36, {180, 209, 205}, 1580);
        w_ = std::max(a_.width(), b_.width()) + 2 * 37;
        h_ = a_.height() + b_.height() + 13 + 2 * 20;
        x_ = (FW - w_) / 2, y_ = 936 - h_, ax_ = (FW - a_.width()) / 2, bx_ = (FW - b_.width()) / 2;
        ay_ = y_ + 20, by_ = ay_ + a_.height() + 13;
        pl_ = rect(w_, h_, {28, 37, 39}, 10);
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, 0.10, 0.56), q = ease(t, 0.23, 0.75), r = ease(t, 0.54, 1.12), v = 1 - ease(t, 4.86, 5.76);
        const int top = 936 - pyRound(h_ * p);
        clip(f, pl_, x_, y_, x_, top, x_ + w_, 936, .94 * v);
        clip(f, a_.im, ax_, ay_ + 12 * (1 - q), x_, top, x_ + w_, 936, q * v);
        clip(f, b_.im, bx_, by_ + 10 * (1 - r), x_, top, x_ + w_, 936, r * v);
    }

private:
    Tile a_, b_;
    int w_ = 0, h_ = 0, x_ = 0, y_ = 0, ax_ = 0, bx_ = 0, ay_ = 0, by_ = 0;
    Rgba pl_;
};

// ---- 57F Sidenote: a headline, a rule that opens from its centre, notes wiped in --------------------
class Sidenote : public DraftEffect
{
public:
    Sidenote(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "The details"), 126, {248, 244, 236}, 950);
        const QStringList notes = val(a.secondary, "Small changes.\nLasting impact.").split(QLatin1Char('\n')).mid(0, 3);
        int nh = 0;
        for (const QString &n : notes) {
            ts_.push_back(textTile(g, QStringLiteral("medium"), n, 44, {215, 223, 220}, 560));
            nh += ts_.back().height();
        }
        nh += 14 * (int(ts_.size()) - 1);
        h_ = std::max(a_.height(), nh) + 24;
        rx_ = 180 + a_.width() + 55, nx_ = rx_ + 55;
        top_ = 438 + floorDiv(a_.height() - h_, 2);
        int cursor = top_ + floorDiv(h_ - nh, 2);
        for (const Tile &t : ts_)
            pos_.push_back(cursor), cursor += t.height() + 14;
        rule_ = rect(4, h_, {232, 176, 113});
    }
    static int floorDiv(int a, int b) { return int(std::floor(double(a) / b)); }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, 0.14, 0.82), q = ease(t, 0.66, 1.42), v = 1 - ease(t, 4.84, 5.76);
        paste(f, a_.im, 180 - 20 * (1 - p), 438, p * v);
        const int hh = pyRound(h_ * p);
        clip(f, rule_, rx_, top_, rx_, top_ + pyRound((h_ - hh) / 2.0), rx_ + 4, top_ + pyRound((h_ + hh) / 2.0), v);
        for (size_t i = 0; i < ts_.size(); ++i)
            clip(f, ts_[i].im, nx_, pos_[i], nx_, top_, nx_ + pyRound(ts_[i].width() * q), top_ + h_, v);
    }

private:
    Tile a_;
    std::vector<Tile> ts_;
    std::vector<int> pos_;
    int h_ = 0, rx_ = 0, nx_ = 0, top_ = 0;
    Rgba rule_;
};

// ---- 58A Redact: a cover bar runs out, then uncovers the headline ----------------------------------
class Redact : public DraftEffect
{
public:
    Redact(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("bold"), val(a.text, "The real story"), 140, {246, 243, 234}, 1370);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "BEHIND THE HEADLINES"), 27, {177, 190, 185}, 1300);
        w_ = a_.width() + 24, h_ = a_.height() + 24;
        cover_ = rect(w_, h_, {211, 229, 216}, 1);
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .14, .74), q = ease(t, .84, 1.64), v = 1 - ease(t, 4.83, 5.76);
        const int X = 242, Y = 462, cx = X - 12, cy = Y - 12, right = cx + pyRound(w_ * p), edge = cx + pyRound(w_ * q);
        clip(f, a_.im, X, Y, cx, cy, std::min(edge, right), cy + h_, v);
        clip(f, cover_, cx, cy, edge, cy, right, cy + h_, v);
        paste(f, b_.im, X, Y - 74 + 12 * (1 - p), p * v);
    }

private:
    Tile a_, b_;
    int w_ = 0, h_ = 0;
    Rgba cover_;
};

// ---- 58B Progress: chapter heading over a five-segment indicator filled to the third ---------------
class Progress : public DraftEffect
{
public:
    Progress(const GoldText &g, const DraftActor &a)
    {
        const Rgb ACC{241, 190, 124};
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "Find your direction"), 100, {247, 244, 236}, 1490);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "THE CREATIVE PROCESS"), 29, {188, 199, 201}, 1200);
        c_ = textTile(g, QStringLiteral("medium"), QStringLiteral("03 / 05"), 29, ACC, 200);
        const double sw = (720 - 12 * 4) / 5.0;
        w_ = pyRound(sw);
        for (int i = 0; i < 5; ++i)
            xs_[i] = pyRound(214 + i * (sw + 12));
        dim_ = rect(w_, 7, {72, 79, 83}, 3), acc_ = rect(w_, 7, ACC, 3);
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 214, Y = 376, by = Y + 213, cy = by - 12, cx = X + 720 + 28, ay = Y + 60;
        const double p = ease(t, .14, .82), q = ease(t, .66, 1.62), r = ease(t, .49, 1.24), v = 1 - ease(t, 4.86, 5.76);
        paste(f, b_.im, X, Y + 10 * (1 - p), p * v);
        paste(f, a_.im, X, ay + 28 * (1 - r), r * v);
        for (int i = 0; i < 5; ++i) {
            paste(f, dim_, xs_[i], by, p * v);
            const int fw = pyRound(w_ * clamp01(3 * q - i));
            if (fw > 0)
                clip(f, acc_, xs_[i], by, xs_[i], by, xs_[i] + fw, by + 7, p * v);
        }
        paste(f, c_.im, cx, cy, p * v);
    }

private:
    Tile a_, b_, c_;
    int w_ = 0, xs_[5] = {0, 0, 0, 0, 0};
    Rgba dim_, acc_;
};

// ---- 58C Focal: the line focuses from a blur, the sharp edge travelling left to right ---------------
class Focal : public DraftEffect
{
public:
    Focal(const GoldText &g, const DraftActor &a)
    {
        const Tile t = textTile(g, QStringLiteral("serif"), val(a.text, "Come closer."), 153, {246, 243, 232}, 1440);
        m_ = gold::M8(t.width() + 132, t.height() + 132);
        for (int y = 0; y < t.height(); ++y)
            for (int x = 0; x < t.width(); ++x)
                m_.at(x + 66, y + 66) = t.im.px(x, y)[3];
        blur_ = gaussianBlur(m_, 17);
        x_ = (FW - m_.w) / 2, y_ = (FH - m_.h) / 2;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .65), q = ease(t, .38, 1.84), v = p * (1 - ease(t, 4.83, 5.76));
        const int w = m_.w, h = m_.h;
        Rgba tile(w, h);
        const double edge = -290 + q * (w + 2 * 290);
        for (int x = 0; x < w; ++x) {
            const int k = q == 1 ? 255 : pyRound(255 * clamp01((edge - x) / 290));
            for (int y = 0; y < h; ++y) {
                std::uint8_t *o = tile.px(x, y);
                o[0] = 246, o[1] = 243, o[2] = 232;
                o[3] = q == 1 ? m_.at(x, y) : blend8(m_.at(x, y), blur_.at(x, y), k);
            }
        }
        paste(f, tile, x_, y_, v);
    }

private:
    gold::M8 m_, blur_;
    int x_ = 0, y_ = 0;
};


// ---- 58D Credit: role and name either side of a rule that opens from its centre -----------------------
class Credit : public DraftEffect
{
public:
    Credit(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "Elena Ionescu"), 87, {246, 244, 236}, 800);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "Directed by"), 40, {171, 197, 193}, 590);
        y_ = 568 + a_.box.top - 2, bx_ = 960 - 36 - b_.width(), by_ = 568 + b_.box.top - 2;
        rule_ = rect(3, 108, {213, 176, 121}, 1);
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .13, .82), q = ease(t, .44, 1.24), v = 1 - ease(t, 4.85, 5.76);
        const int rh = 108, ry = 568 - 85, hh = pyRound(rh * p);
        clip(f, rule_, 959, ry, 959, ry + Sidenote::floorDiv(rh - hh, 2), 962, ry + Sidenote::floorDiv(rh + hh, 2), v);
        clip(f, b_.im, bx_ + 74 * (1 - p), by_, 70, ry - 40, 960 - 36 + 1, ry + rh + 40, p * v);
        clip(f, a_.im, 996 - 74 * (1 - q), y_, 960 + 36 - 1, ry - 50, FW - 60, ry + rh + 60, q * v);
    }

private:
    Tile a_, b_;
    int y_ = 0, bx_ = 0, by_ = 0;
    Rgba rule_;
};

// ---- 58E Pricepoint: the old price struck through, the new one rising in -------------------------------
class Pricepoint : public DraftEffect
{
public:
    Pricepoint(const GoldText &g, const DraftActor &a)
    {
        const Rgb ACC{160, 231, 191};
        a_ = textTile(g, QStringLiteral("bold"), val(a.text, "$79"), 166, {249, 244, 232}, 1050);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "$129"), 77, {172, 179, 174}, 700);
        c_ = textTile(g, QStringLiteral("medium"), QStringLiteral("SPECIAL OFFER"), 30, ACC, 900);
        by_ = 320 + 66, ay_ = by_ + b_.height() + 29;
        stroke_ = rect(b_.width() + 10, 5, ACC, 2);
        sy_ = by_ + pyRound(b_.height() * .53);
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 260, Y = 320, sx = X - 5;
        const double p = ease(t, .12, .71), q = ease(t, .78, 1.29), r = ease(t, 1.05, 1.76), v = 1 - ease(t, 4.86, 5.76);
        paste(f, c_.im, X, Y + 12 * (1 - p), p * v);
        paste(f, b_.im, X, by_ + 15 * (1 - p), p * v);
        clip(f, stroke_, sx, sy_, sx, sy_, sx + pyRound(stroke_.w * q), sy_ + 5, v);
        clip(f, a_.im, X, ay_ + 45 * (1 - r), X - 2, ay_ - 3, FW - 80, ay_ + a_.height() + 2, r * v);
    }

private:
    Tile a_, b_, c_;
    int by_ = 0, ay_ = 0, sy_ = 0;
    Rgba stroke_;
};

// ---- 58F Bracket: four corner arms draw themselves and close in on the headline --------------------
class Bracket : public DraftEffect
{
public:
    Bracket(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("bold"), val(a.text, "The next step"), 126, {246, 244, 237}, 1420);
        x_ = (FW - a_.width()) / 2, y_ = (FH - a_.height()) / 2;
    }
    static Rgba corner(double p, int sx, int sy)
    {
        const int sz = 38 + 5 + 4;
        const double o = 6;
        std::vector<std::pair<double, double>> path{{o + 38, o}};
        const double length = 2 * 38 * p;
        if (length <= 38)
            path.push_back({o + 38 - length, o});
        else
            path.push_back({o, o}), path.push_back({o, o + length - 38});
        Rgba im(sz, sz);
        if (p > 0)
            im = intoTile(lineLayer(path, 5, {229, 184, 118}), sz, sz);
        if (sx < 0)
            im = flipH(im);
        if (sy < 0)
            im = flipV(im);
        return im;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .13, .87), q = ease(t, .47, 1.23), v = 1 - ease(t, 4.84, 5.76);
        paste(f, a_.im, x_, y_ + 14 * (1 - q), q * v);
        const int l = x_ - 47, r = x_ + a_.width() + 47, tp = y_ - 34, b = y_ + a_.height() + 34;
        const int cs[4][4] = {{l, tp, 1, 1}, {r, tp, -1, 1}, {l, b, 1, -1}, {r, b, -1, -1}};
        for (const auto &c : cs) {
            const Rgba im = corner(p, c[2], c[3]);
            const int ox = c[2] > 0 ? 6 : im.w - 7, oy = c[3] > 0 ? 6 : im.h - 7;
            paste(f, im, c[0] - ox - c[2] * 66 * (1 - p), c[1] - oy - c[3] * 66 * .5 * (1 - p), v);
        }
    }

private:
    Tile a_;
    int x_ = 0, y_ = 0;
};

// ---- 59A Horizon: a rule swings down to the horizontal under the heading ---------------------------
class Horizon : public DraftEffect
{
public:
    Horizon(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "A new perspective"), 110, {248, 244, 234}, 1380);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "CHAPTER 01"), 28, {165, 190, 188}, 1100);
        ay_ = 414 + a_.height() + 30, length_ = std::min(a_.width(), 680);
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 250, Y = 414;
        const double p = ease(t, .12, .92), q = ease(t, .70, 1.46), v = 1 - ease(t, 4.83, 5.76);
        const double theta = -kPi / 2 * (1 - p), l = length_ * (.15 + .85 * p);
        const int sz = length_ + 16, ox = 7, oy = length_ + 7;
        const double ex = std::nearbyint((ox + l * std::cos(theta)) * 3) / 3, ey = std::nearbyint((oy + l * std::sin(theta)) * 3) / 3;
        const Rgba rule = intoTile(lineLayer({{ox, oy}, {ex, ey}}, 5, {110, 211, 207}), sz, sz);
        paste(f, rule, X - ox, ay_ - oy, p * v);
        paste(f, b_.im, X, Y - 64 + 12 * (1 - p), p * v);
        clip(f, a_.im, X, Y + 34 * (1 - q), X - 2, Y - 3, FW - 50, ay_ - 10, q * v);
    }

private:
    Tile a_, b_;
    int ay_ = 0, length_ = 0;
};

// ---- 59C Echo: two outline echoes slide into the solid headline --------------------------------------
class Echo : public DraftEffect
{
public:
    Echo(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("bold"), val(a.text, "Make it matter."), 147, {249, 246, 239}, 1460);
        gold::M8 m(a_.width() + 22, a_.height() + 22);
        for (int y = 0; y < a_.height(); ++y)
            for (int x = 0; x < a_.width(); ++x)
                m.at(x + 11, y + 11) = a_.im.px(x, y)[3];
        const gold::M8 mx = rankFilter(m, 5, true), mn = rankFilter(m, 3, false);
        o_ = Rgba(m.w, m.h);
        for (int y = 0; y < m.h; ++y)
            for (int x = 0; x < m.w; ++x) {
                std::uint8_t *q = o_.px(x, y);
                q[0] = 131, q[1] = 166, q[2] = 174, q[3] = std::uint8_t(std::max(0, mx.at(x, y) - mn.at(x, y)));
            }
        x_ = (FW - a_.width()) / 2, y_ = (FH - a_.height()) / 2;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .84), q = ease(t, .42, 1.66), v = 1 - ease(t, 4.84, 5.76), d = 52 * (1 - q);
        paste(f, o_, x_ - 11 - d, y_ - 11 + .43 * d, .62 * p * (1 - q) * v);
        paste(f, o_, x_ - 11 + d, y_ - 11 - .43 * d, .48 * p * (1 - q) * v);
        paste(f, a_.im, x_, y_, q * v);
    }

private:
    Tile a_;
    Rgba o_;
    int x_ = 0, y_ = 0;
};

// ---- 59D Dateline: the day, a rule, then month and place --------------------------------------------
class Dateline : public DraftEffect
{
public:
    Dateline(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "26"), 152, {249, 245, 236}, 620);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "NOVEMBER 2026"), 43, {249, 245, 236}, 800);
        c_ = textTile(g, QStringLiteral("medium"), QStringLiteral("TRANSYLVANIA, ROMANIA"), 27, {180, 196, 200}, 800);
        rh_ = std::max(a_.height(), b_.height() + c_.height() + 22);
        rx_ = 220 + a_.width() + 30, bx_ = rx_ + 30;
        by_ = 690 + Sidenote::floorDiv(rh_ - b_.height() - c_.height() - 22, 2), cy_ = by_ + b_.height() + 22;
        rule_ = rect(4, rh_, {224, 178, 106}, 1);
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 220, Y = 690;
        const double p = ease(t, .14, .82), q = ease(t, .52, 1.36), v = 1 - ease(t, 4.85, 5.76);
        const int hh = pyRound(rh_ * p);
        paste(f, a_.im, X - 30 * (1 - p), Y + Sidenote::floorDiv(rh_ - a_.height(), 2), p * v);
        clip(f, rule_, rx_, Y, rx_, Y + Sidenote::floorDiv(rh_ - hh, 2), rx_ + 4, Y + Sidenote::floorDiv(rh_ + hh, 2), v);
        clip(f, b_.im, bx_ - 34 * (1 - q), by_, bx_ - 2, Y - 2, FW - 70, Y + rh_ + 2, q * v);
        paste(f, c_.im, bx_, cy_ + 10 * (1 - q), q * v);
    }

private:
    Tile a_, b_, c_;
    int rh_ = 0, rx_ = 0, bx_ = 0, by_ = 0, cy_ = 0;
    Rgba rule_;
};


// ---- 59E Capsule: a light tag and a dark topic opening from one seam --------------------------------
class Capsule : public DraftEffect
{
public:
    Capsule(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("medium"), val(a.text, "Better by design"), 55, {242, 247, 245}, 940);
        b_ = textTile(g, QStringLiteral("bold"), val(a.secondary, "IN FOCUS"), 27, {25, 54, 51}, 360);
        w1_ = b_.width() + 60, w2_ = a_.width() + 60 + 14, seam_ = 196 + w1_;
        left_ = rect(w1_ + 24, 100, {166, 228, 207}, 22), right_ = rect(w2_ + 24, 100, {35, 56, 62}, 22);
        ax_ = seam_ + 30, ay_ = 803 + Sidenote::floorDiv(100 - a_.height(), 2), bx_ = 196 + 30, by_ = 803 + Sidenote::floorDiv(100 - b_.height(), 2);
    }
    void draw(Rgba &f, double t) const override
    {
        const int Y = 803, h = 100;
        const double p = ease(t, .14, .86), q = ease(t, .45, 1.32), v = 1 - ease(t, 4.84, 5.76);
        const int l = pyRound(w1_ * p), r = pyRound(w2_ * q);
        clip(f, right_, seam_ - 24, Y, seam_, Y, seam_ + r, Y + h, q * v);
        clip(f, left_, 196, Y, seam_ - l, Y, seam_, Y + h, p * v);
        clip(f, b_.im, bx_, by_, seam_ - l, Y, seam_, Y + h, p * v);
        clip(f, a_.im, ax_, ay_, seam_, Y, seam_ + r, Y + h, q * v);
    }

private:
    Tile a_, b_;
    int w1_ = 0, w2_ = 0, seam_ = 0, ax_ = 0, ay_ = 0, bx_ = 0, by_ = 0;
    Rgba left_, right_;
};

// ---- 59F Dropcap: a large initial, the rest of the title and a rule beside it ----------------------
class Dropcap : public DraftEffect
{
public:
    Dropcap(const GoldText &g, const DraftActor &a)
    {
        const QList<uint> cp = val(a.text, "Stories worth telling").normalized(QString::NormalizationForm_C).toUcs4();
        int i = 1;
        while (i < cp.size() && QChar::combiningClass(char32_t(cp[i])) != 0)
            ++i;
        auto sub = [&](int from, int to) { return QString::fromUcs4(reinterpret_cast<const char32_t *>(cp.constData()) + from, std::max(0, to - from)); };
        const Rgb CAP{226, 181, 122};
        a_ = textTile(g, QStringLiteral("serif"), sub(0, i), 236, CAP, 420);
        const QString rest = sub(i, int(cp.size()));
        b_ = textTile(g, QStringLiteral("serif"), rest.isEmpty() ? QStringLiteral(" ") : rest, 87, {248, 244, 236}, 1100);
        c_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "THE ART OF EVERYDAY LIFE"), 29, {172, 187, 187}, 1100);
        bx_ = 272 + a_.width() + 18, by_ = 384 + 25, cy_ = by_ + b_.height() + 37;
        rw_ = std::min(std::max(b_.width(), c_.width()), 630);
        rule_ = rect(rw_, 3, CAP, 1);
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 272, Y = 384;
        const double p = ease(t, .13, .95), q = ease(t, .52, 1.48), v = 1 - ease(t, 4.83, 5.76);
        paste(f, a_.im, X, Y + 25 * (1 - p), p * v);
        clip(f, b_.im, bx_ - 44 * (1 - q), by_, bx_ - 2, Y - 10, FW - 65, Y + a_.height() + 70, q * v);
        clip(f, rule_, bx_, cy_ - 15, bx_, cy_ - 15, bx_ + pyRound(rw_ * q), cy_ - 12, v);
        paste(f, c_.im, bx_, cy_ + 12 * (1 - q), q * v);
    }

private:
    Tile a_, b_, c_;
    int bx_ = 0, by_ = 0, cy_ = 0, rw_ = 0;
    Rgba rule_;
};

// ---- 60A Interpose: the phrase parts and a qualifier rises into the gap ----------------------------
class Interpose : public DraftEffect
{
public:
    Interpose(const GoldText &g, const DraftActor &a)
    {
        const Rgb INK{248, 245, 239};
        a_ = textTile(g, QStringLiteral("medium"), val(a.text, "See"), 130, INK, 440);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "more."), 130, INK, 500);
        c_ = textTile(g, QStringLiteral("medium"), QStringLiteral("a little"), 130, {126, 220, 203}, 430);
        ys_[0] = 590 + a_.box.top - 2, ys_[1] = 590 + b_.box.top - 2, ys_[2] = 590 + c_.box.top - 2;
        const int w0 = a_.width() + 34 + b_.width(), w1 = w0 + c_.width() + 34;
        x0_ = (FW - w0) / 2, x1_ = (FW - w1) / 2, bx0_ = x0_ + a_.width() + 34;
        const int cx = x1_ + a_.width() + 34;
        bx1_ = cx + c_.width() + 34;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .76), q = ease(t, .98, 1.86), v = 1 - ease(t, 4.88, 5.76);
        const double ax = mix(x0_, x1_, q), bx = mix(bx0_, bx1_, q), mid = ax + a_.width() + 34;
        paste(f, a_.im, ax, ys_[0] + 20 * (1 - p), p * v);
        paste(f, b_.im, bx, ys_[1] + 20 * (1 - p), p * v);
        clip(f, c_.im, mid, ys_[2] + 16 * (1 - q), mid - 1, 0, std::max(mid - 1, bx - 34), FH, q * v);
    }

private:
    Tile a_, b_, c_;
    int ys_[3] = {0, 0, 0}, x0_ = 0, x1_ = 0, bx0_ = 0, bx1_ = 0;
};

// ---- 60C Reanchor: a centred name shrinks into a lower third, the role follows ---------------------
class Reanchor : public DraftEffect
{
public:
    Reanchor(const GoldText &g, const DraftActor &a)
    {
        const QString txt = val(a.text, "Elena Ionescu");
        for (int s = 67; s <= 126; ++s)
            tiles_.emplace_back(textTile(g, QStringLiteral("medium"), txt, s, {250, 247, 240}, s > 67 ? 1350 : 1100), Rgb{250, 247, 240});
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "DESIGN DIRECTOR"), 28, {146, 218, 203}, 1100);
        by_ = 807 + tiles_.front().height() + 22;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .76), q = ease(t, 1.12, 2.12), r = ease(t, 1.70, 2.26), v = 1 - ease(t, 4.88, 5.76);
        const MaskTile &tile = tiles_[size_t(pyRound(mix(126, 67, q)) - 67)];
        const double x = mix((FW - tile.width()) / 2.0, 182, q), y = mix((FH - tile.height()) / 2.0, 807, q);
        paste(f, tile.image(), x, y + 18 * (1 - p), p * v);
        clip(f, b_.im, 182, by_ + 12 * (1 - r), 180, by_ - 1, FW - 100, FH - 85, r * v);
    }

private:
    std::vector<MaskTile> tiles_;
    Tile b_;
    int by_ = 0;
};

// ---- 60D Between: two lines part and a small line appears between them -----------------------------
class Between : public DraftEffect
{
public:
    Between(const GoldText &g, const DraftActor &a)
    {
        const Rgb INK{246, 242, 232};
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "The details"), 105, INK, 1360);
        b_ = textTile(g, QStringLiteral("serif"), val(a.secondary, "make the difference."), 105, INK, 1360);
        c_ = textTile(g, QStringLiteral("medium"), QStringLiteral("IN EVERY FRAME"), 25, {230, 171, 120}, 900);
        const int h0 = a_.height() + b_.height() + 22, h1 = a_.height() + b_.height() + c_.height() + 62;
        y0_ = (FH - h0) / 2, y1_ = (FH - h1) / 2, by0_ = y0_ + a_.height() + 22;
        const int cy1 = y1_ + a_.height() + 31;
        by1_ = cy1 + c_.height() + 31;
        ax_ = (FW - a_.width()) / 2, bx_ = (FW - b_.width()) / 2, cx_ = (FW - c_.width()) / 2;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .86), q = ease(t, .98, 1.94), v = 1 - ease(t, 4.88, 5.76);
        const double ay = mix(y0_, y1_, q), by = mix(by0_, by1_, q), cy = (ay + a_.height() + by - c_.height()) / 2;
        paste(f, a_.im, ax_, ay + 12 * (1 - p), p * v);
        paste(f, b_.im, bx_, by + 12 * (1 - p), p * v);
        clip(f, c_.im, cx_, cy, 0, ay + a_.height() + 6, FW, by - 6, q * v);
    }

private:
    Tile a_, b_, c_;
    int y0_ = 0, y1_ = 0, by0_ = 0, by1_ = 0, ax_ = 0, bx_ = 0, cx_ = 0;
};

// ---- 61A Gloss: a term, a rule and a two-line definition ------------------------------------------
class Gloss : public DraftEffect
{
public:
    Gloss(const GoldText &g, const DraftActor &a)
    {
        const Rgb SEC{213, 220, 222};
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "Perspective"), 112, {249, 246, 239}, 1300);
        b_ = textTile(g, QStringLiteral("regular"), val(a.secondary, "A different way of seeing"), 49, SEC, 1280);
        c_ = textTile(g, QStringLiteral("regular"), QStringLiteral("the same thing."), 49, SEC, 1280);
        ry_ = 355 + a_.height() + 26, by_ = ry_ + 26 + 4, cy_ = by_ + b_.height() + 12, rw_ = std::max(b_.width(), c_.width());
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 290, Y = 355;
        const double p = ease(t, .12, .78), q = ease(t, .48, 1.04), r = ease(t, .81, 1.45), s = ease(t, 1.01, 1.64), v = 1 - ease(t, 4.92, 5.76);
        paste(f, a_.im, X, Y + 16 * (1 - p), p * v);
        if (q > 0)
            paste(f, rect(std::max(1, pyRound(rw_ * q)), 3, {125, 208, 196}), X, ry_, v);
        clip(f, b_.im, X, by_ + 17 * (1 - r), X - 2, by_ - 1, FW - 120, FH, r * v);
        clip(f, c_.im, X, cy_ + 17 * (1 - s), X - 2, cy_ - 1, FW - 120, FH, s * v);
    }

private:
    Tile a_, b_, c_;
    int ry_ = 0, by_ = 0, cy_ = 0, rw_ = 0;
};


// ---- 56A Waypoint: a marker travels a schematic route between two places ---------------------------
class Waypoint : public DraftEffect
{
public:
    Waypoint(const GoldText &g, const DraftActor &a)
    {
        const Rgb INK{248, 245, 237}, ACC{244, 186, 111};
        left_ = textTile(g, QStringLiteral("bold"), val(a.text, "Bra\xC8\x99""ov"), 105, INK, 740);
        right_ = textTile(g, QStringLiteral("bold"), val(a.secondary, "Sibiu"), 105, INK, 740);
        tag_ = textTile(g, QStringLiteral("medium"), QStringLiteral("DEPARTURE"), 34, ACC);
        tag2_ = textTile(g, QStringLiteral("medium"), QStringLiteral("ARRIVAL"), 34, ACC);
        const double P[6][2] = {{270, 515}, {650, 515}, {835, 382}, {1085, 382}, {1280, 515}, {1650, 515}};
        pts_ = {{P[0][0], P[0][1]}, {P[1][0], P[1][1]}};
        for (int i = 1; i <= 160; ++i) {
            const double u = i / 160.0, v = 1 - u;
            pts_.push_back({v * v * v * P[1][0] + 3 * v * v * u * P[2][0] + 3 * v * u * u * P[3][0] + u * u * u * P[4][0],
                            v * v * v * P[1][1] + 3 * v * v * u * P[2][1] + 3 * v * u * u * P[3][1] + u * u * u * P[4][1]});
        }
        pts_.push_back({P[5][0], P[5][1]});
        ds_ = {0.0};
        for (size_t i = 1; i < pts_.size(); ++i)
            ds_.push_back(ds_.back() + std::hypot(pts_[i].first - pts_[i - 1].first, pts_[i].second - pts_[i - 1].second));
        full_ = route(1.0);
    }
    Rgba route(double p) const
    {
        const double ox = 240, oy = 340;
        const double d = p * ds_.back();
        size_t j = ds_.size() - 1;
        for (size_t i = 1; i < ds_.size(); ++i)
            if (ds_[i] >= d) {
                j = i;
                break;
            }
        const double f = (d - ds_[j - 1]) / (ds_[j] - ds_[j - 1]);
        const std::pair<double, double> q{mix(pts_[j - 1].first, pts_[j].first, f), mix(pts_[j - 1].second, pts_[j].second, f)};
        auto local = [&](std::vector<std::pair<double, double>> v) {
            for (auto &e : v)
                e.first -= ox, e.second -= oy;
            return snap3(v);
        };
        const Rgb ACC{244, 186, 111}, DIM{94, 103, 112};
        Rgba im(1440, 205);
        Coverage dim(0, 0, im.w, im.h);
        dim.polyline(local(pts_), 2.5);
        dim.paintOver(im, 0, 0, DIM);
        if (p > 0) {
            std::vector<std::pair<double, double>> part(pts_.begin(), pts_.begin() + long(j));
            part.push_back(q);
            Coverage acc(0, 0, im.w, im.h);
            acc.polyline(local(part), 2.5);
            acc.paintOver(im, 0, 0, ACC);
        }
        const struct { double x, y, r; Rgb c; } discs[3] = {{270, 515, 8, ACC}, {1650, 515, 8, p >= 1 ? ACC : DIM}, {q.first, q.second, 12, ACC}};
        for (const auto &e : discs) {
            Coverage c(0, 0, im.w, im.h);
            c.disc(e.x - ox, e.y - oy, e.r + 1 / 6.0);
            c.paintOver(im, 0, 0, e.c);
        }
        return im;
    }
    void draw(Rgba &f, double t) const override
    {
        const double e = ease(t, 0.12, 0.72), o = ease(t, 4.94, 5.76), al = e * (1 - o), p = ease(t, 0.70, 2.65), d = ease(t, 2.03, 2.70);
        const int dy = pyRound(18 * (1 - e) - 12 * o);
        paste(f, p >= 1 ? full_ : route(p), 240, 340 + dy, al);
        paste(f, tag_.im, 270, 570 + dy, al);
        paste(f, left_.im, 270, 625 + dy, al);
        paste(f, tag2_.im, 1650 - tag2_.width(), 570 + dy, al * d);
        paste(f, right_.im, 1650 - right_.width(), 625 + dy + pyRound(12 * (1 - d)), al * d);
    }

private:
    Tile left_, right_, tag_, tag2_;
    std::vector<std::pair<double, double>> pts_;
    std::vector<double> ds_;
    Rgba full_;
};

// ---- 56D Pinpoint: a leader runs from an anchor point to a panel that opens upward ---------------------
class Pinpoint : public DraftEffect
{
public:
    Pinpoint(const GoldText &g, const DraftActor &a)
    {
        title_ = textTile(g, QStringLiteral("bold"), val(a.text, "Optical stabilization"), 77, {247, 246, 240}, 965);
        sub_ = textTile(g, QStringLiteral("regular"), val(a.secondary, "Reduce unwanted camera movement"), 39, {191, 203, 205}, 965);
        bw_ = std::max(title_.width(), sub_.width()) + 72, bh_ = 72 + title_.height() + 22 + sub_.height();
        const int right = 210 + bw_;
        pts_ = {{1330, 350}, {1120, 520}, {double(std::min(right - 32, std::max(210 + 32, 1120))), 548}};
        lens_[0] = std::hypot(pts_[1].first - pts_[0].first, pts_[1].second - pts_[0].second);
        lens_[1] = std::hypot(pts_[2].first - pts_[1].first, pts_[2].second - pts_[1].second);
        panel_ = Rgba(bw_, bh_);
        for (int y = 0; y < bh_; ++y)
            for (int x = 0; x < bw_; ++x) {
                const double c = rrectCoverage(x + .5, y + .5, 0, 0, bw_, bh_, 12);
                if (c <= 0)
                    continue;
                std::uint8_t *q = panel_.px(x, y);
                q[0] = 20, q[1] = 29, q[2] = 33, q[3] = std::uint8_t(pyRound(239 * c));
            }
        full_ = trace(1.0);
    }
    Rgba trace(double p) const
    {
        double distance = (lens_[0] + lens_[1]) * p;
        std::vector<std::pair<double, double>> ps{pts_[0]};
        for (int i = 0; i < 2; ++i) {
            const double l = lens_[i], f = l ? clamp01(distance / l) : 1.0;
            ps.push_back({mix(pts_[size_t(i)].first, pts_[size_t(i) + 1].first, f), mix(pts_[size_t(i)].second, pts_[size_t(i) + 1].second, f)});
            distance -= l;
            if (distance <= 0)
                break;
        }
        for (auto &e : ps)
            e.first -= 180, e.second -= 310;
        ps = snap3(ps);
        Rgba im(1190, 270);
        Coverage c(0, 0, im.w, im.h);
        c.polyline(ps, 2.0);
        c.disc(ps[0].first, ps[0].second, 9 + 1 / 6.0);
        c.paintOver(im, 0, 0, {132, 217, 223});
        return im;
    }
    void draw(Rgba &f, double t) const override
    {
        const double o = ease(t, 4.94, 5.76), a = 1 - o, e = ease(t, .12, .55), p = ease(t, .38, 1.24), g = ease(t, .92, 1.58), h = ease(t, 1.18, 1.83);
        paste(f, p >= 1 ? full_ : trace(p), 180, 310, a * e);
        const int ch = pyRound(bh_ * g);
        if (ch)
            paste(f, crop(panel_, 0, 0, bw_, ch), 210, 548, a);
        const std::pair<const Tile *, int> rows[2] = {{&title_, 548 + 36}, {&sub_, 548 + 36 + title_.height() + 22}};
        for (const auto &r : rows) {
            const int v = std::min(r.first->height(), std::max(0, 548 + ch - r.second));
            if (v)
                paste(f, crop(r.first->im, 0, 0, r.first->width(), v), 210 + 36, r.second, a * h);
        }
    }

private:
    Tile title_, sub_;
    int bw_ = 0, bh_ = 0;
    std::vector<std::pair<double, double>> pts_;
    double lens_[2] = {0, 0};
    Rgba panel_, full_;
};

// ---- 61B Signpost: a destination, an arrow growing beside it, a tag below --------------------------
class Signpost : public DraftEffect
{
public:
    Signpost(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("medium"), val(a.text, "Explore more"), 78, {247, 244, 234}, 1000);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "FOLLOW THE COAST"), 27, {240, 191, 121}, 1200);
        ay_ = 770 + a_.height() / 2.0, ax_ = 155 + a_.width() + 30, by_ = 770 + a_.height() + 21;
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 155, Y = 770;
        const double p = ease(t, .12, .72), q = ease(t, .62, 1.34), s = ease(t, .8, 1.43), v = 1 - ease(t, 4.92, 5.76);
        paste(f, a_.im, X - 22 * (1 - p), Y, p * v);
        paste(f, b_.im, X, by_ + 10 * (1 - s), s * v);
        if (q > 0) {
            const double h = 18 * ease(q, .56, 1), yy = 26, tt = 8 + 116 * q;
            Rgba tile(132, 52);
            Coverage c(0, 0, tile.w, tile.h);
            c.polyline(snap3({{8, yy}, {tt, yy}}), 2.0);
            c.polyline(snap3({{tt - h, yy - h}, {tt, yy}, {tt - h, yy + h}}), 2.0);
            c.paintOver(tile, 0, 0, {240, 191, 121});
            paste(f, tile, ax_ - 8, ay_ - 18 - 8, v);
        }
    }

private:
    Tile a_, b_;
    double ay_ = 0;
    int ax_ = 0, by_ = 0;
};

// ---- 61C Parallel: two matched labels slide apart from a centre rule ----------------------------------
class Parallel : public DraftEffect
{
public:
    Parallel(const GoldText &g, const DraftActor &a)
    {
        const Rgb INK{245, 245, 240};
        a_ = textTile(g, QStringLiteral("medium"), val(a.text, "Before"), 88, INK, 700);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "After"), 88, INK, 700);
        l_ = textTile(g, QStringLiteral("medium"), QStringLiteral("ORIGINAL"), 27, {173, 195, 218}, 690);
        r_ = textTile(g, QStringLiteral("medium"), QStringLiteral("REFINED"), 27, {129, 218, 184}, 690);
        ly_ = 454 + std::max(a_.height(), b_.height()) + 28;
        cy_ = (454 + ly_ + std::max(l_.height(), r_.height())) / 2.0;
    }
    void draw(Rgba &f, double t) const override
    {
        const int CX = 960, Y = 454, G = 72;
        const double q = ease(t, .08, .75), p = ease(t, .24, 1.02), s = ease(t, .68, 1.35), v = 1 - ease(t, 4.92, 5.76);
        if (q > 0)
            paste(f, rect(2, std::max(1, pyRound(178 * q)), {170, 180, 185}), CX - 1, cy_ - 178 * q / 2, v);
        clip(f, a_.im, CX - G - a_.width() + 56 * (1 - p), Y, 0, 0, CX - G, FH, p * v);
        clip(f, b_.im, CX + G - 56 * (1 - p), Y, CX + G, 0, FW, FH, p * v);
        clip(f, l_.im, CX - G - l_.width() + 24 * (1 - s), ly_, 0, 0, CX - G, FH, s * v);
        clip(f, r_.im, CX + G - 24 * (1 - s), ly_, CX + G, 0, FW, FH, s * v);
    }

private:
    Tile a_, b_, l_, r_;
    int ly_ = 0;
    double cy_ = 0;
};

// ---- 61D Cutline: a restrained caption matte grows from the bottom left -------------------------------
class Cutline : public DraftEffect
{
public:
    Cutline(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("regular"), val(a.text, "A quieter kind of morning."), 54, {248, 247, 240}, 1460);
        ww_ = a_.width() + 52, hh_ = a_.height() + 40, top_ = 946 - hh_;
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 150, B = 946;
        const double p = ease(t, .12, .72), q = ease(t, .44, 1.12), v = 1 - ease(t, 4.92, 5.76);
        const int h = std::max(1, pyRound(hh_ * p)), w = std::max(1, pyRound(ww_ * ease(t, .12, .48)));
        if (p > 0) {
            paste(f, rect(w, h, {23, 33, 40}), X, B - h, v);
            paste(f, rect(w, 3, {135, 218, 201}), X, B - 3, v);
        }
        clip(f, a_.im, X + 26, top_ + 20 + 14 * (1 - q), X + 26, B - h, X + w - 26, B - 3, q * v);
    }

private:
    Tile a_;
    int ww_ = 0, hh_ = 0, top_ = 0;
};

// ---- 61E Focuslist: three topics, a cursor moving the emphasis down them -------------------------------
class Focuslist : public DraftEffect
{
public:
    Focuslist(const GoldText &g, const DraftActor &a)
    {
        const QString rows[3] = {val(a.text, "Plan with intention"), val(a.secondary, "Create with clarity"), QStringLiteral("Finish with confidence")};
        for (int i = 0; i < 3; ++i) {
            Tile on = textTile(g, QStringLiteral("medium"), rows[i], 69, {248, 246, 240}, 1300);
            Tile off = textTile(g, QStringLiteral("medium"), rows[i], on.size, {119, 132, 143}, 1300);
            rows_.push_back({on.im, off.im, 344 + i * 118 + Sidenote::floorDiv(118 - on.height(), 2)});
        }
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .76), q = ease(t, 1.04, 1.53) + ease(t, 2.0, 2.51), v = 1 - ease(t, 4.92, 5.76);
        for (size_t i = 0; i < rows_.size(); ++i) {
            const double w = std::max(0.0, 1 - std::abs(q - double(i)));
            paste(f, blendImg(rows_[i].off, rows_[i].on, float(w)), 352 - 15 * (1 - p), rows_[i].y, p * v);
        }
        paste(f, rect(5, 61, {244, 188, 110}), 352 - 38, 344 + 118 / 2.0 - 61 / 2.0 + 118 * q, p * v);
    }

private:
    struct Row { Rgba on, off; int y; };
    std::vector<Row> rows_;
};


// ---- 62A Compose: the line types itself in, glyph cluster by glyph cluster, behind a cursor ------------
class Compose : public DraftEffect
{
public:
    Compose(const GoldText &g, const DraftActor &a)
    {
        const QList<uint> cp = val(a.text, "Make it matter.").toUcs4();
        const QString txt = QString::fromUcs4(reinterpret_cast<const char32_t *>(cp.constData()), cp.size());
        a_ = textTile(g, QStringLiteral("medium"), txt, 116, {249, 245, 236}, 1340);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "A STORY WORTH TELLING"), 27, {241, 183, 112}, 1340);
        for (int i = 0; i < cp.size(); ++i)
            if (i + 1 == cp.size() || QChar::combiningClass(char32_t(cp[i + 1])) == 0)
                ends_.push_back(i + 1);
        adv_.push_back(0);
        for (int e : ends_)
            adv_.push_back(pyRound(g.length(QStringLiteral("medium"), a_.size, QString::fromUcs4(reinterpret_cast<const char32_t *>(cp.constData()), e)) - a_.box.left + 2));
        adv_.back() = a_.width();
        by_ = 430 + a_.height() + 38;
    }
    void draw(Rgba &f, double t) const override
    {
        const double u = clamp01((t - .12) / (1.52 - .12));
        const int k = std::min(int(ends_.size()), int(u * ends_.size() + 1e-9)), cut = adv_[size_t(k)];
        const double v = 1 - ease(t, 4.92, 5.76), p = ease(t, 1.45, 2.0);
        if (cut)
            paste(f, crop(a_.im, 0, 0, cut, a_.height()), 240, 430, v);
        const double ca = ease(t, .12, .24) * (1 - ease(t, 1.52, 1.9)) * v;
        if (ca > 0)
            paste(f, rect(4, a_.height() + 14, {241, 183, 112}), 240 + cut + 12, 430 - 7, ca);
        paste(f, b_.im, 240, by_ + 12 * (1 - p), p * v);
    }

private:
    Tile a_, b_;
    std::vector<int> ends_, adv_;
    int by_ = 0;
};

// ---- 62B Lumen: a warm light sweeps across the headline's ink ---------------------------------------
class Lumen : public DraftEffect
{
public:
    Lumen(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "Into the light"), 145, {222, 225, 221}, 1540);
        b_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "A NEW PERSPECTIVE"), 27, {150, 171, 177}, 1450);
        ax_ = (FW - a_.width()) / 2.0, bx_ = (FW - b_.width()) / 2.0, by_ = 425 + a_.height() + 38;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .72), q = ease(t, 1.6, 2.12), v = 1 - ease(t, 4.92, 5.76);
        Rgba tile = a_.im;
        if (.55 < t && t < 1.95) {
            const double u = clamp01((t - .55) / 1.4), center = -360 + (a_.width() + 720) * u, strength = std::pow(std::sin(kPi * u), .45);
            const double INK[3] = {222, 225, 221}, HI[3] = {255, 230, 174};
            for (int x = 0; x < tile.w; ++x) {
                const double k = strength * std::exp(-.5 * std::pow((x - center) / 120, 2));
                std::uint8_t c[3];
                for (int i = 0; i < 3; ++i)
                    c[i] = std::uint8_t(pyRound(mix(INK[i], HI[i], k)));
                for (int y = 0; y < tile.h; ++y)
                    std::copy_n(c, 3, tile.px(x, y));
            }
        }
        paste(f, tile, ax_, 425 + 10 * (1 - p), p * v);
        paste(f, b_.im, bx_, by_ + 10 * (1 - q), q * v);
    }

private:
    Tile a_, b_;
    double ax_ = 0, bx_ = 0;
    int by_ = 0;
};

// ---- 62C Stack: two lines rise into fixed slots, the second just behind --------------------------------
class Stack : public DraftEffect
{
public:
    Stack(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("bold"), val(a.text, "Start here."), 130, {246, 244, 237}, 1430);
        b_ = textTile(g, QStringLiteral("bold"), val(a.secondary, "Go further."), 130, {157, 223, 203}, 1430);
        by_ = 374 + a_.height() + 18;
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 236, Y = 374;
        const double p = ease(t, .12, .83), q = ease(t, .36, 1.15), v = 1 - ease(t, 4.92, 5.76);
        clip(f, a_.im, X, Y + 96 * (1 - p), X, Y, X + a_.width(), Y + a_.height(), p * v);
        clip(f, b_.im, X, by_ + 96 * (1 - q), X, by_, X + b_.width(), by_ + b_.height(), q * v);
    }

private:
    Tile a_, b_;
    int by_ = 0;
};

// ---- 62D Footnote: a headline, its reference mark, a rule and the source ------------------------------
class Footnote : public DraftEffect
{
public:
    Footnote(const GoldText &g, const DraftActor &a)
    {
        const Rgb ACC{232, 185, 124};
        a_ = textTile(g, QStringLiteral("medium"), val(a.text, "Every detail counts."), 96, {245, 243, 237}, 1350);
        b_ = textTile(g, QStringLiteral("regular"), val(a.secondary, "Source: field notes, 2026"), 35, {187, 195, 200}, 1400);
        m_ = textTile(g, QStringLiteral("medium"), QStringLiteral("1"), 35, ACC, 100);
        n_ = textTile(g, QStringLiteral("medium"), QStringLiteral("1"), 35, ACC, 100);
        mx_ = 230 + a_.width() + 9, ry_ = 415 + a_.height() + 34, by_ = ry_ + 24, bx_ = 230 + n_.width() + 16;
        rw_ = std::min(160, b_.width() + n_.width() + 16);
    }
    void draw(Rgba &f, double t) const override
    {
        const int X = 230, Y = 415;
        const double p = ease(t, .12, .78), q = ease(t, .68, 1.12), r = ease(t, .9, 1.38), s = ease(t, 1.15, 1.82), v = 1 - ease(t, 4.92, 5.76);
        paste(f, a_.im, X, Y + 14 * (1 - p), p * v);
        paste(f, m_.im, mx_, Y - 10 + 10 * (1 - q), q * v);
        if (r > 0)
            paste(f, rect(std::max(1, pyRound(rw_ * r)), 2, {232, 185, 124}), X, ry_, v);
        paste(f, n_.im, X, by_ + 10 * (1 - s), s * v);
        paste(f, b_.im, bx_, by_ + 10 * (1 - s), s * v);
    }

private:
    Tile a_, b_, m_, n_;
    int mx_ = 0, ry_ = 0, by_ = 0, bx_ = 0, rw_ = 0;
};

// ---- 63A Baseline: words drop and rise alternately onto one shared baseline -----------------------------
class Baseline : public DraftEffect
{
public:
    Baseline(const GoldText &g, const DraftActor &a)
    {
        const QStringList w = words(val(a.text, "Make every moment count."));
        if (w.isEmpty())
            return;
        int size = 110;
        std::vector<Tile> tiles;
        double total = 0;
        while (true) {
            tiles.clear();
            total = 0;
            for (int i = 0; i < w.size(); ++i) {
                tiles.push_back(textTile(g, QStringLiteral("medium"), w[i], size, i == w.size() - 1 ? Rgb{163, 213, 200} : Rgb{246, 244, 237}, 1460));
                total += tiles.back().width();
            }
            total += 29.0 * (w.size() - 1);
            if (total <= 1460)
                break;
            if (--size < 28)
                return;
        }
        double x = (FW - total) / 2;
        const double n = std::max(1, int(w.size()) - 1);
        for (int i = 0; i < w.size(); ++i) {
            Tile &a = tiles[size_t(i)];
            items_.push_back({a.im, x, double(590 + a.box.top - 2), 64.0 * (i % 2 == 0 ? 1 : -1), .12 + .36 * i / n, .92 + .36 * i / n});
            x += a.width() + 29;
        }
    }
    void draw(Rgba &f, double t) const override
    {
        const double v = 1 - ease(t, 4.92, 5.76);
        for (const Item &it : items_) {
            const double p = ease(t, it.c0, it.c1);
            paste(f, it.im, it.x, it.y + it.off * (1 - p), p * v);
        }
    }

private:
    struct Item { Rgba im; double x, y, off, c0, c1; };
    std::vector<Item> items_;
};

// ---- 63B Underscore: a hand-drawn underline traced under the headline -----------------------------------
class Underscore : public DraftEffect
{
public:
    Underscore(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("serif"), val(a.text, "Worth remembering."), 133, {246, 241, 230}, 1450);
        x_ = (FW - a_.width()) / 2.0, y_ = 418 + a_.height() + 26;
        const double wid = a_.width() - 10;
        cum_ = {0.0};
        for (int i = 0; i < 121; ++i) {
            const double u = i / 120.0, v = 1 - u;
            pts_.push_back({x_ + 5 + wid * u, y_ + 3 * v * v * v - 18 * 3 * v * v * u + 16 * 3 * v * u * u - 2 * u * u * u});
            if (i)
                cum_.push_back(cum_.back() + std::hypot(pts_[size_t(i)].first - pts_[size_t(i) - 1].first, pts_[size_t(i)].second - pts_[size_t(i) - 1].second));
        }
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, .90), q = ease(t, .82, 1.65), v = 1 - ease(t, 4.92, 5.76);
        paste(f, a_.im, x_, 418 + 13 * (1 - p), p * v);
        if (q > 0 && v > 0) {
            const double target = cum_.back() * q;
            std::vector<std::pair<double, double>> ls{pts_[0]};
            for (size_t i = 1; i < pts_.size(); ++i) {
                if (cum_[i] <= target) {
                    ls.push_back(pts_[i]);
                    continue;
                }
                const double fr = (target - cum_[i - 1]) / (cum_[i] - cum_[i - 1]);
                ls.push_back({mix(pts_[i - 1].first, pts_[i].first, fr), mix(pts_[i - 1].second, pts_[i].second, fr)});
                break;
            }
            const int l = pyRound(x_) - 10, top = pyRound(y_) - 30;
            for (auto &e : ls)
                e.first -= l, e.second -= top;
            ls = snap3(ls);
            Rgba im(a_.width() + 20, 60);
            Coverage c(0, 0, im.w, im.h);
            c.polyline(ls, 2.5);
            c.disc(ls.front().first, ls.front().second, 2.5);
            c.disc(ls.back().first, ls.back().second, 2.5);
            c.paintOver(im, 0, 0, {241, 183, 121});
            paste(f, im, l, top, v);
        }
    }

private:
    Tile a_;
    double x_ = 0;
    int y_ = 0;
    std::vector<std::pair<double, double>> pts_;
    std::vector<double> cum_;
};


// ---- 63C Ratio (smart): a percentage counts up with its bar; secondary is the value 0…100 --------------
class Ratio : public DraftEffect
{
public:
    Ratio(const GoldText &g, const DraftActor &a)
    {
        bool ok = false;
        target_ = val(a.secondary, "72").trimmed().toInt(&ok);
        if (!ok || target_ < 0 || target_ > 100)
            return;
        b_ = textTile(g, QStringLiteral("regular"), val(a.text, "project completed"), 52, {245, 245, 239}, 1350);
        int hmax = 0;
        for (int i = 0; i <= target_; ++i) {
            digits_.emplace_back(textTile(g, QStringLiteral("medium"), QString::number(i) + QLatin1Char('%'), 164, {128, 213, 190}, 700), Rgb{128, 213, 190});
            hmax = std::max(hmax, digits_.back().box.bottom - digits_.back().box.top);
        }
        base_ = 345 + hmax + 20, by_ = base_ + 20, ry_ = by_ + b_.height() + 42;
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double p = ease(t, .12, .65), q = ease(t, .52, 1.72), v = 1 - ease(t, 4.92, 5.76);
        const MaskTile &n = digits_[size_t(pyRound(target_ * q))];
        paste(f, n.image(), 270, base_ + n.box.top - 2, p * v);
        paste(f, b_.im, 270, by_ + 10 * (1 - p), p * v);
        paste(f, rect(1350, 12, {85, 100, 108}, 6), 270, ry_, p * v);
        const int w = pyRound(1350.0 * target_ * q / 100);
        if (w > 0)
            paste(f, rect(w, 12, {128, 213, 190}, std::min(6, w / 2)), 270, ry_, p * v);
    }

private:
    bool ok_ = false;
    int target_ = 0, base_ = 0, by_ = 0, ry_ = 0;
    Tile b_;
    std::vector<MaskTile> digits_;
};

// ---- 63D Shutter: the upper and lower halves slide in from opposite sides ------------------------------
class Shutter : public DraftEffect
{
public:
    Shutter(const GoldText &g, const DraftActor &a)
    {
        const Tile t = textTile(g, QStringLiteral("medium"), val(a.text, "Beyond the ordinary"), 128, {247, 243, 234}, 1470);
        x_ = (FW - t.width()) / 2.0, cut_ = pyRound(t.height() * .52);
        top_ = crop(t.im, 0, 0, t.width(), cut_), bot_ = crop(t.im, 0, cut_, t.width(), t.height());
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, 1.18), v = 1 - ease(t, 4.92, 5.76), d = 86 * (1 - p);
        paste(f, top_, x_ - d, 470, p * v);
        paste(f, bot_, x_ + d, 470 + cut_, p * v);
    }

private:
    double x_ = 0;
    int cut_ = 0;
    Rgba top_, bot_;
};

// ---- 64A Delta (smart): old value, arrow, new value and the change; secondary is "old,new" ---------------
class Delta : public DraftEffect
{
public:
    Delta(const GoldText &g, const DraftActor &a)
    {
        const QStringList v = val(a.secondary, "120,150").split(QLatin1Char(','));
        bool o1 = false, o2 = false;
        const long long old = v.size() == 2 ? v[0].trimmed().toLongLong(&o1) : 0, nw = v.size() == 2 ? v[1].trimmed().toLongLong(&o2) : 0;
        if (!o1 || !o2 || old < 1 || old > 999999 || nw < 0 || nw > 999999)
            return;
        const double pct = (nw - old) * 100.0 / old;
        c_ = pct >= 0 ? Rgb{145, 214, 186} : Rgb{244, 156, 139};
        label_ = textTile(g, QStringLiteral("regular"), val(a.text, "monthly revenue"), 48, {157, 168, 169}, 1350);
        a_ = textTile(g, QStringLiteral("medium"), grouped(old), 104, {157, 168, 169}, 570);
        b_ = textTile(g, QStringLiteral("medium"), grouped(nw), 104, {244, 240, 230}, 570);
        s_ = textTile(g, QStringLiteral("medium"), (pct >= 0 ? QStringLiteral("+") : QString()) + QString::number(pct, 'f', 1) + QLatin1Char('%'), 150, c_, 1350);
        ax_ = 280 + a_.width() + 42, bx_ = ax_ + 98;
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double p = ease(t, .12, .72), q = ease(t, .65, 1.60), v = 1 - ease(t, 4.92, 5.76);
        paste(f, label_.im, 280, 305, p * v);
        paste(f, a_.im, 280, 380 + 16 * (1 - p), p * v);
        paste(f, b_.im, bx_, 380 + 16 * (1 - q), q * v);
        if (q > 0) {
            Rgba ar(82, 42);
            const double e = pyRound(66 * q);
            hardLine(ar, {{0, 21}, {e, 21}}, 3, c_);
            hardLine(ar, {{e - 12, 9}, {e, 21}, {e - 12, 33}}, 3, c_);
            paste(f, ar, ax_, 380 + 30, q * v);
        }
        paste(f, s_.im, 280, 580 + 23 * (1 - q), q * v);
    }

private:
    bool ok_ = false;
    Rgb c_;
    Tile label_, a_, b_, s_;
    int ax_ = 0, bx_ = 0;
};

// ---- 64B Compare (smart): two values on one shared scale; secondary is "Name,value;Name,value" ---------
class Compare : public DraftEffect
{
public:
    Compare(const GoldText &g, const DraftActor &a)
    {
        const QStringList pairs = val(a.secondary, "Train,38;Car,65").split(QLatin1Char(';'));
        if (pairs.size() != 2)
            return;
        QString names[2];
        long long vals[2] = {0, 0};
        for (int i = 0; i < 2; ++i) {
            const int c = int(pairs[i].lastIndexOf(QLatin1Char(',')));
            bool ok = false;
            if (c < 0)
                return;
            names[i] = pairs[i].left(c), vals[i] = pairs[i].mid(c + 1).trimmed().toLongLong(&ok);
            if (!ok)
                return;
        }
        const long long mx = std::max(vals[0], vals[1]);
        if (std::min(vals[0], vals[1]) < 0 || mx > 999999 || mx == 0)
            return;
        h_ = textTile(g, QStringLiteral("serif"), val(a.text, "Travel time (min)"), 79, {245, 244, 237}, 1300);
        const Rgb cs[2] = {{129, 212, 189}, {207, 185, 147}};
        for (int i = 0; i < 2; ++i)
            items_.push_back({textTile(g, QStringLiteral("regular"), names[i], 45, {245, 244, 237}, 760),
                              textTile(g, QStringLiteral("medium"), grouped(vals[i]), 49, cs[i], 280), 455 + i * 152,
                              pyRound(1010.0 * vals[i] / mx), cs[i]});
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double p = ease(t, .12, .72), q = ease(t, .65, 1.72), v = 1 - ease(t, 4.92, 5.76);
        paste(f, h_.im, 330, 250 + 12 * (1 - p), p * v);
        for (const Item &it : items_) {
            paste(f, it.n.im, 330, it.y, p * v);
            paste(f, it.a.im, 330 + 1010 - it.a.width(), it.y, p * v);
            paste(f, rect(1010, 19, {118, 132, 138}, 3), 330, it.y + 63, .45 * p * v);
            const int ww = pyRound(it.w * q);
            if (ww)
                paste(f, rect(ww, 19, it.c, std::min(3, ww / 2)), 330, it.y + 63, p * v);
        }
    }

private:
    struct Item { Tile n, a; int y, w; Rgb c; };
    bool ok_ = false;
    Tile h_;
    std::vector<Item> items_;
};


// ---- 64C Duration (smart): a time span drawn between two clock times; secondary is "HH:MM,HH:MM" -------
class Duration : public DraftEffect
{
public:
    Duration(const GoldText &g, const DraftActor &a)
    {
        const QStringList v = val(a.secondary, "09:20,11:05").split(QLatin1Char(','));
        if (v.size() != 2)
            return;
        auto minutes = [](const QString &s, bool *ok) {
            const QStringList hm = s.split(QLatin1Char(':'));
            bool a = false, b = false;
            const int h = hm.size() == 2 ? hm[0].trimmed().toInt(&a) : -1, m = hm.size() == 2 ? hm[1].trimmed().toInt(&b) : -1;
            *ok = a && b && h >= 0 && h < 24 && m >= 0 && m < 60;
            return h * 60 + m;
        };
        bool o1 = false, o2 = false;
        const int a0 = minutes(v[0], &o1), b0 = minutes(v[1], &o2);
        if (!o1 || !o2)
            return;
        const int elapsed = ((b0 - a0) % 1440 + 1440) % 1440, hours = elapsed / 60, mins = elapsed % 60;
        const QString dur = hours ? QStringLiteral("%1h %2m").arg(hours).arg(mins, 2, 10, QLatin1Char('0')) : QStringLiteral("%1m").arg(mins);
        const QString end = b0 < a0 ? v[1] + QStringLiteral(" +1d") : v[1];
        const Rgb INK{245, 240, 230};
        h_ = textTile(g, QStringLiteral("regular"), val(a.text, "Estimated journey"), 47, {132, 147, 151}, 1260);
        a_ = textTile(g, QStringLiteral("medium"), v[0], 95, INK, 480);
        b_ = textTile(g, QStringLiteral("medium"), end, 95, INK, 630);
        z_ = textTile(g, QStringLiteral("serif"), dur, 135, {231, 189, 138}, 1260);
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const int X = 330, ly = 535, left = X + 8, right = X + 1260 - 8;
        const double p = ease(t, .12, .78), q = ease(t, .6, 1.45), v = 1 - ease(t, 4.92, 5.76);
        paste(f, h_.im, X, 280, p * v);
        paste(f, a_.im, X, 390, p * v);
        paste(f, b_.im, X + 1260 - b_.width(), 390, p * v);
        const int alpha = pyRound(255 * p * v), xx = pyRound(mix(left, right, q));
        const Rgb ACC{231, 189, 138};
        hardLine(f, {{double(left), double(ly)}, {double(right), double(ly)}}, 3, {132, 147, 151}, alpha);
        hardLine(f, {{double(left), double(ly)}, {double(xx), double(ly)}}, 4, ACC, alpha);
        for (int x : {left, xx})
            hardDisc(f, x - 6, ly - 6, x + 6, ly + 6, ACC, alpha);
        paste(f, z_.im, (FW - z_.width()) / 2.0, 615 + 17 * (1 - q), q * v);
    }

private:
    bool ok_ = false;
    Tile h_, a_, b_, z_;
};

// ---- 64D Share (smart): one bar split into complementary shares; secondary is "A,B,percent" ------------
class Share : public DraftEffect
{
public:
    Share(const GoldText &g, const DraftActor &a)
    {
        const QString s = val(a.secondary, "Mobile,Desktop,68");
        const int c2 = int(s.lastIndexOf(QLatin1Char(','))), c1 = c2 > 0 ? int(s.lastIndexOf(QLatin1Char(','), c2 - 1)) : -1;
        if (c1 < 0)
            return;
        bool ok = false;
        share_ = s.mid(c2 + 1).trimmed().toInt(&ok);
        if (!ok || share_ < 0 || share_ > 100)
            return;
        const Rgb INK{246, 244, 235}, A{136, 213, 188}, B{219, 183, 133};
        h_ = textTile(g, QStringLiteral("serif"), val(a.text, "Audience breakdown"), 76, INK, 1320);
        const QString names[2] = {s.left(c1), s.mid(c1 + 1, c2 - c1 - 1)};
        const int vals[2] = {share_, 100 - share_};
        const Rgb cs[2] = {A, B};
        for (int i = 0; i < 2; ++i) {
            Tile n = textTile(g, QStringLiteral("regular"), names[i], 43, INK, 580);
            Tile z = textTile(g, QStringLiteral("medium"), QString::number(vals[i]) + QLatin1Char('%'), 112, cs[i], 580);
            const int xx = i == 0 ? 300 : 300 + 1320 - z.width(), nx = i == 0 ? 300 : 300 + 1320 - n.width();
            items_.push_back({n, z, nx, xx});
        }
        boundary_ = pyRound(1320.0 * share_ / 100);
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double p = ease(t, .12, .76), q = ease(t, .60, 1.62), v = 1 - ease(t, 4.92, 5.76);
        paste(f, h_.im, 300, 280 + 12 * (1 - p), p * v);
        for (const Item &it : items_) {
            paste(f, it.n.im, it.nx, 690, p * v);
            paste(f, it.z.im, it.xx, 435 + 18 * (1 - q), q * v);
        }
        paste(f, rect(1320, 42, {100, 112, 117}), 300, 610, .35 * p * v);
        const int wa = pyRound(boundary_ * q), wb = pyRound((1320 - boundary_) * q);
        if (wa)
            paste(f, rect(wa, 42, {136, 213, 188}), 300, 610, p * v);
        if (wb)
            paste(f, rect(wb, 42, {219, 183, 133}), 300 + 1320 - wb, 610, p * v);
    }

private:
    struct Item { Tile n, z; int nx, xx; };
    bool ok_ = false;
    int share_ = 0, boundary_ = 0;
    Tile h_;
    std::vector<Item> items_;
};

// ---- 65A Turn: the headline hinges up from 78° about its horizontal axis ------------------------------
class Turn : public DraftEffect
{
public:
    Turn(const GoldText &g, const DraftActor &a)
    {
        const Tile raw = textTile(g, QStringLiteral("serif"), val(a.text, "A new perspective"), 126, {245, 240, 229}, 1480);
        tile_ = Rgba(raw.width() + 64, raw.height() + 64);
        composite(tile_, raw.im, 32, 32);
        x_ = (FW - tile_.w) / 2.0, y_ = (FH - tile_.h) / 2.0;
    }
    Rgba project(double theta) const
    {
        if (theta == 0)
            return tile_;
        const double c = std::cos(theta), s = std::sin(theta), f = 1100, cx = tile_.w / 2.0, cy = tile_.h / 2.0, den = c + s * cy / f;
        const double m[8] = {c / den, (-cx * s / f) / den, (-c * cx + cx * c + cx * s * cy / f) / den,
                             0, (1 - cy * s / f) / den, (-cy + cy * c + cy * s * cy / f) / den, 0, (-s / f) / den};
        return perspectiveBicubic(tile_, m);
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .10, 1.08), v = 1 - ease(t, 4.92, 5.76);
        if (p > 0 && v > 0)
            paste(f, project(78 * kPi / 180 * (1 - p)), x_, y_, p * v);
    }

private:
    Rgba tile_;
    double x_ = 0, y_ = 0;
};

// ---- 65B Wash: a soft ink boundary washes across, a warm halo riding on it --------------------------
class Wash : public DraftEffect
{
public:
    Wash(const GoldText &g, const DraftActor &a)
    {
        tile_ = textTile(g, QStringLiteral("bold"), val(a.text, "Made for more."), 146, {244, 239, 229}, 1480).im;
        x_ = (FW - tile_.w) / 2.0, y_ = (FH - tile_.h) / 2.0;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .15, 1.25), v = 1 - ease(t, 4.92, 5.76);
        if (p <= 0 || v <= 0)
            return;
        if (p >= 1) {
            paste(f, tile_, x_, y_, v);
            return;
        }
        const float F = 105.f;
        const float edge = float(mix(-105, tile_.w + 105, p));
        Rgba out = tile_;
        const double INK[3] = {244, 239, 229}, EDGE[3] = {224, 174, 117};
        for (int x = 0; x < out.w; ++x) {
            const float d = edge - float(x);
            float m = std::clamp(d / F + .5f, 0.f, 1.f);
            m = m * m * (3.f - 2.f * m);
            const float hz = (d - F * .28f) / (F * .37f);
            const float halo = std::exp(-(hz * hz)) * .7f;
            std::uint8_t rgb[3];
            for (int k = 0; k < 3; ++k)
                rgb[k] = std::uint8_t(std::nearbyint(INK[k] * (1 - double(halo)) + EDGE[k] * double(halo)));
            for (int y = 0; y < out.h; ++y) {
                std::uint8_t *q = out.px(x, y);
                q[3] = std::uint8_t(std::nearbyint(float(q[3]) * m));
                std::copy_n(rgb, 3, q);
            }
        }
        paste(f, out, x_, y_, v);
    }

private:
    Rgba tile_;
    double x_ = 0, y_ = 0;
};


// ---- 65C Total (smart): two values and their sum counting up; secondary is "Label,value;Label,value" ----
class Total : public DraftEffect
{
public:
    Total(const GoldText &g, const DraftActor &a)
    {
        const QStringList pairs = val(a.secondary, "Equipment,240;Materials,160").split(QLatin1Char(';'));
        if (pairs.size() != 2)
            return;
        const Rgb INK{246, 241, 232}, DIM{159, 170, 171};
        for (int i = 0; i < 2; ++i) {
            const int c = int(pairs[i].lastIndexOf(QLatin1Char(',')));
            bool ok = false;
            if (c < 0)
                return;
            vals_[i] = pairs[i].mid(c + 1).trimmed().toLongLong(&ok);
            if (!ok || vals_[i] < 0 || vals_[i] > 999999)
                return;
            items_.push_back({textTile(g, QStringLiteral("regular"), pairs[i].left(c), 46, DIM, 760),
                              textTile(g, QStringLiteral("medium"), grouped(vals_[i]), 54, INK, 350), 374 + i * 100});
        }
        title_ = textTile(g, QStringLiteral("serif"), val(a.text, "Total investment"), 74, INK, 1210);
        caption_ = textTile(g, QStringLiteral("medium"), QStringLiteral("TOTAL"), 38, {144, 216, 190}, 500);
        // Every value the count can show, kept as coverage (up to 400 of them; a longer count
        // shows the nearest of 400 evenly spaced steps).
        steps_ = int(std::min<long long>(vals_[1], 400));
        for (int k = 0; k <= steps_; ++k) {
            const long long v = vals_[0] + (steps_ ? pyRound(double(vals_[1]) * k / steps_) : 0);
            const Tile tl = textTile(g, QStringLiteral("medium"), grouped(v), 128, {144, 216, 190}, 1210 - 350);
            gold::M8 m(tl.width(), tl.height());
            for (size_t i = 0; i < m.p.size(); ++i)
                m.p[i] = tl.im.p[i * 4 + 3];
            sums_.push_back(std::move(m));
        }
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const int X = 355, BW = 1210;
        const double p = ease(t, .10, .62), q = ease(t, .65, 1.65), v = 1 - ease(t, 4.92, 5.76);
        paste(f, title_.im, X, 224 + 14 * (1 - p), p * v);
        for (size_t i = 0; i < items_.size(); ++i) {
            const double a = i == 0 ? p : ease(t, .47, 1.03);
            paste(f, items_[i].n.im, X, items_[i].y, a * v);
            paste(f, items_[i].z.im, X + BW - items_[i].z.width(), items_[i].y, a * v);
        }
        const int ww = pyRound(BW * q);
        if (ww)
            paste(f, rect(ww, 2, {159, 170, 171}), X, 585, p * v);
        const long long value = vals_[0] + pyRound(vals_[1] * q);
        const int k = steps_ ? int(std::llround(double(value - vals_[0]) * steps_ / double(vals_[1]))) : 0;
        const Rgba z = tint(sums_[size_t(std::clamp(k, 0, steps_))], {144, 216, 190});
        paste(f, caption_.im, X, 680, q * v);
        paste(f, z, X + BW - z.w, 631, q * v);
    }

private:
    struct Item { Tile n, z; int y; };
    bool ok_ = false;
    long long vals_[2] = {0, 0};
    int steps_ = 0;
    std::vector<Item> items_;
    Tile title_, caption_;
    std::vector<gold::M8> sums_;
};

// ---- 65D Labelshift: a centred topic label moves up into its place above the headline ------------------
class Labelshift : public DraftEffect
{
public:
    Labelshift(const GoldText &g, const DraftActor &a)
    {
        tag_ = textTile(g, QStringLiteral("medium"), val(a.secondary, "THE NEXT CHAPTER"), 43, {226, 191, 145}, 1380);
        title_ = textTile(g, QStringLiteral("bold"), val(a.text, "Built for tomorrow"), 118, {245, 241, 231}, 1410);
        sx_ = (FW - tag_.width()) / 2.0, sy_ = (FH - tag_.height()) / 2.0, rulew_ = std::min(title_.width(), 1410);
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .10, .55), q = ease(t, .52, 1.40), v = 1 - ease(t, 4.92, 5.76);
        paste(f, tag_.im, mix(sx_, 255, q), mix(sy_, 410, q), p * v);
        const double r = ease(t, .84, 1.48);
        paste(f, title_.im, 255, 480 + 32 * (1 - r), r * v);
        const double a = ease(t, 1.0, 1.62);
        const int ww = pyRound(rulew_ * a);
        if (ww)
            paste(f, rect(ww, 3, {226, 191, 145}), 255, 480 + title_.height() + 26, a * v);
    }

private:
    Tile tag_, title_;
    double sx_ = 0, sy_ = 0;
    int rulew_ = 0;
};

// ---- 66A Relay: one headline hands over to the next through a shared aperture ---------------------------
class Relay : public DraftEffect
{
public:
    Relay(const GoldText &g, const DraftActor &a)
    {
        a_ = textTile(g, QStringLiteral("bold"), val(a.text, "Make it simple"), 130, {245, 242, 233}, 1380);
        b_ = textTile(g, QStringLiteral("bold"), val(a.secondary, "Make it matter"), 130, {245, 242, 233}, 1380);
        tag_ = textTile(g, QStringLiteral("medium"), QStringLiteral("A BETTER WAY"), 37, {235, 186, 126}, 1380);
        hh_ = std::max(a_.height(), b_.height()) + 24;
        ay_ = 470 + (hh_ - a_.height()) / 2.0, by_ = 470 + (hh_ - b_.height()) / 2.0;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .10, .65), q = ease(t, 1.62, 2.45), v = 1 - ease(t, 4.92, 5.76);
        paste(f, tag_.im, 270, 402, p * v);
        clip(f, a_.im, 270, ay_ + hh_ * q, 270, 470, 270 + 1400, 470 + hh_, p * v);
        clip(f, b_.im, 270, by_ - hh_ * (1 - q), 270, 470, 270 + 1400, 470 + hh_, p * v);
    }

private:
    Tile a_, b_, tag_;
    int hh_ = 0;
    double ay_ = 0, by_ = 0;
};

// ---- 66B Radiance: the headline is revealed outward from its optical centre -----------------------------
class Radiance : public DraftEffect
{
public:
    Radiance(const GoldText &g, const DraftActor &a)
    {
        tile_ = textTile(g, QStringLiteral("serif"), val(a.text, "Into the light"), 146, {248, 241, 227}, 1510).im;
        x_ = (FW - tile_.w) / 2.0, y_ = (FH - tile_.h) / 2.0;
        dist_.assign(size_t(tile_.w) * tile_.h, 0.0);
        double mx = 0;
        for (int y = 0; y < tile_.h; ++y)
            for (int x = 0; x < tile_.w; ++x) {
                const double dx = x - (tile_.w - 1) / 2.0, dy = y - (tile_.h - 1) / 2.0;
                mx = std::max(mx, dist_[size_t(y) * tile_.w + x] = std::sqrt(dx * dx + 2.5 * dy * dy));
            }
        limit_ = mx + 108;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .12, 1.42), v = 1 - ease(t, 4.92, 5.76);
        if (p <= 0 || v <= 0)
            return;
        if (p >= 1) {
            paste(f, tile_, x_, y_, v);
            return;
        }
        const double r = mix(-108, limit_, p), INK[3] = {248, 241, 227}, EDGE[3] = {235, 190, 125};
        Rgba out = tile_;
        for (int y = 0; y < out.h; ++y)
            for (int x = 0; x < out.w; ++x) {
                const double z = r - dist_[size_t(y) * out.w + x];
                double m = clamp01(z / 108 + .5);
                m = m * m * (3 - 2 * m);
                const double halo = .56 * std::exp(-std::pow((z - 15) / 45, 2));
                std::uint8_t *q = out.px(x, y);
                q[3] = std::uint8_t(std::nearbyint(q[3] * m));
                for (int k = 0; k < 3; ++k)
                    q[k] = std::uint8_t(std::nearbyint(INK[k] * (1 - halo) + EDGE[k] * halo));
            }
        paste(f, out, x_, y_, v);
    }

private:
    Rgba tile_;
    double x_ = 0, y_ = 0, limit_ = 0;
    std::vector<double> dist_;
};


// ---- 66C Countdown (smart): a ring and the seconds tied to one clock; secondary is 1…4 ----------------
class Countdown : public DraftEffect
{
public:
    Countdown(const GoldText &g, const DraftActor &a)
    {
        bool ok = false;
        s_ = (a.secondary ? *a.secondary : QStringLiteral("3")).trimmed().toInt(&ok);
        if (!ok || s_ < 1 || s_ > 4)
            return;
        cap_ = textTile(g, QStringLiteral("medium"), val(a.text, "Starting in"), 49, {245, 242, 233}, 1250);
        for (int i = 0; i <= s_; ++i)
            digits_.push_back(textTile(g, QStringLiteral("medium"), QStringLiteral("%1").arg(i, 2, 10, QLatin1Char('0')), 156, {245, 242, 233}, 300));
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double p = ease(t, .08, .50), v = 1 - ease(t, 4.92, 5.76);
        const double left = std::max(0.0, std::min(double(s_), s_ - (t - .60)));
        const int n = std::max(0, int(std::ceil(left - 1e-9)));
        const double fr = left / s_;
        if (p <= 0 || v <= 0)
            return;
        // The 3× ring: outer radius 178, 5 wide; the accent arc clockwise from the top over the fraction left.
        const int size = 2 * (178 + 8);
        Rgba ring(size, size);
        const double c = 186, ro = 178, ri = 173;
        for (int y = 0; y < size; ++y)
            for (int x = 0; x < size; ++x) {
                const double dx = x + .5 - c, dy = y + .5 - c, r = std::hypot(dx, dy);
                const double cov = clamp01(ro + .5 - r) - clamp01(ri + .5 - r);
                if (cov <= 0)
                    continue;
                std::uint8_t *q = ring.px(x, y);
                q[0] = 90, q[1] = 106, q[2] = 115, q[3] = std::uint8_t(pyRound(cov * 255));
                if (fr > 0) {
                    double ang = std::atan2(dx, -dy) * 180 / kPi; // clockwise from the top
                    if (ang < 0)
                        ang += 360;
                    const double span = 360 * fr, edge = std::min(ang, span - ang) * kPi / 180 * r; // px inside the arc
                    const double ac = fr >= 1 ? 1.0 : clamp01(edge + .5);
                    if (ac > 0) {
                        const std::uint8_t accent[4] = {154, 222, 198, std::uint8_t(pyRound(cov * 255))};
                        std::uint8_t mixd[4];
                        for (int k = 0; k < 4; ++k)
                            mixd[k] = std::uint8_t(pyRound(q[k] + (accent[k] - q[k]) * ac));
                        std::copy_n(mixd, 4, q);
                    }
                }
            }
        paste(f, ring, 960 - size / 2.0, 495 - size / 2.0, p * v);
        const Tile &num = digits_[size_t(n)];
        paste(f, num.im, 960 - num.width() / 2.0, 495 - num.height() / 2.0, p * v);
        paste(f, cap_.im, (FW - cap_.width()) / 2.0, 718, p * v);
    }

private:
    bool ok_ = false;
    int s_ = 3;
    Tile cap_;
    std::vector<Tile> digits_;
};

// ---- 66D Rulefit: rules that fill the room left and right of the measured title ---------------------
class Rulefit : public DraftEffect
{
public:
    Rulefit(const GoldText &g, const DraftActor &a)
    {
        tile_ = textTile(g, QStringLiteral("serif"), val(a.text, "The next chapter"), 110, {245, 239, 229}, FW - 2 * (220 + 43 + 72));
        x_ = (FW - tile_.width()) / 2.0, y_ = (FH - tile_.height()) / 2.0;
        left_ = pyRound(x_ - 43), right_ = FW - left_, length_ = left_ - 220;
        ok_ = length_ >= 72 - 2;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double p = ease(t, .10, .90), q = ease(t, .40, 1.28), v = 1 - ease(t, 4.92, 5.76);
        paste(f, tile_.im, x_, y_ + 22 * (1 - p), p * v);
        const int n = pyRound(length_ * q);
        if (n) {
            const Rgba bar = rect(n, 3, {211, 180, 132});
            paste(f, bar, left_ - n, 540 - 1, v * q);
            paste(f, bar, right_, 540 - 1, v * q);
        }
    }

private:
    bool ok_ = false;
    Tile tile_;
    double x_ = 0, y_ = 0;
    int left_ = 0, right_ = 0, length_ = 0;
};

// ---- 67A Matchword: one statement hands over to the next above an unchanged anchor word ----------------
class Matchword : public DraftEffect
{
public:
    Matchword(const GoldText &g, const DraftActor &a)
    {
        const Rgb INK{246, 241, 230};
        a_ = textTile(g, QStringLiteral("regular"), val(a.text, "Make room for"), 77, INK, 1250);
        b_ = textTile(g, QStringLiteral("regular"), val(a.secondary, "Find your own"), 77, INK, 1250);
        c_ = textTile(g, QStringLiteral("serif"), QStringLiteral("freedom"), 166, {240, 177, 120}, 1410);
        hh_ = std::max(a_.height(), b_.height()) + 18;
        ay_ = 370 + (hh_ - a_.height()) / 2.0, by_ = 370 + (hh_ - b_.height()) / 2.0, cy_ = 370 + hh_ + 18;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .10, .85), q = ease(t, 1.72, 2.52), v = 1 - ease(t, 4.92, 5.76);
        clip(f, a_.im, 270, ay_ - hh_ * q, 270, 370, 270 + 1450, 370 + hh_, p * v);
        clip(f, b_.im, 270, by_ + hh_ * (1 - q), 270, 370, 270 + 1450, 370 + hh_, p * v);
        paste(f, c_.im, 270, cy_ + 30 * (1 - p), p * v);
    }

private:
    Tile a_, b_, c_;
    int hh_ = 0, cy_ = 0;
    double ay_ = 0, by_ = 0;
};

// ---- 67B Revision: a word is struck through and corrected above itself --------------------------------
class Revision : public DraftEffect
{
public:
    Revision(const GoldText &g, const DraftActor &a)
    {
        const QString word = val(a.text, "perfection");
        a_ = textTile(g, QStringLiteral("regular"), QStringLiteral("Choose"), 105, {245, 241, 232}, 500);
        b_ = textTile(g, QStringLiteral("medium"), word, 105, {245, 241, 232}, 850);
        d_ = textTile(g, QStringLiteral("medium"), word, 105, {142, 154, 160}, 850);
        c_ = textTile(g, QStringLiteral("serif"), val(a.secondary, "progress"), 93, {252, 177, 126}, 850);
        xx_ = (FW - a_.width() - 28 - std::max(b_.width(), c_.width())) / 2.0, bx_ = xx_ + a_.width() + 28;
        ay_ = 635 + a_.box.top - 2, by_ = 635 + b_.box.top - 2, cy_ = by_ - c_.height() - 28;
        liney_ = pyRound(by_ + b_.height() * .54), length_ = b_.width() + 8;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .10, .78), q = ease(t, 1.55, 2.05), r = ease(t, 1.96, 2.66), v = 1 - ease(t, 4.92, 5.76);
        paste(f, a_.im, xx_, ay_ + 15 * (1 - p), p * v);
        paste(f, b_.im, bx_, by_ + 15 * (1 - p), p * v * (1 - q));
        paste(f, d_.im, bx_, by_, p * v * q);
        const int n = pyRound(length_ * q);
        if (n)
            paste(f, rect(n, 4, {252, 177, 126}), bx_ - 4, liney_, v);
        paste(f, c_.im, bx_, cy_ + 20 * (1 - r), r * v);
    }

private:
    Tile a_, b_, d_, c_;
    double xx_ = 0, bx_ = 0;
    int ay_ = 0, by_ = 0, cy_ = 0, liney_ = 0, length_ = 0;
};

// ---- 67C Convert (smart): kilometres and the miles they come to; secondary is the distance in km --------
class Convert : public DraftEffect
{
public:
    // Python's Decimal quantize to 0.1, ROUND_HALF_UP, shown as f'{v:,}' without a trailing .0.
    static QString fmt(long long tenths)
    {
        return grouped(tenths / 10) + QLatin1Char('.') + QString::number(tenths % 10);
    }
    static bool tenthsOf(const QString &s, long long *km10, long double *exact)
    {
        bool ok = false;
        const long double v = s.trimmed().toDouble(&ok);
        if (!ok || !std::isfinite(double(v)) || v < 0 || v > 999999)
            return false;
        *exact = v;
        // exact decimal rounding of the typed digits: the digit after the tenths decides
        const QString t = s.trimmed();
        const int dot = int(t.indexOf(QLatin1Char('.')));
        const QString ip = dot < 0 ? t : t.left(dot), fp = dot < 0 ? QString() : t.mid(dot + 1);
        long long n = ip.isEmpty() ? 0 : ip.toLongLong() * 10 + (fp.size() > 0 ? fp[0].digitValue() : 0);
        if (fp.size() > 1 && fp[1].digitValue() >= 5)
            ++n;
        *km10 = n;
        return true;
    }
    Convert(const GoldText &g, const DraftActor &a)
    {
        long long km10 = 0;
        long double v = 0;
        if (!tenthsOf(a.secondary ? *a.secondary : QStringLiteral("120"), &km10, &v))
            return;
        const long double mi = v / 1.609344L;
        const long long mi10 = (long long)std::floor(double(mi * 10 + 0.5L + 1e-12L));
        // a whole value is quantized to 1 (half up), not to 0.1
        const QString km = km10 % 10 ? fmt(km10) : grouped((long long)std::floor(double(v + 0.5L)));
        const QString ms = mi10 % 10 ? fmt(mi10) : grouped((long long)std::floor(double(mi + 0.5L)));
        const Rgb INK{245, 242, 233}, ACC{151, 220, 204};
        cap_ = textTile(g, QStringLiteral("medium"), val(a.text, "Distance travelled"), 45, INK, 1380);
        a_ = textTile(g, QStringLiteral("medium"), km, 138, INK, 580);
        b_ = textTile(g, QStringLiteral("medium"), ms, 138, ACC, 580);
        u_ = textTile(g, QStringLiteral("regular"), QStringLiteral("km"), 45, INK, 150);
        w_ = textTile(g, QStringLiteral("regular"), QStringLiteral("mi"), 45, ACC, 150);
        eq_ = textTile(g, QStringLiteral("regular"), QStringLiteral("\u2248"), 75, {100, 121, 131}, 180);
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double p = ease(t, .12, .78), q = ease(t, .65, 1.55), v = 1 - ease(t, 4.92, 5.76);
        paste(f, cap_.im, (FW - cap_.width()) / 2.0, 300, p * v);
        paste(f, a_.im, 570 - a_.width() / 2.0, 590 + a_.box.top - 2, p * v);
        paste(f, u_.im, 570 - u_.width() / 2.0, 628, p * v);
        paste(f, eq_.im, (FW - eq_.width()) / 2.0, 590 - eq_.height() / 2.0, p * v);
        paste(f, b_.im, 1350 - b_.width() / 2.0, 590 + b_.box.top - 2 + 22 * (1 - q), q * v);
        paste(f, w_.im, 1350 - w_.width() / 2.0, 628, q * v);
    }

private:
    bool ok_ = false;
    Tile cap_, a_, b_, u_, w_, eq_;
};

// ---- 67D Trail: three fading echoes trail the headline into place -----------------------------------
class Trail : public DraftEffect
{
public:
    Trail(const GoldText &g, const DraftActor &a)
    {
        const QString txt = val(a.text, "Keep moving");
        tile_ = textTile(g, QStringLiteral("bold"), txt, 146, {245, 242, 235}, 1470);
        ghost_ = textTile(g, QStringLiteral("bold"), txt, 146, {130, 183, 194}, 1470);
        x_ = (FW - tile_.width()) / 2.0, y_ = (FH - tile_.height()) / 2.0;
    }
    void draw(Rgba &f, double t) const override
    {
        const double p = ease(t, .10, 1.20), v = 1 - ease(t, 4.92, 5.76);
        if (p <= 0 || v <= 0)
            return;
        const double delays[3] = {.16, .105, .05}, weights[3] = {.09, .16, .25};
        for (int i = 0; i < 3; ++i) {
            const double q = ease(t - delays[i], .10, 1.20), fade = clamp01((p - q) * 7) * q * weights[i] * v;
            if (fade)
                paste(f, ghost_.im, x_ - 170 * (1 - q), y_, fade);
        }
        paste(f, tile_.im, x_ - 170 * (1 - p), y_, p * v);
    }

private:
    Tile tile_, ghost_;
    double x_ = 0, y_ = 0;
};


// ---- 54B Milestone: a three-digit odometer rolls 236 → 248 beside its unit (the unit is the text) ---------
class Milestone : public DraftEffect
{
public:
    Milestone(const GoldText &g, const DraftActor &a)
    {
        const QString unit = a.text ? *a.text : QStringLiteral("km");
        const QString digitFont = QStringLiteral("bold+tnum"); // features=['tnum']
        int u = 80;
        TextBox ub;
        while (true) {
            ub = g.bbox(QStringLiteral("medium"), u, unit);
            if (ub.right - ub.left <= 650 || u == 40)
                break;
            --u;
        }
        if (ub.right - ub.left > 650)
            return;
        const int uw = ub.right - ub.left, total = 3 * 151 + (unit.isEmpty() ? 0 : 30 + uw);
        x0_ = pyRound(960 - total / 2.0);
        const int ux = x0_ + 3 * 151 + 30 - ub.left;
        for (int d = 0; d < 10; ++d) {
            const QString s = QString::number(d);
            const TextBox bb = g.bbox(digitFont, 230, s);
            if (bb.right - bb.left > 151)
                return;
            masks_[d] = gold::M8(151, 634 - 396);
            g.draw(masks_[d], digitFont, 230, s, pyRound((151 - bb.left - bb.right) / 2.0), 605 - 396);
        }
        gold::M8 um(FW, FH);
        g.draw(um, QStringLiteral("medium"), u, unit, ux, 605);
        unit_ = tint(um, {154, 208, 194});
        ok_ = true;
    }
    void draw(Rgba &f, double t) const override
    {
        if (!ok_)
            return;
        const double e = ease(t, 0, .30), o = ease(t, 4.80, 5.64), progress = ease(t, .30, 1.78);
        const double v = 236 + 12 * progress, alpha = e * (1 - o);
        const int dy = pyRound(22 * (1 - e) + -18 * o), wh = 634 - 396;
        Rgba layer(FW, FH);
        for (size_t k = 0; k < layer.p.size(); k += 4)
            layer.p[k] = 244, layer.p[k + 1] = 241, layer.p[k + 2] = 232;
        for (int i = 0; i < 3; ++i) {
            const int factor = i == 0 ? 100 : (i == 1 ? 10 : 1);
            const long whole = long(std::floor(v / factor + 1e-9));
            int d = int(whole % 10);
            double phase = smoothOf(clamp01(v - factor * whole - (factor - 1)));
            if (progress >= 1)
                phase = 0, d = (248 / factor) % 10;
            const int offset = pyRound(-204 * phase);
            gold::M8 aa(151, wh);
            const std::pair<int, int> parts[2] = {{d, offset}, {(d + 1) % 10, offset + 204}};
            for (const auto &pt : parts) {
                const int lo = std::max(0, pt.second), hi = std::min(wh, pt.second + wh);
                for (int y = lo; y < hi; ++y)
                    for (int x = 0; x < 151; ++x)
                        aa.at(x, y) = std::max(aa.at(x, y), masks_[pt.first].at(x, y - pt.second));
            }
            if (phase > 1e-6 && phase < 1 - 1e-6)
                for (int y = 0; y < wh; ++y) {
                    const double fe = std::clamp(std::min(std::min(y / 24.0, (wh - 1 - y) / 24.0), 1.0), 0.0, 1.0);
                    for (int x = 0; x < 151; ++x)
                        aa.at(x, y) = std::uint8_t(std::nearbyint(aa.at(x, y) * fe));
                }
            const int ox = x0_ + i * 151, oy = 396 + dy;
            for (int y = 0; y < wh; ++y)
                for (int x = 0; x < 151; ++x)
                    if (ox + x >= 0 && ox + x < FW && oy + y >= 0 && oy + y < FH)
                        layer.px(ox + x, oy + y)[3] = aa.at(x, y);
        }
        composite(layer, unit_, 0, dy);
        for (size_t k = 3; k < layer.p.size(); k += 4)
            layer.p[k] = std::uint8_t(std::nearbyint(layer.p[k] * alpha));
        composite(f, layer, 0, 0);
    }
    static double smoothOf(double x) { return x * x * (3 - 2 * x); }

private:
    bool ok_ = false;
    int x0_ = 0;
    gold::M8 masks_[10];
    Rgba unit_;
};

} // namespace d

// Every draft1x effect type: its script's defaults (DEFAULT_TEXT, SECONDARY) and its class.
struct DraftKind
{
    const char *kind, *text, *secondary;
    std::function<std::shared_ptr<const DraftEffect>(const GoldText &, const DraftActor &)> make;
};

template <typename T>
std::shared_ptr<const DraftEffect> makeAs(const GoldText &g, const DraftActor &a)
{
    return std::make_shared<T>(g, a);
}

inline const std::vector<DraftKind> &draftTable()
{
    using namespace d;
    static const std::vector<DraftKind> t{
        {"art30", "ART", nullptr,
         [](const GoldText &g, const DraftActor &a) -> std::shared_ptr<const DraftEffect> {
             return std::make_shared<Art>(g, a.font, val(a.text, "ART"),
                                          ArtStyle{98.676, 14.42, 142.088, 79.625, 2.41645, 3.44241, 0, {255, 255, 255}, {70, 157, 243}});
         }},
        {"art27", "ART", nullptr,
         [](const GoldText &g, const DraftActor &a) -> std::shared_ptr<const DraftEffect> {
             return std::make_shared<Art>(g, a.font, val(a.text, "ART"),
                                          ArtStyle{88.004, 13.601, 145.511, 80.744, 0, 0, 7.17864, {170, 74, 55}, {255, 255, 255}});
         }},
        {"milestone", "km", nullptr, makeAs<Milestone>},
        {"keyphrase", "Make every moment count", nullptr, makeAs<Keyphrase>},
        {"linebreak", "Make every moment count", nullptr, makeAs<Linebreak>},
        {"pullquote", "The best stories\nmake us look closer.", "FROM THE FIELD NOTES", makeAs<Pullquote>},
        {"switchword", "Make", "it happen.", makeAs<Switchword>},
        {"nameplate", "Alex Morgan", "Creative director", makeAs<Nameplate>},
        {"checklist", "Plan the story", "Review the cut", makeAs<Checklist>},
        {"section", "A new perspective", "02", makeAs<Section>},
        {"dualcaption", "Every journey begins somewhere.", "Orice c\xC4\x83""l\xC4\x83""torie \xC3\xAE""ncepe de undeva.", makeAs<Dualcaption>},
        {"sidenote", "The details", "Small changes.\nLasting impact.", makeAs<Sidenote>},
        {"redact", "The real story", "BEHIND THE HEADLINES", makeAs<Redact>},
        {"progress", "Find your direction", "THE CREATIVE PROCESS", makeAs<Progress>},
        {"focal", "Come closer.", nullptr, makeAs<Focal>},
        {"credit", "Elena Ionescu", "Directed by", makeAs<Credit>},
        {"pricepoint", "$79", "$129", makeAs<Pricepoint>},
        {"bracket", "The next step", nullptr, makeAs<Bracket>},
        {"horizon", "A new perspective", "CHAPTER 01", makeAs<Horizon>},
        {"echo", "Make it matter.", nullptr, makeAs<Echo>},
        {"dateline", "26", "NOVEMBER 2026", makeAs<Dateline>},
        {"capsule", "Better by design", "IN FOCUS", makeAs<Capsule>},
        {"dropcap", "Stories worth telling", "THE ART OF EVERYDAY LIFE", makeAs<Dropcap>},
        {"interpose", "See", "more.", makeAs<Interpose>},
        {"reanchor", "Elena Ionescu", "DESIGN DIRECTOR", makeAs<Reanchor>},
        {"between", "The details", "make the difference.", makeAs<Between>},
        {"gloss", "Perspective", "A different way of seeing", makeAs<Gloss>},
        {"waypoint", "Bra\xC8\x99""ov", "Sibiu", makeAs<Waypoint>},
        {"pinpoint", "Optical stabilization", "Reduce unwanted camera movement", makeAs<Pinpoint>},
        {"signpost", "Explore more", "FOLLOW THE COAST", makeAs<Signpost>},
        {"parallel", "Before", "After", makeAs<Parallel>},
        {"cutline", "A quieter kind of morning.", nullptr, makeAs<Cutline>},
        {"focuslist", "Plan with intention", "Create with clarity", makeAs<Focuslist>},
        {"compose", "Make it matter.", "A STORY WORTH TELLING", makeAs<Compose>},
        {"lumen", "Into the light", "A NEW PERSPECTIVE", makeAs<Lumen>},
        {"stack", "Start here.", "Go further.", makeAs<Stack>},
        {"footnote", "Every detail counts.", "Source: field notes, 2026", makeAs<Footnote>},
        {"baseline", "Make every moment count.", nullptr, makeAs<Baseline>},
        {"underscore", "Worth remembering.", nullptr, makeAs<Underscore>},
        {"ratio", "project completed", "72", makeAs<Ratio>},
        {"shutter", "Beyond the ordinary", nullptr, makeAs<Shutter>},
        {"delta", "monthly revenue", "120,150", makeAs<Delta>},
        {"compare", "Travel time (min)", "Train,38;Car,65", makeAs<Compare>},
        {"duration", "Estimated journey", "09:20,11:05", makeAs<Duration>},
        {"share", "Audience breakdown", "Mobile,Desktop,68", makeAs<Share>},
        {"turn", "A new perspective", nullptr, makeAs<Turn>},
        {"wash", "Made for more.", nullptr, makeAs<Wash>},
        {"total", "Total investment", "Equipment,240;Materials,160", makeAs<Total>},
        {"labelshift", "Built for tomorrow", "THE NEXT CHAPTER", makeAs<Labelshift>},
        {"relay", "Make it simple", "Make it matter", makeAs<Relay>},
        {"radiance", "Into the light", nullptr, makeAs<Radiance>},
        {"countdown", "Starting in", "3", makeAs<Countdown>},
        {"rulefit", "The next chapter", nullptr, makeAs<Rulefit>},
        {"matchword", "Make room for", "Find your own", makeAs<Matchword>},
        {"revision", "perfection", "progress", makeAs<Revision>},
        {"convert", "Distance travelled", "120", makeAs<Convert>},
        {"trail", "Keep moving", nullptr, makeAs<Trail>},
    };
    return t;
}

inline QStringList draftKinds()
{
    QStringList out;
    for (const DraftKind &k : draftTable())
        out << QString::fromLatin1(k.kind);
    return out;
}

inline std::shared_ptr<const DraftEffect> makeDraftEffect(const GoldText &g, DraftActor a)
{
    for (const DraftKind &k : draftTable()) {
        if (a.kind != QLatin1String(k.kind))
            continue;
        // a string equal to the script's default is passed as None
        if (a.text && *a.text == QString::fromUtf8(k.text))
            a.text.reset();
        if (a.secondary && k.secondary && *a.secondary == QString::fromUtf8(k.secondary))
            a.secondary.reset();
        return k.make(g, a);
    }
    return nullptr;
}

} // namespace drift::scene::draft
