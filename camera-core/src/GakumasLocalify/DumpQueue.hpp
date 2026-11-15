#pragma once

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <thread>
#include <unordered_map>

namespace GakumasLocal::Local {
// One writer owns each batch. Events arriving during the delay or the file
// write go into the next batch, and the writer drains it before going idle.
class DumpQueue {
public:
    using Dictionary = std::unordered_map<std::string, std::string>;
    using Writer = std::function<void(const std::filesystem::path&, const Dictionary&)>;

    explicit DumpQueue(Writer writer, std::chrono::milliseconds delay = std::chrono::seconds(5))
        : writer_(std::move(writer)), delay_(delay) {}

    void Append(const std::filesystem::path& file, const std::string& key, const std::string& value) {
        const auto state = state_;
        std::lock_guard lock(state->mutex);
        state->pending[file][key] = value;
        if (state->running) return;
        state->running = true;
        std::thread([state, writer = writer_, delay = delay_] {
            std::unique_lock lock(state->mutex);
            state->changed.wait_for(lock, delay, [&] { return state->flushNow; });
            for (;;) {
                if (state->pending.empty()) {
                    state->running = false;
                    state->flushNow = false;
                    state->changed.notify_all();
                    return;
                }
                std::map<std::filesystem::path, Dictionary> batch;
                batch.swap(state->pending);
                lock.unlock();
                for (const auto& [file, values] : batch) {
                    // Production writers log I/O failures. Never let an error
                    // escape a thread or leave the queue permanently running.
                    try { writer(file, values); } catch (...) {}
                }
                lock.lock();
            }
        }).detach(); // State and batches outlive their caller; no live maps are borrowed.
    }

    bool FlushFor(std::chrono::milliseconds timeout) {
        std::unique_lock lock(state_->mutex);
        if (!state_->running) return true;
        state_->flushNow = true;
        state_->changed.notify_all();
        return state_->changed.wait_for(lock, timeout, [&] { return !state_->running; });
    }

private:
    struct State {
        std::mutex mutex;
        std::condition_variable changed;
        std::map<std::filesystem::path, Dictionary> pending;
        bool running{}, flushNow{};
    };
    std::shared_ptr<State> state_ = std::make_shared<State>();
    Writer writer_;
    std::chrono::milliseconds delay_;
};
}
