#pragma once
#include "SceneGate.hpp"
#include "NativeApi.hpp"
#include <GakuPlugin.h>
#include <atomic>
#include <chrono>

namespace GakuActors {
// Load-time asset-name seam. The game's load task and cancellation are not
// replaced. GakuLoader owns the exclusive hook and detects other providers.
class StageLoadHook {
    friend struct StageLoadHookTestAccess;
    using Original=void*(*)(void*,int,const void*);
    inline static StageLoadHook* current_{};
    GakuHookService hooks_{};GakuMainThreadService main_{};
    NativeApi* native_{};SceneGate* gate_{};
    Original original_{};const void* method_{};void* target_{};
    std::atomic<int> choice_{0};std::atomic<bool> enabled_{false};
    static uint64_t Clock() {return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
    static void* Detour(void* background,int time,const void* method) noexcept {
        StageLoadHook* self=current_;if(!self||!self->original_)return nullptr;
        void* result=self->original_(background,time,method);
        try {
            if(!self->enabled_.load(std::memory_order_acquire)||!self->main_.isMainThread||!self->main_.isMainThread(self->main_.context)||
               (method&&method!=self->method_)||!self->gate_||!self->gate_->Fresh(Clock())||self->gate_->Mode()!=PerformanceMode::Photography)return result;
            const char* target=Key(self->choice_.load(std::memory_order_acquire));if(!target||!target[0])return result;
            const auto before=self->native_->String(result);if(before.rfind("env_3d_",0)!=0)return result;
            void* changed=before==target?result:self->native_->NewManagedString(target);if(!changed)return result;
            if(!self->gate_->AuthorizeReplacement(self->gate_->Scene(),target))return result;
            return changed;
        } catch(...) {return result;}
    }
public:
    static const char* Key(int choice) {
        static const char* keys[]{"","env_3d_live_all001-00-noon","env_3d_prod_hifstage-00-00-noon","env_3d_live_courtyard-00-00-noon"};
        return choice>=0&&choice<4?keys[choice]:nullptr;
    }
    void Configure(NativeApi* native,SceneGate* gate,const GakuHookService& hooks,const GakuMainThreadService& main) {
        native_=native;gate_=gate;hooks_=hooks;main_=main;current_=this;
    }
    bool Select(int choice) {
        if(!Key(choice))return false;
        if(choice==0){
            enabled_.store(false,std::memory_order_release);
            if(target_&&(!hooks_.release||hooks_.release(hooks_.context,target_)!=GAKU_OK)){
                enabled_.store(true,std::memory_order_release);return false;
            }
            target_=nullptr;choice_.store(0,std::memory_order_release);return true;
        }
        if(!native_||!gate_||!gate_->Fresh(Clock())||gate_->Mode()!=PerformanceMode::Photography||
           !hooks_.acquireBatch||!hooks_.release||!main_.isMainThread||!main_.isMainThread(main_.context))return false;
        if(!enabled_.load(std::memory_order_acquire)) {
            method_=native_->PhotoBackgroundMethod();if(!method_)return false;
            void* address=*static_cast<void* const*>(method_);if(!address)return false;
            const GakuHookRequest request{address,reinterpret_cast<void*>(Detour),reinterpret_cast<void**>(&original_)};
            const auto status=hooks_.acquireBatch(hooks_.context,1,&request);
            if(status==GAKU_OK||original_)target_=address;
            if(status!=GAKU_OK||!original_){if(target_&&hooks_.release(hooks_.context,target_)==GAKU_OK)target_=nullptr;return false;}
            enabled_.store(true,std::memory_order_release);
        }
        choice_.store(choice,std::memory_order_release);return true;
    }
    void Shutdown() {
        choice_.store(0,std::memory_order_release);enabled_.store(false,std::memory_order_release);
        if(target_&&hooks_.release&&hooks_.release(hooks_.context,target_)==GAKU_OK)target_=nullptr;
    }
};
}
