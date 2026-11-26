// CPU (swscale) and GPU (NV12 shader) must read the same YUV->RGB matrix and range from a
// frame's tags. The CPU side runs Dafin's real configureSwsRange(); the GPU side is the shader's
// arithmetic (same matrix table, same range offsets) done in float, since the shader itself needs
// a GL context (tst_engine gpuNv12MatchesSwsBt709 covers that for BT.709).
#include "engine/decode/SwsColorRange.h"

#include <QtTest>

#include <algorithm>
#include <cmath>
#include <vector>

extern "C" {
#include <libavutil/imgutils.h>
}

using namespace drift;

namespace {

constexpr int kW = 64;
constexpr int kH = 64; // 4 x 4 patches of 16 px: chroma subsampling stays inside a patch

struct Kr_Kb {
    double kr, kb;
};

Kr_Kb coefficientsFor(int colorspace)
{
    switch (yuvMatrixFromColorspace(colorspace)) {
    case YuvMatrix::Bt601: return {0.299, 0.114};
    case YuvMatrix::Smpte240M: return {0.212, 0.087};
    case YuvMatrix::Fcc: return {0.30, 0.11};
    case YuvMatrix::Bt2020: return {0.2627, 0.0593};
    case YuvMatrix::Bt709: break;
    }
    return {0.2126, 0.0722};
}

// Exact matrix, double precision, from the native-bit-depth sample values.
void referenceRgb(double y, double cb, double cr, int bits, bool full, int colorspace, double rgb[3])
{
    const double s = double(1 << (bits - 8));
    double yn, un, vn;
    if (full) {
        const double mx = double((1 << bits) - 1);
        yn = y / mx;
        un = (cb - 128 * s) / mx;
        vn = (cr - 128 * s) / mx;
    } else {
        yn = (y - 16 * s) / (219 * s);
        un = (cb - 128 * s) / (224 * s);
        vn = (cr - 128 * s) / (224 * s);
    }
    const Kr_Kb k = coefficientsFor(colorspace);
    const double r = yn + 2 * (1 - k.kr) * vn;
    const double b = yn + 2 * (1 - k.kb) * un;
    const double g = (yn - k.kr * r - k.kb * b) / (1 - k.kr - k.kb);
    rgb[0] = r;
    rgb[1] = g;
    rgb[2] = b;
}

struct Patch {
    int y, cb, cr; // 8-bit-scale values
};

// Colours inside the RGB cube for every matrix tested (checked per matrix below), so clipping
// behaviour, which differs between swscale and the shader, does not enter the comparison.
const Patch kPatches[16] = {
    {64, 128, 128},  {128, 128, 128}, {192, 128, 128}, {110, 90, 150},
    {110, 150, 110}, {140, 110, 170}, {150, 170, 100}, {90, 130, 180},
    {170, 100, 120}, {70, 150, 140},  {130, 120, 100}, {160, 140, 150},
    {100, 110, 120}, {180, 130, 110}, {120, 160, 160}, {80, 100, 100},
};

struct Result {
    int cpuMaxVsRef = 0;
    int gpuMaxVsRef = 0;
    int cpuMaxVsGpu = 0;
    int tested = 0;
};

Result measure(int bits, bool full, int colorspace)
{
    const AVPixelFormat fmt = bits == 8 ? (full ? AV_PIX_FMT_YUVJ420P : AV_PIX_FMT_YUV420P)
                                        : AV_PIX_FMT_YUV420P10LE;
    AVFrame *f = av_frame_alloc();
    f->format = fmt;
    f->width = kW;
    f->height = kH;
    av_frame_get_buffer(f, 0);
    f->colorspace = AVColorSpace(colorspace);
    f->color_range = full ? AVCOL_RANGE_JPEG : AVCOL_RANGE_MPEG;

    const int sh = bits - 8;
    auto put = [&](int plane, int x, int y, int v) {
        if (bits == 8)
            f->data[plane][y * f->linesize[plane] + x] = uint8_t(v);
        else
            reinterpret_cast<uint16_t *>(f->data[plane] + y * f->linesize[plane])[x] = uint16_t(v);
    };
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            const Patch &p = kPatches[(y / 16) * 4 + x / 16];
            // Full range uses the whole 0..255 swing, limited stays inside 16..235/240.
            const int yy = full ? std::clamp(p.y * 255 / 235, 0, 255) : p.y;
            put(0, x, y, yy << sh);
            if (!(x & 1) && !(y & 1)) {
                put(1, x / 2, y / 2, p.cb << sh);
                put(2, x / 2, y / 2, p.cr << sh);
            }
        }
    }

    // CPU: ClipReader's RGBA conversion (same size => SWS_BICUBIC).
    std::vector<uint8_t> cpu(size_t(kW) * kH * 4);
    SwsContext *sws = sws_getContext(kW, kH, swsSourceFormat(fmt), kW, kH, AV_PIX_FMT_RGBA,
                                     SWS_BICUBIC, nullptr, nullptr, nullptr);
    configureSwsRange(sws, f, 1);
    uint8_t *dst[4] = {cpu.data(), nullptr, nullptr, nullptr};
    int dstStride[4] = {kW * 4, 0, 0, 0};
    sws_scale(sws, f->data, f->linesize, 0, kH, dst, dstStride);
    sws_freeContext(sws);

    // GPU: the NV12 shader's arithmetic on 8-bit NV12 (what GlRuntime uploads).
    float m[9];
    yuvMatrixCoefficients(yuvMatrixFromColorspace(colorspace), m);
    const float offY = full ? 0.f : 16.f / 255.f;
    const float scY = full ? 1.f : 255.f / 219.f;
    const float scC = full ? 1.f : 255.f / 224.f;

    Result res;
    for (int y = 0; y < kH; ++y) {
        for (int x = 0; x < kW; ++x) {
            // Patch interiors only: at a patch edge swscale blends chroma across the
            // boundary for 10-bit input, which is chroma upsampling, not the matrix.
            if (x % 16 < 2 || x % 16 > 13 || y % 16 < 2 || y % 16 > 13)
                continue;
            const Patch &p = kPatches[(y / 16) * 4 + x / 16];
            const int yy8 = full ? std::clamp(p.y * 255 / 235, 0, 255) : p.y;
            const float a = (yy8 / 255.f - offY) * scY;
            const float b = (p.cb / 255.f - 128.f / 255.f) * scC;
            const float c = (p.cr / 255.f - 128.f / 255.f) * scC;
            double ref[3];
            referenceRgb(double(yy8 << sh), double(p.cb << sh), double(p.cr << sh), bits, full,
                         colorspace, ref);
            if (std::min({ref[0], ref[1], ref[2]}) < 0.002 || std::max({ref[0], ref[1], ref[2]}) > 0.998)
                continue; // out of gamut for this matrix: clipped differently by design
            for (int k = 0; k < 3; ++k) {
                const float v = std::clamp(m[k * 3] * a + m[k * 3 + 1] * b + m[k * 3 + 2] * c, 0.f, 1.f);
                const int gpu = int(std::lround(v * 255.f));
                const int want = int(std::lround(std::clamp(ref[k], 0.0, 1.0) * 255.0));
                const int got = cpu[(size_t(y) * kW + x) * 4 + k];
                res.cpuMaxVsRef = std::max(res.cpuMaxVsRef, std::abs(got - want));
                res.gpuMaxVsRef = std::max(res.gpuMaxVsRef, std::abs(gpu - want));
                res.cpuMaxVsGpu = std::max(res.cpuMaxVsGpu, std::abs(got - gpu));
            }
            ++res.tested;
        }
    }
    av_frame_free(&f);
    return res;
}

} // namespace

