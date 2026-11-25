// Dafin: HDR colour maths, the HDR export converter, the half-float LUT bake, and real HDR10/HLG
// files encoded through the same HdrExport calls Exporter::runDirect makes.
//
// Set DAFIN_HDR_TEST_OUT to a folder to keep the encoded files (for ffprobe / zscale checks).

#include "engine/color/CubeLut.h"
#include "engine/color/HdrColor.h"
#include "engine/encode/export/HdrExport.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtCore/qfloat16.h>
#include <QtTest>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/frame.h>
#include <libavutil/mastering_display_metadata.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
}

#include <array>
#include <cmath>
#include <cstring>
#include <random>
#include <vector>

using namespace drift;
using hdr::Transfer;

namespace {

// Test card: 8 vertical bars — white, 75% grey, 50% grey, black, red, green, blue, 18% grey.
const uint8_t kBars[8][3] = {
    {255, 255, 255}, {191, 191, 191}, {128, 128, 128}, {0, 0, 0},
    {255, 0, 0},     {0, 255, 0},     {0, 0, 255},     {118, 118, 118},
};

std::vector<uint8_t> barsImage(int w, int h)
{
    std::vector<uint8_t> img(size_t(w) * size_t(h) * 4);
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const uint8_t *c = kBars[(x * 8) / w];
            uint8_t *p = &img[(size_t(y) * size_t(w) + size_t(x)) * 4];
            p[0] = c[0];
            p[1] = c[1];
            p[2] = c[2];
            p[3] = 255;
        }
    }
    return img;
}

void referenceCodes(Transfer t, const uint8_t rgb[3], int bitDepth, int out[3])
{
    const double in[3] = {rgb[0] / 255.0, rgb[1] / 255.0, rgb[2] / 255.0};
    double sig[3];
    hdr::sdrToHdrSignal(t, in, sig);
    hdr::rgbToYcbcr2020Limited(sig, bitDepth, out);
}

uint16_t sample(const AVFrame *f, int plane, int x, int y)
{
    return reinterpret_cast<const uint16_t *>(f->data[plane] + size_t(y) * size_t(f->linesize[plane]))[x];
}

struct Encoded
{
    QString path;
    QString error;
};

