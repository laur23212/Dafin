#pragma once

#include <atomic>
#include <cstdint>
#include <string>

// Dafin: which road each frame took, and why a faster road was refused. Feeds the Debug info
// dialog (and so its Copy button and the "Send diagnostics" report).
//
// Cheap on purpose: a counter is one relaxed atomic add at a place that already made the
// decision, nothing here waits on the GPU, and nothing records a file name, a path, or any
// picture, sound or transcript content. The only free text is the ONNX provider's name, and it is
// refused when it looks like a path. Pure C++ so it can be tested without Qt or a GPU
// (tests/tst_routestats).
namespace drift::routes {

// How a preview video frame reached the compositor.
enum class ImportRoute {
    CudaInterop,    // NVDEC surface copied device-side into GL textures
    VaapiDmaBuf,    // VAAPI surface bound as a dma-buf
    D3d11Interop,   // D3D11VA texture shared with GL
    MediaCodecImage,
    HwDownload,     // decoded on a GPU, then read back to system memory and uploaded again
    SoftwareUpload, // decoded (or made) in system memory and uploaded through a PBO
    Count_
};

// How an exported frame reached the encoder.
enum class EncodeRoute {
    DirectCuda,    // GL textures -> CUDA surface, no CPU copy (NVENC only)
    GlPboHostNv12, // GL -> PBO -> system-memory NV12 (-> encoder upload)
    CpuSwscale,    // composed on the CPU and converted by swscale
    Count_
};

// Why a faster road was not taken, or was given up part-way.
enum class Fallback {
    CudaInteropRefused,   // a CUDA frame could not be imported and was downloaded instead
    ExportCudaCopyFailed, // the CUDA copy into the encoder failed; the export switched to PBO
    ExportHostReadFailed, // a pixel buffer could not be read back (fence timeout, map failure)
    ExportFramePlaceholder, // a frame could not be produced at all and a stand-in was sent
    OrtFellBackToCpu,     // an AI model's GPU provider could not be set up
    Count_
};

// The export's route decision, taken once per export.
enum class ExportPlan {
    None,
    DirectCudaEligible,
    HostPboNv12,
    CpuSwscaleForced,        // DRIFT_EXPORT_SWSCALE
    CpuSwscaleNoGpuCompositor,
    CudaNeedsNv12Surface,    // NVENC but the surface format is not CUDA/NV12
    CudaRenderGpuNotNvidia,  // NVENC, but GL draws on another GPU
    Count_
};

constexpr int kImportRoutes = int(ImportRoute::Count_);
constexpr int kEncodeRoutes = int(EncodeRoute::Count_);
constexpr int kFallbacks = int(Fallback::Count_);

struct Snapshot
{
    uint64_t imports[kImportRoutes] = {};
    uint64_t encodes[kEncodeRoutes] = {};
    uint64_t fallbacks[kFallbacks] = {};
    ExportPlan plan = ExportPlan::None;
    std::string ortProvider; // empty until a model session was built
};

void countImport(ImportRoute route);
void countEncode(EncodeRoute route);
void noteFallback(Fallback reason);
void notePlan(ExportPlan plan);
// `provider` is the execution provider's short name ("cuda", "dml"...). Anything that looks
// like a path, or is long, is stored as "other": this is the one place text comes in.
void noteOrtProvider(const std::string &provider);

Snapshot snapshot();
void reset();

// Stable untranslated ids, for the report and for tests.
const char *id(ImportRoute route);
const char *id(EncodeRoute route);
const char *id(Fallback reason);
const char *id(ExportPlan plan);

// --- device identity -------------------------------------------------------------------------

// What is known about one GPU. `available` false means that stage does not exist on this machine
// (no NVENC, no ONNX GPU provider). `vendorId` 0 means it could not be named. `key` is something
// that tells two cards of one vendor apart (PCI vendor:device plus bus position, a LUID, a
// UUID), empty when the stage cannot say which card it opened.
struct DeviceId
{
    bool available = false;
    uint16_t vendorId = 0;
    std::string key;
};

enum class Match { Matched, Mismatched, Unknown, Unavailable };

// Whether `other` runs on the GPU that draws (`render`). The same vendor is NOT enough: two
// NVIDIA cards, or an NVIDIA card that is not the one drawing, would pass a vendor test. Same
// vendor without keys on both sides is Unknown, and a caller must treat Unknown as "do not
// assume": keep a cross-device feature off, not the whole app.
Match classifyDeviceMatch(const DeviceId &render, const DeviceId &other);
const char *id(Match match);

} // namespace drift::routes
