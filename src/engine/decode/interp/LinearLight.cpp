#include "engine/decode/interp/LinearLight.h"

#include <algorithm>
#include <cmath>

namespace drift::interp::linearlight {
namespace {

double srgbToLinear(double v)
{
    return v <= 0.04045 ? v / 12.92 : std::pow((v + 0.055) / 1.055, 2.4);
}

double linearToSrgb(double v)
{
    return v <= 0.0031308 ? v * 12.92 : 1.055 * std::pow(v, 1.0 / 2.4) - 0.055;
}

} // namespace

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

void mixPixel(const quint8 *a, const quint8 *b, int weightA, int weightB, quint8 *out)
{
    const std::array<quint16, 256> &toLin = toLinearTable();
    const std::array<quint8, kLinearMax + 1> &toEnc = toSrgbTable();

    const int alphaA = a[3];
    const int alphaB = b[3];

    // The ordinary case for video: both frames opaque, so premultiplying and unpremultiplying
    // would both be multiplying by one.
    if (alphaA == 255 && alphaB == 255) {
        for (int c = 0; c < 3; ++c) {
            const int mixed = (toLin[a[c]] * weightA + toLin[b[c]] * weightB) >> kWeightShift;
            out[c] = toEnc[std::clamp(mixed, 0, kLinearMax)];
        }
        out[3] = 255;
        return;
    }

    const int outAlpha = (alphaA * weightA + alphaB * weightB) >> kWeightShift;
    if (outAlpha <= 0) {
        // Transparent in the result, so there is no colour to carry and nothing to divide by.
        // Zero rather than an arbitrary leftover, so that whatever composites this later has
        // nothing to drag into an edge either.
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        out[3] = 0;
        return;
    }

    for (int c = 0; c < 3; ++c) {
        const int premulA = (toLin[a[c]] * alphaA) / 255;
        const int premulB = (toLin[b[c]] * alphaB) / 255;
        const int mixed = (premulA * weightA + premulB * weightB) >> kWeightShift;
        const int straight = (mixed * 255) / outAlpha;
        out[c] = toEnc[std::clamp(straight, 0, kLinearMax)];
    }
    out[3] = static_cast<quint8>(std::clamp(outAlpha, 0, 255));
}

Premul sampleBilinearPremul(const quint8 *bits, int bytesPerLine, int width, int height, float x,
                            float y)
{
    const std::array<quint16, 256> &toLin = toLinearTable();

    const float cx = std::clamp(x, 0.0f, static_cast<float>(width - 1));
    const float cy = std::clamp(y, 0.0f, static_cast<float>(height - 1));

    const int x0 = static_cast<int>(std::floor(cx));
    const int y0 = static_cast<int>(std::floor(cy));
    const int x1 = std::min(x0 + 1, width - 1);
    const int y1 = std::min(y0 + 1, height - 1);
    const float fx = cx - static_cast<float>(x0);
    const float fy = cy - static_cast<float>(y0);

    const float w00 = (1.0f - fx) * (1.0f - fy);
    const float w10 = fx * (1.0f - fy);
    const float w01 = (1.0f - fx) * fy;
    const float w11 = fx * fy;

    const quint8 *row0 = bits + static_cast<ptrdiff_t>(y0) * bytesPerLine;
    const quint8 *row1 = bits + static_cast<ptrdiff_t>(y1) * bytesPerLine;
    const quint8 *p00 = row0 + static_cast<ptrdiff_t>(x0) * 4;
    const quint8 *p10 = row0 + static_cast<ptrdiff_t>(x1) * 4;
    const quint8 *p01 = row1 + static_cast<ptrdiff_t>(x0) * 4;
    const quint8 *p11 = row1 + static_cast<ptrdiff_t>(x1) * 4;

    const float a00 = p00[3] / 255.0f;
    const float a10 = p10[3] / 255.0f;
    const float a01 = p01[3] / 255.0f;
    const float a11 = p11[3] / 255.0f;

    Premul out;
    out.a = w00 * a00 + w10 * a10 + w01 * a01 + w11 * a11;

    float *channel[3] = {&out.r, &out.g, &out.b};
    for (int c = 0; c < 3; ++c) {
        *channel[c] = w00 * (toLin[p00[c]] * a00) + w10 * (toLin[p10[c]] * a10)
            + w01 * (toLin[p01[c]] * a01) + w11 * (toLin[p11[c]] * a11);
    }
    return out;
}

void storePremul(const Premul &c, quint8 *out)
{
    const std::array<quint8, kLinearMax + 1> &toEnc = toSrgbTable();

    const int outAlpha = std::clamp(static_cast<int>(std::lround(c.a * 255.0f)), 0, 255);
    if (outAlpha <= 0) {
        out[0] = 0;
        out[1] = 0;
        out[2] = 0;
        out[3] = 0;
        return;
    }

    const float straightScale = 1.0f / c.a;
    const float source[3] = {c.r, c.g, c.b};
    for (int i = 0; i < 3; ++i) {
        const int straight =
            static_cast<int>(std::lround(std::max(0.0f, source[i]) * straightScale));
        out[i] = toEnc[std::clamp(straight, 0, kLinearMax)];
    }
    out[3] = static_cast<quint8>(outAlpha);
}

} // namespace drift::interp::linearlight
