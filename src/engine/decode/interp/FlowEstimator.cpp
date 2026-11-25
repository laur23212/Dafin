#include "engine/decode/interp/FlowEstimator.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace drift::interp {
namespace {

struct Plane
{
    int width = 0;
    int height = 0;
    std::vector<float> v;

    void resize(int w, int h, float fill = 0.0f)
    {
        width = w;
        height = h;
        v.assign(static_cast<size_t>(w) * h, fill);
    }
    float &at(int x, int y) { return v[static_cast<size_t>(y) * width + x]; }
    float at(int x, int y) const { return v[static_cast<size_t>(y) * width + x]; }
    bool empty() const { return width <= 0 || height <= 0; }
};

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Bilinear sample with edge clamping. Clamping rather than zero-filling: a zero off the edge is
// a black frame the solver then tries to match motion against, and it drags every vector near
// the border outward.
float sampleBilinear(const Plane &p, float x, float y)
{
    if (p.empty())
        return 0.0f;
    x = clampf(x, 0.0f, static_cast<float>(p.width - 1));
    y = clampf(y, 0.0f, static_cast<float>(p.height - 1));
    const int x0 = static_cast<int>(x);
    const int y0 = static_cast<int>(y);
    const int x1 = std::min(x0 + 1, p.width - 1);
    const int y1 = std::min(y0 + 1, p.height - 1);
    const float fx = x - static_cast<float>(x0);
    const float fy = y - static_cast<float>(y0);
    const float top = p.at(x0, y0) * (1.0f - fx) + p.at(x1, y0) * fx;
    const float bottom = p.at(x0, y1) * (1.0f - fx) + p.at(x1, y1) * fx;
    return top * (1.0f - fy) + bottom * fy;
}

// Luma from an RGBA8888 row set. Rec.709 weights on the encoded values: this is a matching
// signal, not a colour conversion, and encoded luma is what gradient-based matching works best
// on anyway.
Plane lumaOf(const QImage &image, int targetW, int targetH)
{
    Plane out;
    const QImage scaled = (image.width() == targetW && image.height() == targetH)
        ? image.convertToFormat(QImage::Format_RGBA8888)
        : image.scaled(targetW, targetH, Qt::IgnoreAspectRatio, Qt::SmoothTransformation)
              .convertToFormat(QImage::Format_RGBA8888);
    if (scaled.isNull())
        return out;
    out.resize(scaled.width(), scaled.height());
    for (int y = 0; y < scaled.height(); ++y) {
        const quint8 *row = scaled.constScanLine(y);
        for (int x = 0; x < scaled.width(); ++x) {
            const int i = x * 4;
            out.at(x, y) = 0.2126f * row[i] + 0.7152f * row[i + 1] + 0.0722f * row[i + 2];
        }
    }
    return out;
}

Plane halfSize(const Plane &src)
{
    Plane out;
    if (src.width < 2 || src.height < 2)
        return out;
    out.resize(src.width / 2, src.height / 2);
    for (int y = 0; y < out.height; ++y) {
        for (int x = 0; x < out.width; ++x) {
            const int sx = x * 2;
            const int sy = y * 2;
            out.at(x, y) = 0.25f
                * (src.at(sx, sy) + src.at(std::min(sx + 1, src.width - 1), sy)
                   + src.at(sx, std::min(sy + 1, src.height - 1))
                   + src.at(std::min(sx + 1, src.width - 1), std::min(sy + 1, src.height - 1)));
        }
    }
    return out;
}

// Separable box blur, which is what makes the per-pixel least squares affordable: the window
// sums the solver needs are five of these rather than a loop over the window per pixel.
void boxBlur(Plane &plane, int radius)
{
    if (plane.empty() || radius <= 0)
        return;
    const int w = plane.width;
    const int h = plane.height;
    std::vector<float> tmp(plane.v.size(), 0.0f);

    for (int y = 0; y < h; ++y) {
        float running = 0.0f;
        for (int x = -radius; x <= radius; ++x)
            running += plane.at(std::clamp(x, 0, w - 1), y);
        for (int x = 0; x < w; ++x) {
            tmp[static_cast<size_t>(y) * w + x] = running / static_cast<float>(2 * radius + 1);
            const int add = std::clamp(x + radius + 1, 0, w - 1);
            const int drop = std::clamp(x - radius, 0, w - 1);
            running += plane.at(add, y) - plane.at(drop, y);
        }
    }
    for (int x = 0; x < w; ++x) {
        float running = 0.0f;
        for (int y = -radius; y <= radius; ++y)
            running += tmp[static_cast<size_t>(std::clamp(y, 0, h - 1)) * w + x];
        for (int y = 0; y < h; ++y) {
            plane.at(x, y) = running / static_cast<float>(2 * radius + 1);
            const int add = std::clamp(y + radius + 1, 0, h - 1);
            const int drop = std::clamp(y - radius, 0, h - 1);
            running += tmp[static_cast<size_t>(add) * w + x] - tmp[static_cast<size_t>(drop) * w + x];
        }
    }
}

struct Pyramid
{
    std::vector<Plane> levels; // 0 = full analysis size
};

Pyramid buildPyramid(const Plane &base, int levels)
{
    Pyramid out;
    out.levels.push_back(base);
    for (int i = 1; i < levels; ++i) {
        Plane next = halfSize(out.levels.back());
        if (next.empty() || next.width < 16 || next.height < 16)
            break;
        out.levels.push_back(std::move(next));
    }
    return out;
}

// One direction, coarse to fine. Returns displacement in pixels of the finest level.
void solveDirection(const Pyramid &from, const Pyramid &to, const FlowOptions &options,
                    Plane &outDx, Plane &outDy)
{
    const int topLevel = static_cast<int>(from.levels.size()) - 1;
    outDx.resize(from.levels[topLevel].width, from.levels[topLevel].height);
    outDy.resize(from.levels[topLevel].width, from.levels[topLevel].height);

    for (int level = topLevel; level >= 0; --level) {
        const Plane &src = from.levels[level];
        const Plane &dst = to.levels[level];
        const int w = src.width;
        const int h = src.height;

        if (level != topLevel) {
            // Carry the coarse answer down and double it: the grid halved, so the same motion is
            // twice as many pixels.
            Plane upX;
            Plane upY;
            upX.resize(w, h);
            upY.resize(w, h);
            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    upX.at(x, y) = 2.0f * sampleBilinear(outDx, x * 0.5f, y * 0.5f);
                    upY.at(x, y) = 2.0f * sampleBilinear(outDy, x * 0.5f, y * 0.5f);
                }
            }
            outDx = std::move(upX);
            outDy = std::move(upY);
        }