// The order Exporter::runDirect uses for an HDR export.
Encoded encodeBars(const QString &dir, const char *encoder, const char *exportCodecId,
                   AVPixelFormat pixFmt, const char *fileName, Transfer t, int frames)
{
    Encoded r;
    r.path = QDir(dir).filePath(QString::fromLatin1(fileName));
    const QByteArray path = r.path.toUtf8();
    const AVCodec *codec = avcodec_find_encoder_by_name(encoder);
    if (!codec) {
        r.error = QStringLiteral("no encoder");
        return r;
    }
    if (!hdrexport::codecSupportsHdr(exportCodecId)) {
        r.error = QStringLiteral("codec not HDR");
        return r;
    }
    const int w = 320, h = 180;
    AVFormatContext *fmt = nullptr;
    avformat_alloc_output_context2(&fmt, nullptr, nullptr, path.constData());
    AVStream *st = fmt ? avformat_new_stream(fmt, nullptr) : nullptr;
    AVCodecContext *ctx = avcodec_alloc_context3(codec);
    AVFrame *frame = av_frame_alloc();
    AVPacket *pkt = av_packet_alloc();
    const std::vector<uint8_t> img = barsImage(w, h);
    auto fail = [&](const QString &e) {
        r.error = e;
    };
    auto drain = [&](AVFrame *in) -> bool {
        if (avcodec_send_frame(ctx, in) < 0)
            return false;
        for (;;) {
            const int rc = avcodec_receive_packet(ctx, pkt);
            if (rc == AVERROR(EAGAIN) || rc == AVERROR_EOF)
                return true;
            if (rc < 0)
                return false;
            av_packet_rescale_ts(pkt, ctx->time_base, st->time_base);
            pkt->stream_index = st->index;
            if (av_interleaved_write_frame(fmt, pkt) < 0)
                return false;
        }
    };

    if (!fmt || !st || !ctx || !frame || !pkt) {
        fail(QStringLiteral("alloc"));
        goto done;
    }
    ctx->width = w;
    ctx->height = h;
    ctx->pix_fmt = pixFmt;
    ctx->time_base = AVRational{1, 25};
    ctx->framerate = AVRational{25, 1};
    if (std::strcmp(encoder, "libsvtav1") == 0)
        av_opt_set(ctx->priv_data, "preset", "12", 0);
    if (std::strcmp(encoder, "libx265") == 0)
        av_opt_set(ctx->priv_data, "preset", "ultrafast", 0);
    if (std::strcmp(encoder, "libvpx-vp9") == 0) {
        av_opt_set(ctx->priv_data, "deadline", "realtime", 0);
        av_opt_set(ctx->priv_data, "cpu-used", "8", 0);
    }
    av_opt_set(ctx->priv_data, "crf", "4", 0);
    if (!hdrexport::pixelFormatSupportsHdr(ctx->pix_fmt)) {
        fail(QStringLiteral("pixfmt"));
        goto done;
    }
    hdrexport::applyColorTags(ctx, t);
    if (fmt->oformat->flags & AVFMT_GLOBALHEADER)
        ctx->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;
    if (!hdrexport::applyEncoderHdrOptions(ctx, t)) {
        fail(QStringLiteral("options"));
        goto done;
    }
    if (avcodec_open2(ctx, codec, nullptr) < 0) {
        fail(QStringLiteral("open"));
        goto done;
    }
    avcodec_parameters_from_context(st->codecpar, ctx);
    if (!hdrexport::attachStreamHdrMetadata(st, t)) {
        fail(QStringLiteral("stream metadata"));
        goto done;
    }
    st->time_base = ctx->time_base;
    if (avio_open(&fmt->pb, path.constData(), AVIO_FLAG_WRITE) < 0
        || avformat_write_header(fmt, nullptr) < 0) {
        fail(QStringLiteral("header"));
        goto done;
    }
    frame->format = ctx->pix_fmt;
    frame->width = w;
    frame->height = h;
    if (av_frame_get_buffer(frame, 32) < 0) {
        fail(QStringLiteral("frame"));
        goto done;
    }
    for (int i = 0; i < frames; ++i) {
        av_frame_make_writable(frame);
        if (!hdrexport::convertRgba8ToHdr(img.data(), w * 4, w, h, t, frame)) {
            fail(QStringLiteral("convert"));
            goto done;
        }
        hdrexport::applyColorTags(frame, t);
        frame->pts = i;
        if (!drain(frame)) {
            fail(QStringLiteral("encode"));
            goto done;
        }
    }
    if (!drain(nullptr) || av_write_trailer(fmt) < 0)
        fail(QStringLiteral("flush"));

done:
    av_packet_free(&pkt);
    av_frame_free(&frame);
    avcodec_free_context(&ctx);
    if (fmt && fmt->pb)
        avio_closep(&fmt->pb);
    avformat_free_context(fmt);
    return r;
}

} // namespace

class TestHdrExport : public QObject
{
    Q_OBJECT

private slots:
    void pqMatchesSt2084();
    void hlgMatchesBt2100();
    void bt709ToBt2020KeepsWhite();
    void sdrWhiteSitsAtReferenceWhite();
    void profileIdsDefaultToSdr();
    void sdrTagsAreTheOldBt709Tags();
    void converterMatchesReference_data();
    void converterMatchesReference();
    void lutHalfFloatKeepsValuesAboveOne();
    void realFiles_data();
    void realFiles();
};

