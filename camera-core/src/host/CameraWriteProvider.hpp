#pragma once
#include "GakuLocalifyCameraWrite.h"
#include <array>
#include <mutex>

namespace GkmsHosted {

// Only the four concrete Localify Transform detours may publish a target.
// The subscriber receives the unchanged raw icall self after the game write.
class CameraWriteProvider {
    std::mutex mutex_;
    std::array<void*, GAKULOCALIFY_CAMERA_WRITE_COUNT> targets_{};
    GakuLocalifyCameraWriteFn callback_{nullptr};
    void* userData_{nullptr};
    bool closed_{false};
public:
    void SetInstalledTarget(uint32_t kind, void* target) {
        if (kind >= targets_.size() || !target) return;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!closed_) targets_[kind] = target;
    }
    void* GetTarget(uint32_t kind) {
        if (kind >= targets_.size()) return nullptr;
        std::lock_guard<std::mutex> lock(mutex_);
        return closed_ ? nullptr : targets_[kind];
    }
    GakuStatus Subscribe(GakuLocalifyCameraWriteFn callback, void* userData) {
        if (!callback) return GAKU_E_INVALID_ARGUMENT;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return GAKU_E_SHUTTING_DOWN;
        if (callback_ && (callback_ != callback || userData_ != userData)) return GAKU_E_CONFLICT;
        callback_ = callback;
        userData_ = userData;
        return GAKU_OK;
    }
    GakuStatus Unsubscribe(GakuLocalifyCameraWriteFn callback, void* userData) {
        if (!callback) return GAKU_E_INVALID_ARGUMENT;
        std::lock_guard<std::mutex> lock(mutex_);
        if (!callback_) return GAKU_OK;
        if (callback_ != callback || userData_ != userData) return GAKU_E_CONFLICT;
        callback_ = nullptr;
        userData_ = nullptr;
        return GAKU_OK;
    }
    void Notify(uint32_t kind, void* transformSelf) {
        if (kind >= targets_.size() || !transformSelf) return;
        GakuLocalifyCameraWriteFn callback;
        void* userData;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (closed_ || !targets_[kind]) return;
            callback = callback_;
            userData = userData_;
        }
        // No lock across plugin code: reentry and self-unsubscription are valid.
        // A snapshot already in flight may complete after Unsubscribe/Close.
        if (callback) callback(userData, kind, transformSelf);
    }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        callback_ = nullptr;
        userData_ = nullptr;
        targets_.fill(nullptr);
    }
    GakuLocalifyCameraWriteService Service() {
        return {GAKULOCALIFY_CAMERA_WRITE_VERSION, sizeof(GakuLocalifyCameraWriteService), this,
            [](void* self, uint32_t kind) -> void* {
                return self ? static_cast<CameraWriteProvider*>(self)->GetTarget(kind) : nullptr;
            },
            [](void* self, GakuLocalifyCameraWriteFn callback, void* userData) -> GakuStatus {
                return self ? static_cast<CameraWriteProvider*>(self)->Subscribe(callback, userData)
                            : GAKU_E_INVALID_ARGUMENT;
            },
            [](void* self, GakuLocalifyCameraWriteFn callback, void* userData) -> GakuStatus {
                return self ? static_cast<CameraWriteProvider*>(self)->Unsubscribe(callback, userData)
                            : GAKU_E_INVALID_ARGUMENT;
            }};
    }
};

}
