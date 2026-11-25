#include "engine/decode/interp/FlowSynth.h"

#include "engine/decode/interp/LinearLight.h"

#include <algorithm>
#include <cmath>

namespace drift::interp {
namespace {

using linearlight::kWeightOne;
using linearlight::Premul;

float clampf(float v, float lo, float hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Bilinear read of a float grid, clamped at the edges.
float sampleGrid(const std::vector<float> &data, int width, int height, float x, float y)
{
    const float cx = clampf(x, 0.0f, static_cast<float>(width - 1));
    const float cy = clampf(y, 0.0f, static_cast<float>(height - 1));
    const int x0 = static_cast<int>(std::floor(cx));
    const int y0 = static_cast<int>(std::floor(cy));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);

    const float v00 = data[static_cast<size_t>(y0) * width + x0];
    const float v10 = data[static_cast<size_t>(y0) * width + x1];
    const float v01 = data[static_cast<size_t>(y1) * width + x0];
    const float v11 = data[static_cast<size_t>(y1) * width + x1];

    return (1.0f - fy) * ((1.0f - fx) * v00 + fx * v10) + fy * ((1.0f - fx) * v01 + fx * v11);
}

// Bilinear read of a 0..255 grid, returned as 0..1.
float sampleMap(const ScalarMap &map, float x, float y)
{
    const float cx = clampf(x, 0.0f, static_cast<float>(map.width - 1));
    const float cy = clampf(y, 0.0f, static_cast<float>(map.height - 1));
    const int x0 = static_cast<int>(std::floor(cx));
    const int y0 = static_cast<int>(std::floor(cy));
    const int x1 = std::min(x0 + 1, map.width - 1);
    const int y1 = std::min(y0 + 1, map.height - 1);
    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);

    const float v00 = map.at(x0, y0);
    const float v10 = map.at(x1, y0);
    const float v01 = map.at(x0, y1);
    const float v11 = map.at(x1, y1);

    const float mixed =
        (1.0f - fy) * ((1.0f - fx) * v00 + fx * v10) + fy * ((1.0f - fx) * v01 + fx * v11);
    return mixed / 255.0f;
}

double meanOf(const ScalarMap &a, const ScalarMap &b)
{
    if (a.value.empty() || b.value.empty())
        return 0.0;
    double total = 0.0;
    for (quint8 v : a.value)
        total += v;
    for (quint8 v : b.value)
        total += v;
    return total / (255.0 * static_cast<double>(a.value.size() + b.value.size()));
}

double zeroFractionOf(const ScalarMap &a, const ScalarMap &b)
{
    if (a.value.empty() || b.value.empty())
        return 0.0;
    size_t zeros = 0;
    for (quint8 v : a.value)
        zeros += (v == 0) ? 1 : 0;
    for (quint8 v : b.value)
        zeros += (v == 0) ? 1 : 0;
    return static_cast<double>(zeros) / static_cast<double>(a.value.size() + b.value.size());
}

} // namespace

