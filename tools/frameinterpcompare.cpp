// Measures flow interpolation against plain blending on real footage, with ground truth.
//
// Point it at frames extracted from a clip in order. For every consecutive triple it throws the
// middle frame away, rebuilds it from its two neighbours both ways, and compares each rebuild
// with the frame that was actually there. The middle frame is the answer, so nothing here rests
// on anyone's opinion of a still.
//
//   ffmpeg -i clip.mp4 -vsync 0 -q:v 1 frames/%05d.png
//   frameinterpcompare frames/
//
// Reading the result:
//
//   PSNR rewards blur, and a blend of two frames is a blur. Flow losing on PSNR while winning
//   on the gradient metric is the signature of it getting the motion right and the texture
//   slightly wrong, which is usually the one to prefer; flow winning on PSNR and losing on
//   gradients is the signature of it smearing. Neither number decides anything on its own.
//
//   `declined` and `fellBack` are not failures. A path that declines on the frames it cannot
//   do and blends there instead is the design working. A decline rate near 100% means flow is
//   switched on and doing nothing, which looks identical to it being off and is a different
//   bug.
//
// This is the first gate, not the last. Shipping it as a quality feature needs the rendered
// motion watched at native speed and approved, on this footage, per the Stage 19 acceptance
// gates -- none of which a number in this table can stand in for.

#include "engine/decode/interp/FlowEstimator.h"
#include "engine/decode/interp/FlowSynth.h"
#include "engine/decode/interp/FrameBlend.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QImage>
#include <QString>
#include <QStringList>
#include <QTextStream>

#include <algorithm>
#include <cmath>
#include <vector>

