#pragma once

// Dafin: the opt-in HDR export profiles (HDR10 / PQ and HLG). The default SDR BT.709 export does
// not go through any of this.
//
// The compositor's 8-bit SDR BT.709 frame is mapped into BT.2020 PQ or HLG with SDR white at the
// BT.2408 reference level (203 cd/m², HLG 0.75), written as 10/12-bit limited-range BT.2020 NCL
// Y'CbCr, and tagged accordingly; PQ files also carry SMPTE ST 2086 mastering-display and CTA-861.3
// content-light metadata in the container and, where the encoder takes it, in the bitstream.

#include "engine/color/HdrColor.h"

extern "C" {
#include <libavutil/pixfmt.h>
}

struct AVCodecContext;
struct AVFrame;
struct AVStream;

namespace drift::hdrexport {

using hdr::Transfer;

// Profile ids used by ExportSettings::colorProfile / the export dialog.
// "sdr" (default) | "hdr10" | "hlg". Unknown ids are SDR.
Transfer transferFromProfileId(const char *id);
const char *profileIdFromTransfer(Transfer transfer);

// HDR profiles are offered only on software encoders that take a >= 10-bit planar Y'CbCr frame
// and carry BT.2020/PQ/HLG tags: x265 10/12-bit, SVT-AV1 10-bit, VP9 10-bit, ProRes 422 and
// DNxHR 10-bit. Hardware encoders and 8-bit codecs are refused rather than silently SDR.
bool codecSupportsHdr(const char *exportCodecId);
bool pixelFormatSupportsHdr(AVPixelFormat fmt);

// Colour tags for every frame and for the encoder. For Transfer::Sdr709 these write the same
// BT.709 limited-range tags the SDR export always wrote.
void applyColorTags(AVCodecContext *ctx, Transfer transfer);
void applyColorTags(AVFrame *frame, Transfer transfer);

// Encoder-private HDR10 options (x265-params / svtav1-params with the mastering display and
// content light level). Call after the codec context is allocated and before avcodec_open2. Does
// nothing for SDR, HLG, or an encoder without such options. Returns false if an option was refused.
bool applyEncoderHdrOptions(AVCodecContext *ctx, Transfer transfer);

// Container-level HDR10 static metadata (mp4 mdcv/clli boxes, Matroska MasteringMetadata).
// Call after avcodec_parameters_from_context and before avformat_write_header. PQ only.
bool attachStreamHdrMetadata(AVStream *stream, Transfer transfer);

// The static metadata Dafin writes, exposed for tests: mastering display P3-D65,
// 1000 / 0.0001 cd/m²; MaxCLL = MaxFALL = 203 cd/m², the brightest any pixel can be.
struct StaticMetadata
{
    double primaries[3][2]; // R, G, B (CIE 1931 x, y)
    double whitePoint[2];
    double maxLuminance;
    double minLuminance;
    unsigned maxCll;
    unsigned maxFall;
};
StaticMetadata hdr10StaticMetadata();

// Converts a straight-alpha RGBA8888 SDR BT.709 image (alpha ignored: the export frame is opaque)
// into `dst`, an allocated planar 4:2:0 / 4:2:2 / 4:4:4 Y'CbCr frame of 10 or 12 bits with the
// same width and height. Chroma is the box average of each subsampled block (centre-sited).
// Returns false for an unsupported destination format or size mismatch.
bool convertRgba8ToHdr(const unsigned char *rgba, int strideBytes, int width, int height,
                       Transfer transfer, AVFrame *dst);

} // namespace drift::hdrexport
