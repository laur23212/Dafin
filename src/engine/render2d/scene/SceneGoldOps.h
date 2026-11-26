// SceneGoldOps.h — image operations for the gold1x scene profile (the Golden Run 18 effects,
// docs/scene-titles.md "gold1x"). Plain C++ (no Qt, no Skia) so they can be tested on their own:
// the reference renderer (tools/scene-ref/golden.py) runs the same steps with OpenCV and numpy.
//
// The masks are 8-bit or float images; a Frame is a 1920 × 1080 RGB image that starts black.
// Close to cv2 where the effects lean on it: resize (area / linear / cubic, half-pixel centres),
// remap and warpAffine (bilinear / bicubic, constant 0 border), warpPerspective (bilinear),
// distanceTransform (L2 chamfer 3 × 3 and 5 × 5, and exact), and numpy's default_rng (PCG64 with
// SeedSequence, uniform, bounded integers) bit for bit, so seeded particles land where they did.
#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <vector>

namespace drift::scene::gold {

constexpr int FW = 1920, FH = 1080;

inline double clamp01(double v) { return v < 0.0 ? 0.0 : (v > 1.0 ? 1.0 : v); }
inline double smooth(double v) { v = clamp01(v); return v * v * (3.0 - 2.0 * v); }
inline double outc(double v) { v = clamp01(v); return 1.0 - (1.0 - v) * (1.0 - v) * (1.0 - v); }
inline double quint(double v) { v = clamp01(v); return v * v * v * (v * (v * 6.0 - 15.0) + 10.0); }
inline double roundEven(double v) { return std::nearbyint(v); } // Python round(), under FE_TONEAREST
inline std::uint8_t sat8(double v) { return std::uint8_t(v <= 0.0 ? 0.0 : (v >= 255.0 ? 255.0 : v)); } // astype: trunc
inline std::uint8_t sat8r(double v) { return std::uint8_t(v <= 0.0 ? 0 : (v >= 255.0 ? 255 : int(std::lround(v)))); } // cv2 saturate_cast

template <typename T>
struct Img
{
    int w = 0, h = 0;
    std::vector<T> p;
    Img() = default;
    Img(int w_, int h_, T fill = T(0)) : w(std::max(0, w_)), h(std::max(0, h_)), p(size_t(w) * size_t(h), fill) {}
    bool empty() const { return w <= 0 || h <= 0; }
    T &at(int x, int y) { return p[size_t(y) * size_t(w) + size_t(x)]; }
    T at(int x, int y) const { return p[size_t(y) * size_t(w) + size_t(x)]; }
    T get(int x, int y) const { return (x < 0 || y < 0 || x >= w || y >= h) ? T(0) : at(x, y); }
};
using M8 = Img<std::uint8_t>;
using MF = Img<float>;

struct Frame
{
    std::vector<std::uint8_t> p = std::vector<std::uint8_t>(size_t(FW) * FH * 3, 0);
    std::uint8_t *px(int x, int y) { return &p[(size_t(y) * FW + size_t(x)) * 3]; }
};

// ---- crops, borders ---------------------------------------------------------------------------
// numpy m[max(0, ys.min() - before):ys.max() + after, …] of the pixels above `thr`.
inline M8 cropInk(const M8 &m, int before, int after, int thr = 0)
{
    int x0 = m.w, y0 = m.h, x1 = -1, y1 = -1;
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x)
            if (m.at(x, y) > thr) {
                x0 = std::min(x0, x); x1 = std::max(x1, x);
                y0 = std::min(y0, y); y1 = std::max(y1, y);
            }
    if (x1 < 0)
        return m;
    const int l = std::max(0, x0 - before), t = std::max(0, y0 - before);
    const int r = std::min(m.w, x1 + after), b = std::min(m.h, y1 + after);
    M8 out(r - l, b - t);
    for (int y = 0; y < out.h; ++y)
        for (int x = 0; x < out.w; ++x)
            out.at(x, y) = m.at(l + x, t + y);
    return out;
}

