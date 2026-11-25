#include "engine/diagnostics/BenchPhases.h"

namespace drift::bench {

namespace {

std::atomic<bool> g_enabled{false};
std::atomic<uint64_t> g_ns[kPhases];
std::atomic<uint64_t> g_count[kPhases];
std::atomic<uint64_t> g_max[kPhases];

bool inRange(Phase phase)
{
    return int(phase) >= 0 && int(phase) < kPhases;
}

} // namespace

void setEnabled(bool on)
{
    g_enabled.store(on, std::memory_order_relaxed);
}

bool enabled()
{
    return g_enabled.load(std::memory_order_relaxed);
}

void add(Phase phase, uint64_t ns)
{
    if (!inRange(phase))
        return;
    const int i = int(phase);
    g_ns[i].fetch_add(ns, std::memory_order_relaxed);
    g_count[i].fetch_add(1, std::memory_order_relaxed);
    uint64_t seen = g_max[i].load(std::memory_order_relaxed);
    while (ns > seen && !g_max[i].compare_exchange_weak(seen, ns, std::memory_order_relaxed)) {
    }
}

Snapshot snapshot()
{
    Snapshot out;
    for (int i = 0; i < kPhases; ++i) {
        out.phases[i].ns = g_ns[i].load(std::memory_order_relaxed);
        out.phases[i].count = g_count[i].load(std::memory_order_relaxed);
        out.phases[i].maxNs = g_max[i].load(std::memory_order_relaxed);
    }
    return out;
}

void reset()
{
    for (int i = 0; i < kPhases; ++i) {
        g_ns[i].store(0, std::memory_order_relaxed);
        g_count[i].store(0, std::memory_order_relaxed);
        g_max[i].store(0, std::memory_order_relaxed);
    }
}

const char *id(Phase phase)
{
    switch (phase) {
    case Phase::ExportCompose:
        return "export_compose";
    case Phase::ExportFenceWait:
        return "export_fence_wait";
    case Phase::ExportHostCopy:
        return "export_host_copy";
    case Phase::ExportCudaCopy:
        return "export_cuda_copy";
    case Phase::ExportSend:
        return "export_send";
    case Phase::ExportCpuCompose:
        return "export_cpu_compose";
    case Phase::PreviewCudaImport:
        return "preview_cuda_import";
    case Phase::PreviewCudaSync:
        return "preview_cuda_sync";
    case Phase::Count_:
        break;
    }
    return "unknown";
}

} // namespace drift::bench