class TstYuvMatrix : public QObject
{
    Q_OBJECT

private slots:
    void taggedColorspaceSelectsTheSameMatrixOnBothPaths()
    {
        auto frame = [](AVColorSpace cs) {
            AVFrame *f = av_frame_alloc();
            f->colorspace = cs;
            return f;
        };
        struct Row { AVColorSpace cs; YuvMatrix matrix; int sws; };
        const Row rows[] = {
            {AVCOL_SPC_BT709, YuvMatrix::Bt709, SWS_CS_ITU709},
            {AVCOL_SPC_UNSPECIFIED, YuvMatrix::Bt709, SWS_CS_ITU709},
            {AVCOL_SPC_BT470BG, YuvMatrix::Bt601, SWS_CS_ITU601},
            {AVCOL_SPC_SMPTE170M, YuvMatrix::Bt601, SWS_CS_ITU601},
            {AVCOL_SPC_SMPTE240M, YuvMatrix::Smpte240M, SWS_CS_SMPTE240M},
            {AVCOL_SPC_FCC, YuvMatrix::Fcc, SWS_CS_FCC},
            {AVCOL_SPC_BT2020_NCL, YuvMatrix::Bt2020, SWS_CS_BT2020},
            {AVCOL_SPC_BT2020_CL, YuvMatrix::Bt2020, SWS_CS_BT2020},
        };
        for (const Row &r : rows) {
            AVFrame *f = frame(r.cs);
            QCOMPARE(int(yuvMatrixFromColorspace(r.cs)), int(r.matrix));
            QCOMPARE(swsColorspaceFromFrame(f), r.sws);
            av_frame_free(&f);
        }
        QCOMPARE(swsColorspaceFromFrame(nullptr), int(SWS_CS_ITU709));
    }

