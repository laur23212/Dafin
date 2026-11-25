#pragma once

#include <QtGlobal>

#include <array>

// The transfer conversion and the pixel mix, in one place because the blend path and the flow
// path have to agree pixel for pixel. Flow declines on parts of a frame and falls back to the
// plain blend there, so if the two paths rounded differently the seam between them would be
// visible as a faint tide line exactly along the edge of a moving subject.
//
// sRGB is the transfer function assumed, close enough to BT.709 for the 8-bit SDR frames the
// compositor works in. PQ and HLG are not handled and must not be claimed.
namespace drift::interp::linearlight {

constexpr int kLinearMax = 65535;

// Mix weights as integers, so the per-pixel work is multiplies and a shift.
constexpr int kWeightShift = 12;
constexpr int kWeightOne = 1 << kWeightShift;

// 8-bit encoded -> 16-bit linear.
const std::array<quint16, 256> &toLinearTable();

// 16-bit linear -> 8-bit encoded. Indexed by the whole linear value rather than a truncation of
// it: a 4096-entry table would fold the bottom sixteen linear steps onto one output code, and
// the bottom of the range is where 8-bit sRGB keeps most of its precision.
const std::array<quint8, kLinearMax + 1> &toSrgbTable();

// One RGBA8888 pixel from each of two frames, mixed at integer weights that sum to kWeightOne,
// in linear light and premultiplied by alpha.
//
// Premultiplying is what keeps a fully transparent pixel from contributing its leftover RGB:
// such a pixel carries whatever was in the buffer, and mixing it straight drags that colour
// into the edge of anything with an alpha channel.
void mixPixel(const quint8 *a, const quint8 *b, int weightA, int weightB, quint8 *out);

// A colour in linear light, premultiplied by alpha, which is the only form in which samples
// from different places in different frames can be added together.
struct Premul
{
    float r = 0.0f;
    float g = 0.0f;
    float b = 0.0f;
    float a = 0.0f;
};

// Bilinear sample of an RGBA8888 image at a position in pixels, converted to linear light and
// premultiplied before the four taps are weighted -- not after, which would interpolate encoded
// values and darken the result wherever the taps differ.
//
// Positions outside the image clamp to the edge. There is no hole here and no transparent
// border: a vector that points off the frame is a vector that was wrong about something, and
// the honest answer is the nearest real pixel, not a puncture in the picture.
Premul sampleBilinearPremul(const quint8 *bits, int bytesPerLine, int width, int height, float x,
                            float y);

// Premultiplied linear back to an encoded RGBA8888 pixel.
void storePremul(const Premul &c, quint8 *out);

} // namespace drift::interp::linearlight
