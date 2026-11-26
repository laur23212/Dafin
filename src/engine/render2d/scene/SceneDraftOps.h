// SceneDraftOps.h — the image steps of the draft1x scene profile (the Stage 54–67 library titles,
// docs/scene-titles.md "draft1x"; reference: tools/scene-ref/drafts/d<asset>.py, run verbatim).
//
// The scripts build straight-alpha RGBA layers with Pillow and stack them with alpha_composite.
// These are the same steps in plain C++: alpha_composite with Pillow's integer arithmetic, the
// alpha point table of their paste(), crops, text tiles (ImageDraw.text on a clear RGBA tile:
// colour where there is ink, alpha = coverage), rounded rectangles (pixel centres inside the
// corner circles: within a few edge pixels of Pillow's pieslices) and line_layer (an anti-aliased
// polyline with round joins, as the 3× supersampled line it stands for).
#pragma once

#include "SceneGoldEffects.h"

#include <QString>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <utility>
#include <vector>

namespace drift::scene::draft {

using gold::GoldText;
using gold::TextBox;
constexpr int FW = 1920, FH = 1080;
constexpr double kPi = 3.14159265358979323846;

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline double ease(double t, double a, double b) // smoothstep of (t − a) / (b − a)
{
    const double x = clamp01((t - a) / (b - a));
    return x * x * (3.0 - 2.0 * x);
}
inline double mix(double a, double b, double p) { return a + (b - a) * p; }
inline int pyRound(double v) { return int(std::nearbyint(v)); } // Python round(): ties to even

struct Rgb
{
    int r = 255, g = 255, b = 255;
};

// A straight-alpha RGBA image.
struct Rgba
{
    int w = 0, h = 0;
    std::vector<std::uint8_t> p;
    Rgba() = default;
    Rgba(int w_, int h_) : w(std::max(0, w_)), h(std::max(0, h_)), p(size_t(w) * size_t(h) * 4, 0) {}
    std::uint8_t *px(int x, int y) { return &p[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
    const std::uint8_t *px(int x, int y) const { return &p[(size_t(y) * size_t(w) + size_t(x)) * 4]; }
    bool empty() const { return w <= 0 || h <= 0; }
};

// Pillow's AlphaComposite.c (PRECISION_BITS 7), one pixel: src over dst into dst.
inline void over(std::uint8_t *d, const std::uint8_t *s)
{
    if (s[3] == 0)
        return;
    const std::uint32_t blend = std::uint32_t(d[3]) * (255u - s[3]);
    const std::uint32_t outa255 = std::uint32_t(s[3]) * 255u + blend;
    const std::uint32_t coef1 = std::uint32_t(s[3]) * 255u * 255u * 128u / outa255;
    const std::uint32_t coef2 = 255u * 128u - coef1;
    auto div = [](std::uint32_t a) { return ((a >> 8) + a) >> 8; };
    for (int k = 0; k < 3; ++k) {
        const std::uint32_t tmp = s[k] * coef1 + d[k] * coef2;
        d[k] = std::uint8_t(div(tmp + (0x80u << 7)) >> 7);
    }
    d[3] = std::uint8_t(div(outa255 + 0x80u));
}

// dst.alpha_composite(src, (x, y)), clipped to dst.
inline void composite(Rgba &dst, const Rgba &src, int x, int y)
{
    const int x0 = std::max(0, x), y0 = std::max(0, y), x1 = std::min(dst.w, x + src.w), y1 = std::min(dst.h, y + src.h);
    for (int yy = y0; yy < y1; ++yy)
        for (int xx = x0; xx < x1; ++xx)
            over(dst.px(xx, yy), src.px(xx - x, yy - y));
}

// The scripts' paste(): alpha through the table [round(i·a)], then alpha_composite at (round(x), round(y)).
inline void paste(Rgba &dst, const Rgba &src, double x, double y, double a = 1.0)
{
    if (a <= 0 || src.empty())
        return;
    if (a >= 1) {
        composite(dst, src, pyRound(x), pyRound(y));
        return;
    }
    std::uint8_t lut[256];
    for (int i = 0; i < 256; ++i)
        lut[i] = std::uint8_t(pyRound(i * a));
    Rgba s = src;
    for (size_t i = 3; i < s.p.size(); i += 4)
        s.p[i] = lut[s.p[i]];
    composite(dst, s, pyRound(x), pyRound(y));
}

// Image.crop((l, t, r, b)): outside the image is clear.
inline Rgba crop(const Rgba &s, int l, int t, int r, int b)
{
    Rgba out(r - l, b - t);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const int sx = x + l, sy = y + t;
            if (sx >= 0 && sy >= 0 && sx < s.w && sy < s.h)
                std::copy_n(s.px(sx, sy), 4, out.px(x, y));
        }
    return out;
}

// The scripts' clip(): the part of src (placed at round(x), round(y)) inside the box, pasted.
inline void clip(Rgba &dst, const Rgba &src, double xf, double yf, double bl, double bt, double br, double bb, double a = 1.0)
{
    const int x = pyRound(xf), y = pyRound(yf);
    const int l = std::max(0, pyRound(bl) - x), t = std::max(0, pyRound(bt) - y);
    const int r = std::min(src.w, pyRound(br) - x), b = std::min(src.h, pyRound(bb) - y);
    if (r > l && b > t)
        paste(dst, crop(src, l, t, r, b), x + l, y + t, a);
}

// An 8-bit coverage mask as a layer of one colour.
inline Rgba tint(const gold::M8 &m, Rgb c)
{
    Rgba out(m.w, m.h);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            const std::uint8_t v = m.at(x, y);
            if (!v)
                continue;
            std::uint8_t *q = out.px(x, y);
            q[0] = std::uint8_t(c.r), q[1] = std::uint8_t(c.g), q[2] = std::uint8_t(c.b), q[3] = v;
        }
    return out;
}

// texttile(): the text shrunk 1 px at a time (not below 24) until getlength fits, drawn with
// anchor 'ls' at (2 − left, 2 − top) on a tile 4 px larger than its ink box.
struct Tile
{
    Rgba im;
    QString text, role;
    int size = 0;
    TextBox box;
    double advance = 0;
    int width() const { return im.w; }
    int height() const { return im.h; }
};

inline Tile textTile(const GoldText &g, const QString &role, const QString &txt, int size, Rgb color, double maxWidth = 1600)
{
    while (g.length(role, size, txt) > maxWidth && size > 24)
        --size;
    Tile t;
    t.text = txt, t.role = role, t.size = size;
    t.box = g.bbox(role, size, txt);
    t.advance = g.length(role, size, txt);
    gold::M8 m(t.box.right - t.box.left + 4, t.box.bottom - t.box.top + 4);
    g.draw(m, role, size, txt, 2 - t.box.left, 2 - t.box.top);
    t.im = tint(m, color);
    return t;
}


// A text tile kept as its coverage (a quarter of the memory), tinted when drawn.
struct MaskTile
{
    gold::M8 m;
    TextBox box;
    Rgb c;
    explicit MaskTile(const Tile &t, Rgb col) : m(t.width(), t.height()), box(t.box), c(col)
    {
        for (size_t i = 0; i < m.p.size(); ++i)
            m.p[i] = t.im.p[i * 4 + 3];
    }
    Rgba image() const { return tint(m, c); }
    int width() const { return m.w; }
    int height() const { return m.h; }
};

// ImageDraw.rounded_rectangle((0, 0, w − 1, h − 1), radius=r) filled: corner circles of
// diameter d = min(w − 1, h − 1, 2r) (joined into a capsule when they meet).
inline Rgba rect(int w, int h, Rgb c, double radius = 0)
{
    Rgba out(w, h);
    const int x1 = w - 1, y1 = h - 1;
    double d = std::min({double(x1), double(y1), radius * 2});
    if (d >= x1 - 1)
        d = x1;
    if (d >= y1 - 1)
        d = std::min(d, double(y1));
    const double rr = (d + 0.85) / 2, cl = d / 2, cr = x1 - d / 2, ct = d / 2, cb = y1 - d / 2;
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double dx = x < cl ? cl - x : (x > cr ? x - cr : 0), dy = y < ct ? ct - y : (y > cb ? y - cb : 0);
            if (d > 0 && dx * dx + dy * dy > rr * rr)
                continue;
            std::uint8_t *q = out.px(x, y);
            q[0] = std::uint8_t(c.r), q[1] = std::uint8_t(c.g), q[2] = std::uint8_t(c.b), q[3] = 255;
        }
    return out;
}

