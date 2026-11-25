#include "engine/decode/interp/FrameBlend.h"

#include "engine/decode/interp/LinearLight.h"

#include <QtGlobal>

#include <algorithm>
#include <cmath>

namespace drift::interp {

QImage blendFrames(const QImage &a, const QImage &b, double alpha)
{
    using namespace drift::interp::linearlight;

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

    const int weightB = std::clamp(static_cast<int>(std::lround(alpha * kWeightOne)), 0, kWeightOne);
    const int weightA = kWeightOne - weightB;

    const int height = sa.height();
    const int width = sa.width();

    for (int y = 0; y < height; ++y) {
        const quint8 *rowA = sa.constScanLine(y);
        const quint8 *rowB = sb.constScanLine(y);
        quint8 *rowOut = out.scanLine(y);

        for (int x = 0; x < width; ++x) {
            const int i = x * 4;
            mixPixel(rowA + i, rowB + i, weightA, weightB, rowOut + i);
        }
    }

    return out;
}

} // namespace drift::interp