inline M8 border(const M8 &m, int top, int bottom, int left, int right)
{
    M8 out(m.w + left + right, m.h + top + bottom);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x)
            out.at(x + left, y + top) = m.at(x, y);
    return out;
}

inline M8 centred(const M8 &m, int w, int h)
{
    M8 out(w, h);
    const int oy = (h - m.h) / 2, ox = (w - m.w) / 2;
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x)
            if (ox + x >= 0 && oy + y >= 0 && ox + x < w && oy + y < h)
                out.at(ox + x, oy + y) = m.at(x, y);
    return out;
}

inline M8 flipV(const M8 &m)
{
    M8 out(m.w, m.h);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x)
            out.at(x, m.h - 1 - y) = m.at(x, y);
    return out;
}

// ---- resize (cv2.resize) ------------------------------------------------------------------------
// One axis at a time, half-pixel centres, edge pixels repeated (cv2's border for resize).
template <typename T>
Img<float> resizeAxisLinear(const Img<T> &src, int nw, int nh)
{
    Img<float> tmp(nw, src.h), out(nw, nh);
    const double sx = double(src.w) / nw, sy = double(src.h) / nh;
    for (int x = 0; x < nw; ++x) {
        double fx = (x + 0.5) * sx - 0.5;
        int x0 = int(std::floor(fx));
        double a = fx - x0;
        if (x0 < 0) { x0 = 0; a = 0; }
        const int x1 = std::min(src.w - 1, x0 + 1);
        if (x0 >= src.w - 1) { x0 = src.w - 1; a = 0; }
        for (int y = 0; y < src.h; ++y)
            tmp.at(x, y) = float(src.at(x0, y) * (1 - a) + src.at(x1, y) * a);
    }
    for (int y = 0; y < nh; ++y) {
        double fy = (y + 0.5) * sy - 0.5;
        int y0 = int(std::floor(fy));
        double a = fy - y0;
        if (y0 < 0) { y0 = 0; a = 0; }
        const int y1 = std::min(tmp.h - 1, y0 + 1);
        if (y0 >= tmp.h - 1) { y0 = tmp.h - 1; a = 0; }
        for (int x = 0; x < nw; ++x)
            out.at(x, y) = float(tmp.at(x, y0) * (1 - a) + tmp.at(x, y1) * a);
    }
    return out;
}

inline double cubicW(double x)
{
    const double A = -0.75;
    x = std::fabs(x);
    if (x <= 1.0)
        return ((A + 2) * x - (A + 3)) * x * x + 1;
    if (x < 2.0)
        return ((A * x - 5 * A) * x + 8 * A) * x - 4 * A;
    return 0.0;
}

template <typename T>
Img<float> resizeCubicF(const Img<T> &src, int nw, int nh)
{
    Img<float> tmp(nw, src.h), out(nw, nh);
    const double sx = double(src.w) / nw, sy = double(src.h) / nh;
    for (int x = 0; x < nw; ++x) {
        const double fx = (x + 0.5) * sx - 0.5;
        const int ix = int(std::floor(fx));
        const double a = fx - ix;
        for (int y = 0; y < src.h; ++y) {
            double s = 0;
            for (int k = -1; k <= 2; ++k)
                s += cubicW(k - a) * src.at(std::clamp(ix + k, 0, src.w - 1), y);
            tmp.at(x, y) = float(s);
        }
    }
    for (int y = 0; y < nh; ++y) {
        const double fy = (y + 0.5) * sy - 0.5;
        const int iy = int(std::floor(fy));
        const double a = fy - iy;
        for (int x = 0; x < nw; ++x) {
            double s = 0;
            for (int k = -1; k <= 2; ++k)
                s += cubicW(k - a) * tmp.at(x, std::clamp(iy + k, 0, tmp.h - 1));
            out.at(x, y) = float(s);
        }
    }
    return out;
}