void TestHdrExport::pqMatchesSt2084()
{
    // Reference signal values from the closed-form ST 2084 inverse EOTF.
    QVERIFY(std::abs(hdr::pqInverseEotf(100.0 / 10000.0) - 0.508078) < 1e-5);
    QVERIFY(std::abs(hdr::pqInverseEotf(203.0 / 10000.0) - 0.580689) < 1e-5);
    QVERIFY(std::abs(hdr::pqInverseEotf(1000.0 / 10000.0) - 0.751827) < 1e-5);
    QCOMPARE(hdr::pqInverseEotf(1.0), 1.0);
    for (int i = 0; i <= 1000; ++i) {
        const double y = std::pow(double(i) / 1000.0, 3.0);
        QVERIFY2(std::abs(hdr::pqEotf(hdr::pqInverseEotf(y)) - y) < 1e-9 + y * 1e-9,
                 qPrintable(QString::number(y)));
    }
}

void TestHdrExport::hlgMatchesBt2100()
{
    QVERIFY(std::abs(hdr::hlgOetf(1.0 / 12.0) - 0.5) < 1e-12);
    QVERIFY(std::abs(hdr::hlgOetf(1.0) - 1.0) < 1e-6); // the published a, b, c are rounded
    QCOMPARE(hdr::hlgOetf(0.0), 0.0);
    for (int i = 0; i <= 1000; ++i) {
        const double e = double(i) / 1000.0;
        QVERIFY(std::abs(hdr::hlgInverseOetf(hdr::hlgOetf(e)) - e) < 1e-9);
    }
    QVERIFY(std::abs(hdr::hlgSystemGamma(1000.0) - 1.2) < 1e-12);
}

void TestHdrExport::bt709ToBt2020KeepsWhite()
{
    const double white[3] = {1, 1, 1};
    double out[3];
    hdr::bt709ToBt2020(white, out);
    for (double v : out)
        QVERIFY(std::abs(v - 1.0) < 1e-9);
    const double red[3] = {1, 0, 0};
    hdr::bt709ToBt2020(red, out);
    QVERIFY(std::abs(out[0] - 0.6274) < 1e-4 && std::abs(out[1] - 0.0691) < 1e-4
            && std::abs(out[2] - 0.0164) < 1e-4);
}

void TestHdrExport::sdrWhiteSitsAtReferenceWhite()
{
    const double white[3] = {1, 1, 1};
    double out[3];
    // BT.2408: HDR reference white (203 cd/m²) = HLG 75 %, not 100 %.
    hdr::sdrToHdrSignal(Transfer::Hlg, white, out);
    for (double v : out)
        QVERIFY2(std::abs(v - 0.75) < 0.001, qPrintable(QString::number(v, 'f', 6)));
    hdr::sdrToHdrSignal(Transfer::Pq, white, out);
    for (double v : out)
        QVERIFY2(std::abs(v - 0.580689) < 1e-5, qPrintable(QString::number(v, 'f', 6)));
    const double black[3] = {0, 0, 0};
    int codes[3];
    hdr::sdrToHdrSignal(Transfer::Pq, black, out);
    hdr::rgbToYcbcr2020Limited(out, 10, codes);
    QCOMPARE(codes[0], 64);
    QCOMPARE(codes[1], 512);
    QCOMPARE(codes[2], 512);
    hdr::sdrToHdrSignal(Transfer::Hlg, white, out);
    hdr::rgbToYcbcr2020Limited(out, 10, codes);
    QCOMPARE(codes[0], 721); // 64 + 0.75 * 876 = 721
}

