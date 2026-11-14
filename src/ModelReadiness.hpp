#pragma once
#include <string>
namespace GakuActors {
struct ModelReadiness {
    bool buildSuccess{},rendererActive{},bodyPart{},facePart{};
    int parts{},meshes{},vertices{},materials{},bones{};
    bool hairPart{};
    int nativeBones{},nativeRenderers{},nativeMaterials{};
};
inline bool ValidateModelReadiness(const ModelReadiness& m,std::string& error) {
    if(!m.buildSuccess){error="游戏报告人物模型构建失败";return false;}
    if(!m.bodyPart||!m.facePart||!m.hairPart||m.parts<3){error="身体、脸部或头发模型尚未装配完整";return false;}
    const bool unity=m.meshes>0&&m.vertices>0&&m.materials>0&&m.bones>0;
    const bool native=m.nativeBones>0&&m.nativeRenderers>0&&m.nativeMaterials>0;
    if(!unity&&!native){error="模型网格、材质或原生蒙皮骨架未就绪";return false;}
    if(!m.rendererActive){error="人物原生渲染器未启用";return false;}
    error.clear();return true;
}
}