// line_layer(points, width, colour): returned as a tile with its origin.
struct Layer
{
    Rgba im;
    int x = 0, y = 0;
};

// Coverage of a thick polyline (butt ends, round inner joins) and of discs, accumulated as the
// maximum per pixel over each piece's own box.
struct Coverage
{
    int x0 = 0, y0 = 0;
    gold::MF c;
    Coverage(int x0_, int y0_, int w, int h) : x0(x0_), y0(y0_), c(w, h, 0.0f) {}
    void put(int x, int y, double v)
    {
        x -= x0, y -= y0;
        if (x >= 0 && y >= 0 && x < c.w && y < c.h && v > c.at(x, y))
            c.at(x, y) = float(v);
    }
    void segment(double ax, double ay, double bx, double by, double hw, bool joinAtA)
    {
        const double vx = bx - ax, vy = by - ay, len = std::hypot(vx, vy);
        const int l = int(std::floor(std::min(ax, bx) - hw - 2)), r = int(std::ceil(std::max(ax, bx) + hw + 2));
        const int t = int(std::floor(std::min(ay, by) - hw - 2)), b = int(std::ceil(std::max(ay, by) + hw + 2));
        for (int y = t; y < b; ++y)
            for (int x = l; x < r; ++x) {
                const double px = x + 0.5, py = y + 0.5;
                double v = 0;
                if (len > 0) {
                    const double ux = vx / len, uy = vy / len;
                    const double along = (px - ax) * ux + (py - ay) * uy, across = std::abs((px - ax) * uy - (py - ay) * ux);
                    v = clamp01(hw + 0.5 - across) * clamp01(std::min(along, len - along) + 0.5);
                }
                if (joinAtA)
                    v = std::max(v, clamp01(hw + 0.5 - std::hypot(px - ax, py - ay)));
                if (v > 0)
                    put(x, y, v);
            }
    }
    void polyline(const std::vector<std::pair<double, double>> &p, double hw)
    {
        for (size_t i = 0; i + 1 < p.size(); ++i)
            segment(p[i].first, p[i].second, p[i + 1].first, p[i + 1].second, hw, i > 0);
    }
    void disc(double cx, double cy, double r)
    {
        for (int y = int(std::floor(cy - r - 2)); y < int(std::ceil(cy + r + 2)); ++y)
            for (int x = int(std::floor(cx - r - 2)); x < int(std::ceil(cx + r + 2)); ++x)
                put(x, y, clamp01(r + 0.5 - std::hypot(x + 0.5 - cx, y + 0.5 - cy)));
    }
    // Paint this coverage in one colour over `into` (src-over: later colours cover earlier ones).
    void paintOver(Rgba &into, int ox, int oy, Rgb col) const
    {
        Rgba l(c.w, c.h);
        for (int y = 0; y < c.h; ++y)
            for (int x = 0; x < c.w; ++x) {
                const float v = c.at(x, y);
                if (v <= 0)
                    continue;
                std::uint8_t *q = l.px(x, y);
                q[0] = std::uint8_t(col.r), q[1] = std::uint8_t(col.g), q[2] = std::uint8_t(col.b), q[3] = std::uint8_t(pyRound(v * 255));
            }
        composite(into, l, x0 - ox, y0 - oy);
    }
};