// INTER_AREA when shrinking: each output pixel is the area-weighted mean of the source it covers.
template <typename T>
Img<float> resizeAreaF(const Img<T> &src, int nw, int nh)
{
    const auto axis = [](int n, int m) { // weights of source cells for each of m outputs
        std::vector<std::vector<std::pair<int, double>>> w(static_cast<size_t>(m));
        const double s = double(n) / m;
        for (int i = 0; i < m; ++i) {
            const double a = i * s, b = (i + 1) * s;
            for (int k = int(std::floor(a)); k < int(std::ceil(b)) && k < n; ++k) {
                const double o = std::min(b, double(k + 1)) - std::max(a, double(k));
                if (o > 1e-9)
                    w[size_t(i)].push_back({k, o / s});
            }
        }
        return w;
    };
    const auto wx = axis(src.w, nw), wy = axis(src.h, nh);
    Img<float> tmp(nw, src.h), out(nw, nh);
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < nw; ++x) {
            double s = 0;
            for (const auto &[k, c] : wx[size_t(x)])
                s += c * src.at(k, y);
            tmp.at(x, y) = float(s);
        }
    for (int y = 0; y < nh; ++y)
        for (int x = 0; x < nw; ++x) {
            double s = 0;
            for (const auto &[k, c] : wy[size_t(y)])
                s += c * tmp.at(x, k);
            out.at(x, y) = float(s);
        }
    return out;
}

inline M8 to8(const MF &m)
{
    M8 out(m.w, m.h);
    for (size_t i = 0; i < m.p.size(); ++i)
        out.p[i] = sat8r(m.p[i]);
    return out;
}

enum class Interp { Area, Linear, Cubic };
// cv2.resize of an 8-bit mask: INTER_AREA when either side shrinks (as the scripts chose), else the
// interpolation asked for.
inline M8 resize8(const M8 &m, int nw, int nh, Interp how)
{
    nw = std::max(1, nw);
    nh = std::max(1, nh);
    if (m.empty())
        return M8(nw, nh);
    if (how == Interp::Area && (nw < m.w || nh < m.h))
        return to8(resizeAreaF(m, nw, nh));
    if (how == Interp::Cubic)
        return to8(resizeCubicF(m, nw, nh));
    return to8(resizeAxisLinear(m, nw, nh));
}

// ---- warps ---------------------------------------------------------------------------------------
template <typename T>
inline double bilinear(const Img<T> &s, double x, double y)
{
    const int x0 = int(std::floor(x)), y0 = int(std::floor(y));
    const double a = x - x0, b = y - y0;
    return (s.get(x0, y0) * (1 - a) + s.get(x0 + 1, y0) * a) * (1 - b) + (s.get(x0, y0 + 1) * (1 - a) + s.get(x0 + 1, y0 + 1) * a) * b;
}

inline double bicubic(const M8 &s, double x, double y)
{
    const int ix = int(std::floor(x)), iy = int(std::floor(y));
    const double a = x - ix, b = y - iy;
    double v = 0;
    for (int j = -1; j <= 2; ++j) {
        double row = 0;
        for (int i = -1; i <= 2; ++i)
            row += cubicW(i - a) * s.get(ix + i, iy + j);
        v += cubicW(j - b) * row;
    }
    return v;
}

// cv2.getRotationMatrix2D: [[α, β, (1−α)cx − β cy], [−β, α, β cx + (1−α) cy]], angle in degrees.
inline std::array<double, 6> rotationMatrix(double cx, double cy, double angleDeg)
{
    const double r = angleDeg * 3.14159265358979323846 / 180.0;
    const double al = std::cos(r), be = std::sin(r);
    return {al, be, (1 - al) * cx - be * cy, -be, al, be * cx + (1 - al) * cy};
}

// cv2.warpAffine(src, M, (w, h)): dst(x, y) = src(M⁻¹ (x, y)), constant 0 outside.
inline M8 warpAffine(const M8 &src, const std::array<double, 6> &M, int w, int h, bool cubic)
{
    const double det = M[0] * M[4] - M[1] * M[3];
    const double i0 = M[4] / det, i1 = -M[1] / det, i3 = -M[3] / det, i4 = M[0] / det;
    const double i2 = -(i0 * M[2] + i1 * M[5]), i5 = -(i3 * M[2] + i4 * M[5]);
    M8 out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            const double sx = i0 * x + i1 * y + i2, sy = i3 * x + i4 * y + i5;
            if (sx < -1 || sy < -1 || sx > src.w || sy > src.h)
                continue;
            out.at(x, y) = sat8r(cubic ? bicubic(src, sx, sy) : bilinear(src, sx, sy));
        }
    return out;
}

