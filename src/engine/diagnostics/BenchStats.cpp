#include "engine/diagnostics/BenchStats.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace drift::bench {

namespace {

std::string format(const char *fmt, double a, double b = 0, double c = 0, double e = 0)
{
    char buf[320];
    std::snprintf(buf, sizeof(buf), fmt, a, b, c, e);
    return buf;
}

double sum(const std::vector<double> &v)
{
    double s = 0;
    for (double x : v)
        s += x;
    return s;
}

} // namespace

double median(std::vector<double> v)
{
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    const size_t n = v.size();
    return n % 2 ? v[n / 2] : 0.5 * (v[n / 2 - 1] + v[n / 2]);
}

double percentile(std::vector<double> v, double p)
{
    if (v.empty())
        return 0;
    std::sort(v.begin(), v.end());
    p = std::clamp(p, 0.0, 100.0);
    // Nearest rank: the smallest value with at least p% of the list at or below it.
    size_t rank = size_t(std::ceil(p / 100.0 * double(v.size())));
    rank = std::clamp<size_t>(rank, 1, v.size());
    return v[rank - 1];
}

const char *id(Verdict verdict)
{
    switch (verdict) {
    case Verdict::Go:
        return "GO";
    case Verdict::Stop:
        return "STOP";
    case Verdict::Unsupported:
        return "UNSUPPORTED";
    case Verdict::Inconclusive:
        return "INCONCLUSIVE";
    }
    return "INCONCLUSIVE";
}

Decision decideExport(const std::vector<ExportRun> &runs, const Thresholds &t)
{
    Decision d;
    if (runs.empty()) {
        d.reason = "No export run finished.";
        return d;
    }
    std::vector<double> fractions;
    for (const ExportRun &r : runs) {
        if (!r.nvenc) {
            d.verdict = Verdict::Unsupported;
            d.reason = "The export did not run on NVENC, so there is no CUDA export route to speed up here.";
            return d;
        }
        if (r.hostNv12 + r.directCuda <= 0 || r.wallS <= 0) {
            d.reason = "No frame was seen taking the GPU NV12 route, so the export's road is not proven.";
            return d;
        }
        fractions.push_back((r.hostCopyS + r.sendS) / r.wallS);
    }
    d.value = median(fractions);
    const double lo = *std::min_element(fractions.begin(), fractions.end());
    const double hi = *std::max_element(fractions.begin(), fractions.end());
    if (d.value < t.exportMinFraction) {
        d.verdict = Verdict::Stop;
        d.reason = format("Copying frames to the CPU and handing them to NVENC is at most %.1f%% of the "
                          "export (runs %.1f-%.1f%%), under the %.0f%% you would notice; direct CUDA "
                          "export cannot save more than that.",
                          d.value * 100, lo * 100, hi * 100, t.exportMinFraction * 100);
    } else {
        d.verdict = Verdict::Go;
        d.reason = format("Up to %.1f%% of the export (runs %.1f-%.1f%%) is in the steps direct CUDA "
                          "export removes; worth an opt-in experiment, whose own A/B decides.",
                          d.value * 100, lo * 100, hi * 100);
    }
    return d;
}

Decision decidePreviewSync(const std::vector<PreviewRun> &runs, const Thresholds &t)
{
    Decision d;
    std::vector<double> p95s, p95sWithoutSync, shares;
    long long imports = 0;
    double budget = 0;
    for (const PreviewRun &r : runs) {
        imports += r.cudaImports;
        budget = r.budgetMs;
        if (r.frameMs.empty())
            continue;
        std::vector<double> without;
        without.reserve(r.frameMs.size());
        for (size_t i = 0; i < r.frameMs.size(); ++i) {
            const double sync = i < r.frameSyncMs.size() ? r.frameSyncMs[i] : 0.0;
            without.push_back(std::max(0.0, r.frameMs[i] - sync));
        }
        p95s.push_back(percentile(r.frameMs, 95));
        p95sWithoutSync.push_back(percentile(without, 95));
        const double total = sum(r.frameMs);
        shares.push_back(total > 0 ? sum(r.frameSyncMs) / total : 0.0);
    }
    if (p95s.empty()) {
        d.reason = "No preview run finished.";
        return d;
    }
    if (imports <= 0) {
        d.verdict = Verdict::Unsupported;
        d.reason = "No frame took the CUDA-to-OpenGL import (NVDEC not used, or the interop was "
                   "refused), so there is no preview sync to remove here.";
        return d;
    }
    const double p95 = median(p95s);
    const double p95Without = median(p95sWithoutSync);
    const double share = median(shares);
    d.value = share;
    if (p95 <= budget) {
        d.verdict = Verdict::Stop;
        d.reason = format("Preview already makes the frame budget at p95 (%.1f ms against %.1f ms); a "
                          "faster sync would not be visible.",
                          p95, budget);
    } else if (p95Without <= budget || share >= t.previewMinSyncShare) {
        d.verdict = Verdict::Go;
        d.reason = format("p95 %.1f ms misses the %.1f ms budget and the CUDA sync is %.0f%% of frame "
                          "time; worth an opt-in sync experiment.",
                          p95, budget, share * 100);
    } else {
        d.verdict = Verdict::Stop;
        d.reason = format("p95 %.1f ms misses the budget, but the CUDA sync is only %.0f%% of frame "
                          "time: even without it p95 would be %.1f ms. The time goes elsewhere.",
                          p95, share * 100, p95Without);
    }
    return d;
}

Decision decideScrub(const std::vector<PreviewRun> &runs, const Thresholds &t)
{
    Decision d;
    std::vector<double> p95s, shares;
    long long hw = 0;
    for (const PreviewRun &r : runs) {
        hw += r.nvdecFrames;
        if (r.seekMs.empty())
            continue;
        p95s.push_back(percentile(r.seekMs, 95));
        const double total = sum(r.seekMs);
        shares.push_back(total > 0 ? sum(r.seekDecodeMs) / total : 0.0);
    }
    if (p95s.empty()) {
        d.reason = "No scrub run finished.";
        return d;
    }
    if (hw <= 0) {
        d.verdict = Verdict::Unsupported;
        d.reason = "No frame came out of NVDEC, so there is no NVDEC pool to tune here.";
        return d;
    }
    const double p95 = median(p95s);
    const double share = median(shares);
    d.value = p95;
    if (p95 <= t.scrubLaggyP95Ms) {
        d.verdict = Verdict::Stop;
        d.reason = format("Scrubbing answers within %.0f ms at p95 (limit %.0f ms); a decoder pool change "
                          "would not be felt.",
                          p95, t.scrubLaggyP95Ms);
    } else if (share >= t.scrubMinDecodeShare) {
        d.verdict = Verdict::Go;
        d.reason = format("Scrubbing is slow (p95 %.0f ms) and %.0f%% of it is waiting on the decoder; "
                          "worth the NVDEC pool/cache study.",
                          p95, share * 100);
    } else {
        d.verdict = Verdict::Stop;
        d.reason = format("Scrubbing is slow (p95 %.0f ms) but only %.0f%% of it is waiting on the "
                          "decoder; the time goes elsewhere.",
                          p95, share * 100);
    }
    return d;
}

} // namespace drift::bench
