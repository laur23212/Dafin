#pragma once

#include <string>
#include <vector>

// Dafin: the numbers and the go/stop rules behind `dafin --bench` (docs/cuda-benchmark.md).
//
// The benchmark can only measure the routes Dafin already has. What it can say about a CUDA
// change it has not made yet (C4 direct NVENC export, C5 preview sync, C6 NVDEC pool for
// scrubbing) is a ceiling: the time spent in the steps that change would remove. A ceiling
// can rule a change out (if removing all of it would not be noticed, building it is not worth
// it); it can never prove a gain. A "go" here only means "worth an opt-in experiment", whose own
// A/B then decides.
//
// Pure C++, no Qt (tests/tst_benchstats).
namespace drift::bench {

// What counts as a gain the user would notice. Explicit so a report states what it was judged by.
struct Thresholds
{
    // C4: the export would have to get at least this much shorter.
    double exportMinFraction = 0.10;
    // C5: preview is only worth touching when its p95 frame misses the frame budget.
    // A sync share below this is not the reason it misses.
    double previewMinSyncShare = 0.15;
    // C6: scrubbing feels laggy above this p95 per-seek latency...
    double scrubLaggyP95Ms = 100.0;
    // ...and a decoder change only helps when waiting on the decoder is most of it.
    double scrubMinDecodeShare = 0.5;
};

// Median of `v` (mean of the middle two for an even count). 0 for an empty list.
double median(std::vector<double> v);
// Nearest-rank percentile, p in [0, 100]. With five values p95 is the largest: said in the report.
double percentile(std::vector<double> v, double p);

// One export run (default route), from the child's JSON.
struct ExportRun
{
    double wallS = 0;
    bool nvenc = false;        // the encoder was NVENC
    long long frames = 0;      // frames encoded
    long long hostNv12 = 0;    // frames that went GL -> PBO -> host NV12
    long long directCuda = 0;  // frames that went GL -> CUDA surface
    double hostCopyS = 0;      // ExportHostCopy
    double sendS = 0;          // ExportSend (encode + mux + audio)
};

// One preview run: playback sweep and scrub, from the child's JSON.
struct PreviewRun
{
    std::vector<double> frameMs;      // playback, per frame
    std::vector<double> frameSyncMs;  // the cuStreamSynchronize part of each frame
    std::vector<double> seekMs;       // scrub, per seek
    std::vector<double> seekDecodeMs; // decoder wait inside each seek
    double budgetMs = 33.3;           // 1000 / project fps
    long long cudaImports = 0;        // frames imported through CUDA-GL interop
    long long nvdecFrames = 0;        // frames that came out of NVDEC (CUDA surfaces), imported or downloaded
};

enum class Verdict {
    Go,           // the ceiling is big enough: an opt-in experiment is worth building
    Stop,         // even removing all of it would not be noticed
    Unsupported,  // the route this change would replace never ran here (no NVENC, no NVDEC import)
    Inconclusive, // runs missing or disagreeing
};

const char *id(Verdict verdict);

struct Decision
{
    Verdict verdict = Verdict::Inconclusive;
    double value = 0;       // the measured ceiling the verdict is about (a fraction or ms)
    std::string reason;     // one plain sentence, English
};

// C4: upper bound on what direct CUDA->NVENC could save = host copy + send, over wall time.
Decision decideExport(const std::vector<ExportRun> &runs, const Thresholds &t = {});
// C5: removing the preview's cuStreamSynchronize altogether, would p95 meet the budget?
Decision decidePreviewSync(const std::vector<PreviewRun> &runs, const Thresholds &t = {});
// C6: is scrubbing laggy, and is the decoder what it waits for?
Decision decideScrub(const std::vector<PreviewRun> &runs, const Thresholds &t = {});

} // namespace drift::bench