// The 3× drawing's points snap to the centre of 3× pixel round(3p), (round(3p) + ½) / 3 in frame
// pixels, and its width to round(3w) / 3.
inline std::vector<std::pair<double, double>> snap3(const std::vector<std::pair<double, double>> &pts)
{
    std::vector<std::pair<double, double>> p;
    for (const auto &q : pts)
        p.push_back({(std::nearbyint(q.first * 3) + 0.5) / 3.0, (std::nearbyint(q.second * 3) + 0.5) / 3.0});
    return p;
}

// line_layer(points, width, colour): the line as a tile with its origin.
inline Layer lineLayer(const std::vector<std::pair<double, double>> &pts, double width, Rgb col)
{
    Layer out;
    if (pts.size() < 2)
        return out;
    const auto p = snap3(pts);
    const double hw = std::nearbyint(width * 3) / 6.0;
    double l = 1e9, t = 1e9, r = -1e9, b = -1e9;
    for (const auto &q : p)
        l = std::min(l, q.first), t = std::min(t, q.second), r = std::max(r, q.first), b = std::max(b, q.second);
    out.x = int(std::floor(l - hw - 2)), out.y = int(std::floor(t - hw - 2));
    Coverage c(out.x, out.y, int(std::ceil(r + hw + 2)) - out.x, int(std::ceil(b + hw + 2)) - out.y);
    c.polyline(p, hw);
    out.im = Rgba(c.c.w, c.c.h);
    c.paintOver(out.im, out.x, out.y, col);
    return out;
}

