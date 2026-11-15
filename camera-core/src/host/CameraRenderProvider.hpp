#pragma once
#include "GakuLocalifyCameraRender.h"
#include <array>
#include <mutex>

namespace GkmsHosted {

struct CameraRenderPushDispatch {
    bool active{false};
    GakuLocalifyCameraRenderCallbacks callbacks{};
};

// Concrete camera-entry bridge, not a general hook chain. A Push pair carries
// one resident snapshot across its original call, even during reentry/unsubscribe.
class CameraRenderProvider {
    std::mutex mutex_;
    std::array<void*, GAKULOCALIFY_CAMERA_RENDER_COUNT> targets_{};
    GakuLocalifyCameraRenderCallbacks callbacks_{};
    bool closed_{false};

    GakuLocalifyCameraRenderCallbacks Snapshot(uint32_t kind) noexcept {
        try {
            std::lock_guard<std::mutex> lock(mutex_);
            return !closed_ && kind < targets_.size() && targets_[kind] ? callbacks_
                : GakuLocalifyCameraRenderCallbacks{};
        } catch (...) { return {}; }
    }
    static bool Active(const GakuLocalifyCameraRenderCallbacks& callbacks) noexcept {
        try { return callbacks.isActive && callbacks.isActive(callbacks.userData) != 0; }
        catch (...) { return false; }
    }
    static bool Equal(const GakuLocalifyCameraRenderCallbacks& a,
        const GakuLocalifyCameraRenderCallbacks& b) noexcept {
        return a.userData == b.userData && a.isActive == b.isActive &&
            a.onMainCamera == b.onMainCamera && a.beforePush == b.beforePush &&
            a.afterPush == b.afterPush && a.afterEndCamera == b.afterEndCamera;
    }
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
    GakuStatus Subscribe(const GakuLocalifyCameraRenderCallbacks* callbacks) {
        if (!callbacks) return GAKU_E_INVALID_ARGUMENT;
        if (callbacks->abiVersion != GAKULOCALIFY_CAMERA_RENDER_VERSION) return GAKU_E_VERSION;
        if (callbacks->structSize < sizeof(*callbacks)) return GAKU_E_SIZE;
        if (!callbacks->userData || !callbacks->isActive || !callbacks->onMainCamera ||
            !callbacks->beforePush || !callbacks->afterPush || !callbacks->afterEndCamera)
            return GAKU_E_INVALID_ARGUMENT;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return GAKU_E_SHUTTING_DOWN;
        if (callbacks_.isActive && !Equal(callbacks_, *callbacks)) return GAKU_E_CONFLICT;
        callbacks_ = *callbacks;
        return GAKU_OK;
    }
    GakuStatus Unsubscribe(void* userData) {
        if (!userData) return GAKU_E_INVALID_ARGUMENT;
        std::lock_guard<std::mutex> lock(mutex_);
        if (closed_) return GAKU_E_SHUTTING_DOWN;
        if (!callbacks_.isActive) return GAKU_OK;
        if (callbacks_.userData != userData) return GAKU_E_CONFLICT;
        callbacks_ = {};
        return GAKU_OK;
    }
    bool DispatchMain(void** camera) noexcept {
        if (!camera) return false;
        const auto callbacks = Snapshot(GAKULOCALIFY_CAMERA_RENDER_MAIN);
        if (!Active(callbacks)) return false;
        void* original = *camera;
        try { callbacks.onMainCamera(callbacks.userData, camera); }
        catch (...) { *camera = original; }
        return true;
    }
    CameraRenderPushDispatch DispatchBeforePush(void* self, void* state) noexcept {
        CameraRenderPushDispatch dispatch;
        dispatch.callbacks = Snapshot(GAKULOCALIFY_CAMERA_RENDER_PUSH);
        dispatch.active = Active(dispatch.callbacks);
        if (dispatch.active) {
            try { dispatch.callbacks.beforePush(dispatch.callbacks.userData, self, state); }
            catch (...) {}
        }
        return dispatch;
    }
    void DispatchAfterPush(const CameraRenderPushDispatch& dispatch, void* self) noexcept {
        if (dispatch.active) {
            try { dispatch.callbacks.afterPush(dispatch.callbacks.userData, self); }
            catch (...) {}
        }
    }
    bool DispatchEnd(void* camera) noexcept {
        const auto callbacks = Snapshot(GAKULOCALIFY_CAMERA_RENDER_END);
        if (!Active(callbacks)) return false;
        try { callbacks.afterEndCamera(callbacks.userData, camera); }
        catch (...) {}
        return true;
    }
    void Close() {
        std::lock_guard<std::mutex> lock(mutex_);
        closed_ = true;
        callbacks_ = {};
        targets_.fill(nullptr);
    }
    GakuLocalifyCameraRenderService Service() noexcept {
        return {GAKULOCALIFY_CAMERA_RENDER_VERSION, sizeof(GakuLocalifyCameraRenderService), this,
            [](void* self, uint32_t kind) -> void* {
                try { return self ? static_cast<CameraRenderProvider*>(self)->GetTarget(kind) : nullptr; }
                catch (...) { return nullptr; }
            },
            [](void* self, const GakuLocalifyCameraRenderCallbacks* callbacks) -> GakuStatus {
                try { return self ? static_cast<CameraRenderProvider*>(self)->Subscribe(callbacks) : GAKU_E_INVALID_ARGUMENT; }
                catch (...) { return GAKU_E_INTERNAL; }
            },
            [](void* self, void* userData) -> GakuStatus {
                try { return self ? static_cast<CameraRenderProvider*>(self)->Unsubscribe(userData) : GAKU_E_INVALID_ARGUMENT; }
                catch (...) { return GAKU_E_INTERNAL; }
            }};
    }
};
}
