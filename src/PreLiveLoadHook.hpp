#pragma once
#include "NativeApi.hpp"
#include <GakuPlugin.h>
#include <chrono>
#include <atomic>

namespace GakuActors {
// Restricted to the captured x64 ABI and to a single armed Live. The
// original preload is always called; no actor controller is updated.
class PreLiveLoadHook {
    friend struct PreLiveLoadHookTestAccess;
    using Original=void*(*)(void*,void*,void*,void*,void*,uintptr_t,const void*);
    inline static PreLiveLoadHook* current_{};
    GakuHookService hooks_{};GakuMainThreadService main_{};NativeApi* native_{};
    Original original_{};const void* method_{};void* target_{};
    std::atomic<bool> armed_{false},wrongThread_{false};std::string character_;
    std::array<std::string,3> before_{},after_{};
    std::chrono::steady_clock::time_point expires_{};
    static bool MethodMatches(const void* observed,const void* expected){return !observed||observed==expected;}
    static void* Detour(void* result,void* player,void* fixed,void* helper,void* progress,uintptr_t token,const void* method) {
        auto* self=current_;if(!self||!self->original_)return nullptr;
        bool changed=false;
        const bool onMain=self->main_.isMainThread&&self->main_.isMainThread(self->main_.context);
        const bool methodMatches=MethodMatches(method,self->method_);
        if(self->armed_.load(std::memory_order_acquire)&&!onMain){self->armed_.store(false,std::memory_order_release);self->wrongThread_.store(true,std::memory_order_release);}
        if(self->armed_) {
            self->native_->Trace((std::string("PreLive preload HIT: main_thread=")+(onMain?"1":"0")+" method="+(!method?"null":methodMatches?"match":"different")).c_str());
            if(!onMain||!methodMatches){self->armed_=false;self->status=!onMain?"预加载不在主线程，已跳过换装":"预加载方法不匹配，已跳过换装";}
        }
        if(self->armed_&&onMain&&methodMatches) {
            self->armed_=false;
            if(std::chrono::steady_clock::now()>self->expires_)self->status="换装准备已超时，继续使用原服装";
            else try {
                self->native_->ReplacePreLiveDescriptor(fixed,helper,self->character_,self->before_,self->after_);
                self->status="已提交预加载；正在等待游戏加载和人物回读";
                self->submitted_=true;changed=true;
            } catch(const std::exception& e) {
                self->status=std::string("换装未提交，使用原服装：")+e.what();
                self->native_->Trace((std::string("PreLive preload REJECTED: ")+e.what()).c_str());
            } catch(...) {self->status="换装未提交，使用原服装：未知错误";self->native_->Trace("PreLive preload REJECTED: unknown error");}
        }
        void* output=self->original_(result,player,fixed,helper,progress,token,method);
        if(changed)try {
            if(!self->native_->ObservePreLiveTask(self->method_,result)) {
                self->native_->ClearPreLivePending();self->status="无法监控预加载任务；若画面异常请重新进入 Live";
                self->native_->Trace("PreLive preload task observation could not be started");
            }
        }catch(...){self->native_->ClearPreLivePending();self->status="无法监控预加载任务；若画面异常请重新进入 Live";}
        return output;
    }
public:
    std::string status;bool submitted_{};
    void Configure(NativeApi* native,const GakuHookService& hooks,const GakuMainThreadService& main){native_=native;hooks_=hooks;main_=main;current_=this;}
    bool Arm(const std::string& character,const std::array<std::string,3>& before,const std::array<std::string,3>& after) {
        armed_=false;submitted_=false;
        if(!native_||!hooks_.acquireBatch||!hooks_.release||!main_.isMainThread||!main_.isMainThread(main_.context)){status="预加载钩子不可用";return false;}
        if(!target_) {
            method_=native_->PreLiveLoadMethod();if(!method_){status="当前游戏的预加载接口或 ABI 不匹配";return false;}
            void* address=*static_cast<void* const*>(method_);
            const GakuHookRequest request{address,reinterpret_cast<void*>(Detour),reinterpret_cast<void**>(&original_)};
            const auto result=hooks_.acquireBatch(hooks_.context,1,&request);
            if(result==GAKU_OK||original_)target_=address;
            if(result!=GAKU_OK||!original_){if(target_&&hooks_.release(hooks_.context,target_)==GAKU_OK)target_=nullptr;status="预加载钩子被占用或安装失败";return false;}
        }
        character_=character;before_=before;after_=after;expires_=std::chrono::steady_clock::now()+std::chrono::minutes(3);
        submitted_=false;armed_=true;status="已准备换装：请在三分钟内确认进入 Live";
        native_->Trace(("PreLive preload ARMED: character="+character+" body="+after[0]+" face="+after[1]+" hair="+after[2]).c_str());
        return true;
    }
    void Cancel(){armed_=false;status="已取消待应用换装";if(native_)native_->Trace("PreLive preload CANCELLED");}
    void DrainSkip(){if(wrongThread_.exchange(false,std::memory_order_acq_rel)){status="预加载不在主线程，已跳过换装";if(native_)native_->Trace("PreLive preload SKIPPED: not on main thread");}}
    void SceneEntered(){if(armed_.exchange(false,std::memory_order_acq_rel)){status="已进入场景，但未命中预加载接口，本场未换装";if(native_)native_->Trace("PreLive preload MISSED: performance scene entered before hook hit");}}
    void Shutdown(){armed_=false;if(target_&&hooks_.release&&hooks_.release(hooks_.context,target_)==GAKU_OK)target_=nullptr;}
    const std::array<std::string,3>& Requested()const{return after_;}
};
}