inline void paste(Rgba &dst, const Layer &l, double a = 1.0) { paste(dst, l.im, l.x, l.y, a); }


// ImageFilter.GaussianBlur(radius) on an 'L' image: Pillow's three box-blur passes each way
// (BoxBlur.c, the fixed-point 24-bit weights and the edge-repeat accumulator, float radius).
inline void boxBlurLine(const std::uint8_t *in, std::uint8_t *out, int n, float fr)
{
    const int radius = int(fr), lastx = n - 1;
    const std::uint32_t ww = std::uint32_t(float(1 << 24) / (fr * 2 + 1));
    const std::uint32_t fw = ((1u << 24) - std::uint32_t(radius * 2 + 1) * ww) / 2;
    const int edgeA = std::min(radius + 1, n), edgeB = std::max(n - radius - 1, 0);
    std::uint32_t acc = in[0] * std::uint32_t(radius + 1), bulk;
    for (int x = 0; x < edgeA - 1; ++x)
        acc += in[x];
    acc += in[lastx] * std::uint32_t(radius - edgeA + 1);
    auto step = [&](int x, int sub, int add, int left, int right) {
        acc += in[add] - in[sub];
        bulk = acc * ww + (in[left] + in[right]) * fw;
        out[x] = std::uint8_t((bulk + (1u << 23)) >> 24);
    };
    if (edgeA <= edgeB) {
        for (int x = 0; x < edgeA; ++x) step(x, 0, x + radius, 0, x + radius + 1);
        for (int x = edgeA; x < edgeB; ++x) step(x, x - radius - 1, x + radius, x - radius - 1, x + radius + 1);
        for (int x = edgeB; x <= lastx; ++x) step(x, x - radius - 1, lastx, x - radius - 1, lastx);
    } else {
        for (int x = 0; x < edgeB; ++x) step(x, 0, x + radius, 0, x + radius + 1);
        for (int x = edgeB; x < edgeA; ++x) step(x, 0, lastx, 0, lastx);
        for (int x = edgeA; x <= lastx; ++x) step(x, x - radius - 1, lastx, x - radius - 1, lastx);
    }
}

inline gold::M8 gaussianBlur(const gold::M8 &src, float radius, int passes = 3)
{
    const float sigma2 = radius * radius / passes;
    const float L = std::sqrt(12.0f * sigma2 + 1.0f);
    const float l = std::floor((L - 1.0f) / 2.0f);
    float a = (2 * l + 1) * (l * (l + 1) - 3 * sigma2);
    a /= 6 * (sigma2 - (l + 1) * (l + 1));
    const float r = l + a;
    gold::M8 m = src;
    std::vector<std::uint8_t> line(size_t(std::max(m.w, m.h))), out(line.size());
    if (r != 0) {
        for (int k = 0; k < passes; ++k)
            for (int y = 0; y < m.h; ++y) {
                std::copy_n(&m.p[size_t(y) * m.w], m.w, line.data());
                boxBlurLine(line.data(), out.data(), m.w, r);
                std::copy_n(out.data(), m.w, &m.p[size_t(y) * m.w]);
            }
        for (int k = 0; k < passes; ++k)
            for (int x = 0; x < m.w; ++x) {
                for (int y = 0; y < m.h; ++y)
                    line[size_t(y)] = m.at(x, y);
                boxBlurLine(line.data(), out.data(), m.h, r);
                for (int y = 0; y < m.h; ++y)
                    m.at(x, y) = out[size_t(y)];
            }
    }
    return m;
}

