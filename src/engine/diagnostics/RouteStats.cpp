#include "engine/diagnostics/RouteStats.h"

#include <mutex>

namespace drift::routes {

namespace {

std::atomic<uint64_t> g_imports[kImportRoutes];
std::atomic<uint64_t> g_encodes[kEncodeRoutes];
std::atomic<uint64_t> g_fallbacks[kFallbacks];
std::atomic<int> g_plan{int(ExportPlan::None)};
std::mutex g_ortMutex; // model sessions are built rarely, never per frame
std::string g_ortProvider;

template <typename E>
bool inRange(E value, int count)
{
    return int(value) >= 0 && int(value) < count;
}

} // namespace

void countImport(ImportRoute route)
{
    if (inRange(route, kImportRoutes))
        g_imports[int(route)].fetch_add(1, std::memory_order_relaxed);
}

void countEncode(EncodeRoute route)
{
    if (inRange(route, kEncodeRoutes))
        g_encodes[int(route)].fetch_add(1, std::memory_order_relaxed);
}

void noteFallback(Fallback reason)
{
    if (inRange(reason, kFallbacks))
        g_fallbacks[int(reason)].fetch_add(1, std::memory_order_relaxed);
}

void notePlan(ExportPlan plan)
{
    g_plan.store(int(plan), std::memory_order_relaxed);
}

void noteOrtProvider(const std::string &provider)
{
    bool plain = !provider.empty() && provider.size() <= 24;
    for (const char c : provider) {
        const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9')
                        || c == '_' || c == '-';
        if (!ok)
            plain = false; // '/', '\\', ':', '.', spaces: could be (part of) a path
    }
    std::lock_guard<std::mutex> lock(g_ortMutex);
    g_ortProvider = plain ? provider : std::string("other");
}

Snapshot snapshot()
{
    Snapshot s;
    for (int i = 0; i < kImportRoutes; ++i)
        s.imports[i] = g_imports[i].load(std::memory_order_relaxed);
    for (int i = 0; i < kEncodeRoutes; ++i)
        s.encodes[i] = g_encodes[i].load(std::memory_order_relaxed);
    for (int i = 0; i < kFallbacks; ++i)
        s.fallbacks[i] = g_fallbacks[i].load(std::memory_order_relaxed);
    s.plan = ExportPlan(g_plan.load(std::memory_order_relaxed));
    std::lock_guard<std::mutex> lock(g_ortMutex);
    s.ortProvider = g_ortProvider;
    return s;
}

void reset()
{
    for (auto &c : g_imports)
        c.store(0, std::memory_order_relaxed);
    for (auto &c : g_encodes)
        c.store(0, std::memory_order_relaxed);
    for (auto &c : g_fallbacks)
        c.store(0, std::memory_order_relaxed);
    g_plan.store(int(ExportPlan::None), std::memory_order_relaxed);
    std::lock_guard<std::mutex> lock(g_ortMutex);
    g_ortProvider.clear();
}

const char *id(ImportRoute route)
{
    switch (route) {
    case ImportRoute::CudaInterop: return "cuda-interop";
    case ImportRoute::VaapiDmaBuf: return "vaapi-dmabuf";
    case ImportRoute::D3d11Interop: return "d3d11-interop";
    case ImportRoute::MediaCodecImage: return "mediacodec-image";
    case ImportRoute::HwDownload: return "hw-download";
    case ImportRoute::SoftwareUpload: return "software-pbo";
    case ImportRoute::Count_: break;
    }
    return "unknown";
}

const char *id(EncodeRoute route)
{
    switch (route) {
    case EncodeRoute::DirectCuda: return "direct_cuda";
    case EncodeRoute::GlPboHostNv12: return "gl_pbo_host_nv12";
    case EncodeRoute::CpuSwscale: return "cpu_swscale";
    case EncodeRoute::Count_: break;
    }
    return "unknown";
}

const char *id(Fallback reason)
{
    switch (reason) {
    case Fallback::CudaInteropRefused: return "cuda-interop-refused";
    case Fallback::ExportCudaCopyFailed: return "export-cuda-copy-failed";
    case Fallback::ExportHostReadFailed: return "export-host-read-failed";
    case Fallback::ExportFramePlaceholder: return "export-frame-placeholder";
    case Fallback::OrtFellBackToCpu: return "onnx-fell-back-to-cpu";
    case Fallback::Count_: break;
    }
    return "unknown";
}

const char *id(ExportPlan plan)
{
    switch (plan) {
    case ExportPlan::None: return "none";
    case ExportPlan::DirectCudaEligible: return "direct-cuda-eligible";
    case ExportPlan::HostPboNv12: return "host-pbo-nv12";
    case ExportPlan::CpuSwscaleForced: return "cpu-swscale-forced-by-env";
    case ExportPlan::CpuSwscaleNoGpuCompositor: return "cpu-swscale-no-gpu-compositor";
    case ExportPlan::CudaNeedsNv12Surface: return "cuda-skipped-surface-not-cuda-nv12";
    case ExportPlan::CudaRenderGpuNotNvidia: return "cuda-skipped-render-gpu-not-nvidia";
    case ExportPlan::Count_: break;
    }
    return "unknown";
}

Match classifyDeviceMatch(const DeviceId &render, const DeviceId &other)
{
    if (!render.available || !other.available)
        return Match::Unavailable;
    if (render.vendorId == 0 || other.vendorId == 0)
        return Match::Unknown;
    if (render.vendorId != other.vendorId)
        return Match::Mismatched;
    // Same vendor. That alone says nothing about which physical card each side opened.
    if (render.key.empty() || other.key.empty())
        return Match::Unknown;
    return render.key == other.key ? Match::Matched : Match::Mismatched;
}

const char *id(Match match)
{
    switch (match) {
    case Match::Matched: return "MATCHED";
    case Match::Mismatched: return "MISMATCHED";
    case Match::Unknown: return "UNKNOWN";
    case Match::Unavailable: return "UNAVAILABLE";
    }
    return "UNKNOWN";
}

} // namespace drift::routes