// The homography taking the four `from` points to the four `to` points (cv2.getPerspectiveTransform).
inline std::array<double, 9> homography(const double from[8], const double to[8])
{
    double A[8][9] = {};
    for (int i = 0; i < 4; ++i) {
        const double x = from[2 * i], y = from[2 * i + 1], u = to[2 * i], v = to[2 * i + 1];
        double *r0 = A[2 * i], *r1 = A[2 * i + 1];
        r0[0] = x; r0[1] = y; r0[2] = 1; r0[6] = -x * u; r0[7] = -y * u; r0[8] = u;
        r1[3] = x; r1[4] = y; r1[5] = 1; r1[6] = -x * v; r1[7] = -y * v; r1[8] = v;
    }
    for (int c = 0; c < 8; ++c) {
        int piv = c;
        for (int r = c + 1; r < 8; ++r)
            if (std::fabs(A[r][c]) > std::fabs(A[piv][c]))
                piv = r;
        for (int k = 0; k < 9; ++k)
            std::swap(A[c][k], A[piv][k]);
        const double d = A[c][c] == 0 ? 1e-12 : A[c][c];
        for (int k = c; k < 9; ++k)
            A[c][k] /= d;
        for (int r = 0; r < 8; ++r)
            if (r != c && A[r][c] != 0) {
                const double f = A[r][c];
                for (int k = c; k < 9; ++k)
                    A[r][k] -= f * A[c][k];
            }
    }
    return {A[0][8], A[1][8], A[2][8], A[3][8], A[4][8], A[5][8], A[6][8], A[7][8], 1.0};
}

// cv2.warpPerspective(src, H, (FW, FH)) with H mapping src → dst: sample through dst → src.
inline M8 warpPerspectiveToFrame(const M8 &src, const double srcPts[8], const double dstPts[8])
{
    const std::array<double, 9> Hi = homography(dstPts, srcPts);
    M8 out(FW, FH);
    for (int y = 0; y < FH; ++y)
        for (int x = 0; x < FW; ++x) {
            const double z = Hi[6] * x + Hi[7] * y + Hi[8];
            if (std::fabs(z) < 1e-12)
                continue;
            const double sx = (Hi[0] * x + Hi[1] * y + Hi[2]) / z, sy = (Hi[3] * x + Hi[4] * y + Hi[5]) / z;
            if (sx < -1 || sy < -1 || sx > src.w || sy > src.h)
                continue;
            out.at(x, y) = sat8r(bilinear(src, sx, sy));
        }
    return out;
}

// cv2.remap(src, mapx, mapy, INTER_LINEAR, BORDER_CONSTANT 0), maps given as a function.
template <typename T, typename Map>
Img<float> remapLinear(const Img<T> &src, int w, int h, Map map)
{
    Img<float> out(w, h);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            double sx, sy;
            map(x, y, sx, sy);
            if (sx < -1 || sy < -1 || sx > src.w || sy > src.h)
                continue;
            out.at(x, y) = float(bilinear(src, sx, sy));
        }
    return out;
}

