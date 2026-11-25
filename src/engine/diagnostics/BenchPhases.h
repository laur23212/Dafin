#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>

// Dafin: time spent in the few places a CUDA change could save time, for `dafin --bench`
// (app/workers/BenchTool.cpp, docs/cuda-benchmark.md). Off unless the benchmark turns it on: a
// scope then costs one relaxed atomic load and nothing else. On, it is two steady_clock reads and
// three relaxed atomic adds. Nothing here waits on the GPU; a phase is CPU wall time on the thread
// that ran it, so a wait on the GPU shows up as the time the CPU sat in that wait.
//
// Pure C++, no Qt, so tests/tst_benchstats can drive it without a GPU.
namespace drift::bench {

enum class Phase {
    // Export, GPU NV12 route. One export thread runs all of these in turn.
    ExportCompose,   // build the scene and submit the GL passes (prepare hook)
    ExportFenceWait, // waiting for the GL readback fence of a slot (GPU still busy)
    ExportHostCopy,  // map the PBO and copy NV12 into the encoder frame (direct CUDA removes this)
    ExportCudaCopy,  // GL planes -> CUDA surface, including its stream wait (direct CUDA adds this)
    ExportSend,      // avcodec_send_frame/receive/mux for video and the audio that goes with it
    // Export, CPU route (DRIFT_EXPORT_SWSCALE or no GPU compositor).
    ExportCpuCompose, // compose to a QImage and swscale it
    // Preview import of an NVDEC frame into GL textures.
    PreviewCudaImport, // the whole map/copy/unmap/sync
    PreviewCudaSync,   // the cuStreamSynchronize inside it (what a preview sync change could save)
    Count_
};

constexpr int kPhases = int(Phase::Count_);

struct PhaseTotals
{
    uint64_t ns = 0;
    uint64_t count = 0;
    uint64_t maxNs = 0;
};

struct Snapshot
{
    PhaseTotals phases[kPhases];
};

void setEnabled(bool on);
bool enabled();

void add(Phase phase, uint64_t ns);
Snapshot snapshot();
void reset();

// Stable ids for the JSON report and tests.
const char *id(Phase phase);

// Times the enclosing block into `phase` when the benchmark is on.
class Scope
{
public:
    explicit Scope(Phase phase)
        : m_phase(phase)
        , m_on(enabled())
    {
        if (m_on)
            m_start = std::chrono::steady_clock::now();
    }
    ~Scope()
    {
        if (m_on) {
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - m_start);
            add(m_phase, uint64_t(ns.count() > 0 ? ns.count() : 0));
        }
    }
    Scope(const Scope &) = delete;
    Scope &operator=(const Scope &) = delete;

private:
    Phase m_phase;
    bool m_on;
    std::chrono::steady_clock::time_point m_start{};
};

// Like Scope, for a stretch that does not end with a block: stop() records it once.
class Stopwatch
{
public:
    explicit Stopwatch(Phase phase)
        : m_phase(phase)
        , m_on(enabled())
    {
        if (m_on)
            m_start = std::chrono::steady_clock::now();
    }
    void stop()
    {
        if (!m_on)
            return;
        m_on = false;
        const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::steady_clock::now() - m_start);
        add(m_phase, uint64_t(ns.count() > 0 ? ns.count() : 0));
    }

private:
    Phase m_phase;
    bool m_on;
    std::chrono::steady_clock::time_point m_start{};
};

} // namespace drift::bench
