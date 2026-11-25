#pragma once

#include <cstdint>
#include <functional>

// Dafin: the frames an NVENC/PBO export has composed but not yet handed to the encoder.
//
// The exporter keeps up to kCapacity frames in flight. Each one is prepared for exactly one of
// two routes: Cuda (the planes stay in GL textures and are copied device-side into the encoder's
// surface) or Pbo (the planes are read back into a pixel buffer and mapped on the CPU). The two
// are not interchangeable per frame: a Cuda frame never wrote its pixel buffer, so mapping it
// would hand the encoder whatever that slot held before, as if it were this frame.
//
// This class owns that rule. When a CUDA copy fails it redoes EVERY pending Cuda frame on the
// Pbo route before any of them is read on the host, turns CUDA off for frames queued afterwards,
// and never passes an unprepared frame off as good (the caller's placeholder hook is used and
// counted instead). It has no GL, CUDA or FFmpeg in it, so it is tested without a GPU
// (tests/tst_nv12exportqueue); the exporter supplies the real operations as hooks.
namespace drift::nv12q {

enum class Mode { Cuda, Pbo };

enum class Outcome {
    Ok,         // the frame was delivered to the encoder
    CopyFailed, // nothing was delivered; the route could not produce this frame
    Fatal       // the export must stop (could not send, mux failed, ...); error is the caller's
};

struct Job
{
    int slot = 0;
    int64_t pts = 0;    // frame index, the encoder's presentation order
    int64_t timeUs = 0; // timeline time the frame is composed at
    int width = 0;      // output size this frame was prepared for
    int height = 0;
    Mode mode = Mode::Pbo;
    bool prepared = false; // the planes of `mode` really exist for this frame
    int attempt = 0;       // 0 for the first compose, +1 each time the frame is composed again
};

struct Hooks
{
    // Compose job.timeUs into job.slot for `mode`. True only when the planes now exist.
    std::function<bool(const Job &job, Mode mode)> prepare;
    // Copy the slot's textures into the encoder surface and send it.
    std::function<Outcome(const Job &job)> deliverCuda;
    // Map the slot's pixel buffer and send it. CopyFailed when it could not be read.
    std::function<Outcome(const Job &job)> deliverHost;
    // Send a stand-in (and warn) for a frame that could not be produced. Ok or Fatal.
    std::function<Outcome(const Job &job)> deliverPlaceholder;
    // A prepared frame is being dropped (cancel): let the GL side finish with its slot.
    std::function<void(const Job &job)> discard;
};

struct Counters
{
    int cudaDelivered = 0;
    int hostDelivered = 0;
    int recomposed = 0;    // frames composed a second time because their route failed
    int placeholders = 0;  // frames that could not be produced at all
    int cudaFallbacks = 0; // CUDA copy failures that switched the export to Pbo
};

class Queue
{
public:
    static constexpr int kCapacity = 3;

    Queue(Hooks hooks, bool cudaEnabled);

    // Whether frames queued from now on are prepared for CUDA.
    bool cudaEnabled() const { return m_cudaEnabled; }
    int pending() const { return m_count; }
    bool full() const { return m_count == kCapacity; }
    const Counters &counters() const { return m_counters; }

    // Prepare a frame for the current route and queue it. Caller makes room first (full()).
    void push(int slot, int64_t pts, int64_t timeUs, int width, int height);

    // Deliver the oldest frame. False means the export must stop.
    bool consumeHead();

    // Drop everything pending without delivering it (cancel).
    void discardAll();

private:
    Job popFront();
    void redoOnPbo(Job &job);
    bool deliverOnHost(Job job);

    Hooks m_hooks;
    bool m_cudaEnabled;
    Job m_jobs[kCapacity];
    int m_count = 0;
    Counters m_counters;
};

} // namespace drift::nv12q
