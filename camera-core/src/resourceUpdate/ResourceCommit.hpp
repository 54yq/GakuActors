#pragma once

// The two pieces that keep an interrupted resource update away from the
// installed resources:
//
//   * CommitGate serializes "move staged files into the live tree" with
//     shutdown. Closing the gate first, then waiting for the same mutex, means
//     a commit either finished before shutdown returns or will be refused.
//   * CommitStagedTree only performs per-file renames; extraction and copying
//     happen outside it, in a staging directory. It never copies over a live
//     file, so the gate cannot be held for the duration of a large write, and
//     a target that cannot be replaced refuses the commit instead.
//
// They live in a header because the offline fixture exercises this exact code
// without the network/GUI translation unit around it.

#include <atomic>
#include <filesystem>
#include <mutex>
#include <string>
#include <system_error>

namespace GkmsResourceUpdate {
class CommitGate {
public:
    // Shutdown side. Closes the door, then waits for a commit that is already
    // inside the critical section. After Close() returns no Acquire() can yield
    // a locked guard, so a task detached after the join deadline cannot write
    // the live tree.
    void Close() {
        m_open.store(false, std::memory_order_release);
        std::lock_guard<std::mutex> waitForCommit(m_mutex);
    }

    // Commit side. The returned guard is locked when committing is allowed; an
    // unlocked guard means "closed, or the caller was cancelled", and the
    // caller must leave the live tree untouched.
    std::unique_lock<std::mutex> Acquire(const std::atomic_bool* cancel) {
        std::unique_lock<std::mutex> guard(m_mutex, std::defer_lock);
        if (cancel && cancel->load(std::memory_order_acquire)) return guard;
        guard.lock();
        if (!m_open.load(std::memory_order_acquire) ||
            (cancel && cancel->load(std::memory_order_acquire))) {
            guard.unlock();
        }
        return guard;
    }

    bool IsOpen() const { return m_open.load(std::memory_order_acquire); }

private:
    std::mutex m_mutex;
    std::atomic_bool m_open{true};
};

// Moves every regular file from `staging` to the same relative path under
// `live`. A rename is atomic per file, so the game never reads a half-written
// translation. A target that cannot be replaced (the game holds it open without
// delete sharing) fails the commit: copying over it in place would make a
// truncated file visible to that reader, which is the exact failure the staging
// tree exists to prevent. Files already moved stay moved - each one is complete,
// and a later successful update overwrites them again. Returns false and fills
// `error` on the first failure.
//
// Enumeration never skips unreadable entries: `skip_permission_denied` would make
// a subtree that exists but cannot be listed look like an empty one, so the
// caller would report a successful commit that moved nothing.
inline bool CommitStagedTree(const std::filesystem::path& staging,
    const std::filesystem::path& live, std::string& error) {
    std::error_code walkError;
    std::filesystem::recursive_directory_iterator entry(staging, walkError);
    if (walkError) {
        error = "cannot enumerate staging: " + walkError.message();
        return false;
    }
    const std::filesystem::recursive_directory_iterator end;
    while (!walkError && entry != end) {
        std::error_code entryError;
        if (entry->is_regular_file(entryError)) {
            std::error_code relativeError;
            const auto relative = std::filesystem::relative(entry->path(), staging, relativeError);
            if (relativeError || relative.empty()) {
                error = "cannot map " + entry->path().string() + " into the live tree: " +
                    relativeError.message();
                return false;
            }
            const auto target = live / relative;
            std::error_code ec;
            std::filesystem::create_directories(target.parent_path(), ec);
            if (ec) {
                error = "cannot create " + target.parent_path().string() + ": " + ec.message();
                return false;
            }
            std::filesystem::rename(entry->path(), target, ec);
            if (ec) {
                // Never fall back to an in-place overwrite: a reader that kept
                // the target open would observe it being truncated. Leave the
                // staged file in place and let the caller report the failure.
                error = "cannot replace " + target.string() + " (still open?): " + ec.message();
                return false;
            }
        } else if (entryError) {
            error = "cannot inspect " + entry->path().string() + ": " + entryError.message();
            return false;
        }
        entry.increment(walkError);
    }
    if (walkError) {
        error = "enumeration stopped: " + walkError.message();
        return false;
    }
    return true;
}
} // namespace GkmsResourceUpdate
