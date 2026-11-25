#include "engine/encode/export/HdrExport.h"

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavcodec/packet.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace drift::hdrexport {
namespace {

constexpr const char *kHdrCodecs[] = {
    "h265_10", "h265_12", "av1_svt_10", "vp9_10", "prores", "dnxhr_10",
};

// Float versions of the HdrColor maths for the per-pixel loop; HdrColor.cpp (double) is the
// reference the tests hold these to.
constexpr float kM1 = 2610.0f / 16384.0f;
constexpr float kM2 = 2523.0f / 4096.0f * 128.0f;
constexpr float kC1 = 3424.0f / 4096.0f;
constexpr float kC2 = 2413.0f / 4096.0f * 32.0f;
constexpr float kC3 = 2392.0f / 4096.0f * 32.0f;
constexpr float kHlgA = 0.17883277f;
constexpr float kHlgB = 0.28466892f;
constexpr float kHlgC = 0.55991073f;
constexpr float kKr = float(hdr::kBt2020Kr);
constexpr float kKb = float(hdr::kBt2020Kb);
constexpr float kKg = 1.0f - kKr - kKb;

inline float pqSignal(float y)
{
    const float ym = std::pow(std::max(y, 0.0f), kM1);
    return std::pow((kC1 + kC2 * ym) / (1.0f + kC3 * ym), kM2);
}

inline float hlgSignal(float e)
{
    e = std::clamp(e, 0.0f, 1.0f);
    return e <= 1.0f / 12.0f ? std::sqrt(3.0f * e) : kHlgA * std::log(12.0f * e - kHlgB) + kHlgC;
}

struct Converter
{
    Transfer transfer = Transfer::Pq;
    float lin[256]; // BT.1886 display light of each 8-bit code, relative to SDR white
    float hlgExponent = 0.0f;
    float pqScale = 0.0f;
    float hlgScale = 0.0f;

    explicit Converter(Transfer t) : transfer(t)
    {
        for (int i = 0; i < 256; ++i)
            lin[i] = float(hdr::bt1886Eotf(double(i) / 255.0));
        pqScale = float(hdr::kReferenceWhiteNits / hdr::kPqPeakNits);
        hlgScale = float(hdr::kReferenceWhiteNits / hdr::kHlgNominalPeakNits);
        const double gamma = hdr::hlgSystemGamma(hdr::kHlgNominalPeakNits);
        hlgExponent = float((1.0 - gamma) / gamma);
    }

    // 8-bit BT.709 R'G'B' → BT.2020 PQ/HLG R'G'B' signal.
    inline void signal(const uint8_t *px, float out[3]) const
    {
        const float r = lin[px[0]], g = lin[px[1]], b = lin[px[2]];
        float l[3] = {
            0.627403895934699f * r + 0.329283038377884f * g + 0.043313065687417f * b,
            0.069097289358232f * r + 0.919540395075459f * g + 0.011362315566309f * b,
            0.016391438875150f * r + 0.088013307877226f * g + 0.895595253247624f * b,
        };
        if (transfer == Transfer::Pq) {
            for (int c = 0; c < 3; ++c)
                out[c] = pqSignal(l[c] * pqScale);
            return;
        }
        for (float &v : l)
            v *= hlgScale;
        const float yd = kKr * l[0] + kKg * l[1] + kKb * l[2];
        if (yd <= 0.0f) {
            out[0] = out[1] = out[2] = 0.0f;
            return;
        }
        const float s = std::pow(yd, hlgExponent);
        for (int c = 0; c < 3; ++c)
            out[c] = hlgSignal(l[c] * s);
    }
};

inline uint16_t lumaCode(float y, float scale, int maxCode)
{
    return uint16_t(std::clamp(int(std::lround((219.0f * y + 16.0f) * scale)), 0, maxCode));
}

inline uint16_t chromaCode(float c, float scale, int maxCode)
{
    return uint16_t(std::clamp(int(std::lround((224.0f * c + 128.0f) * scale)), 0, maxCode));
}

void convertRows(const Converter &conv, const uint8_t *rgba, int stride, int width, int y0, int y1,
                 int log2w, int log2h, int bitDepth, AVFrame *dst)
{
    const float scale = float(1 << (bitDepth - 8));
    const int maxCode = (1 << bitDepth) - 1;
    const int bw = 1 << log2w;
    const int bh = 1 << log2h;
    const int chromaW = (width + bw - 1) >> log2w;
    const size_t cw = size_t(chromaW);
    std::vector<float> sumR(cw), sumG(cw), sumB(cw);
    std::vector<int> count(cw);

    for (int by = y0; by < y1; by += bh) {
        std::fill(sumR.begin(), sumR.end(), 0.0f);
        std::fill(sumG.begin(), sumG.end(), 0.0f);
        std::fill(sumB.begin(), sumB.end(), 0.0f);
        std::fill(count.begin(), count.end(), 0);
        for (int dy = 0; dy < bh && by + dy < y1; ++dy) {
            const int y = by + dy;
            const uint8_t *src = rgba + size_t(y) * size_t(stride);
            auto *yRow = reinterpret_cast<uint16_t *>(dst->data[0] + size_t(y) * size_t(dst->linesize[0]));
            for (int x = 0; x < width; ++x) {
                float s[3];
                conv.signal(src + size_t(x) * 4, s);
                yRow[x] = lumaCode(kKr * s[0] + kKg * s[1] + kKb * s[2], scale, maxCode);
                const int cx = x >> log2w;
                sumR[size_t(cx)] += s[0];
                sumG[size_t(cx)] += s[1];
                sumB[size_t(cx)] += s[2];
                ++count[size_t(cx)];
            }
        }
        const int cy = by >> log2h;
        auto *cbRow = reinterpret_cast<uint16_t *>(dst->data[1] + size_t(cy) * size_t(dst->linesize[1]));
        auto *crRow = reinterpret_cast<uint16_t *>(dst->data[2] + size_t(cy) * size_t(dst->linesize[2]));
        for (int cx = 0; cx < chromaW; ++cx) {
            const float n = float(std::max(count[size_t(cx)], 1));
            const float r = sumR[size_t(cx)] / n, g = sumG[size_t(cx)] / n, b = sumB[size_t(cx)] / n;
            const float yy = kKr * r + kKg * g + kKb * b;
            cbRow[cx] = chromaCode((b - yy) / (2.0f * (1.0f - kKb)), scale, maxCode);
            crRow[cx] = chromaCode((r - yy) / (2.0f * (1.0f - kKr)), scale, maxCode);
        }
    }
}

uint16_t toMasteringPrimary(double v)
{
    return uint16_t(std::lround(v / 0.00002));
}

} // namespace

Transfer transferFromProfileId(const char *id)
{
    if (id && std::strcmp(id, "hdr10") == 0)
        return Transfer::Pq;
    if (id && std::strcmp(id, "hlg") == 0)
        return Transfer::Hlg;
    return Transfer::Sdr709;
}

const char *profileIdFromTransfer(Transfer transfer)
{
    switch (transfer) {
    case Transfer::Pq:
        return "hdr10";
    case Transfer::Hlg:
        return "hlg";
    case Transfer::Sdr709:
        break;
    }
    return "sdr";
}

bool codecSupportsHdr(const char *exportCodecId)
{
    if (!exportCodecId)
        return false;
    for (const char *id : kHdrCodecs) {
        if (std::strcmp(id, exportCodecId) == 0)
            return true;
    }
    return false;
}

bool pixelFormatSupportsHdr(AVPixelFormat fmt)
{
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(fmt);
    if (!desc || (desc->flags & (AV_PIX_FMT_FLAG_HWACCEL | AV_PIX_FMT_FLAG_RGB | AV_PIX_FMT_FLAG_ALPHA
                                 | AV_PIX_FMT_FLAG_BE)))
        return false;
    if (!(desc->flags & AV_PIX_FMT_FLAG_PLANAR) || desc->nb_components != 3)
        return false;
    const int depth = desc->comp[0].depth;
    return (depth == 10 || depth == 12) && desc->comp[0].step == 2 && desc->comp[0].plane == 0
           && desc->comp[1].plane == 1 && desc->comp[2].plane == 2;
}

void applyColorTags(AVCodecContext *ctx, Transfer transfer)
{
    if (!ctx)
        return;
    ctx->color_range = AVCOL_RANGE_MPEG;
    if (transfer == Transfer::Sdr709) {
        ctx->colorspace = AVCOL_SPC_BT709;
        ctx->color_primaries = AVCOL_PRI_BT709;
        ctx->color_trc = AVCOL_TRC_BT709;
        return;
    }
    ctx->colorspace = AVCOL_SPC_BT2020_NCL;
    ctx->color_primaries = AVCOL_PRI_BT2020;
    ctx->color_trc = transfer == Transfer::Pq ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_ARIB_STD_B67;
    ctx->chroma_sample_location = AVCHROMA_LOC_CENTER;
}

void applyColorTags(AVFrame *frame, Transfer transfer)
{
    if (!frame)
        return;
    frame->color_range = AVCOL_RANGE_MPEG;
    if (transfer == Transfer::Sdr709) {
        frame->colorspace = AVCOL_SPC_BT709;
        frame->color_primaries = AVCOL_PRI_BT709;
        frame->color_trc = AVCOL_TRC_BT709;
        return;
    }
    frame->colorspace = AVCOL_SPC_BT2020_NCL;
    frame->color_primaries = AVCOL_PRI_BT2020;
    frame->color_trc = transfer == Transfer::Pq ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_ARIB_STD_B67;
    frame->chroma_location = AVCHROMA_LOC_CENTER;
}

StaticMetadata hdr10StaticMetadata()
{
    StaticMetadata m{};
    // Display P3, D65 white (SMPTE EG 432-1).
    m.primaries[0][0] = 0.680;
    m.primaries[0][1] = 0.320;
    m.primaries[1][0] = 0.265;
    m.primaries[1][1] = 0.690;
    m.primaries[2][0] = 0.150;
    m.primaries[2][1] = 0.060;
    m.whitePoint[0] = 0.3127;
    m.whitePoint[1] = 0.3290;
    m.maxLuminance = 1000.0;
    m.minLuminance = 0.0001;
    m.maxCll = unsigned(hdr::kReferenceWhiteNits);
    m.maxFall = unsigned(hdr::kReferenceWhiteNits);
    return m;
}

bool applyEncoderHdrOptions(AVCodecContext *ctx, Transfer transfer)
{
    if (!ctx || !ctx->codec || !ctx->priv_data || transfer != Transfer::Pq)
        return true;
    const StaticMetadata m = hdr10StaticMetadata();
    char buf[512];
    if (std::strcmp(ctx->codec->name, "libx265") == 0) {
        // x265 units: chromaticity 0.00002, luminance 0.0001 cd/m². R/G/B order is G, B, R.
        std::snprintf(buf, sizeof(buf),
                      "hdr10=1:hdr10-opt=0:repeat-headers=1:"
                      "master-display=G(%u,%u)B(%u,%u)R(%u,%u)WP(%u,%u)L(%lld,%lld):max-cll=%u,%u",
                      toMasteringPrimary(m.primaries[1][0]), toMasteringPrimary(m.primaries[1][1]),
                      toMasteringPrimary(m.primaries[2][0]), toMasteringPrimary(m.primaries[2][1]),
                      toMasteringPrimary(m.primaries[0][0]), toMasteringPrimary(m.primaries[0][1]),
                      toMasteringPrimary(m.whitePoint[0]), toMasteringPrimary(m.whitePoint[1]),
                      (long long)std::llround(m.maxLuminance * 10000.0),
                      (long long)std::llround(m.minLuminance * 10000.0), m.maxCll, m.maxFall);
        return av_opt_set(ctx->priv_data, "x265-params", buf, 0) >= 0;
    }
    if (std::strcmp(ctx->codec->name, "libsvtav1") == 0) {
        std::snprintf(buf, sizeof(buf),
                      "mastering-display=G(%.4f,%.4f)B(%.4f,%.4f)R(%.4f,%.4f)WP(%.4f,%.4f)L(%.4f,%.4f):"
                      "content-light=%u,%u",
                      m.primaries[1][0], m.primaries[1][1], m.primaries[2][0], m.primaries[2][1],
                      m.primaries[0][0], m.primaries[0][1], m.whitePoint[0], m.whitePoint[1],
                      m.maxLuminance, m.minLuminance, m.maxCll, m.maxFall);
        return av_opt_set(ctx->priv_data, "svtav1-params", buf, 0) >= 0;
    }
    return true;
}

bool attachStreamHdrMetadata(AVStream *stream, Transfer transfer)
{
    if (!stream || !stream->codecpar || transfer != Transfer::Pq)
        return true;
    const StaticMetadata m = hdr10StaticMetadata();
    AVCodecParameters *par = stream->codecpar;

    AVPacketSideData *sd = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data,
                                                   AV_PKT_DATA_MASTERING_DISPLAY_METADATA,
                                                   sizeof(AVMasteringDisplayMetadata), 0);
    if (!sd)
        return false;
    auto *mdm = reinterpret_cast<AVMasteringDisplayMetadata *>(sd->data);
    std::memset(mdm, 0, sizeof(*mdm));
    for (int i = 0; i < 3; ++i) {
        mdm->display_primaries[i][0] = av_d2q(m.primaries[i][0], 50000);
        mdm->display_primaries[i][1] = av_d2q(m.primaries[i][1], 50000);
    }
    mdm->white_point[0] = av_d2q(m.whitePoint[0], 50000);
    mdm->white_point[1] = av_d2q(m.whitePoint[1], 50000);
    mdm->max_luminance = av_make_q(int(std::lround(m.maxLuminance * 10000.0)), 10000);
    mdm->min_luminance = av_make_q(int(std::lround(m.minLuminance * 10000.0)), 10000);
    mdm->has_primaries = 1;
    mdm->has_luminance = 1;

    sd = av_packet_side_data_new(&par->coded_side_data, &par->nb_coded_side_data,
                                 AV_PKT_DATA_CONTENT_LIGHT_LEVEL, sizeof(AVContentLightMetadata), 0);
    if (!sd)
        return false;
    auto *cll = reinterpret_cast<AVContentLightMetadata *>(sd->data);
    cll->MaxCLL = m.maxCll;
    cll->MaxFALL = m.maxFall;
    return true;
}