// Pillow's BLEND (paste with a mask): b + (a − b)·m/255, rounded as Paste.c does.
inline std::uint8_t blend8(int a, int b, int m)
{
    const int tmp = (a - b) * m + 128;
    return std::uint8_t(b + (((tmp >> 8) + tmp) >> 8));
}

// Anti-aliased coverage of a rounded rectangle ([l, r] × [t, b] in pixel edges, corner radius rad):
// what a supersampled ImageDraw shape comes to after the Lanczos reduction.
inline double rrectCoverage(double px, double py, double l, double t, double r, double b, double rad)
{
    const double cx = (l + r) / 2, cy = (t + b) / 2, hx = (r - l) / 2, hy = (b - t) / 2;
    rad = std::min({rad, hx, hy});
    const double qx = std::abs(px - cx) - (hx - rad), qy = std::abs(py - cy) - (hy - rad);
    const double ox = std::max(qx, 0.0), oy = std::max(qy, 0.0);
    const double sdf = std::hypot(ox, oy) + std::min(std::max(qx, qy), 0.0) - rad;
    return clamp01(0.5 - sdf);
}


// Pillow's Max/MinFilter(size) on 'L' (zero outside the image) and ImageChops.subtract.
inline gold::M8 rankFilter(const gold::M8 &m, int size, bool max)
{
    const int r = size / 2;
    gold::M8 h(m.w, m.h), out(m.w, m.h);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            int v = max ? 0 : 255;
            for (int k = -r; k <= r; ++k) {
                const int s = (x + k < 0 || x + k >= m.w) ? m.at(std::clamp(x + k, 0, m.w - 1), y) : m.at(x + k, y);
                v = max ? std::max(v, s) : std::min(v, s);
            }
            h.at(x, y) = std::uint8_t(v);
        }
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            int v = max ? 0 : 255;
            for (int k = -r; k <= r; ++k) {
                const int s = h.at(x, std::clamp(y + k, 0, m.h - 1));
                v = max ? std::max(v, s) : std::min(v, s);
            }
            out.at(x, y) = std::uint8_t(v);
        }
    return out;
}

