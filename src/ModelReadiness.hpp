#pragma once
#include <string>
namespace GakuActors {
struct ModelReadiness {
    bool buildSuccess{},rendererActive{},bodyPart{},facePart{};
    int parts{},meshes{},vertices{},materials{},bones{};
};
inline bool ValidateModelReadiness(const ModelReadiness& m,std::string& error) {
    if(!m.buildSuccess){error="游戏报告人物模型构建失败";return false;}
    if(!m.bodyPart||!m.facePart||m.parts<2){error="身体或脸部模型尚未装配完整";return false;}
    if(!m.meshes||!m.vertices||!m.materials||!m.bones){error="模型网格、材质或蒙皮骨架未就绪";return false;}
    if(!m.rendererActive){error="人物原生渲染器未启用";return false;}
    error.clear();return true;
}
}