bool convertRgba8ToHdr(const unsigned char *rgba, int strideBytes, int width, int height,
                       Transfer transfer, AVFrame *dst)
{
    if (!rgba || !dst || transfer == Transfer::Sdr709 || width <= 0 || height <= 0
        || dst->width != width || dst->height != height
        || !pixelFormatSupportsHdr(static_cast<AVPixelFormat>(dst->format)))
        return false;
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(static_cast<AVPixelFormat>(dst->format));
    const int log2w = desc->log2_chroma_w;
    const int log2h = desc->log2_chroma_h;
    const int bitDepth = desc->comp[0].depth;
    const Converter conv(transfer);

    // Bands of whole chroma rows, one per hardware thread.
    const int bh = 1 << log2h;
    const int blockRows = (height + bh - 1) / bh;
    const unsigned hw = std::max(1u, std::thread::hardware_concurrency());
    const int threads = int(std::min<unsigned>(hw, unsigned(std::max(1, blockRows / 8))));
    if (threads <= 1) {
        convertRows(conv, rgba, strideBytes, width, 0, height, log2w, log2h, bitDepth, dst);
        return true;
    }
    std::vector<std::thread> pool;
    pool.reserve(size_t(threads));
    for (int t = 0; t < threads; ++t) {
        const int y0 = std::min(height, (blockRows * t / threads) * bh);
        const int y1 = std::min(height, (blockRows * (t + 1) / threads) * bh);
        if (y0 >= y1)
            continue;
        pool.emplace_back([&, y0, y1] {
            convertRows(conv, rgba, strideBytes, width, y0, y1, log2w, log2h, bitDepth, dst);
        });
    }
    for (std::thread &th : pool)
        th.join();
    return true;
}

} // namespace drift::hdrexport
