#pragma once

extern "C" {
#include <libavutil/frame.h>
#include <libavutil/pixfmt.h>
#include <libswscale/swscale.h>
}

// Shared by every decode-side swscale user (ClipReader, MediaThumbnail, ProxyEncoder,
// GlRuntime) so the deprecated-pixel-format normalization and the color
// range it depends on can't drift out of sync between call sites again.
namespace drift {

// Decoders that emit a YUVJ* format (e.g. MJPEG) always set AVFrame.color_range to
// AVCOL_RANGE_JPEG in lockstep, so the actual range is recovered from color_range rather
// than the pixel format itself. Passing the deprecated YUVJ* enum straight into swscale
// gets nothing extra and makes it log "deprecated pixel format used" on every frame.
inline AVPixelFormat swsSourceFormat(AVPixelFormat fmt)
{
    switch (fmt) {
    case AV_PIX_FMT_YUVJ420P:
        return AV_PIX_FMT_YUV420P;
    case AV_PIX_FMT_YUVJ422P:
        return AV_PIX_FMT_YUV422P;
    case AV_PIX_FMT_YUVJ444P:
        return AV_PIX_FMT_YUV444P;
    case AV_PIX_FMT_YUVJ440P:
        return AV_PIX_FMT_YUV440P;
    case AV_PIX_FMT_YUVJ411P:
        return AV_PIX_FMT_YUV411P;
    default:
        return fmt;
    }
}

// The YUV→RGB matrix a frame's colorspace tag selects. One mapping for both decode paths, so the
// CPU (swscale) and the GPU (NV12 shader in GlRuntime) can't pick different matrices for the same
// frame: swsColorspaceFromFrame() and yuvMatrixCoefficients() both start from here. SDR only.
// BT.2020 NCL and CL both land on the NCL matrix: swscale has no constant-luminance mode, and a
// true CL conversion needs the transfer function, which belongs with the HDR work. Untagged
// frames stay BT.709, Drift's SDR default.
enum class YuvMatrix { Bt709, Bt601, Smpte240M, Fcc, Bt2020 };

inline YuvMatrix yuvMatrixFromColorspace(int colorspace)
{
    switch (colorspace) {
    case AVCOL_SPC_BT470BG:
    case AVCOL_SPC_SMPTE170M:
        return YuvMatrix::Bt601;
    case AVCOL_SPC_SMPTE240M:
        return YuvMatrix::Smpte240M;
    case AVCOL_SPC_FCC:
        return YuvMatrix::Fcc;
    case AVCOL_SPC_BT2020_NCL:
    case AVCOL_SPC_BT2020_CL:
        return YuvMatrix::Bt2020;
    case AVCOL_SPC_BT709:
    case AVCOL_SPC_UNSPECIFIED:
    default:
        return YuvMatrix::Bt709;
    }
}

inline int swsColorspaceFromFrame(const AVFrame *frame)
{
    if (!frame)
        return SWS_CS_ITU709;
    switch (yuvMatrixFromColorspace(frame->colorspace)) {
    case YuvMatrix::Bt601:
        return SWS_CS_ITU601;
    case YuvMatrix::Smpte240M:
        return SWS_CS_SMPTE240M;
    case YuvMatrix::Fcc:
        return SWS_CS_FCC;
    case YuvMatrix::Bt2020:
        return SWS_CS_BT2020;
    case YuvMatrix::Bt709:
        break;
    }
    return SWS_CS_ITU709;
}

// Row-major 3x3 for the GPU shader: rgb = m * (Y, Cb, Cr) after range expansion (Y in 0..1,
// Cb/Cr in -0.5..0.5). Same matrices swscale uses for the same YuvMatrix.
inline void yuvMatrixCoefficients(YuvMatrix matrix, float m[9])
{
    // {Cr→R, Cb→G, Cr→G, Cb→B}
    float c[4] = {1.5748f, -0.1873f, -0.4681f, 1.8556f}; // BT.709
    switch (matrix) {
    case YuvMatrix::Bt601:
        c[0] = 1.402f; c[1] = -0.344f; c[2] = -0.714f; c[3] = 1.772f;
        break;
    case YuvMatrix::Smpte240M:
        c[0] = 1.576f; c[1] = -0.2266f; c[2] = -0.4766f; c[3] = 1.826f;
        break;
    case YuvMatrix::Fcc:
        c[0] = 1.4f; c[1] = -0.3319f; c[2] = -0.7119f; c[3] = 1.78f;
        break;
    case YuvMatrix::Bt2020:
        c[0] = 1.4746f; c[1] = -0.1646f; c[2] = -0.5714f; c[3] = 1.8814f;
        break;
    case YuvMatrix::Bt709:
        break;
    }
    const float out[9] = {1.f, 0.f, c[0], 1.f, c[1], c[2], 1.f, c[3], 0.f};
    for (int i = 0; i < 9; ++i)
        m[i] = out[i];
}

// Tells an existing scaler the source's actual sample range — recovered from color_range
// since the deprecated YUVJ* pixel formats are normalized away via swsSourceFormat() before
// reaching swscale — so full-range MJPEG/YUVJ-sourced frames aren't silently treated as
// limited range. dstRange is 1 for full-range (e.g. an RGB destination meant for display)
// or 0 for limited range.
inline void configureSwsRange(SwsContext *sws, const AVFrame *src, int dstRange)
{
    if (!sws || !src)
        return;
    const int *coeff = sws_getCoefficients(swsColorspaceFromFrame(src));
    // Unspecified range is treated as limited (MPEG/TV) — the common case for camera footage —
    // unless the format is YUVJ*, which is full range by definition.
    const bool yuvj = swsSourceFormat(static_cast<AVPixelFormat>(src->format)) != src->format;
    const int srcRange = src->color_range == AVCOL_RANGE_JPEG
                                 || (src->color_range == AVCOL_RANGE_UNSPECIFIED && yuvj)
                             ? 1
                             : 0;
    sws_setColorspaceDetails(sws, coeff, srcRange, coeff, dstRange, 0, 1 << 16, 1 << 16);
}

// Range-preserving variant for a YUV/NV12 destination that keeps the source's own range tag
// (e.g. the caller copies src->color_range onto the output frame) rather than converting to
// RGB for display, so the destination range must match the source's, not always full.
inline void configureSwsRangePreserving(SwsContext *sws, const AVFrame *src)
{
    if (!src)
        return;
    const int srcRange = src->color_range == AVCOL_RANGE_JPEG ? 1 : 0;
    configureSwsRange(sws, src, srcRange);
}

} // namespace drift
