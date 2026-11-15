#pragma once
#include "FreeCameraInputApi.hpp"
#include "GakuPlugin.h"
#include <atomic>

namespace GkmsHosted {
// Unity legacy polling is separate from Windows messages. Only the keys and
// navigation names used by free camera are masked while the game is focused.
class FreeInputFence {
    using KeyFn=bool(*)(int,const void*);
    using StringFn=bool(*)(void*,const void*);
    using AxisFn=float(*)(void*,const void*);
    inline static FreeInputFence* current_{};
    FreeCameraInputApi* api_{};GakuHookService hooks_{};HWND window_{};
    std::atomic_bool* active_{};std::atomic_bool* scene_{};std::atomic_bool* text_{};
    FreeCameraInputApi::FreeInputContract contract_{};void* originals_[14]{};bool leased_[14]{};
    std::atomic_uint64_t blocked_{0};
    bool Suppress()const noexcept {return active_&&scene_&&text_&&active_->load(std::memory_order_acquire)&&scene_->load(std::memory_order_acquire)&&!text_->load(std::memory_order_acquire)&&GetForegroundWindow()==window_;}
    static bool Key(int k)noexcept {
        switch(k){case 97:case 100:case 101:case 102:case 105:case 106:case 107:case 108:case 113:case 114:case 115:case 118:case 119:case 32:case 273:case 274:case 275:case 276:case 303:case 304:case 305:case 306:return true;default:return false;}
    }
    bool Match(void* text,const wchar_t* name)const noexcept{return api_&&api_->FreeInputStringEquals(text,name);}
    bool KeyName(void* s)const noexcept {
        return Match(s,L"w")||Match(s,L"a")||Match(s,L"s")||Match(s,L"d")||Match(s,L"q")||Match(s,L"e")||Match(s,L"r")||Match(s,L"f")||Match(s,L"v")||Match(s,L"i")||Match(s,L"j")||Match(s,L"k")||Match(s,L"l")||
          Match(s,L"up")||Match(s,L"down")||Match(s,L"left")||Match(s,L"right")||Match(s,L"space")||
          Match(s,L"left shift")||Match(s,L"right shift")||Match(s,L"left ctrl")||Match(s,L"right ctrl");
    }
    bool Navigation(void* s)const noexcept {return Match(s,L"horizontal")||Match(s,L"vertical")||Match(s,L"submit")||Match(s,L"cancel");}
    template<int I> static bool KeyHook(int key,const void* method) noexcept {
        auto* self=current_;if(!self)return false;
        if(self->Suppress()&&Key(key)){self->blocked_.fetch_add(1,std::memory_order_relaxed);return false;}
        auto fn=reinterpret_cast<KeyFn>(self->originals_[I]);return fn?fn(key,method):false;
    }
    template<int I> static bool StringHook(void* name,const void* method) noexcept {
        auto* self=current_;if(!self)return false;
        if(self->Suppress()&&(I<6?self->KeyName(name):self->Navigation(name))){self->blocked_.fetch_add(1,std::memory_order_relaxed);return false;}
        auto fn=reinterpret_cast<StringFn>(self->originals_[I]);return fn?fn(name,method):false;
    }
    template<int I> static float AxisHook(void* name,const void* method) noexcept {
        auto* self=current_;if(!self)return 0;
        if(self->Suppress()&&self->Navigation(name)){self->blocked_.fetch_add(1,std::memory_order_relaxed);return 0;}
        auto fn=reinterpret_cast<AxisFn>(self->originals_[I]);return fn?fn(name,method):0;
    }
public:
    void Configure(FreeCameraInputApi* api,const GakuHookService& hooks,std::atomic_bool* active,std::atomic_bool* scene,std::atomic_bool* text){api_=api;hooks_=hooks;active_=active;scene_=scene;text_=text;current_=this;}
    bool Install(HWND window){
        if(!api_||!hooks_.acquireBatch||!hooks_.release)return false;
        if(!api_->ResolveFreeInputContract(contract_)){api_->Trace("Free camera input: Unity input signature validation failed");return false;}
        window_=window;blocked_.store(0,std::memory_order_relaxed);
        void* detours[]{reinterpret_cast<void*>(KeyHook<0>),reinterpret_cast<void*>(KeyHook<1>),reinterpret_cast<void*>(KeyHook<2>),
            reinterpret_cast<void*>(StringHook<3>),reinterpret_cast<void*>(StringHook<4>),reinterpret_cast<void*>(StringHook<5>),
            reinterpret_cast<void*>(AxisHook<6>),reinterpret_cast<void*>(AxisHook<7>),reinterpret_cast<void*>(StringHook<8>),reinterpret_cast<void*>(StringHook<9>),reinterpret_cast<void*>(StringHook<10>),
            reinterpret_cast<void*>(KeyHook<11>),reinterpret_cast<void*>(KeyHook<12>),reinterpret_cast<void*>(KeyHook<13>)};
        GakuHookRequest requests[14]{};int indices[14]{},count=0;
        for(int i=0;i<14;++i){
            if(leased_[i])return false;
            bool duplicate=false;for(int j=0;j<i;++j)if(contract_.targets[i]==contract_.targets[j]){duplicate=true;break;}
            if(duplicate)continue;
            indices[count]=i;requests[count++]={contract_.targets[i],detours[i],&originals_[i]};
        }
        const auto result=hooks_.acquireBatch(hooks_.context,static_cast<uint32_t>(count),requests);
        api_->Trace((std::string("Free camera input: unique targets=")+std::to_string(count)+" hook batch status="+std::to_string(result)).c_str());
        for(int i=0;i<count;++i)leased_[indices[i]]=originals_[indices[i]]!=nullptr;
        if(result!=GAKU_OK){Release();return false;}
        for(int i=0;i<count;++i)if(!leased_[indices[i]]){Release();return false;}
        return true;
    }
    bool Release(){bool ok=true;for(int i=0;i<14;++i)if(leased_[i]){if(hooks_.release(hooks_.context,contract_.targets[i])==GAKU_OK)leased_[i]=false;else ok=false;}return ok;}
    bool HasLease()const {for(bool lease:leased_)if(lease)return true;return false;}
    uint64_t BlockedCount()const{return blocked_.load(std::memory_order_relaxed);}
};
}

