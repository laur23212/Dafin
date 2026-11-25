#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>

// Dafin: the CUDA <-> OpenGL interop calls, behind a table of function pointers so a failure of
// any single call can be injected in a test (tests/tst_cudaglinterop) without a GPU or a driver.
//
// What this layer guarantees, and GlRuntime relies on:
//   * every call's return value is read; the first failure is reported with the call's name;
//   * a registration is all-or-nothing: if the second plane cannot be registered the first is
//     unregistered again, so nothing is left behind;
//   * once resources are mapped they are unmapped before anything else happens, and the stream
//     is drained first when a copy was only partly queued, so no queued copy still points at an
//     array that is about to be unregistered;
//   * a resource whose unmap failed (even after one retry) is reported as `leftMapped`; the
//     caller must NOT unregister it or delete the texture under it, and must stop using interop;
//   * the context pushed for a call is popped again on every path, and a failed pop is counted.
//
// Pure C++: no Qt, no GL, no FFmpeg.
namespace drift::cudagl {

using CUresult = int;
using CUdeviceptr = void *;
using CUarray = void *;
using CUcontext = void *;
using CUstream = void *;
using CUgraphicsResource = void *;

enum : int {
    kSuccess = 0,
    kMemoryDevice = 2,
    kMemoryArray = 3,
    kRegisterReadOnly = 0x01,
    kRegisterWriteDiscard = 0x02,
};

// CUDA_MEMCPY2D as the *_v2 entry points expect it. The unversioned cuMemcpy2D symbol that
// libcuda still exports is the v1 ABI, whose equivalent fields are unsigned int rather than
// size_t — a completely different layout on 64-bit. cuda.h hides this behind
// `#define cuMemcpy2D cuMemcpy2D_v2`, so source that includes it gets v2 automatically and
// only a dlsym of the bare name lands on v1. Load the _v2 names to match this struct.
struct Memcpy2D
{
    size_t srcXInBytes = 0;
    size_t srcY = 0;
    int srcMemoryType = 0;
    int srcPad = 0;
    const void *srcHost = nullptr;
    CUdeviceptr srcDevice = nullptr;
    CUarray srcArray = nullptr;
    size_t srcPitch = 0;
    size_t dstXInBytes = 0;
    size_t dstY = 0;
    int dstMemoryType = 0;
    int dstPad = 0;
    void *dstHost = nullptr;
    CUdeviceptr dstDevice = nullptr;
    CUarray dstArray = nullptr;
    size_t dstPitch = 0;
    size_t WidthInBytes = 0;
    size_t Height = 0;
};

struct DriverApi
{
    void *lib = nullptr;
    CUresult (*cuInit)(unsigned int) = nullptr;
    CUresult (*cuCtxPushCurrent)(CUcontext) = nullptr;
    CUresult (*cuCtxPopCurrent)(CUcontext *) = nullptr;
    CUresult (*cuGraphicsGLRegisterImage)(CUgraphicsResource *, unsigned int, unsigned int,
                                          unsigned int) = nullptr;
    CUresult (*cuGraphicsUnregisterResource)(CUgraphicsResource) = nullptr;
    CUresult (*cuGraphicsMapResources)(unsigned int, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*cuGraphicsUnmapResources)(unsigned int, CUgraphicsResource *, CUstream) = nullptr;
    CUresult (*cuGraphicsSubResourceGetMappedArray)(CUarray *, CUgraphicsResource, unsigned int,
                                                    unsigned int) = nullptr;
    CUresult (*cuMemcpy2DAsync)(const Memcpy2D *, CUstream) = nullptr;
    CUresult (*cuStreamSynchronize)(CUstream) = nullptr;
    bool ok = false;
};

// The calls this layer makes, for error reporting and the counters.
enum class Call {
    ContextPush,
    ContextPop,
    Register,
    Unregister,
    Map,
    GetMappedArray,
    Copy,
    Unmap,
    StreamSync,
    Count_
};
constexpr int kCalls = int(Call::Count_);
const char *callName(Call call);

// Error counters. Relaxed atomics, bumped only on a failure, so the working path pays nothing.
struct CounterSnapshot
{
    uint64_t failures[kCalls] = {};
    uint64_t leftMapped = 0;        // unmap refused twice: resources abandoned, interop given up
    uint64_t abandonedRegistrations = 0; // registrations not unregistered because they are mapped
};
CounterSnapshot counters();
void resetCounters();
void noteAbandonedRegistration();

// The first thing that went wrong.
struct Failure
{
    bool failed = false;
    Call call = Call::Map;
    CUresult rc = kSuccess;
};

// Push a context for the life of the object; pop it again on every path. `ok()` false means the
// push was refused and nothing was pushed (so nothing is popped).
class ContextScope
{
public:
    ContextScope(const DriverApi &api, CUcontext ctx);
    ~ContextScope();
    ContextScope(const ContextScope &) = delete;
    ContextScope &operator=(const ContextScope &) = delete;

    bool ok() const { return m_pushed; }
    CUresult pushResult() const { return m_pushRc; }
    // Pop now rather than at the end of scope. False when the pop was refused.
    bool release();

private:
    const DriverApi &m_api;
    bool m_pushed = false;
    CUresult m_pushRc = kSuccess;
};

// A registered pair of GL textures (the Y and the interleaved UV plane).
struct RegisteredPair
{
    CUgraphicsResource y = nullptr;
    CUgraphicsResource uv = nullptr;
    bool valid() const { return y && uv; }
};

// Register both textures, or neither. Needs the owning CUDA context pushed and the GL context
// current. On failure `out` is untouched and nothing stays registered; a registration made for
// the first plane is unregistered again before returning.
Failure registerPair(const DriverApi &api, unsigned yTexture, unsigned uvTexture,
                     unsigned glTarget, unsigned flags, RegisteredPair *out);

// Unregister both. The resources must not be mapped. Both are attempted even if the first is
// refused. Returns the first failure, if any; `pair` is cleared either way (a refused
// unregister cannot be retried meaningfully).
Failure unregisterPair(const DriverApi &api, RegisteredPair *pair);

struct Plane
{
    CUdeviceptr device = nullptr; // the encoder's / decoder's surface plane
    size_t pitch = 0;
};

enum class Direction {
    DeviceToTexture, // decoder surface -> GL textures (preview import)
    TextureToDevice  // GL textures -> encoder surface (CUDA export)
};

struct CopyResult
{
    bool ok = false;
    Failure failure;
    // Unmap was refused twice: the resources may still be mapped. See the header comment.
    bool leftMapped = false;
};

// Map both textures, copy both planes on `stream`, unmap, and wait for the stream. The Y plane
// is width x height bytes, the UV plane width x height/2 bytes (RG8 at half size).
CopyResult copyNv12Planes(const DriverApi &api, CUstream stream, const RegisteredPair &pair,
                          Direction direction, const Plane &y, const Plane &uv, int width,
                          int height);

} // namespace drift::cudagl
