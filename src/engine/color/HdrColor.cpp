#include "engine/color/HdrColor.h"

#include <algorithm>
#include <cmath>

namespace drift::hdr {
namespace {

// SMPTE ST 2084, exact rational constants.
constexpr double kM1 = 2610.0 / 16384.0;
constexpr double kM2 = 2523.0 / 4096.0 * 128.0;
constexpr double kC1 = 3424.0 / 4096.0;
constexpr double kC2 = 2413.0 / 4096.0 * 32.0;
constexpr double kC3 = 2392.0 / 4096.0 * 32.0;

// ITU-R BT.2100 HLG.
constexpr double kHlgA = 0.17883277;
constexpr double kHlgB = 0.28466892; // 1 - 4a
constexpr double kHlgC = 0.55991073; // 0.5 - a ln(4a)

constexpr double kBt2020Kg = 1.0 - kBt2020Kr - kBt2020Kb;

double clamp01(double v)
{
    return std::clamp(v, 0.0, 1.0);
}

} // namespace

double pqInverseEotf(double y)
{
    const double ym = std::pow(clamp01(y), kM1);
    return std::pow((kC1 + kC2 * ym) / (1.0 + kC3 * ym), kM2);
}

double pqEotf(double e)
{
    const double ep = std::pow(clamp01(e), 1.0 / kM2);
    const double num = std::max(ep - kC1, 0.0);
    return std::pow(num / (kC2 - kC3 * ep), 1.0 / kM1);
}

double hlgOetf(double e)
{
    e = clamp01(e);
    if (e <= 1.0 / 12.0)
        return std::sqrt(3.0 * e);
    return kHlgA * std::log(12.0 * e - kHlgB) + kHlgC;
}

double hlgInverseOetf(double e)
{
    e = clamp01(e);
    if (e <= 0.5)
        return e * e / 3.0;
    return (std::exp((e - kHlgC) / kHlgA) + kHlgB) / 12.0;
}

double hlgSystemGamma(double lw)
{
    return 1.2 + 0.42 * std::log10(lw / 1000.0);
}

double bt1886Eotf(double v)
{
    return std::pow(clamp01(v), 2.4);
}

void bt709ToBt2020(const double in[3], double out[3])
{
    // ITU-R BT.2087-0, Annex 1 (derived from the two sets of primaries, D65 white).
    static constexpr double m[9] = {
        0.627403895934699, 0.329283038377884, 0.043313065687417,
        0.069097289358232, 0.919540395075459, 0.011362315566309,
        0.016391438875150, 0.088013307877226, 0.895595253247624,
    };
    for (int r = 0; r < 3; ++r)
        out[r] = m[r * 3 + 0] * in[0] + m[r * 3 + 1] * in[1] + m[r * 3 + 2] * in[2];
}

void sdrToHdrSignal(Transfer transfer, const double sdr709[3], double out2020[3])
{
    if (transfer == Transfer::Sdr709) {
        for (int c = 0; c < 3; ++c)
            out2020[c] = sdr709[c];
        return;
    }
    // SDR display light relative to SDR white, then BT.2020 primaries.
    double lin709[3];
    for (int c = 0; c < 3; ++c)
        lin709[c] = bt1886Eotf(sdr709[c]);
    double lin[3];
    bt709ToBt2020(lin709, lin);

    if (transfer == Transfer::Pq) {
        for (int c = 0; c < 3; ++c)
            out2020[c] = pqInverseEotf(std::max(lin[c], 0.0) * kReferenceWhiteNits / kPqPeakNits);
        return;
    }

    // HLG: the SDR picture is display light Fd on the nominal 1000 cd/m² HLG display; undo the
    // BT.2100 OOTF (Fd = Lw · Ys^(γ-1) · Es) to get scene light Es, then apply the OETF.
    const double lw = kHlgNominalPeakNits;
    const double gamma = hlgSystemGamma(lw);
    double fd[3];
    for (int c = 0; c < 3; ++c)
        fd[c] = std::max(lin[c], 0.0) * kReferenceWhiteNits / lw; // normalized to Lw
    const double yd = kBt2020Kr * fd[0] + kBt2020Kg * fd[1] + kBt2020Kb * fd[2];
    if (yd <= 0.0) {
        out2020[0] = out2020[1] = out2020[2] = 0.0;
        return;
    }
    const double scale = std::pow(yd, (1.0 - gamma) / gamma);
    for (int c = 0; c < 3; ++c)
        out2020[c] = hlgOetf(fd[c] * scale);
}

int quantizeLumaLimited(double y, int bitDepth)
{
    const double scale = double(1 << (bitDepth - 8));
    const int maxCode = (1 << bitDepth) - 1;
    return std::clamp(int(std::lround((219.0 * y + 16.0) * scale)), 0, maxCode);
}

int quantizeChromaLimited(double c, int bitDepth)
{
    const double scale = double(1 << (bitDepth - 8));
    const int maxCode = (1 << bitDepth) - 1;
    return std::clamp(int(std::lround((224.0 * c + 128.0) * scale)), 0, maxCode);
}

void rgbToYcbcr2020Limited(const double rgb[3], int bitDepth, int out[3])
{
    const double y = kBt2020Kr * rgb[0] + kBt2020Kg * rgb[1] + kBt2020Kb * rgb[2];
    const double cb = (rgb[2] - y) / (2.0 * (1.0 - kBt2020Kb));
    const double cr = (rgb[0] - y) / (2.0 * (1.0 - kBt2020Kr));
    out[0] = quantizeLumaLimited(y, bitDepth);
    out[1] = quantizeChromaLimited(cb, bitDepth);
    out[2] = quantizeChromaLimited(cr, bitDepth);
}

} // namespace drift::hdr