// ---- distance transforms ----------------------------------------------------------------------
// cv2.distanceTransform(src, DIST_L2, 3 or 5): for each nonzero pixel the chamfer distance to the
// nearest zero pixel (outside the image is not zero). mask 3: a 0.955, b 1.3693; mask 5: a 1,
// b 1.4, c 2.1969.
inline MF chamfer(const M8 &src, int mask)
{
    const float INF = 1e9f;
    const float a = mask == 5 ? 1.0f : 0.955f, b = mask == 5 ? 1.4f : 1.3693f, c = 2.1969f;
    MF d(src.w, src.h);
    for (size_t i = 0; i < src.p.size(); ++i)
        d.p[i] = src.p[i] ? INF : 0.0f;
    const auto relax = [&](int x, int y, int dx, int dy, float wgt) {
        const int nx = x + dx, ny = y + dy;
        if (nx < 0 || ny < 0 || nx >= d.w || ny >= d.h)
            return;
        d.at(x, y) = std::min(d.at(x, y), d.at(nx, ny) + wgt);
    };
    for (int y = 0; y < d.h; ++y)
        for (int x = 0; x < d.w; ++x) {
            if (d.at(x, y) == 0.0f)
                continue;
            relax(x, y, -1, 0, a); relax(x, y, 0, -1, a); relax(x, y, -1, -1, b); relax(x, y, 1, -1, b);
            if (mask == 5) {
                relax(x, y, -2, -1, c); relax(x, y, -1, -2, c); relax(x, y, 1, -2, c); relax(x, y, 2, -1, c);
            }
        }
    for (int y = d.h - 1; y >= 0; --y)
        for (int x = d.w - 1; x >= 0; --x) {
            if (d.at(x, y) == 0.0f)
                continue;
            relax(x, y, 1, 0, a); relax(x, y, 0, 1, a); relax(x, y, 1, 1, b); relax(x, y, -1, 1, b);
            if (mask == 5) {
                relax(x, y, 2, 1, c); relax(x, y, 1, 2, c); relax(x, y, -1, 2, c); relax(x, y, -2, 1, c);
            }
        }
    for (float &v : d.p)
        if (v >= INF)
            v = 0.0f; // no zero pixel at all: cv2 leaves a huge value; nothing in the titles needs it
    return d;
}

// Exact Euclidean distance (DIST_MASK_PRECISE / scipy edt): Felzenszwalb & Huttenlocher.
inline MF exactDistance(const M8 &src)
{
    const double inf = 1e20;
    const int w = src.w, h = src.h, n = std::max(w, h);
    std::vector<double> g(size_t(w) * size_t(h)), f(static_cast<size_t>(n)), d(static_cast<size_t>(n)), z(static_cast<size_t>(n) + 1);
    std::vector<int> v(static_cast<size_t>(n));
    const auto pass = [&](int len) {
        int k = 0;
        v[0] = 0;
        z[0] = -inf;
        z[1] = inf;
        for (int q = 1; q < len; ++q) {
            double s;
            while (true) {
                const int vk = v[size_t(k)];
                s = ((f[size_t(q)] + double(q) * q) - (f[size_t(vk)] + double(vk) * vk)) / (2.0 * q - 2.0 * vk);
                if (s > z[size_t(k)] || k == 0)
                    break;
                --k;
            }
            if (s <= z[size_t(k)]) { // k == 0 and still not past: replace the first parabola
                v[0] = q;
                z[0] = -inf;
                z[1] = inf;
                continue;
            }
            ++k;
            v[size_t(k)] = q;
            z[size_t(k)] = s;
            z[size_t(k) + 1] = inf;
        }
        k = 0;
        for (int q = 0; q < len; ++q) {
            while (z[size_t(k) + 1] < q)
                ++k;
            const double r = q - v[size_t(k)];
            d[size_t(q)] = r * r + f[size_t(v[size_t(k)])];
        }
    };
    for (int x = 0; x < w; ++x) {
        for (int y = 0; y < h; ++y)
            f[size_t(y)] = src.at(x, y) ? inf : 0.0;
        pass(h);
        for (int y = 0; y < h; ++y)
            g[size_t(y) * size_t(w) + size_t(x)] = d[size_t(y)];
    }
    MF out(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x)
            f[size_t(x)] = g[size_t(y) * size_t(w) + size_t(x)];
        pass(w);
        for (int x = 0; x < w; ++x)
            out.at(x, y) = float(d[size_t(x)] >= inf * 0.5 ? 0.0 : std::sqrt(d[size_t(x)]));
    }
    return out;
}

