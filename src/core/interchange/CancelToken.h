#pragma once

#include <atomic>
#include <memory>

namespace drift::interchange {

// Dafin (C20): one flag a background import polls between steps and inside long loops. The GUI
// sets it from its own thread; nothing else about the import is shared between the threads.
class CancelToken
{
public:
    void cancel() { m_flag.store(true, std::memory_order_relaxed); }
    bool cancelled() const { return m_flag.load(std::memory_order_relaxed); }

private:
    std::atomic<bool> m_flag{false};
};

using CancelPtr = std::shared_ptr<CancelToken>;

inline bool isCancelled(const CancelPtr &token)
{
    return token && token->cancelled();
}

} // namespace drift::interchange