        // Spatial gradients of the frame being moved *from*, which is the frame the window sums
        // are taken on.
        Plane gx;
        Plane gy;
        gx.resize(w, h);
        gy.resize(w, h);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                const int xm = std::max(x - 1, 0);
                const int xp = std::min(x + 1, w - 1);
                const int ym = std::max(y - 1, 0);
                const int yp = std::min(y + 1, h - 1);
                gx.at(x, y) = 0.5f * (src.at(xp, y) - src.at(xm, y));
                gy.at(x, y) = 0.5f * (src.at(x, yp) - src.at(x, ym));
            }
        }

        for (int iteration = 0; iteration < options.iterationsPerLevel; ++iteration) {
            Plane sxx;
            Plane sxy;
            Plane syy;
            Plane sxt;
            Plane syt;
            sxx.resize(w, h);
            sxy.resize(w, h);
            syy.resize(w, h);
            sxt.resize(w, h);
            syt.resize(w, h);

            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const float ix = gx.at(x, y);
                    const float iy = gy.at(x, y);
                    const float warped = sampleBilinear(dst, static_cast<float>(x) + outDx.at(x, y),
                                                        static_cast<float>(y) + outDy.at(x, y));
                    const float it = warped - src.at(x, y);
                    sxx.at(x, y) = ix * ix;
                    sxy.at(x, y) = ix * iy;
                    syy.at(x, y) = iy * iy;
                    sxt.at(x, y) = ix * it;
                    syt.at(x, y) = iy * it;
                }
            }

            boxBlur(sxx, options.windowRadius);
            boxBlur(sxy, options.windowRadius);
            boxBlur(syy, options.windowRadius);
            boxBlur(sxt, options.windowRadius);
            boxBlur(syt, options.windowRadius);

            // Tikhonov term. Without it the 2x2 system is singular wherever the window has no
            // texture, or only an edge in one direction — which is most of a typical frame — and
            // the solution runs off to whatever the rounding says.
            constexpr float kRegularization = 1.0f;

            for (int y = 0; y < h; ++y) {
                for (int x = 0; x < w; ++x) {
                    const float a11 = sxx.at(x, y) + kRegularization;
                    const float a12 = sxy.at(x, y);
                    const float a22 = syy.at(x, y) + kRegularization;
                    const float b1 = -sxt.at(x, y);
                    const float b2 = -syt.at(x, y);
                    const float det = a11 * a22 - a12 * a12;
                    if (std::abs(det) < 1e-6f)
                        continue;
                    const float du = (b1 * a22 - b2 * a12) / det;
                    const float dv = (b2 * a11 - b1 * a12) / det;
                    // A single step is capped so one badly conditioned window cannot throw a
                    // vector across the frame and have the next iteration chase it.
                    outDx.at(x, y) += clampf(du, -4.0f, 4.0f);
                    outDy.at(x, y) += clampf(dv, -4.0f, 4.0f);
                }
            }

            boxBlur(outDx, 1);
            boxBlur(outDy, 1);
        }
    }

    const float cap = options.maxDisplacement;
    for (size_t i = 0; i < outDx.v.size(); ++i) {
        outDx.v[i] = clampf(outDx.v[i], -cap, cap);
        outDy.v[i] = clampf(outDy.v[i], -cap, cap);
    }
}