// inside − outside of a thresholded mask; mask 0 = exact.
inline MF signedField(const M8 &m, int thr, int mask)
{
    M8 in(m.w, m.h), out(m.w, m.h);
    for (size_t i = 0; i < m.p.size(); ++i) {
        in.p[i] = m.p[i] > thr ? 1 : 0;
        out.p[i] = in.p[i] ? 0 : 1;
    }
    const MF a = mask ? chamfer(in, mask) : exactDistance(in);
    const MF b = mask ? chamfer(out, mask) : exactDistance(out);
    MF s(m.w, m.h);
    for (size_t i = 0; i < s.p.size(); ++i)
        s.p[i] = a.p[i] - b.p[i];
    return s;
}


// ---- more cv2 / numpy pieces (the later stages) ------------------------------------------------
// cv2.GaussianBlur(float, (0, 0), σ): kernel round(8σ + 1) | 1, BORDER_REFLECT_101.
inline MF gaussian(const MF &src, double sigma)
{
    const int k = int(std::lround(sigma * 8.0 + 1.0)) | 1, r = k / 2;
    std::vector<double> ker(static_cast<size_t>(k));
    double sum = 0.0;
    for (int i = 0; i < k; ++i) {
        ker[size_t(i)] = std::exp(-double(i - r) * (i - r) / (2.0 * sigma * sigma));
        sum += ker[size_t(i)];
    }
    for (double &v : ker)
        v /= sum;
    const auto refl = [](int i, int n) {
        if (n == 1)
            return 0;
        while (i < 0 || i >= n)
            i = i < 0 ? -i : 2 * n - 2 - i;
        return i;
    };
    MF tmp(src.w, src.h), out(src.w, src.h);
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            double v = 0;
            for (int i = 0; i < k; ++i)
                v += ker[size_t(i)] * src.at(refl(x + i - r, src.w), y);
            tmp.at(x, y) = float(v);
        }
    for (int y = 0; y < src.h; ++y)
        for (int x = 0; x < src.w; ++x) {
            double v = 0;
            for (int i = 0; i < k; ++i)
                v += ker[size_t(i)] * tmp.at(x, refl(y + i - r, src.h));
            out.at(x, y) = float(v);
        }
    return out;
}

// np.gradient: central differences inside, one-sided at the edges.
inline void gradient(const MF &m, MF &gx, MF &gy)
{
    gx = MF(m.w, m.h);
    gy = MF(m.w, m.h);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            gx.at(x, y) = m.w < 2 ? 0.f : (x == 0 ? m.at(1, y) - m.at(0, y) : x == m.w - 1 ? m.at(x, y) - m.at(x - 1, y) : (m.at(x + 1, y) - m.at(x - 1, y)) * 0.5f);
            gy.at(x, y) = m.h < 2 ? 0.f : (y == 0 ? m.at(x, 1) - m.at(x, 0) : y == m.h - 1 ? m.at(x, y) - m.at(x, y - 1) : (m.at(x, y + 1) - m.at(x, y - 1)) * 0.5f);
        }
}

// cv2.morphologyEx(MORPH_GRADIENT) with the 5 × 5 ellipse: dilation − erosion.
inline M8 morphGradient5(const M8 &m)
{
    static const int k[5][5] = {{0, 0, 1, 0, 0}, {1, 1, 1, 1, 1}, {1, 1, 1, 1, 1}, {1, 1, 1, 1, 1}, {0, 0, 1, 0, 0}};
    M8 out(m.w, m.h);
    for (int y = 0; y < m.h; ++y)
        for (int x = 0; x < m.w; ++x) {
            int hi = 0, lo = 255;
            for (int j = 0; j < 5; ++j)
                for (int i = 0; i < 5; ++i) {
                    if (!k[j][i])
                        continue;
                    const int xx = std::clamp(x + i - 2, 0, m.w - 1), yy = std::clamp(y + j - 2, 0, m.h - 1);
                    hi = std::max(hi, int(m.at(xx, yy)));
                    lo = std::min(lo, int(m.at(xx, yy)));
                }
            out.at(x, y) = std::uint8_t(hi - lo);
        }
    return out;
}