void TestHdrExport::profileIdsDefaultToSdr()
{
    QCOMPARE(hdrexport::transferFromProfileId(nullptr), Transfer::Sdr709);
    QCOMPARE(hdrexport::transferFromProfileId(""), Transfer::Sdr709);
    QCOMPARE(hdrexport::transferFromProfileId("sdr"), Transfer::Sdr709);
    QCOMPARE(hdrexport::transferFromProfileId("HDR10"), Transfer::Sdr709);
    QCOMPARE(hdrexport::transferFromProfileId("hdr10"), Transfer::Pq);
    QCOMPARE(hdrexport::transferFromProfileId("hlg"), Transfer::Hlg);
    QVERIFY(hdrexport::codecSupportsHdr("h265_10"));
    QVERIFY(!hdrexport::codecSupportsHdr("h265"));
    QVERIFY(!hdrexport::codecSupportsHdr("h265_nvenc"));
    QVERIFY(!hdrexport::codecSupportsHdr("h264_10"));
    QVERIFY(!hdrexport::codecSupportsHdr("prores_4444"));
    QVERIFY(hdrexport::pixelFormatSupportsHdr(AV_PIX_FMT_YUV420P10LE));
    QVERIFY(hdrexport::pixelFormatSupportsHdr(AV_PIX_FMT_YUV422P10LE));
    QVERIFY(!hdrexport::pixelFormatSupportsHdr(AV_PIX_FMT_YUV420P));
    QVERIFY(!hdrexport::pixelFormatSupportsHdr(AV_PIX_FMT_P010LE)); // semi-planar
    QVERIFY(!hdrexport::pixelFormatSupportsHdr(AV_PIX_FMT_YUVA444P10LE));
}

void TestHdrExport::sdrTagsAreTheOldBt709Tags()
{
    AVFrame *f = av_frame_alloc();
    hdrexport::applyColorTags(f, Transfer::Sdr709);
    QCOMPARE(f->color_range, AVCOL_RANGE_MPEG);
    QCOMPARE(f->colorspace, AVCOL_SPC_BT709);
    QCOMPARE(f->color_primaries, AVCOL_PRI_BT709);
    QCOMPARE(f->color_trc, AVCOL_TRC_BT709);
    QCOMPARE(f->chroma_location, AVCHROMA_LOC_UNSPECIFIED);
    hdrexport::applyColorTags(f, Transfer::Pq);
    QCOMPARE(f->colorspace, AVCOL_SPC_BT2020_NCL);
    QCOMPARE(f->color_primaries, AVCOL_PRI_BT2020);
    QCOMPARE(f->color_trc, AVCOL_TRC_SMPTE2084);
    hdrexport::applyColorTags(f, Transfer::Hlg);
    QCOMPARE(f->color_trc, AVCOL_TRC_ARIB_STD_B67);
    av_frame_free(&f);
}

void TestHdrExport::converterMatchesReference_data()
{
    QTest::addColumn<int>("transfer");
    QTest::addColumn<int>("pixFmt");
    QTest::newRow("pq-420p10") << int(Transfer::Pq) << int(AV_PIX_FMT_YUV420P10LE);
    QTest::newRow("hlg-420p10") << int(Transfer::Hlg) << int(AV_PIX_FMT_YUV420P10LE);
    QTest::newRow("pq-422p10") << int(Transfer::Pq) << int(AV_PIX_FMT_YUV422P10LE);
    QTest::newRow("hlg-444p12") << int(Transfer::Hlg) << int(AV_PIX_FMT_YUV444P12LE);
}

