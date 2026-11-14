#pragma once
#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>
#include <nlohmann/json.hpp>

namespace GakuActors {
using Json = nlohmann::json;
struct Character { std::string id, name; bool special{}; };
struct Part { std::string id, name, character, resource; bool head{}; };
struct ModelAssets {std::string character,body,face,hair,factory;};
struct ModelChoice {std::string label,body,face,hair;};
struct Preset {
    std::string id, name, character, body, head, factory;
    std::string bodyAsset, faceAsset, hairAsset;
    bool builtin{};
};
struct Catalog {
    std::vector<Character> characters;
    std::vector<Part> bodies, heads;
    std::vector<Preset> defaults;
    std::vector<ModelAssets> models;
    uint64_t revision{};
    std::string notice{"等待游戏目录读取"};
};
struct SceneActor {
    int instance{}, scene{};
    std::string name, preset;
    bool owned{};
    float position[3]{}, rotation[4]{0,0,0,1};
};
inline const Character* FindCharacter(const Catalog& c, const std::string& id) {
    for (const auto& item:c.characters) if(item.id==id)return &item;
    return nullptr;
}
inline const Part* FindPart(const std::vector<Part>& parts,const std::string& id) {
    for(const auto& item:parts)if(item.id==id)return &item;
    return nullptr;
}
inline std::vector<ModelChoice> ModelChoices(const Catalog& catalog,const std::string& category,bool heads,bool showAll) {
    std::vector<ModelChoice> choices;
    for(const auto& model:catalog.models) {
        if(!showAll&&model.character!=category)continue;
        if(heads&&(model.face.empty()||model.hair.empty()))continue;
        if(!heads&&model.body.empty())continue;
        const bool duplicate=std::any_of(choices.begin(),choices.end(),[&](const auto& x){return heads?x.face==model.face&&x.hair==model.hair:x.body==model.body;});
        if(duplicate)continue;
        const auto* c=FindCharacter(catalog,model.character);
        const auto name=c?c->name:model.character;
        choices.push_back({name+" · "+(heads?model.face+" + "+model.hair:model.body),model.body,model.face,model.hair});
    }
    return choices;
}
inline bool TextValid(const std::string& text, size_t maximum=256, bool empty=false) {
    if ((!empty && text.empty()) || text.size()>maximum) return false;
    for (const auto c:text) if(static_cast<unsigned char>(c)<32) return false;
    return true;
}
inline bool ValidFactory(const std::string& factory) {
    return factory=="CreateTeacherDescriptor" || factory=="CreateVocalTrainerDescriptor" ||
        factory=="CreateDanceTrainerDescriptor" || factory=="CreateVisualTrainerDescriptor" ||
        factory=="CreateTeacherGlassHairDescriptor";
}
inline bool ValidatePreset(const Preset& p,std::string& error) {
    if(!TextValid(p.id,96)||!TextValid(p.name,160)||!TextValid(p.character,128)) {
        error="预设名称或人物标识无效";return false;
    }
    const bool raw=!p.bodyAsset.empty()||!p.faceAsset.empty()||!p.hairAsset.empty();
    if(!p.factory.empty()) {
        if(!ValidFactory(p.factory)||raw||!p.body.empty()||!p.head.empty()) {
            error="特殊人物描述与头身配置冲突";return false;
        }
    } else if(raw) {
        if(!p.body.empty()||!p.head.empty()||!TextValid(p.bodyAsset)||
            !TextValid(p.faceAsset)||!TextValid(p.hairAsset)) {
            error="身体、脸、头发资源必须成套填写";return false;
        }
    } else if(!TextValid(p.body)||!TextValid(p.head)) {
        error="请选择身体和头部";return false;
    }
    error.clear();return true;
}
inline Json PresetJson(const Preset& p) {
    return Json{{"id",p.id},{"name",p.name},{"character",p.character},
        {"body",p.body},{"head",p.head},{"factory",p.factory},
        {"bodyAsset",p.bodyAsset},{"faceAsset",p.faceAsset},{"hairAsset",p.hairAsset}};
}
inline bool ReadPreset(const Json& j,Preset& p,std::string& error) {
    if(!j.is_object()){error="预设不是对象";return false;}
    try {
        p={j.at("id").get<std::string>(),j.at("name").get<std::string>(),j.at("character").get<std::string>(),
            j.value("body",std::string{}),j.value("head",std::string{}),j.value("factory",std::string{}),
            j.value("bodyAsset",std::string{}),j.value("faceAsset",std::string{}),j.value("hairAsset",std::string{})};
        return ValidatePreset(p,error);
    } catch(const Json::exception&){error="预设字段类型无效";return false;}
}
// A catalogue hit confirms an ID, not whether its underlying bundle is cached.
inline bool CatalogContains(const Catalog& c,const Preset& p) {
    if(!p.factory.empty())return std::any_of(c.defaults.begin(),c.defaults.end(),[&](const auto& d){return d.factory==p.factory;});
    if(!p.bodyAsset.empty())return false; // raw resource availability is not inferred
    return FindPart(c.bodies,p.body)&&FindPart(c.heads,p.head);
}
}