    void shaderMatrixMatchesTheExactCoefficients()
    {
        for (AVColorSpace cs : {AVCOL_SPC_BT709, AVCOL_SPC_BT470BG, AVCOL_SPC_SMPTE240M, AVCOL_SPC_FCC,
                                AVCOL_SPC_BT2020_NCL}) {
            float m[9];
            yuvMatrixCoefficients(yuvMatrixFromColorspace(cs), m);
            const Kr_Kb k = coefficientsFor(cs);
            const double kg = 1 - k.kr - k.kb;
            QVERIFY(std::abs(m[2] - 2 * (1 - k.kr)) < 1e-3);
            QVERIFY(std::abs(m[7] - 2 * (1 - k.kb)) < 1e-3);
            QVERIFY(std::abs(m[4] + k.kb * 2 * (1 - k.kb) / kg) < 1e-3);
            QVERIFY(std::abs(m[5] + k.kr * 2 * (1 - k.kr) / kg) < 1e-3);
        }
    }

    void cpuAndGpuDecodeAgree_data()
    {
        QTest::addColumn<int>("bits");
        QTest::addColumn<bool>("full");
        QTest::addColumn<int>("colorspace");
        const struct { const char *n; AVColorSpace cs; } spaces[] = {
            {"bt709", AVCOL_SPC_BT709},         {"bt601", AVCOL_SPC_SMPTE170M},
            {"smpte240m", AVCOL_SPC_SMPTE240M}, {"fcc", AVCOL_SPC_FCC},
            {"bt2020ncl", AVCOL_SPC_BT2020_NCL}, {"bt2020cl", AVCOL_SPC_BT2020_CL},
        };
        for (int bits : {8, 10})
            for (bool full : {false, true})
                for (const auto &s : spaces)
                    QTest::newRow(qPrintable(QStringLiteral("%1-%2bit-%3")
                                                 .arg(s.n).arg(bits).arg(full ? "full" : "limited")))
                        << bits << full << int(s.cs);
    }

    void cpuAndGpuDecodeAgree()
    {
        QFETCH(int, bits);
        QFETCH(bool, full);
        QFETCH(int, colorspace);
        const Result r = measure(bits, full, colorspace);
        QVERIFY2(r.tested >= 8, "too few in-gamut patches");
        // swscale is fixed point: up to 3 levels at 8 bit and 7 at 10 bit away from the exact
        // matrix; a wrong matrix (BT.709 for BT.2020 was 13-17) is far outside that.
        const int tolerance = bits == 8 ? 3 : 7;
        QVERIFY2(r.cpuMaxVsRef <= tolerance, qPrintable(QStringLiteral("CPU off by %1").arg(r.cpuMaxVsRef)));
        QVERIFY2(r.gpuMaxVsRef <= 1, qPrintable(QStringLiteral("GPU off by %1").arg(r.gpuMaxVsRef)));
        QVERIFY2(r.cpuMaxVsGpu <= tolerance + 1,
                 qPrintable(QStringLiteral("CPU vs GPU differ by %1").arg(r.cpuMaxVsGpu)));
    }

    // Guards the test itself: with the matrix the old code used for BT.2020 (BT.709), the same
    // check fails, so it can tell the two apart.
    void wrongMatrixIsDetected()
    {
        const Result right = measure(8, false, AVCOL_SPC_BT2020_NCL);
        QVERIFY(right.cpuMaxVsRef <= 3);
        // Reference for BT.709 on BT.2020-tagged data = what the old SwsColorRange.h produced.
        double a[3], b[3];
        referenceRgb(128, 128, 230, 8, false, AVCOL_SPC_BT2020_NCL, a);
        referenceRgb(128, 128, 230, 8, false, AVCOL_SPC_BT709, b);
        double worst = 0;
        for (int k = 0; k < 3; ++k)
            worst = std::max(worst, std::abs(a[k] - b[k]) * 255.0);
        QVERIFY2(worst > 8.0, "BT.709 and BT.2020 must be distinguishable by this test");
    }
};

QTEST_APPLESS_MAIN(TstYuvMatrix)
#include "tst_yuvmatrix.moc"
