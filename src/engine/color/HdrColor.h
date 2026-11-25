#pragma once

// Dafin: HDR transfer functions and the SDR-in-HDR mapping the HDR export profiles use.
// Plain C++ (no Qt, no FFmpeg) so the maths can be tested on its own against the standards:
//   - SMPTE ST 2084 / ITU-R BT.2100 PQ
//   - ITU-R BT.2100 HLG (OETF, inverse OETF, OOTF with the nominal 1000 cd/m² display)
//   - ITU-R BT.2087 / BT.2020 primaries (BT.709 → BT.2020 linear-light matrix)
//   - ITU-R BT.1886 reference EOTF (Lb = 0, gamma 2.4) for the SDR source
//   - ITU-R BT.2408 reference white: SDR white is placed at 203 cd/m², which is PQ ≈ 0.58 and
//     HLG 0.75 — not at signal 1.0.
//
// Dafin composites in 8-bit SDR BT.709, so an HDR export carries that SDR picture inside an HDR
// container at the BT.2408 level. Nothing in the picture is brighter than SDR white (203 cd/m²);
// it is correctly tagged HDR, not HDR-graded highlights.

namespace drift::hdr {

enum class Transfer {
    Sdr709, // BT.709 primaries, BT.1886 display (the existing default export)
    Pq,     // BT.2100 PQ, BT.2020 primaries ("HDR10")
    Hlg,    // BT.2100 HLG, BT.2020 primaries
};

inline constexpr double kReferenceWhiteNits = 203.0; // BT.2408 HDR reference white
inline constexpr double kPqPeakNits = 10000.0;
inline constexpr double kHlgNominalPeakNits = 1000.0; // BT.2100 nominal HLG display, gamma 1.2

// PQ: normalized display light Y in [0,1] (1 = 10000 cd/m²) ↔ signal E' in [0,1].
double pqInverseEotf(double y);
double pqEotf(double e);

// HLG: normalized scene light E in [0,1] ↔ signal E' in [0,1].
double hlgOetf(double e);
double hlgInverseOetf(double e);
// BT.2100 system gamma for a display of peak luminance lw (1.2 at 1000 cd/m²).
double hlgSystemGamma(double lw);

// BT.1886 with Lb = 0: normalized display light from an SDR signal in [0,1].
double bt1886Eotf(double v);

// Linear light, BT.709 primaries → BT.2020 primaries (both D65). Every coefficient is >= 0 and
// every row sums to 1, so in-gamut BT.709 stays within [0,1] and white stays white.
void bt709ToBt2020(const double in[3], double out[3]);
inline constexpr double kBt2020Kr = 0.2627;
inline constexpr double kBt2020Kb = 0.0593;

// One SDR display-referred BT.709 R'G'B' triple (each in [0,1]) → the BT.2020 R'G'B' signal of the
// chosen HDR transfer, with SDR white at kReferenceWhiteNits (BT.2408 display-referred mapping).
// For Transfer::Sdr709 the input is returned unchanged.
void sdrToHdrSignal(Transfer transfer, const double sdr709[3], double out2020[3]);

// Non-constant-luminance BT.2020 Y'CbCr from R'G'B' in [0,1], quantized to limited ("narrow")
// range code values at bitDepth bits (Y' 64..940, Cb/Cr 64..960 at 10 bits).
void rgbToYcbcr2020Limited(const double rgb[3], int bitDepth, int out[3]);
// Same quantization for a Y' or a C value already computed (C in [-0.5, 0.5]).
int quantizeLumaLimited(double y, int bitDepth);
int quantizeChromaLimited(double c, int bitDepth);

} // namespace drift::hdr