inline Rgba flipH(const Rgba &s)
{
    Rgba o(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        for (int x = 0; x < s.w; ++x)
            std::copy_n(s.px(s.w - 1 - x, y), 4, o.px(x, y));
    return o;
}
inline Rgba flipV(const Rgba &s)
{
    Rgba o(s.w, s.h);
    for (int y = 0; y < s.h; ++y)
        std::copy_n(s.px(0, s.h - 1 - y), size_t(s.w) * 4, o.px(0, y));
    return o;
}
// A layer drawn into a w × h tile (clipped), as Image.new + line + resize leaves it.
inline Rgba intoTile(const Layer &l, int w, int h)
{
    Rgba o(w, h);
    composite(o, l.im, l.x, l.y);
    return o;
}


// Image.blend(a, b, w): a + w·(b − a) per channel in float, truncated (Blend.c).
inline Rgba blendImg(const Rgba &a, const Rgba &b, float w)
{
    if (w == 0.0f)
        return a;
    if (w == 1.0f)
        return b;
    Rgba o(a.w, a.h);
    for (size_t i = 0; i < o.p.size(); ++i)
        o.p[i] = std::uint8_t(float(int(a.p[i])) + w * float(int(b.p[i]) - int(a.p[i])));
    return o;
}


// ImageDraw.line(points, width) without anti-aliasing, on whole-pixel coordinates (pixel centres).
inline void hardLine(Rgba &im, const std::vector<std::pair<double, double>> &p, double width, Rgb c, int alpha = 255)
{
    const double hw = width / 2;
    for (size_t i = 0; i + 1 < p.size(); ++i) {
        const double ax = p[i].first, ay = p[i].second, bx = p[i + 1].first, by = p[i + 1].second;
        const double vx = bx - ax, vy = by - ay, len = std::hypot(vx, vy);
        if (len <= 0)
            continue;
        const double ux = vx / len, uy = vy / len;
        for (int y = int(std::floor(std::min(ay, by) - hw - 1)); y <= int(std::ceil(std::max(ay, by) + hw + 1)); ++y)
            for (int x = int(std::floor(std::min(ax, bx) - hw - 1)); x <= int(std::ceil(std::max(ax, bx) + hw + 1)); ++x) {
                if (x < 0 || y < 0 || x >= im.w || y >= im.h)
                    continue;
                // Pillow's wide lines cover the pixels −⌊(w−1)/2⌋ … ⌊w/2⌋ across, ends included.
                const double along = (x - ax) * ux + (y - ay) * uy;
                const double across = (x - ax) * -uy + (y - ay) * ux - (int(width) % 2 == 0 ? 0.5 : 0.0);
                if (along < -1e-9 || along > len + 1e-9 || across <= -hw || across > hw)
                    continue;
                std::uint8_t *q = im.px(x, y);
                q[0] = std::uint8_t(c.r), q[1] = std::uint8_t(c.g), q[2] = std::uint8_t(c.b), q[3] = std::uint8_t(alpha);
            }
    }
}

// Python's f'{n:,}': thousands grouped with commas.
inline QString grouped(long long n)
{
    QString s = QString::number(n < 0 ? -n : n);
    for (int i = int(s.size()) - 3; i > 0; i -= 3)
        s.insert(i, QLatin1Char(','));
    return n < 0 ? QLatin1Char('-') + s : s;
}


// ImageDraw.ellipse((l, t, r, b)) filled, without anti-aliasing (pixel centres inside).
inline void hardDisc(Rgba &im, double l, double t, double r, double b, Rgb c, int alpha = 255)
{
    const double cx = (l + r) / 2, cy = (t + b) / 2, rx = (r - l + 1) / 2, ry = (b - t + 1) / 2;
    for (int y = int(std::floor(t)); y <= int(std::ceil(b)); ++y)
        for (int x = int(std::floor(l)); x <= int(std::ceil(r)); ++x) {
            if (x < 0 || y < 0 || x >= im.w || y >= im.h)
                continue;
            const double dx = (x - cx) / rx, dy = (y - cy) / ry;
            if (dx * dx + dy * dy > 1)
                continue;
            std::uint8_t *q = im.px(x, y);
            q[0] = std::uint8_t(c.r), q[1] = std::uint8_t(c.g), q[2] = std::uint8_t(c.b), q[3] = std::uint8_t(alpha);
        }
}

// RGBA → RGBa → RGBA as Pillow's Convert.c does around a resampling transform.
inline void premultiply(Rgba &im)
{
    for (size_t i = 0; i < im.p.size(); i += 4)
        for (int k = 0; k < 3; ++k) {
            const unsigned tmp = unsigned(im.p[i + size_t(k)]) * im.p[i + 3] + 128;
            im.p[i + size_t(k)] = std::uint8_t(((tmp >> 8) + tmp) >> 8);
        }
}
inline void unpremultiply(Rgba &im)
{
    for (size_t i = 0; i < im.p.size(); i += 4) {
        const unsigned a = im.p[i + 3];
        if (a == 255 || a == 0)
            continue;
        for (int k = 0; k < 3; ++k)
            im.p[i + size_t(k)] = std::uint8_t(std::min(255u, 255u * im.p[i + size_t(k)] / a));
    }
}

// Image.transform(size, PERSPECTIVE, (a…h), BICUBIC) of an RGBA image (Geometry.c: pixel centres,
// Pillow's cubic with p2 = −v1 + v3, rows outside repeated from the last valid one, 0 outside).
inline Rgba perspectiveBicubic(const Rgba &src, const double c[8])
{
    Rgba in = src;
    premultiply(in);
    Rgba out(src.w, src.h);
    auto cubic = [](double v1, double v2, double v3, double v4, double d) {
        const double p1 = v2, p2 = -v1 + v3, p3 = 2 * (v1 - v2) + v3 - v4, p4 = -v1 + v2 - v3 + v4;
        return p1 + d * (p2 + d * (p3 + d * p4));
    };
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x) {
            const double xo = x + 0.5, yo = y + 0.5, den = c[6] * xo + c[7] * yo + 1;
            double xin = (c[0] * xo + c[1] * yo + c[2]) / den, yin = (c[3] * xo + c[4] * yo + c[5]) / den;
            if (xin < 0.0 || xin >= in.w || yin < 0.0 || yin >= in.h)
                continue;
            xin -= 0.5, yin -= 0.5;
            int xi = int(std::floor(xin)), yi = int(std::floor(yin));
            const double dx = xin - xi, dy = yin - yi;
            --xi, --yi;
            int xs[4];
            for (int k = 0; k < 4; ++k)
                xs[k] = std::clamp(xi + k, 0, in.w - 1);
            std::uint8_t *o = out.px(x, y);
            for (int b = 0; b < 4; ++b) {
                auto row = [&](int yy) {
                    const std::uint8_t *r = in.px(0, yy);
                    return cubic(r[xs[0] * 4 + b], r[xs[1] * 4 + b], r[xs[2] * 4 + b], r[xs[3] * 4 + b], dx);
                };
                const double v1 = row(std::clamp(yi, 0, in.h - 1));
                const double v2 = yi + 1 >= 0 && yi + 1 < in.h ? row(yi + 1) : v1;
                const double v3 = yi + 2 >= 0 && yi + 2 < in.h ? row(yi + 2) : v2;
                const double v4 = yi + 3 >= 0 && yi + 3 < in.h ? row(yi + 3) : v3;
                const double v = cubic(v1, v2, v3, v4, dy);
                o[b] = v <= 0 ? 0 : (v >= 255 ? 255 : std::uint8_t(v));
            }
        }
    unpremultiply(out);
    return out;
}


