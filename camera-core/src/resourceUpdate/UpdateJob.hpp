#pragma once
#include <atomic>
#include <memory>
#include <thread>

namespace GkmsResourceUpdate {
// Normal shutdown cancels and joins through BeginShutdown/WaitForJobs. Process
// teardown may bypass that path: retire the handle without blocking in CRT or
// DllMain, rather than letting a joinable thread's destructor terminate it.
struct UpdateJob {
    std::thread thread;
    std::shared_ptr<std::atomic_bool> cancel;
    UpdateJob(std::thread worker, const std::shared_ptr<std::atomic_bool>& cancellation)
        : thread(std::move(worker)), cancel(cancellation) {}
    UpdateJob(UpdateJob&&) = default;
    UpdateJob& operator=(UpdateJob&& other) noexcept {
        Retire();
        thread = std::move(other.thread);
        cancel = std::move(other.cancel);
        return *this;
    }
    ~UpdateJob() { Retire(); }
private:
    void Retire() noexcept {
        if (thread.joinable()) {
            if (cancel) cancel->store(true, std::memory_order_release);
            thread.detach();
        }
    }
};
}