void TestHdrExport::converterMatchesReference()
{
    QFETCH(int, transfer);
    QFETCH(int, pixFmt);
    const Transfer t = Transfer(transfer);
    // Each colour fills a 2x2 block, so chroma averaging cannot blur two colours together.
    std::vector<std::array<uint8_t, 3>> colours;
    for (int v = 0; v < 256; ++v)
        colours.push_back({uint8_t(v), uint8_t(v), uint8_t(v)});
    std::mt19937 rng(2408);
    for (int i = 0; i < 4096; ++i)
        colours.push_back({uint8_t(rng()), uint8_t(rng()), uint8_t(rng())});
    for (int a = 0; a < 256; a += 15)
        for (int b = 0; b < 256; b += 15)
            colours.push_back({uint8_t(a), uint8_t(b), uint8_t(255 - a)});
    const int blocksX = 64;
    const int blocksY = int((colours.size() + blocksX - 1) / blocksX);
    const int w = blocksX * 2, h = blocksY * 2;
    std::vector<uint8_t> img(size_t(w) * size_t(h) * 4, 0);
    for (size_t i = 0; i < colours.size(); ++i) {
        const int bx = int(i % blocksX) * 2, by = int(i / blocksX) * 2;
        for (int dy = 0; dy < 2; ++dy)
            for (int dx = 0; dx < 2; ++dx) {
                uint8_t *p = &img[(size_t(by + dy) * size_t(w) + size_t(bx + dx)) * 4];
                p[0] = colours[i][0];
                p[1] = colours[i][1];
                p[2] = colours[i][2];
                p[3] = 255;
            }
    }
    AVFrame *f = av_frame_alloc();
    f->format = pixFmt;
    f->width = w;
    f->height = h;
    QVERIFY(av_frame_get_buffer(f, 32) >= 0);
    QVERIFY(hdrexport::convertRgba8ToHdr(img.data(), w * 4, w, h, t, f));
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(AVPixelFormat(pixFmt));
    const int depth = desc->comp[0].depth;
    int worst = 0;
    for (size_t i = 0; i < colours.size(); ++i) {
        const int bx = int(i % blocksX) * 2, by = int(i / blocksX) * 2;
        int ref[3];
        referenceCodes(t, colours[i].data(), depth, ref);
        worst = std::max(worst, std::abs(int(sample(f, 0, bx, by)) - ref[0]));
        worst = std::max(worst, std::abs(int(sample(f, 1, bx >> desc->log2_chroma_w, by >> desc->log2_chroma_h)) - ref[1]));
        worst = std::max(worst, std::abs(int(sample(f, 2, bx >> desc->log2_chroma_w, by >> desc->log2_chroma_h)) - ref[2]));
    }
    av_frame_free(&f);
    qInfo("worst code difference vs double reference: %d", worst);
    QVERIFY2(worst <= 1, qPrintable(QString::number(worst)));
    // Refuses an 8-bit destination and SDR.
    AVFrame *g = av_frame_alloc();
    g->format = AV_PIX_FMT_YUV420P;
    g->width = 2;
    g->height = 2;
    QVERIFY(av_frame_get_buffer(g, 32) >= 0);
    QVERIFY(!hdrexport::convertRgba8ToHdr(img.data(), w * 4, 2, 2, t, g));
    av_frame_free(&g);
}

void TestHdrExport::lutHalfFloatKeepsValuesAboveOne()
{
    // An original synthetic LUT whose outputs go above 1 and below 0.
    CubeLut lut = identityLut(2);
    lut.rgb[(((1 * 2) + 1) * 2 + 1) * 3 + 0] = 2.5f;  // white → R 2.5
    lut.rgb[0] = -0.25f;                              // black → R -0.25
    QVERIFY(lutHasOutOfRangeValues(lut));
    QVERIFY(!lutHasOutOfRangeValues(identityLut(33)));

    const QByteArray b8 = bakeTiledRgba8(lut);
    const QByteArray b16 = bakeTiledRgba16F(lut);
    QCOMPARE(b16.size(), b8.size() * 2);
    // Texel (x = b*n + r, y = g): white is x = 3, y = 1 → index (1*4 + 3).
    const int white = (1 * 4 + 3) * 4;
    QCOMPARE(uchar(b8[white]), uchar(255)); // the 8-bit bake clips 2.5 to 1
    const auto *h = reinterpret_cast<const qfloat16 *>(b16.constData());
    QCOMPARE(float(h[white]), 2.5f);
    QCOMPARE(float(h[0]), -0.25f);
    QCOMPARE(float(h[white + 3]), 1.0f);
    // Identity bakes agree between the two (to 8-bit precision).
    const QByteArray i8 = bakeTiledRgba8(identityLut(33));
    const QByteArray i16 = bakeTiledRgba16F(identityLut(33));
    const auto *ih = reinterpret_cast<const qfloat16 *>(i16.constData());
    for (int i = 0; i < i8.size(); ++i)
        QVERIFY(std::abs(float(ih[i]) * 255.0f - float(uchar(i8[i]))) <= 0.5f);

    const float in[3] = {1.0f, 1.0f, 1.0f};
    float out[3];
    applyLutF(lut, in, 1.0f, out);
    QCOMPARE(out[0], 2.5f);
    QCOMPARE(qRed(applyLut(lut, qRgb(255, 255, 255), 1.0f)), 255);
}