// ImageDraw.polygon(points, fill) without anti-aliasing: the integer (truncated) points, the pixels
// whose whole coordinates lie inside or on the outline (nonzero winding).
inline void polygonFill(gold::M8 &m, const std::vector<std::pair<double, double>> &ptsIn, int value)
{
    if (ptsIn.size() < 3)
        return;
    std::vector<std::pair<double, double>> p;
    for (const auto &q : ptsIn)
        p.push_back({double(int(q.first)), double(int(q.second))});
    double t = 1e9, b = -1e9;
    for (const auto &q : p)
        t = std::min(t, q.second), b = std::max(b, q.second);
    for (int y = std::max(0, int(t)); y <= std::min(m.h - 1, int(b)); ++y) {
        std::vector<double> xs;
        for (size_t i = 0; i < p.size(); ++i) {
            const auto &a = p[i], &c = p[(i + 1) % p.size()];
            if (a.second == c.second) {
                if (a.second == y) // a horizontal edge on this row is drawn
                    for (int x = int(std::min(a.first, c.first)); x <= int(std::max(a.first, c.first)); ++x)
                        if (x >= 0 && x < m.w)
                            m.at(x, y) = std::uint8_t(value);
                continue;
            }
            const double lo = std::min(a.second, c.second), hi = std::max(a.second, c.second);
            if (y < lo || y >= hi)
                continue;
            xs.push_back(a.first + (y - a.second) * (c.first - a.first) / (c.second - a.second));
        }
        std::sort(xs.begin(), xs.end());
        for (size_t i = 0; i + 1 < xs.size(); i += 2)
            for (int x = std::max(0, int(std::ceil(xs[i] - 1e-9))); x <= std::min(m.w - 1, int(std::floor(xs[i + 1] + 1e-9))); ++x)
                m.at(x, y) = std::uint8_t(value);
    }
}

} // namespace drift::scene::draft