namespace {

QTextStream &out()
{
    static QTextStream stream(stdout);
    return stream;
}

struct Scores
{
    double psnr = 0.0;
    double gradientSimilarity = 0.0;
};

std::vector<float> lumaOf(const QImage &image)
{
    const QImage rgb = image.convertToFormat(QImage::Format_RGBA8888);
    std::vector<float> luma(static_cast<size_t>(rgb.width()) * rgb.height(), 0.0f);
    for (int y = 0; y < rgb.height(); ++y) {
        const quint8 *row = rgb.constScanLine(y);
        for (int x = 0; x < rgb.width(); ++x) {
            const quint8 *p = row + static_cast<ptrdiff_t>(x) * 4;
            luma[static_cast<size_t>(y) * rgb.width() + x] =
                0.2126f * p[0] + 0.7152f * p[1] + 0.0722f * p[2];
        }
    }
    return luma;
}

// Gradient magnitude similarity, averaged. Sensitive to structure landing in the wrong place,
// which is the fault PSNR is least able to see and the one that matters most here.
double gradientSimilarity(const std::vector<float> &a, const std::vector<float> &b, int w, int h)
{
    const auto gradient = [w, h](const std::vector<float> &src, int x, int y) {
        const int xm = std::max(x - 1, 0);
        const int xp = std::min(x + 1, w - 1);
        const int ym = std::max(y - 1, 0);
        const int yp = std::min(y + 1, h - 1);
        const float gx = src[static_cast<size_t>(y) * w + xp] - src[static_cast<size_t>(y) * w + xm];
        const float gy = src[static_cast<size_t>(yp) * w + x] - src[static_cast<size_t>(ym) * w + x];
        return std::sqrt(gx * gx + gy * gy);
    };

    constexpr double kStabiliser = 170.0; // keeps flat regions from dominating the average
    double total = 0.0;
    for (int y = 0; y < h; ++y) {
        for (int x = 0; x < w; ++x) {
            const double ga = gradient(a, x, y);
            const double gb = gradient(b, x, y);
            total += (2.0 * ga * gb + kStabiliser) / (ga * ga + gb * gb + kStabiliser);
        }
    }
    return total / (static_cast<double>(w) * h);
}

Scores scoreAgainst(const QImage &candidate, const QImage &truth)
{
    Scores scores;
    if (candidate.isNull() || candidate.size() != truth.size())
        return scores;

    const std::vector<float> lc = lumaOf(candidate);
    const std::vector<float> lt = lumaOf(truth);

    double squaredError = 0.0;
    for (size_t i = 0; i < lc.size(); ++i) {
        const double diff = lc[i] - lt[i];
        squaredError += diff * diff;
    }
    const double mse = squaredError / static_cast<double>(lc.size());
    scores.psnr = mse <= 0.0 ? 99.0 : 10.0 * std::log10(255.0 * 255.0 / mse);
    scores.gradientSimilarity = gradientSimilarity(lc, lt, truth.width(), truth.height());
    return scores;
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    const QStringList args = QCoreApplication::arguments();
    if (args.size() < 2) {
        out() << "usage: frameinterpcompare <directory-of-frames-in-order>\n"
              << "       ffmpeg -i clip.mp4 -vsync 0 -q:v 1 frames/%05d.png\n";
        out().flush();
        return 2;
    }

    QDir dir(args.at(1));
    if (!dir.exists()) {
        out() << "no such directory: " << args.at(1) << "\n";
        out().flush();
        return 2;
    }

    QStringList names = dir.entryList({"*.png", "*.PNG", "*.ppm", "*.bmp"}, QDir::Files, QDir::Name);
    if (names.size() < 3) {
        out() << "need at least 3 frames in order, found " << names.size() << "\n";
        out().flush();
        return 2;
    }

    out() << "frames: " << names.size() << "   triples: " << (names.size() - 2) << "\n";
    out() << "alpha 0.5, middle frame held out as truth\n\n";
    out() << "triple        blend PSNR  flow PSNR   blend grad  flow grad   flow\n";
    out().flush();

    drift::interp::FlowOptions flowOptions;
    drift::interp::FlowSynthOptions synthOptions;

    int blendPsnrWins = 0;
    int flowPsnrWins = 0;
    int blendGradWins = 0;
    int flowGradWins = 0;
    int declined = 0;
    int compared = 0;
    double blendPsnrTotal = 0.0;
    double flowPsnrTotal = 0.0;
    double blendGradTotal = 0.0;
    double flowGradTotal = 0.0;

    for (int i = 1; i + 1 < names.size(); ++i) {
        const QImage a(dir.filePath(names.at(i - 1)));
        const QImage truth(dir.filePath(names.at(i)));
        const QImage b(dir.filePath(names.at(i + 1)));
        if (a.isNull() || b.isNull() || truth.isNull() || a.size() != b.size()
            || a.size() != truth.size()) {
            out() << names.at(i) << "  skipped (unreadable or size mismatch)\n";
            continue;
        }

        const QImage blended = drift::interp::blendFrames(a, b, 0.5);

        drift::interp::FlowSynthStats stats;
        const drift::interp::FlowPairAnalysis analysis =
            drift::interp::estimateFlow(a, b, flowOptions);
        const QImage flowed =
            drift::interp::synthesizeFlowFrame(a, b, 0.5, analysis, synthOptions, &stats);

        const Scores blendScores = scoreAgainst(blended, truth);

        if (flowed.isNull()) {
            ++declined;
            out() << QStringLiteral("%1  %2        --          %3       --          declined\n")
                         .arg(names.at(i), -12)
                         .arg(blendScores.psnr, 6, 'f', 2)
                         .arg(blendScores.gradientSimilarity, 6, 'f', 4);
            out().flush();
            continue;
        }

        const Scores flowScores = scoreAgainst(flowed, truth);
        ++compared;
        blendPsnrTotal += blendScores.psnr;
        flowPsnrTotal += flowScores.psnr;
        blendGradTotal += blendScores.gradientSimilarity;
        flowGradTotal += flowScores.gradientSimilarity;
        if (flowScores.psnr > blendScores.psnr)
            ++flowPsnrWins;
        else
            ++blendPsnrWins;
        if (flowScores.gradientSimilarity > blendScores.gradientSimilarity)
            ++flowGradWins;
        else
            ++blendGradWins;

        out() << QStringLiteral("%1  %2      %3      %4      %5      fellBack %6%\n")
                     .arg(names.at(i), -12)
                     .arg(blendScores.psnr, 6, 'f', 2)
                     .arg(flowScores.psnr, 6, 'f', 2)
                     .arg(blendScores.gradientSimilarity, 6, 'f', 4)
                     .arg(flowScores.gradientSimilarity, 6, 'f', 4)
                     .arg(stats.blendFallbackFraction * 100.0, 5, 'f', 1);
        out().flush();
    }

    out() << "\n";
    out() << "compared: " << compared << "   declined: " << declined << "\n";
    if (compared > 0) {
        out() << QStringLiteral("mean PSNR      blend %1   flow %2\n")
                     .arg(blendPsnrTotal / compared, 6, 'f', 2)
                     .arg(flowPsnrTotal / compared, 6, 'f', 2);
        out() << QStringLiteral("mean gradient  blend %1   flow %2\n")
                     .arg(blendGradTotal / compared, 6, 'f', 4)
                     .arg(flowGradTotal / compared, 6, 'f', 4);
        out() << "PSNR wins      blend " << blendPsnrWins << "   flow " << flowPsnrWins << "\n";
        out() << "gradient wins  blend " << blendGradWins << "   flow " << flowGradWins << "\n";
    }
    out() << "\nNumbers are supporting evidence. The decision needs the rendered motion watched\n"
          << "at native speed on this footage and approved; nothing above substitutes for that.\n";
    out().flush();
    return 0;
}