QImage synthesizeFlowFrame(const QImage &a, const QImage &b, double alpha,
                           const FlowPairAnalysis &analysis, const FlowSynthOptions &options,
                           FlowSynthStats *stats)
{
    FlowSynthStats local;
    const auto report = [&](bool declined) {
        local.declined = declined;
        if (stats)
            *stats = local;
    };

    if (a.isNull() || b.isNull() || a.size() != b.size() || !analysis.isValid()) {
        report(true);
        return {};
    }

    // Exact at the ends, for the same reason the blend is: a frame that lands on a real
    // timestamp should come back as the decoder produced it and not as a round trip through a
    // warp that happens to be the identity.
    if (!(alpha > 0.0)) {
        report(false);
        return a;
    }
    if (!(alpha < 1.0)) {
        report(false);
        return b;
    }

    local.meanConfidence = meanOf(analysis.confidenceA, analysis.confidenceB);
    local.disoccludedFraction = zeroFractionOf(analysis.visibleFromA, analysis.visibleFromB);

    // Decline for the whole pair before touching a pixel. Declining is a first-class outcome
    // here: the blend is a working picture, and this path only earns a frame when it can beat
    // it. Measured on footage, not asserted.
    if (local.meanConfidence < options.minMeanConfidence
        || local.disoccludedFraction > options.maxDisoccludedFraction) {
        report(true);
        return {};
    }

    const QImage sa = a.convertToFormat(QImage::Format_RGBA8888);
    const QImage sb = b.convertToFormat(QImage::Format_RGBA8888);
    if (sa.isNull() || sb.isNull()) {
        report(true);
        return {};
    }

    QImage out(sa.size(), QImage::Format_RGBA8888);
    if (out.isNull()) {
        report(true);
        return {};
    }
    out.setDevicePixelRatio(a.devicePixelRatio());

    const int width = sa.width();
    const int height = sa.height();
    const int gw = analysis.forward.width;
    const int gh = analysis.forward.height;

    // The fields are in pixels of the analysis grid, so a displacement has to be scaled on its
    // way out to frame pixels. Writing the conversion down is the whole point of the unit being
    // stated in FlowField: read as normalized UV instead, a half-resolution field is wrong by a
    // factor of two in a way that looks almost right.
    const float gridToFrameX = static_cast<float>(width) / static_cast<float>(gw);
    const float gridToFrameY = static_cast<float>(height) / static_cast<float>(gh);
    const float frameToGridX = 1.0f / gridToFrameX;
    const float frameToGridY = 1.0f / gridToFrameY;

    const float alphaF = static_cast<float>(alpha);
    const int blendWeightB =
        std::clamp(static_cast<int>(std::lround(alpha * kWeightOne)), 0, kWeightOne);
    const int blendWeightA = kWeightOne - blendWeightB;

    const quint8 *bitsA = sa.constBits();
    const quint8 *bitsB = sb.constBits();
    const int strideA = sa.bytesPerLine();
    const int strideB = sb.bytesPerLine();

    size_t fellBack = 0;

    for (int y = 0; y < height; ++y) {
        const quint8 *rowA = sa.constScanLine(y);
        const quint8 *rowB = sb.constScanLine(y);
        quint8 *rowOut = out.scanLine(y);

        // Pixel centres, so a field computed at a different resolution lands where it should
        // rather than half a grid cell off.
        const float gy = (static_cast<float>(y) + 0.5f) * frameToGridY - 0.5f;

        for (int x = 0; x < width; ++x) {
            const int i = x * 4;
            const float gx = (static_cast<float>(x) + 0.5f) * frameToGridX - 0.5f;

            // Each endpoint is moved toward this time along its own field: A forward by alpha,
            // B backward by the rest. Symmetric on purpose -- swapping the two frames and
            // taking 1-alpha has to give the same picture, or a reversed clip would not match
            // the same footage played forwards.
            const float fdx = sampleGrid(analysis.forward.dx, gw, gh, gx, gy);
            const float fdy = sampleGrid(analysis.forward.dy, gw, gh, gx, gy);
            const float bdx = sampleGrid(analysis.backward.dx, gw, gh, gx, gy);
            const float bdy = sampleGrid(analysis.backward.dy, gw, gh, gx, gy);

            const float aGridX = gx - alphaF * fdx;
            const float aGridY = gy - alphaF * fdy;
            const float bGridX = gx - (1.0f - alphaF) * bdx;
            const float bGridY = gy - (1.0f - alphaF) * bdy;

            // Confidence is read where the sample is taken from. Visibility is read on the
            // other frame's grid, because it is a statement about the opposite frame: "can this
            // pixel come from A" is answered by visibleFromA at the pixel's position in B.
            const float confA = sampleMap(analysis.confidenceA, aGridX, aGridY);
            const float confB = sampleMap(analysis.confidenceB, bGridX, bGridY);
            const float visA = sampleMap(analysis.visibleFromA, bGridX, bGridY);
            const float visB = sampleMap(analysis.visibleFromB, aGridX, aGridY);

            const float weightA = (1.0f - alphaF) * confA * visA;
            const float weightB = alphaF * confB * visB;
            const float total = weightA + weightB;

            if (!(total > static_cast<float>(options.minPixelWeight))) {
                // Neither endpoint can be placed here with any conviction. The plain blend of
                // the two pixels where they already are is the honest answer: it is what this
                // feature falls back to for a whole frame, and it is what it falls back to for
                // one pixel.
                linearlight::mixPixel(rowA + i, rowB + i, blendWeightA, blendWeightB, rowOut + i);
                ++fellBack;
                continue;
            }

            const float sampleAX = (aGridX + 0.5f) * gridToFrameX - 0.5f;
            const float sampleAY = (aGridY + 0.5f) * gridToFrameY - 0.5f;
            const float sampleBX = (bGridX + 0.5f) * gridToFrameX - 0.5f;
            const float sampleBY = (bGridY + 0.5f) * gridToFrameY - 0.5f;

            const Premul pa = linearlight::sampleBilinearPremul(bitsA, strideA, width, height,
                                                                sampleAX, sampleAY);
            const Premul pb = linearlight::sampleBilinearPremul(bitsB, strideB, width, height,
                                                                sampleBX, sampleBY);

            const float inv = 1.0f / total;
            Premul mixed;
            mixed.r = (pa.r * weightA + pb.r * weightB) * inv;
            mixed.g = (pa.g * weightA + pb.g * weightB) * inv;
            mixed.b = (pa.b * weightA + pb.b * weightB) * inv;
            mixed.a = (pa.a * weightA + pb.a * weightB) * inv;

            linearlight::storePremul(mixed, rowOut + i);
        }
    }

    local.blendFallbackFraction = static_cast<double>(fellBack)
        / static_cast<double>(static_cast<size_t>(width) * static_cast<size_t>(height));
    report(false);
    return out;
}

} // namespace drift::interp
