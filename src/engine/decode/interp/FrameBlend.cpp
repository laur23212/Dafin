#include "engine/decode/interp/FrameBlend.h"

#include <QtGlobal>

#include <algorithm>
#include <array>
#include <cmath>

namespace drift::interp {
namespace {

// Mixing happens in 16-bit linear light. Two tables carry the whole transfer conversion, so the
// per-pixel work is integer lookups and multiplies rather than a pow() per channel — which is
// the difference between this being usable at export resolution and not.
//
// The reverse table is indexed by the full 16-bit linear value rather than a truncation of it.
// 64 KiB, built once, and it keeps the darks: a 4096-entry table would fold the bottom sixteen
// linear steps onto one output code, and the bottom of the range is where 8-bit sRGB packs most
// of its precision.
constexpr int kLinearMax = 65535;

double srgbToLinear(double v)
{
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double v)
{
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

const std::array<quint16, 256> &toLinearTable()
{
    static const std::array<quint16, 256> table = [] {
        std::array<quint16, 256> t{};
        for (int i = 0; i < 256; ++i)
            t[i] = static_cast<quint16>(std::lround(srgbToLinear(i / 255.0) * kLinearMax));
        return t;
    }();
    return table;
}

const std::array<quint8, kLinearMax + 1> &toSrgbTable()
{
    static const std::array<quint8, kLinearMax + 1> table = [] {
        std::array<quint8, kLinearMax + 1> t{};
        for (int i = 0; i <= kLinearMax; ++i)
            t[i] = static_cast<quint8>(
                std::clamp(std::lround(linearToSrgb(static_cast<double>(i) / kLinearMax) * 255.0),
                           0L, 255L));
        return t;
    }();
    return table;
}

// alpha as a 0..kWeightOne integer, so the mix is a pair of integer multiplies and a shift.
constexpr int kWeightShift = 12;
constexpr int kWeightOne = 1 << kWeightShift;

} // namespace

QImage blendFrames(const QImage &a, const QImage &b, double alpha)
{
    if (a.isNull() || b.isNull())
        return {};
    if (a.size() != b.size())
        return {};

    // Exact at the ends. Worth special-casing rather than letting the arithmetic land there,
    // because it means a frame that lands on a real timestamp comes back byte for byte as the
    // decoder produced it, with no transfer round-trip to account for.
    if (!(alpha > 0.0))
        return a;
    if (!(alpha < 1.0))
        return b;

    const QImage sa = a.convertToFormat(QImage::Format_RGBA8888);
    const QImage sb = b.convertToFormat(QImage::Format_RGBA8888);
    if (sa.isNull() || sb.isNull())
        return {};

    QImage out(sa.size(), QImage::Format_RGBA8888);
    if (out.isNull())
        return {};
    out.setDevicePixelRatio(a.devicePixelRatio());

    const std::array<quint16, 256> &toLin = toLinearTable();
    const std::array<quint8, kLinearMax + 1> &toEnc = toSrgbTable();

    const int wB = std::clamp(static_cast<int>(std::lround(alpha * kWeightOne)), 0, kWeightOne);
    const int wA = kWeightOne - wB;

    const int height = sa.height();
    const int width = sa.width();

    for (int y = 0; y < height; ++y) {
        const quint8 *rowA = sa.constScanLine(y);
        const quint8 *rowB = sb.constScanLine(y);
        quint8 *rowOut = out.scanLine(y);

        for (int x = 0; x < width; ++x) {
            const int i = x * 4;
            const int alphaA = rowA[i + 3];
            const int alphaB = rowB[i + 3];

            // The ordinary case for video: both frames opaque, so premultiplying and
            // unpremultiplying would both be multiplying by one.
            if (alphaA == 255 && alphaB == 255) {
                for (int c = 0; c < 3; ++c) {
                    const int mixed = (toLin[rowA[i + c]] * wA + toLin[rowB[i + c]] * wB)
                        >> kWeightShift;
                    rowOut[i + c] = toEnc[std::clamp(mixed, 0, kLinearMax)];
                }
                rowOut[i + 3] = 255;
                continue;
            }

            const int outAlpha = (alphaA * wA + alphaB * wB) >> kWeightShift;
            if (outAlpha <= 0) {
                // Transparent in the result, so there is no colour to carry and nothing to
                // divide by. Zero rather than an arbitrary leftover, so that whatever composites
                // this later has nothing to drag into an edge either.
                rowOut[i] = 0;
                rowOut[i + 1] = 0;
                rowOut[i + 2] = 0;
                rowOut[i + 3] = 0;
                continue;
            }

            for (int c = 0; c < 3; ++c) {
                // Premultiply in linear light, mix, then undo it against the mixed alpha.
                const int premulA = (toLin[rowA[i + c]] * alphaA) / 255;
                const int premulB = (toLin[rowB[i + c]] * alphaB) / 255;
                const int mixed = (premulA * wA + premulB * wB) >> kWeightShift;
                const int straight = (mixed * 255) / outAlpha;
                rowOut[i + c] = toEnc[std::clamp(straight, 0, kLinearMax)];
            }
            rowOut[i + 3] = static_cast<quint8>(std::clamp(outAlpha, 0, 255));
        }
    }

    return out;
}

} // namespace drift::interp
