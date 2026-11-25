#pragma once

#include <atomic>
#include <cstdint>

// Dafin: when a GL fence guards a reusable slot (an export pixel buffer, a video upload buffer, a
// present-ring texture), what to do with the answer glClientWaitSync gave.
//
//   Signaled  the GPU is done with the slot: drop the fence, the slot may be reused.
//   TimedOut  the GPU may still be using the slot: KEEP the fence, the slot is busy, do not
//             reuse it. The caller asks again later; the same fence is waited for again.
//   Failed    the fence object is unusable (lost context, driver error) so it can no longer say
//             anything. Waiting for it again would never help, so drain the whole queue with
//             glFinish, which is a guaranteed point after which the slot is idle, and only then
//             drop the fence and reuse the slot.
//
// Before, a timeout deleted the fence and the next frame went into the slot as if it were free.
// Header-only and free of GL so it is tested without a context (tests/tst_gpufencegate); the
// runtime supplies the real wait, delete and finish as callables.
namespace drift::fence {

enum class Wait { Signaled, TimedOut, Failed };
enum class Verdict { Reusable, Busy };

struct CounterSnapshot
{
    uint64_t timeouts = 0;    // waits that ran out; the slot stayed busy
    uint64_t waitFailures = 0; // waits the driver refused; resolved with a full finish
};

inline std::atomic<uint64_t> &timeoutCounter()
{
    static std::atomic<uint64_t> n{0};
    return n;
}
inline std::atomic<uint64_t> &failureCounter()
{
    static std::atomic<uint64_t> n{0};
    return n;
}
inline CounterSnapshot counters()
{
    return {timeoutCounter().load(std::memory_order_relaxed),
            failureCounter().load(std::memory_order_relaxed)};
}
inline void resetCounters()
{
    timeoutCounter().store(0, std::memory_order_relaxed);
    failureCounter().store(0, std::memory_order_relaxed);
}

// `fence` is the slot's handle (a GLsync); null means the slot has no pending work.
// wait()   -> Wait   : waits (bounded) for `fence`
// drop(h)            : deletes the sync object
// finish()           : glFinish
template <class Handle, class WaitFn, class DropFn, class FinishFn>
Verdict settle(Handle &fence, WaitFn &&wait, DropFn &&drop, FinishFn &&finish)
{
    if (!fence)
        return Verdict::Reusable;
    switch (wait(fence)) {
    case Wait::Signaled:
        break;
    case Wait::TimedOut:
        timeoutCounter().fetch_add(1, std::memory_order_relaxed);
        return Verdict::Busy;
    case Wait::Failed:
        failureCounter().fetch_add(1, std::memory_order_relaxed);
        finish();
        break;
    }
    drop(fence);
    fence = Handle{};
    return Verdict::Reusable;
}

} // namespace drift::fence