// cv2.polylines(…, thickness 2, LINE_AA) segment, max into a 0..1 layer (about 3.4 px across).
inline void strokeAA(MF &m, double x0, double y0, double x1, double y1)
{
    const double dx = x1 - x0, dy = y1 - y0, len2 = dx * dx + dy * dy;
    for (int y = int(std::min(y0, y1)) - 3; y <= int(std::max(y0, y1)) + 3; ++y)
        for (int x = int(std::min(x0, x1)) - 3; x <= int(std::max(x0, x1)) + 3; ++x) {
            if (x < 0 || y < 0 || x >= m.w || y >= m.h)
                continue;
            const double u = len2 > 0 ? std::clamp(((x - x0) * dx + (y - y0) * dy) / len2, 0.0, 1.0) : 0.0;
            const double d = std::hypot(x - (x0 + u * dx), y - (y0 + u * dy));
            m.at(x, y) = std::max(m.at(x, y), float(clamp01(2.2 - d)));
        }
}

// ---- composites into the frame ------------------------------------------------------------------
// render_stage25.put(): lighten.
inline void putMax(Frame &f, const M8 &m, double xf, double yf, const double col[3], double opacity)
{
    if (opacity <= 0 || m.empty())
        return;
    const int x = int(roundEven(xf)), y = int(roundEven(yf));
    const float k = float(clamp01(opacity) / 255.0);
    for (int j = std::max(0, y); j < std::min(FH, y + m.h); ++j)
        for (int i = std::max(0, x); i < std::min(FW, x + m.w); ++i) {
            const float a = float(m.at(i - x, j - y)) * k;
            std::uint8_t *p = f.px(i, j);
            for (int c = 0; c < 3; ++c)
                p[c] = std::max(p[c], sat8(double(a * float(col[c]))));
        }
}

// The stage26–29 blend(): normal alpha over what is there, truncated to uint8.
template <typename T>
void blendOver(Frame &f, const Img<T> &m, double xf, double yf, const double col[3], double opacity, double scale = 1.0 / 255.0)
{
    if (opacity <= 0 || m.empty())
        return;
    const int x = int(roundEven(xf)), y = int(roundEven(yf));
    const float k = float(clamp01(opacity) * scale);
    for (int j = std::max(0, y); j < std::min(FH, y + m.h); ++j)
        for (int i = std::max(0, x); i < std::min(FW, x + m.w); ++i) {
            const float a = float(m.at(i - x, j - y)) * k;
            if (a <= 0.0f)
                continue;
            std::uint8_t *p = f.px(i, j);
            for (int c = 0; c < 3; ++c)
                p[c] = sat8(double(float(p[c]) * (1.0f - a) + float(col[c]) * a));
        }
}

// ---- numpy's default_rng (PCG64 + SeedSequence) ------------------------------------------------
struct U128
{
    std::uint64_t hi = 0, lo = 0;
};
inline U128 add128(U128 a, U128 b)
{
    U128 r{a.hi + b.hi, a.lo + b.lo};
    if (r.lo < a.lo)
        ++r.hi;
    return r;
}
inline U128 mul128(U128 a, U128 b)
{
    const std::uint64_t a0 = a.lo & 0xffffffffu, a1 = a.lo >> 32, b0 = b.lo & 0xffffffffu, b1 = b.lo >> 32;
    const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
    const std::uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
    U128 r;
    r.lo = (mid << 32) | (p00 & 0xffffffffu);
    r.hi = p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32) + a.hi * b.lo + a.lo * b.hi;
    return r;
}