// Confidence: could a vector be determined here, and does it explain the picture?
//
// Two ingredients, and neither of them is occlusion:
//   - texture, from the smaller eigenvalue of the window's structure tensor. A blank wall and a
//     single straight edge both leave the system underdetermined; the small eigenvalue is what
//     says so.
//   - appearance, from how well the frame actually matches once moved by the vector.
//
// Forward/backward agreement is on purpose *not* in here. It is the standard shortcut and it is
// the trap: a zero vector on a featureless region agrees with itself perfectly in both
// directions while being completely wrong, so a confidence built on it is confidently wrong
// exactly where it matters most.
ScalarMap confidenceOf(const Plane &src, const Plane &dst, const Plane &dx, const Plane &dy,
                       const FlowOptions &options)
{
    const int w = src.width;
    const int h = src.height;
    ScalarMap out;
    out.resize(w, h, 0);

    Plane sxx;
    Plane sxy;
    Plane syy;
    sxx.resize(w, h);
    sxy.resize(w, h);
    syy.resize(w, h);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int xm = std::max(x - 1, 0);
            const int xp = std::min(x + 1, w - 1);
            const int ym = std::max(y - 1, 0);
            const int yp = std::min(y + 1, h - 1);
            const float ix = 0.5f * (src.at(xp, y) - src.at(xm, y));
            const float iy = 0.5f * (src.at(x, yp) - src.at(x, ym));
            sxx.at(x, y) = ix * ix;
            sxy.at(x, y) = ix * iy;
            syy.at(x, y) = iy * iy;
        }
    }
    boxBlur(sxx, options.windowRadius);
    boxBlur(sxy, options.windowRadius);
    boxBlur(syy, options.windowRadius);

    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const float a = sxx.at(x, y);
            const float b = sxy.at(x, y);
            const float c = syy.at(x, y);
            const float trace = a + c;
            const float diff = std::sqrt(std::max(0.0f, (a - c) * (a - c) + 4.0f * b * b));
            const float smaller = 0.5f * (trace - diff);
            // 12 is a gentle knee: a window whose weaker direction carries this much gradient
            // energy is solvable, below it the answer is increasingly a guess.
            const float texture = clampf(smaller / 12.0f, 0.0f, 1.0f);

            const float warped = sampleBilinear(dst, static_cast<float>(x) + dx.at(x, y),
                                                static_cast<float>(y) + dy.at(x, y));
            const float residual = std::abs(warped - src.at(x, y));
            const float appearance = clampf(1.0f - residual / 24.0f, 0.0f, 1.0f);

            out.value[static_cast<size_t>(y) * w + x] =
                static_cast<quint8>(std::lround(255.0f * texture * appearance));
        }
    }
    return out;
}