void TestHdrExport::realFiles_data()
{
    QTest::addColumn<QString>("encoder");
    QTest::addColumn<QString>("codecId");
    QTest::addColumn<int>("pixFmt");
    QTest::addColumn<QString>("file");
    QTest::addColumn<int>("transfer");
    QTest::newRow("x265-pq-mp4") << "libx265" << "h265_10" << int(AV_PIX_FMT_YUV420P10LE) << "pq_x265.mp4" << int(Transfer::Pq);
    QTest::newRow("x265-pq-mkv") << "libx265" << "h265_10" << int(AV_PIX_FMT_YUV420P10LE) << "pq_x265.mkv" << int(Transfer::Pq);
    QTest::newRow("x265-hlg-mp4") << "libx265" << "h265_10" << int(AV_PIX_FMT_YUV420P10LE) << "hlg_x265.mp4" << int(Transfer::Hlg);
    QTest::newRow("x265-12-pq-mp4") << "libx265" << "h265_12" << int(AV_PIX_FMT_YUV420P12LE) << "pq_x265_12.mp4" << int(Transfer::Pq);
    QTest::newRow("svtav1-pq-mp4") << "libsvtav1" << "av1_svt_10" << int(AV_PIX_FMT_YUV420P10LE) << "pq_svtav1.mp4" << int(Transfer::Pq);
    QTest::newRow("vp9-pq-webm") << "libvpx-vp9" << "vp9_10" << int(AV_PIX_FMT_YUV420P10LE) << "pq_vp9.webm" << int(Transfer::Pq);
    QTest::newRow("prores-hlg-mkv") << "prores_ks" << "prores" << int(AV_PIX_FMT_YUV422P10LE) << "hlg_prores.mkv" << int(Transfer::Hlg);
}

