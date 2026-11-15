#pragma once
#include "NativeApi.hpp"
#include <GakuPlugin.h>
#include <atomic>
#include <string>

namespace GakuActors {
// Coordinates the button and scene lifetime with the hosted camera service.
struct LocalifyFreeCameraControl {
    uint32_t abiVersion,structSize;
    GakuStatus(*setEnabled)(uint32_t);
    uint32_t(*isEnabled)();
    uint32_t(*protectionReady)();
    void(*stats)(uint64_t*,uint64_t*,uint64_t*);
};
class FreeCamera {
    GakuMainThreadService main_{};
    LocalifyFreeCameraControl service_{};
    std::atomic_bool active_{false};
    uint64_t startPushes_{};
public:
    std::string status{"自由视角未启用"};
    void Configure(NativeApi*,const GakuHookService&,const GakuMainThreadService& main){main_=main;}
    bool Active()const{return active_.load(std::memory_order_acquire);}
    bool FrameReady()const{return Active()&&service_.isEnabled&&service_.isEnabled()&&service_.protectionReady&&service_.protectionReady();}
    void SetTextInput(bool){}
    bool Enable(const GakuHostApi& host){
        if(Active())return true;
        if(!main_.isMainThread||!main_.isMainThread(main_.context)||!host.queryService){status="自由视角不可用：主线程服务缺失";return false;}
        LocalifyFreeCameraControl service{};
        auto result=host.queryService(host.hostContext,"gakulocalify.free-camera-control",1,sizeof(service),&service);
        if(result!=GAKU_OK){
            service={};
            result=host.queryService(host.hostContext,"gakuactors.camera-core",1,sizeof(service),&service);
        }
        if(result!=GAKU_OK||service.abiVersion!=1||service.structSize<sizeof(service)||!service.setEnabled||!service.isEnabled||!service.protectionReady||!service.stats){
            status="自由视角不可用：相机组件未加载";return false;
        }
        if(service.setEnabled(1)!=GAKU_OK){status="自由视角不可用：相机组件未准备就绪，请检查日志";return false;}
        service_=service;uint64_t keys{},queries{};service_.stats(&keys,&queries,&startPushes_);active_.store(true,std::memory_order_release);
        status="已启用自由视角：WASD 移动，方向键转向，空格 / Ctrl 升降，Q / E 视野，F 切换模式";return true;
    }
    bool Disable(){
        const bool wasActive=active_.exchange(false,std::memory_order_acq_rel);
        const bool ok=!wasActive||!service_.setEnabled||service_.setEnabled(0)==GAKU_OK;
        service_={};status=ok?"自由视角已关闭":"自由视角未能关闭，请检查日志";return ok;
    }
    void Scene(bool allowed){
        if(!allowed&&Active()){Disable();return;}
        if(Active()&&service_.isEnabled&&!service_.isEnabled()){
            active_.store(false,std::memory_order_release);service_={};status="相机组件已停止自由视角，请检查快捷键屏蔽日志";
        }
    }
    std::string Diagnostics()const{
        uint64_t keys{},queries{},pushes{};if(service_.stats)service_.stats(&keys,&queries,&pushes);
        return "Free camera: shortcut_block="+std::to_string(FrameReady()?1:0)+
            " forwarded_window_keys="+std::to_string(keys)+" blocked_unity_queries="+std::to_string(queries)+
            " camera_pushes="+std::to_string(pushes>=startPushes_?pushes-startPushes_:0);
    }
};
}
