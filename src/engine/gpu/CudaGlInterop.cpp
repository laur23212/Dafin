#include "engine/gpu/CudaGlInterop.h"

namespace drift::cudagl {

namespace {

std::atomic<uint64_t> g_failures[kCalls];
std::atomic<uint64_t> g_leftMapped{0};
std::atomic<uint64_t> g_abandoned{0};

Failure note(Call call, CUresult rc)
{
    g_failures[int(call)].fetch_add(1, std::memory_order_relaxed);
    Failure f;
    f.failed = true;
    f.call = call;
    f.rc = rc;
    return f;
}

// A plane copy: one cuMemcpy2DAsync on the stream. Always the frame's own stream, never the null
// stream: FFmpeg creates its decoder stream non-blocking, which does not synchronise against the
// legacy null stream, so a copy issued there would be unordered with the map and unmap around it.
CUresult queueCopy(const DriverApi &api, CUstream stream, Direction direction, CUarray array,
                   const Plane &plane, size_t widthBytes, size_t height)
{
    Memcpy2D op{};
    if (direction == Direction::DeviceToTexture) {
        op.srcMemoryType = kMemoryDevice;
        op.srcDevice = plane.device;
        op.srcPitch = plane.pitch;
        op.dstMemoryType = kMemoryArray;
        op.dstArray = array;
    } else {
        op.srcMemoryType = kMemoryArray;
        op.srcArray = array;
        op.dstMemoryType = kMemoryDevice;
        op.dstDevice = plane.device;
        op.dstPitch = plane.pitch;
    }
    op.WidthInBytes = widthBytes;
    op.Height = height;
    return api.cuMemcpy2DAsync(&op, stream);
}

} // namespace

const char *callName(Call call)
{
    switch (call) {
    case Call::ContextPush:
        return "cuCtxPushCurrent";
    case Call::ContextPop:
        return "cuCtxPopCurrent";
    case Call::Register:
        return "cuGraphicsGLRegisterImage";
    case Call::Unregister:
        return "cuGraphicsUnregisterResource";
    case Call::Map:
        return "cuGraphicsMapResources";
    case Call::GetMappedArray:
        return "cuGraphicsSubResourceGetMappedArray";
    case Call::Copy:
        return "cuMemcpy2DAsync";
    case Call::Unmap:
        return "cuGraphicsUnmapResources";
    case Call::StreamSync:
        return "cuStreamSynchronize";
    case Call::Count_:
        break;
    }
    return "?";
}

CounterSnapshot counters()
{
    CounterSnapshot s;
    for (int i = 0; i < kCalls; ++i)
        s.failures[i] = g_failures[i].load(std::memory_order_relaxed);
    s.leftMapped = g_leftMapped.load(std::memory_order_relaxed);
    s.abandonedRegistrations = g_abandoned.load(std::memory_order_relaxed);
    return s;
}

void resetCounters()
{
    for (auto &f : g_failures)
        f.store(0, std::memory_order_relaxed);
    g_leftMapped.store(0, std::memory_order_relaxed);
    g_abandoned.store(0, std::memory_order_relaxed);
}

void noteAbandonedRegistration()
{
    g_abandoned.fetch_add(1, std::memory_order_relaxed);
}

ContextScope::ContextScope(const DriverApi &api, CUcontext ctx) : m_api(api)
{
    if (!api.ok || !ctx || !api.cuCtxPushCurrent)
        return;
    m_pushRc = api.cuCtxPushCurrent(ctx);
    if (m_pushRc == kSuccess)
        m_pushed = true;
    else
        note(Call::ContextPush, m_pushRc);
}

bool ContextScope::release()
{
    if (!m_pushed)
        return true;
    m_pushed = false;
    CUcontext popped = nullptr;
    const CUresult rc = m_api.cuCtxPopCurrent(&popped);
    if (rc != kSuccess) {
        note(Call::ContextPop, rc);
        return false;
    }
    return true;
}

ContextScope::~ContextScope()
{
    release();
}

Failure registerPair(const DriverApi &api, unsigned yTexture, unsigned uvTexture,
                     unsigned glTarget, unsigned flags, RegisteredPair *out)
{
    CUgraphicsResource yRes = nullptr;
    CUgraphicsResource uvRes = nullptr;
    CUresult rc = api.cuGraphicsGLRegisterImage(&yRes, yTexture, glTarget, flags);
    if (rc != kSuccess)
        return note(Call::Register, rc);
    if (!yRes)
        return note(Call::Register, -1);

    rc = api.cuGraphicsGLRegisterImage(&uvRes, uvTexture, glTarget, flags);
    if (rc != kSuccess || !uvRes) {
        const Failure first = note(Call::Register, rc != kSuccess ? rc : -1);
        // The second plane failed: take the first one back so nothing stays registered.
        if (uvRes)
            api.cuGraphicsUnregisterResource(uvRes);
        const CUresult undo = api.cuGraphicsUnregisterResource(yRes);
        if (undo != kSuccess)
            note(Call::Unregister, undo);
        return first;
    }
    out->y = yRes;
    out->uv = uvRes;
    return {};
}

Failure unregisterPair(const DriverApi &api, RegisteredPair *pair)
{
    Failure first;
    const auto one = [&](CUgraphicsResource res) {
        if (!res)
            return;
        const CUresult rc = api.cuGraphicsUnregisterResource(res);
        if (rc != kSuccess) {
            const Failure f = note(Call::Unregister, rc);
            if (!first.failed)
                first = f;
        }
    };
    one(pair->y);
    one(pair->uv);
    pair->y = nullptr;
    pair->uv = nullptr;
    return first;
}

CopyResult copyNv12Planes(const DriverApi &api, CUstream stream, const RegisteredPair &pair,
                          Direction direction, const Plane &y, const Plane &uv, int width,
                          int height)
{
    CopyResult result;
    CUgraphicsResource resources[2] = {pair.y, pair.uv};

    CUresult rc = api.cuGraphicsMapResources(2, resources, stream);
    if (rc != kSuccess) {
        // Nothing was mapped by a call that failed; there is nothing to undo.
        result.failure = note(Call::Map, rc);
        return result;
    }

    bool ok = true;
    CUarray yArray = nullptr;
    CUarray uvArray = nullptr;
    rc = api.cuGraphicsSubResourceGetMappedArray(&yArray, pair.y, 0, 0);
    if (rc == kSuccess)
        rc = api.cuGraphicsSubResourceGetMappedArray(&uvArray, pair.uv, 0, 0);
    if (rc != kSuccess || !yArray || !uvArray) {
        result.failure = note(Call::GetMappedArray, rc != kSuccess ? rc : -1);
        ok = false;
    }

    bool copiesQueued = false;
    if (ok) {
        copiesQueued = true; // from here a copy may be on the stream, whatever happens next
        rc = queueCopy(api, stream, direction, yArray, y, size_t(width), size_t(height));
        if (rc == kSuccess)
            rc = queueCopy(api, stream, direction, uvArray, uv, size_t(width), size_t(height / 2));
        if (rc != kSuccess) {
            // The second plane (or the first) was refused. Whatever was queued before it is
            // still in flight against arrays that are about to be unmapped and unregistered.
            result.failure = note(Call::Copy, rc);
            ok = false;
        }
    }

    // A partly queued copy has to finish before the arrays go away; a fully queued one is waited
    // for below, after the unmap, as before. The wait result is not the error to report here —
    // the copy failure already is — but a refused wait is still counted.
    bool drained = false;
    if (!ok && copiesQueued) {
        drained = true;
        const CUresult drain = api.cuStreamSynchronize(stream);
        if (drain != kSuccess)
            note(Call::StreamSync, drain);
    }

    rc = api.cuGraphicsUnmapResources(2, resources, stream);
    if (rc != kSuccess) {
        const Failure unmapFailure = note(Call::Unmap, rc);
        if (ok)
            result.failure = unmapFailure;
        ok = false;
        // One retry: a transient refusal must not strand the resources mapped.
        const CUresult retry = api.cuGraphicsUnmapResources(2, resources, stream);
        if (retry != kSuccess) {
            note(Call::Unmap, retry);
            g_leftMapped.fetch_add(1, std::memory_order_relaxed);
            result.leftMapped = true;
        } else if (copiesQueued && !drained) {
            // The unmap needed a second try after the copies were queued; the caller is about to
            // treat this frame as failed and may unregister, so let them finish first.
            drained = true;
            const CUresult drain = api.cuStreamSynchronize(stream);
            if (drain != kSuccess)
                note(Call::StreamSync, drain);
        }
    }

    // Unmap only orders the copies on the stream; it does not wait for them. This does, and it
    // is the sync point that makes the textures (or the encoder surface) safe to use.
    if (ok) {
        rc = api.cuStreamSynchronize(stream);
        if (rc != kSuccess) {
            result.failure = note(Call::StreamSync, rc);
            ok = false;
        }
    }

    result.ok = ok;
    return result;
}

} // namespace drift::cudagl