void TestHdrExport::realFiles()
{
    QFETCH(QString, encoder);
    QFETCH(QString, codecId);
    QFETCH(int, pixFmt);
    QFETCH(QString, file);
    QFETCH(int, transfer);
    const Transfer t = Transfer(transfer);
    if (!avcodec_find_encoder_by_name(encoder.toLatin1().constData()))
        QSKIP("encoder not in this FFmpeg build");

    QTemporaryDir tmp;
    QString dir = qEnvironmentVariable("DAFIN_HDR_TEST_OUT");
    if (dir.isEmpty())
        dir = tmp.path();
    QDir().mkpath(dir);
    const Encoded enc = encodeBars(dir, encoder.toLatin1().constData(), codecId.toLatin1().constData(),
                                   AVPixelFormat(pixFmt), file.toLatin1().constData(), t, 5);
    QVERIFY2(enc.error.isEmpty(), qPrintable(enc.error));

    AVFormatContext *in = nullptr;
    QVERIFY(avformat_open_input(&in, enc.path.toUtf8().constData(), nullptr, nullptr) == 0);
    QVERIFY(avformat_find_stream_info(in, nullptr) >= 0);
    const AVCodecParameters *par = in->streams[0]->codecpar;
    QCOMPARE(par->color_primaries, AVCOL_PRI_BT2020);
    QCOMPARE(par->color_space, AVCOL_SPC_BT2020_NCL);
    QCOMPARE(par->color_range, AVCOL_RANGE_MPEG);
    QCOMPARE(par->color_trc, t == Transfer::Pq ? AVCOL_TRC_SMPTE2084 : AVCOL_TRC_ARIB_STD_B67);

    const AVPacketSideData *mdcv = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
                                                           AV_PKT_DATA_MASTERING_DISPLAY_METADATA);
    const AVPacketSideData *clli = av_packet_side_data_get(par->coded_side_data, par->nb_coded_side_data,
                                                           AV_PKT_DATA_CONTENT_LIGHT_LEVEL);
    if (t == Transfer::Pq) {
        QVERIFY2(mdcv, "container has no mastering display metadata");
        QVERIFY2(clli, "container has no content light level");
        const auto *m = reinterpret_cast<const AVMasteringDisplayMetadata *>(mdcv->data);
        QVERIFY(m->has_luminance && m->has_primaries);
        QVERIFY(std::abs(av_q2d(m->max_luminance) - 1000.0) < 1e-6);
        QVERIFY(std::abs(av_q2d(m->min_luminance) - 0.0001) < 1e-9);
        QVERIFY(std::abs(av_q2d(m->display_primaries[1][1]) - 0.690) < 1e-4);
        QCOMPARE(reinterpret_cast<const AVContentLightMetadata *>(clli->data)->MaxCLL, 203u);
    } else {
        QVERIFY(!mdcv);
    }

    // Decode the first frame and read the bar centres back.
    const AVCodec *dec = avcodec_find_decoder(par->codec_id);
    AVCodecContext *dctx = avcodec_alloc_context3(dec);
    avcodec_parameters_to_context(dctx, par);
    QVERIFY(avcodec_open2(dctx, dec, nullptr) == 0);
    AVPacket *pkt = av_packet_alloc();
    AVFrame *fr = av_frame_alloc();
    bool got = false;
    while (!got && av_read_frame(in, pkt) >= 0) {
        if (avcodec_send_packet(dctx, pkt) >= 0 && avcodec_receive_frame(dctx, fr) == 0)
            got = true;
        av_packet_unref(pkt);
    }
    if (!got) {
        avcodec_send_packet(dctx, nullptr);
        got = avcodec_receive_frame(dctx, fr) == 0;
    }
    QVERIFY(got);
    const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(AVPixelFormat(fr->format));
    QCOMPARE(desc->comp[0].depth, av_pix_fmt_desc_get(AVPixelFormat(pixFmt))->comp[0].depth);
    if (t == Transfer::Pq && encoder == QLatin1String("libx265")) {
        // The HDR10 SEI x265 writes from x265-params comes back as frame side data.
        QVERIFY2(av_frame_get_side_data(fr, AV_FRAME_DATA_MASTERING_DISPLAY_METADATA),
                 "no mastering display SEI in the bitstream");
        const AVFrameSideData *c = av_frame_get_side_data(fr, AV_FRAME_DATA_CONTENT_LIGHT_LEVEL);
        QVERIFY2(c, "no content light SEI in the bitstream");
        QCOMPARE(reinterpret_cast<const AVContentLightMetadata *>(c->data)->MaxCLL, 203u);
    }
    int worst = 0;
    for (int bar = 0; bar < 8; ++bar) {
        const int x = (fr->width * (2 * bar + 1)) / 16;
        const int y = fr->height / 2;
        int ref[3];
        referenceCodes(t, kBars[bar], desc->comp[0].depth, ref);
        const int vals[3] = {sample(fr, 0, x, y), sample(fr, 1, x >> desc->log2_chroma_w, y >> desc->log2_chroma_h),
                             sample(fr, 2, x >> desc->log2_chroma_w, y >> desc->log2_chroma_h)};
        for (int c = 0; c < 3; ++c)
            worst = std::max(worst, std::abs(vals[c] - ref[c]));
    }
    qInfo("%s: worst decoded code difference at bar centres: %d", qPrintable(file), worst);
    // Near-lossless settings; the margin is codec noise, not a colour error (a wrong matrix,
    // range or transfer is off by tens to hundreds of codes).
    QVERIFY2(worst <= (desc->comp[0].depth == 12 ? 16 : 4), qPrintable(QString::number(worst)));

    av_frame_free(&fr);
    av_packet_free(&pkt);
    avcodec_free_context(&dctx);
    avformat_close_input(&in);
}

QTEST_GUILESS_MAIN(TestHdrExport)
#include "tst_hdrexport.moc"
