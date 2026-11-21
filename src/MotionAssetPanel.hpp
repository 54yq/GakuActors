#pragma once
#include "GakuPlugin.h"
#include "Catalog.hpp"

namespace GakuActors {
// Published by Motion as gakumotion.web-panel/v1. Invoke only inside
// onDesktopUi, under the Loader's callback lifetime gate.
struct MotionWebPanelAssets;
struct MotionWebPanel {
    uint32_t abiVersion{},structSize{};
    void* context{};
    GakuStatus (*getAssets)(void*,MotionWebPanelAssets*){};
    void (*onShow)(void*){},(*onHide)(void*){},(*onUiTick)(void*){};
    void (*setEventSink)(void*,void(*)(void*,const char*),void*){};
    GakuStatus (*handleMessage)(void*,const char*,uint32_t,char*,uint32_t*){};
    void (*onShutdown)(void*){};
};

inline Json MotionWebRequest(const MotionWebPanel& panel,const char* method,const Json& params=Json::object()) {
    const auto request=Json{{"v",1},{"type","request"},{"id",1},{"method",method},{"params",params}}.dump();
    std::vector<char> response(4*1024*1024);
    uint32_t required{};
    if(panel.handleMessage(panel.context,request.c_str(),static_cast<uint32_t>(response.size()),response.data(),&required)!=GAKU_OK||
        !required||required>response.size()||response[required-1]!='\0')return {};
    const auto reply=Json::parse(response.data(),response.data()+required-1,nullptr,false);
    return reply.is_object()&&reply.value("ok",false)?reply.value("result",Json::object()):Json();
}
}