// Visibility: does the thing being drawn exist in that frame at all?
//
// Geometric, and computed from coverage rather than from any measure of how good a vector is.
// Every pixel of `from` is pushed along its vector into `to` and the landings are counted. A
// cell of `to` that nothing lands on was not visible in `from` -- it is being uncovered. A cell
// several pixels land on is where something is folding over something else.
//
// This is the part that must not be folded into confidence. A newly uncovered patch of wall can
// be perfectly textured and perfectly matched by a solver that has nothing to match it to, and
// a confidence map will happily call it certain.
ScalarMap coverageOf(const Plane &dx, const Plane &dy, int w, int h)
{
    ScalarMap out;
    out.resize(w, h, 0);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const int tx = static_cast<int>(std::lround(static_cast<float>(x) + dx.at(x, y)));
            const int ty = static_cast<int>(std::lround(static_cast<float>(y) + dy.at(x, y)));
            if (tx < 0 || ty < 0 || tx >= w || ty >= h)
                continue;
            quint8 &cell = out.value[static_cast<size_t>(ty) * w + tx];
            if (cell < 255)
                ++cell;
        }
    }
    return out;
}

void copyInto(FlowField &field, const Plane &dx, const Plane &dy)
{
    field.width = dx.width;
    field.height = dx.height;
    field.dx = dx.v;
    field.dy = dy.v;
}

} // namespace

QString FlowOptions::settingsFingerprint() const
{
    return QStringLiteral("%1/%2/%3/%4/%5/%6")
        .arg(algorithmId())
        .arg(analysisLongSide)
        .arg(static_cast<double>(maxDisplacement))
        .arg(pyramidLevels)
        .arg(iterationsPerLevel)
        .arg(windowRadius);
}

FlowPairAnalysis estimateFlow(const QImage &a, const QImage &b, const FlowOptions &options)
{
    FlowPairAnalysis result;
    if (a.isNull() || b.isNull() || a.size() != b.size())
        return result;

    const int longSide = std::max(a.width(), a.height());
    const int cap = std::max(32, options.analysisLongSide);
    const double scale = longSide > cap ? static_cast<double>(cap) / longSide : 1.0;
    const int aw = std::max(16, static_cast<int>(std::lround(a.width() * scale)));
    const int ah = std::max(16, static_cast<int>(std::lround(a.height() * scale)));

    const Plane lumaA = lumaOf(a, aw, ah);
    const Plane lumaB = lumaOf(b, aw, ah);
    if (lumaA.empty() || lumaB.empty())
        return result;

    const Pyramid pyrA = buildPyramid(lumaA, std::max(1, options.pyramidLevels));
    const Pyramid pyrB = buildPyramid(lumaB, std::max(1, options.pyramidLevels));

    Plane fdx;
    Plane fdy;
    Plane bdx;
    Plane bdy;
    // Both directions are solved independently rather than one being negated into the other.
    // Negating assumes the motion is reversible, which is precisely false where something is
    // being covered up -- the case the visibility maps below exist to find.
    solveDirection(pyrA, pyrB, options, fdx, fdy);
    solveDirection(pyrB, pyrA, options, bdx, bdy);

    copyInto(result.forward, fdx, fdy);
    copyInto(result.backward, bdx, bdy);
    result.confidenceA = confidenceOf(lumaA, lumaB, fdx, fdy, options);
    result.confidenceB = confidenceOf(lumaB, lumaA, bdx, bdy, options);

    // Coverage of B by A says which parts of B are newly uncovered, so the pixel being built
    // there cannot be taken from A. Coverage of A by B says the same the other way.
    const ScalarMap coverageB = coverageOf(fdx, fdy, aw, ah);
    const ScalarMap coverageA = coverageOf(bdx, bdy, aw, ah);

    result.visibleFromA.resize(aw, ah, 255);
    result.visibleFromB.resize(aw, ah, 255);
    for (size_t i = 0; i < result.visibleFromA.value.size(); ++i) {
        // A cell of B that nothing from A landed on is being uncovered, so it is not in A.
        result.visibleFromA.value[i] = coverageB.value[i] == 0 ? 0 : 255;
        // And the mirror: a cell of A that nothing from B reached is about to be covered up.
        result.visibleFromB.value[i] = coverageA.value[i] == 0 ? 0 : 255;
    }

    result.analysisWidth = aw;
    result.analysisHeight = ah;
    result.sourceWidth = a.width();
    result.sourceHeight = a.height();
    return result;
}

} // namespace drift::interp