class NumpyRng
{
public:
    explicit NumpyRng(std::uint64_t seed)
    {
        const std::array<std::uint64_t, 4> s = seedState(seed);
        const U128 initstate{s[0], s[1]}, initseq{s[2], s[3]};
        inc_ = U128{(initseq.hi << 1) | (initseq.lo >> 63), (initseq.lo << 1) | 1u};
        state_ = U128{};
        step();
        state_ = add128(state_, initstate);
        step();
    }
    std::uint64_t next64()
    {
        step();
        const std::uint64_t x = state_.hi ^ state_.lo;
        const unsigned r = unsigned(state_.hi >> 58);
        return (x >> r) | (x << ((64u - r) & 63u));
    }
    double nextDouble() { return double(next64() >> 11) * (1.0 / 9007199254740992.0); }
    // rng.uniform(lo, hi, n)
    std::vector<double> uniform(double lo, double hi, int n)
    {
        std::vector<double> v(size_t(std::max(0, n)));
        for (double &x : v)
            x = lo + (hi - lo) * nextDouble();
        return v;
    }
    // rng.integers(lo, hi, n) for a range below 2³²: Lemire on 32-bit draws, whose high halves the
    // generator keeps for the next 32-bit draw, as numpy's PCG64 does.
    std::vector<std::int64_t> integers(std::int64_t lo, std::int64_t hi, int n)
    {
        std::vector<std::int64_t> v(size_t(std::max(0, n)), lo);
        const std::uint64_t rng = std::uint64_t(hi - lo - 1);
        if (rng == 0)
            return v;
        const auto n32 = [this]() -> std::uint32_t { // pcg64_next32: the high half waits for the next call
            if (hasHalf_) {
                hasHalf_ = false;
                return half_;
            }
            const std::uint64_t v = next64();
            hasHalf_ = true;
            half_ = std::uint32_t(v >> 32);
            return std::uint32_t(v);
        };
        const std::uint32_t ex = std::uint32_t(rng + 1);
        for (auto &x : v) {
            std::uint64_t m = std::uint64_t(n32()) * ex;
            std::uint32_t left = std::uint32_t(m);
            if (left < ex) {
                const std::uint32_t th = std::uint32_t((0xffffffffu - std::uint32_t(rng)) % ex);
                while (left < th) {
                    m = std::uint64_t(n32()) * ex;
                    left = std::uint32_t(m);
                }
            }
            x = lo + std::int64_t(m >> 32);
        }
        return v;
    }

private:
    void step()
    {
        static const U128 mult{0x2360ED051FC65DA4ull, 0x4385DF649FCCF645ull};
        state_ = add128(mul128(state_, mult), inc_);
    }
    static std::array<std::uint64_t, 4> seedState(std::uint64_t seed)
    {
        const std::uint32_t INIT_A = 0x43b0d7e5u, MULT_A = 0x931e8875u, INIT_B = 0x8b51f9ddu, MULT_B = 0x58f38dedu;
        const std::uint32_t MIX_L = 0xca01f9ddu, MIX_R = 0x4973f715u;
        std::vector<std::uint32_t> ent{std::uint32_t(seed)};
        if (seed >> 32)
            ent.push_back(std::uint32_t(seed >> 32));
        std::uint32_t hc = INIT_A;
        const auto hashmix = [&](std::uint32_t v) {
            v ^= hc;
            hc *= MULT_A;
            v *= hc;
            v ^= v >> 16;
            return v;
        };
        const auto mix = [&](std::uint32_t x, std::uint32_t y) {
            std::uint32_t r = MIX_L * x - MIX_R * y;
            r ^= r >> 16;
            return r;
        };
        std::array<std::uint32_t, 4> pool{};
        for (size_t i = 0; i < 4; ++i)
            pool[i] = hashmix(i < ent.size() ? ent[i] : 0u);
        for (size_t s = 0; s < 4; ++s)
            for (size_t d = 0; d < 4; ++d)
                if (s != d)
                    pool[d] = mix(pool[d], hashmix(pool[s]));
        for (size_t s = 4; s < ent.size(); ++s)
            for (size_t d = 0; d < 4; ++d)
                pool[d] = mix(pool[d], hashmix(ent[s]));
        std::uint32_t hb = INIT_B;
        std::array<std::uint32_t, 8> w{};
        for (size_t i = 0; i < 8; ++i) {
            std::uint32_t v = pool[i % 4];
            v ^= hb;
            hb *= MULT_B;
            v *= hb;
            v ^= v >> 16;
            w[i] = v;
        }
        std::array<std::uint64_t, 4> out{};
        for (size_t i = 0; i < 4; ++i)
            out[i] = std::uint64_t(w[2 * i]) | (std::uint64_t(w[2 * i + 1]) << 32);
        return out;
    }
    U128 state_, inc_;
    bool hasHalf_ = false;
    std::uint32_t half_ = 0;
};

} // namespace drift::scene::gold
