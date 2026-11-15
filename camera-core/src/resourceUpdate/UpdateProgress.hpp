#pragma once
#include <algorithm>
#include <atomic>
#include <cstdint>

namespace GkmsResourceUpdate {
enum class UpdatePhase { Idle, Checking, Downloading, Preparing, AwaitingChoice };
class UpdateProgressState {
    std::atomic<UpdatePhase> phase_{UpdatePhase::Idle};
    std::atomic<float> fraction_{-1.0f};
public:
    void Begin(UpdatePhase phase) { fraction_.store(-1.0f); phase_.store(phase); }
    void Downloaded(uint64_t bytes, uint64_t total) {
        fraction_.store(total ? (std::min)(1.0f, static_cast<float>(bytes) / static_cast<float>(total)) : -1.0f);
    }
    UpdatePhase Phase() const { return phase_.load(); }
    float Fraction() const { return fraction_.load(); }
};
inline UpdateProgressState progress;
}
