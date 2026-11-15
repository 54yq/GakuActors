#pragma once

#include <exception>
#include <mutex>
#include <memory>
#include <utility>

namespace GkmsHosted {
// A reload is a parse plus a publication, and only a parse that ran to
// completion may replace anything the game can see.
//
// The worker stages a fully parsed snapshot here. A parse that throws never
// reaches the staging slot, so the previously staged and published snapshots
// stay untouched; the Unity main thread later takes the staged value. Keeping
// the "staged" and "published" states in one object is what makes the failure
// path testable offline with plain snapshot types.
template <typename LocalSnapshot, typename MasterSnapshot>
class ReloadPipeline {
public:
    struct Snapshot {
        LocalSnapshot local;
        MasterSnapshot master;
        std::shared_ptr<const void> resources;
    };

    // Worker thread. `parse` returns Snapshot or throws; `onError` receives the
    // diagnostic. Returns false without changing the staged snapshot on failure.
    template <typename Parse, typename OnError>
    bool Reload(Parse&& parse, OnError&& onError) {
        Snapshot parsed;
        try {
            parsed = parse();
        } catch (const std::exception& error) {
            onError(error.what());
            return false;
        } catch (...) {
            onError("non-standard exception");
            return false;
        }
        std::lock_guard<std::mutex> lock(m_mutex);
        m_pending = std::move(parsed);
        m_hasPending = true;
        return true;
    }

    // Main thread. Returns false when no complete snapshot is waiting.
    bool TakePending(Snapshot& out) {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_hasPending) return false;
        out = std::move(m_pending);
        m_hasPending = false;
        return true;
    }

    bool HasPending() const {
        std::lock_guard<std::mutex> lock(m_mutex);
        return m_hasPending;
    }

private:
    mutable std::mutex m_mutex;
    Snapshot m_pending{};
    bool m_hasPending = false;
};
} // namespace GkmsHosted
