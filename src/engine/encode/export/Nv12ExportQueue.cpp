#include "engine/encode/export/Nv12ExportQueue.h"

namespace drift::nv12q {

Queue::Queue(Hooks hooks, bool cudaEnabled)
    : m_hooks(std::move(hooks))
    , m_cudaEnabled(cudaEnabled)
{
}

void Queue::push(int slot, int64_t pts, int64_t timeUs, int width, int height)
{
    Job job;
    job.slot = slot;
    job.pts = pts;
    job.timeUs = timeUs;
    job.width = width;
    job.height = height;
    job.mode = m_cudaEnabled ? Mode::Cuda : Mode::Pbo;
    job.prepared = m_hooks.prepare(job, job.mode);
    m_jobs[m_count++] = job;
}

Job Queue::popFront()
{
    const Job job = m_jobs[0];
    for (int i = 0; i < m_count - 1; ++i)
        m_jobs[i] = m_jobs[i + 1];
    --m_count;
    return job;
}

void Queue::redoOnPbo(Job &job)
{
    job.mode = Mode::Pbo;
    ++job.attempt;
    job.prepared = m_hooks.prepare(job, Mode::Pbo);
    ++m_counters.recomposed;
}

bool Queue::deliverOnHost(Job job)
{
    if (job.prepared && job.mode == Mode::Pbo) {
        Outcome r = m_hooks.deliverHost(job);
        if (r == Outcome::Fatal)
            return false;
        if (r == Outcome::Ok) {
            ++m_counters.hostDelivered;
            return true;
        }
        // The pixel buffer could not be read. Compose the frame once more rather than guess.
        redoOnPbo(job);
        if (job.prepared) {
            r = m_hooks.deliverHost(job);
            if (r == Outcome::Fatal)
                return false;
            if (r == Outcome::Ok) {
                ++m_counters.hostDelivered;
                return true;
            }
        }
    }
    ++m_counters.placeholders;
    return m_hooks.deliverPlaceholder(job) == Outcome::Ok;
}

bool Queue::consumeHead()
{
    Job job = popFront();

    if (job.mode == Mode::Cuda && job.prepared) {
        const Outcome r = m_hooks.deliverCuda(job);
        if (r == Outcome::Ok) {
            ++m_counters.cudaDelivered;
            return true;
        }
        if (r == Outcome::Fatal)
            return false;
        // The copy failed, so nothing was sent for this frame. Every frame still waiting was
        // prepared for CUDA too: its pixel buffer was never written, and mapping it would send
        // whatever the slot held before. Redo them all on the Pbo route now, before any of them
        // is read on the host, and queue later frames for Pbo from the start.
        m_cudaEnabled = false;
        ++m_counters.cudaFallbacks;
        for (int i = 0; i < m_count; ++i) {
            if (m_jobs[i].mode == Mode::Cuda)
                redoOnPbo(m_jobs[i]);
        }
        redoOnPbo(job);
    }
    return deliverOnHost(job);
}

void Queue::discardAll()
{
    while (m_count > 0) {
        const Job job = popFront();
        if (job.prepared && m_hooks.discard)
            m_hooks.discard(job);
    }
}

} // namespace drift::nv12q
