#pragma once

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace GkmsHosted {

// The startup deadline is a diagnostic threshold, never permission to discard
// a parse that is still running. Keep the original runtime-ready caller (and
// its host lifecycle gate / IL2CPP attachment) until the parser completes.
class InitialTranslationWait {
public:
    enum class Result { Ready, Failed, Cancelled };

    void Reset() {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_state = State::Waiting;
    }

    bool BeginParse() {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (m_state != State::Waiting) return false;
        m_state = State::Parsing;
        return true;
    }

    void FinishParse(bool initial, bool parsed) {
        if (!initial) return;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_state != State::Parsing) return;
            m_state = parsed ? State::Ready : State::Failed;
        }
        m_changed.notify_all();
    }

    template <typename IsStopping, typename OnSlow>
    Result Wait(std::chrono::milliseconds slowAfter, std::chrono::milliseconds poll,
                IsStopping&& isStopping, OnSlow&& onSlow) {
        const auto deadline = std::chrono::steady_clock::now() + slowAfter;
        bool warned = false;
        for (;;) {
            // The host sets cancellation before trying to enter its callback
            // gate. Never depend on this plugin's shutdown callback running.
            if (isStopping()) return Result::Cancelled;
            std::unique_lock<std::mutex> lock(m_mutex);
            if (m_state == State::Ready) return Result::Ready;
            if (m_state == State::Failed) return Result::Failed;
            const auto now = std::chrono::steady_clock::now();
            if (!warned && now >= deadline) {
                warned = true;
                lock.unlock();
                onSlow();
                continue;
            }
            const auto wake = warned ? now + poll : std::min(deadline, now + poll);
            m_changed.wait_until(lock, wake);
        }
    }

private:
    enum class State { Waiting, Parsing, Ready, Failed };
    std::mutex m_mutex;
    std::condition_variable m_changed;
    State m_state{State::Waiting};
};

} // namespace GkmsHosted
