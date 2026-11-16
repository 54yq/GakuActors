#pragma once
#include "DesktopModel.hpp"
#include <cmath>
#include <limits>

namespace GakuActors::desktop_protocol {
// Value-only wire format. Parsing does not execute commands or touch Unity;
// object references are resolved again from a current snapshot on the game
// thread. Frames are length-limited by the transport before JSON parsing.
constexpr std::size_t kMaxRequestBytes=64*1024;
constexpr int kVersion=1;
inline Json Snapshot(const UiSnapshot& s) {
    Json out{{"version",kVersion},{"admitted",s.admitted},{"status",s.admissionNotice},
        {"notice",s.notice},{"store_notice",s.storeNotice},{"catalog_notice",s.catalog.notice},{"catalog_revision",s.catalog.revision},{"resource_catalog_notice",s.catalog.resourceNotice},
        {"resource_status",s.resourceStatus},{"stage_status",s.stageStatus},{"prelive_model_status",s.preLiveApplyStatus+" "+s.preLiveModelStatus},
        {"pose_reset_revision",s.poseResetRevision},{"pose_reset_actor",s.poseResetActor},
        {"pose_reset_position",{s.poseResetPosition[0],s.poseResetPosition[1],s.poseResetPosition[2]}},
        {"free_camera_active",s.freeCameraActive},{"free_camera_status",s.freeCameraStatus},
        {"writable",s.writable},{"busy",s.busy},{"playing",s.playing},{"faulted",s.faulted},
        {"creation_enabled",s.mutationsEnabled},{"resource_enabled",s.resourceControls},{"resource_enable_pending",s.resourceEnablePending},
        {"motion_conflict",s.motionConflict},{"motion_handoff_available",s.motionHandoffAvailable},{"motion_handoff_requested",s.motionHandoffRequested},{"motion_handoff_released",s.motionHandoffReleased},{"camera_enabled",s.cameraColorEnabled},
        {"camera_color",{s.cameraColor[0],s.cameraColor[1],s.cameraColor[2],s.cameraColor[3]}},
        {"stage_choice",s.stageChoice},{"characters",Json::array()},{"presets",Json::array()},
        {"bodies",Json::array()},{"heads",Json::array()},{"resources",Json::array()},
        {"models",Json::array()},{"actors",Json::array()},{"scene_resources",Json::array()},{"live_models",Json::array()}};
    for(const auto& c:PreLiveCharacterChoices(s.catalog,s.presets))out["characters"].push_back({{"id",c.id},{"name",c.name},{"special",c.special}});
    for(const auto& p:s.catalog.defaults){auto v=PresetJson(p);v["builtin"]=true;out["presets"].push_back(std::move(v));}
    for(const auto& p:s.presets){auto v=PresetJson(p);v["builtin"]=false;out["presets"].push_back(std::move(v));}
    auto parts=[&](const auto& values,const char* key){for(const auto& p:values)out[key].push_back({{"id",p.id},{"name",p.name},{"character",p.character},{"resource",p.resource},{"face_resource",p.faceResource},{"thumbnail",p.thumbnail}});};
    parts(s.catalog.bodies,"bodies");parts(s.catalog.heads,"heads");
    // Saved IDs remain editable before the game master catalog is available.
    for(const auto& p:s.presets)for(const auto& entry:{std::pair{"bodies",p.body},std::pair{"heads",p.head}}) {
        if(entry.second.empty())continue;
        auto& list=out[entry.first];
        if(std::none_of(list.begin(),list.end(),[&](const auto& part){return part["id"]==entry.second&&part["character"]==p.character;}))
            list.push_back({{"id",entry.second},{"name",entry.second},{"character",p.character},{"resource",""},{"face_resource",""},{"thumbnail",""}});
    }
    for(const auto& r:s.catalog.resources)out["resources"].push_back({{"character",r.character},{"kind",r.kind},{"resource",r.resource},{"source",r.source}});
    for(const auto& p:s.presets)for(const auto& entry:{std::pair{"body",p.bodyAsset},std::pair{"face",p.faceAsset},std::pair{"hair",p.hairAsset}}) {
        if(entry.second.empty())continue;
        auto& list=out["resources"];
        if(std::none_of(list.begin(),list.end(),[&](const auto& r){return r["character"]==p.character&&r["kind"]==entry.first&&r["resource"]==entry.second;}))
            list.push_back({{"character",p.character},{"kind",entry.first},{"resource",entry.second},{"source","saved_preset"}});
    }
    for(const auto& m:s.catalog.models)out["models"].push_back({{"character",m.character},{"body",m.body},{"face",m.face},{"hair",m.hair},{"factory",m.factory}});
    for(const auto& m:s.liveModels)out["live_models"].push_back({{"instance",m.instance},{"name",m.name},{"body",m.body},{"face",m.face},{"hair",m.hair},{"status",m.status}});
    for(const auto& a:s.actors)out["actors"].push_back({{"instance",a.instance},{"scene",a.scene},{"unique",a.uniqueId},{"name",a.name},{"preset",a.preset},{"owned",a.owned},{"pose_supported",a.identityValid},{"bodyAsset",a.bodyAsset},{"faceAsset",a.faceAsset},{"hairAsset",a.hairAsset},{"modelRestorable",a.modelRestorable},{"position",{a.position[0],a.position[1],a.position[2]}}});
    for(const auto& r:s.resources)out["scene_resources"].push_back({{"kind",static_cast<int>(r.kind)},{"instance",r.instance},{"owner",r.owner},{"scene",r.scene},{"generation",r.generation},{"name",r.name},{"hidden",r.hidden},{"parent",r.parent},{"depth",r.depth},{"locked",r.locked},{"active",r.active}});
    return out;
}
inline bool IntField(const Json& j,const char* key,int& value) {
    if(!j.contains(key)||!j[key].is_number_integer())return false;
    if(j[key]<std::numeric_limits<int>::min()||j[key]>std::numeric_limits<int>::max())return false;
    value=j[key].get<int>();return true;
}
inline bool BoolField(const Json& j,const char* key,bool& value) {
    if(!j.contains(key)||!j[key].is_boolean())return false;value=j[key].get<bool>();return true;
}
inline bool Vector(const Json& j,const char* key,float* values,int count,float low,float high) {
    if(!j.contains(key)||!j[key].is_array()||j[key].size()!=static_cast<size_t>(count))return false;
    for(int i=0;i<count;++i){if(!j[key][i].is_number())return false;double v=j[key][i].get<double>();if(!std::isfinite(v)||v<low||v>high)return false;values[i]=static_cast<float>(v);}return true;
}
inline bool Decode(const std::string& bytes,const UiSnapshot& snapshot,Command& command) noexcept {
    try {
        if(bytes.empty()||bytes.size()>kMaxRequestBytes)return false;
        auto j=Json::parse(bytes,nullptr,false);if(!j.is_object()||!j.contains("version")||j["version"]!=kVersion||!j.contains("action")||!j["action"].is_string())return false;
        const auto action=j["action"].get<std::string>();
        if(action=="scan")command={Action::Scan};
        else if(action=="reload")command={Action::Reload};
        else if(action=="scan_resources")command={Action::ScanResources};
        else if(action=="enable_creation"||action=="enable_resources") {
            bool enabled{};if(!BoolField(j,"enabled",enabled))return false;
            command={action=="enable_creation"?Action::EnableCreation:Action::EnableResourceControls,{},enabled?1:0};
        } else if(action=="free_camera") {
            bool enabled{};if(!BoolField(j,"enabled",enabled))return false;command={Action::FreeCamera,{},enabled?1:0};
        } else if(action=="set_visual_handoff") {
            bool enabled{};if(!BoolField(j,"enabled",enabled))return false;command={Action::SetVisualHandoff,{},enabled?1:0};
        } else if(action=="save_preset") {
            if(!j.contains("preset"))return false;Preset p;std::string error;
            if(!ReadPreset(j["preset"],p,error)||!ValidatePreset(p,error)||p.builtin)return false;command={Action::Save,p};
        } else if(action=="load_part_thumbnail") {
            // Resolve an actual catalogue row; the desktop cannot supply a texture
            // name, path, URL or arbitrary Unity method to execute.
            if(!j.contains("kind")||!j["kind"].is_string()||!j.contains("part_id")||!j["part_id"].is_string())return false;
            const auto kind=j["kind"].get<std::string>(),id=j["part_id"].get<std::string>();
            if((kind!="body"&&kind!="head")||!TextValid(id,256))return false;
            const auto* part=FindPart(kind=="body"?snapshot.catalog.bodies:snapshot.catalog.heads,id);
            if(!part)return false;
            Preset p;p.id="part:"+kind+":"+id;p.character=part->character;
            if(kind=="body")p.body=id;else p.head=id;
            command={Action::LoadThumbnail,std::move(p)};
        } else if(action=="spawn"||action=="delete_preset"||action=="load_thumbnail") {
            if(!j.contains("preset_id")||!j["preset_id"].is_string())return false;
            const auto id=j["preset_id"].get<std::string>();if(!TextValid(id,96))return false;
            const Preset* match=nullptr;
            for(const auto& p:snapshot.presets)if(p.id==id){match=&p;break;}
            if(action!="delete_preset"&&!match)for(const auto& p:snapshot.catalog.defaults)if(p.id==id){match=&p;break;}
            if(!match)return false;
            if(action=="delete_preset")command={Action::DeletePreset,*match};
            else if(action=="load_thumbnail")command={Action::LoadThumbnail,*match};
            else {int reference{};if(!IntField(j,"instance",reference)||!reference)return false;command={Action::Spawn,*match,reference};}
        } else if(action=="remove"||action=="move") {
            int instance{};if(!IntField(j,"instance",instance)||!instance)return false;
            command={action=="remove"?Action::Remove:Action::Move,{},instance};
            if(action=="move"&&!Vector(j,"position",command.position,3,-10000,10000))return false;
        } else if(action=="reset_position"||action=="turn"||action=="set_yaw") {
            int instance{};if(!IntField(j,"instance",instance)||!instance)return false;
            command={action=="turn"?Action::Turn:action=="set_yaw"?Action::SetYaw:Action::ResetPosition,{},instance};
            if(action=="turn"||action=="set_yaw") {if(!j.contains("degrees")||!j["degrees"].is_number())return false;
                const double degrees=j["degrees"].get<double>();if(!std::isfinite(degrees)||std::fabs(degrees)>360)return false;
                command.degrees=static_cast<float>(degrees);}
        } else if(action=="camera_color") {
            bool enabled{};if(!BoolField(j,"enabled",enabled))return false;command={Action::CameraColor};command.hidden=enabled;
            if(!Vector(j,"color",command.color,4,0,1))return false;
        } else if(action=="select_stage") {
            int choice{};if(!IntField(j,"choice",choice)||choice<0||choice>3)return false;command={Action::SelectStage,{},choice};
        } else if(action=="hide_resource") {
            int instance{},scene{},kind{};bool hide{};
            if(!IntField(j,"instance",instance)||!IntField(j,"scene",scene)||!IntField(j,"kind",kind)||!BoolField(j,"hidden",hide)||
               !j.contains("generation")||!j["generation"].is_number_unsigned())return false;
            auto generation=j["generation"].get<uint64_t>();const SceneResource* match=nullptr;
            for(const auto& r:snapshot.resources)if(r.instance==instance&&r.scene==scene&&r.generation==generation&&static_cast<int>(r.kind)==kind){match=&r;break;}
            if(!match||match->locked)return false;command={Action::HideResource};command.resource=*match;command.hidden=hide;
        } else return false;
        return true;
    } catch(...) {return false;}
}
}
