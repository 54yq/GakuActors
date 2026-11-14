#pragma once
#include "Catalog.hpp"
#include <imgui.h>
#include <cstdio>
#include <deque>
#include <chrono>

namespace GakuActors {
enum class Action { Scan, Reload, Save, DeletePreset, Spawn, Remove, Move };
struct Command { Action action; Preset preset; int instance{}; float position[3]{}; };
struct UiSnapshot {
    Catalog catalog;std::vector<Preset> presets;std::vector<SceneActor> actors;
    std::string notice,storeNotice;bool writable{},busy{},playing{},runtime{};
};
class ActorUi {
    char search_[160]{},name_[192]{},bodyAsset_[320]{},faceAsset_[320]{},hairAsset_[320]{};
    std::string character_,selection_,editId_,body_,head_,factory_;
    bool editor_{},advanced_{},special_{},experimental_{},crossCharacter_{};
    int primary_{},positionActor_{};float position_[3]{};
    std::string selectedDelete_;
    uint64_t serial_{};
    void BeginEdit(const Preset& p,bool duplicate) {
        editor_=true;editId_=duplicate?std::string{}:p.id;character_=p.character;
        std::snprintf(name_,sizeof(name_),"%s",p.name.c_str());body_=p.body;head_=p.head;factory_=p.factory;
        std::snprintf(bodyAsset_,sizeof(bodyAsset_),"%s",p.bodyAsset.c_str());
        std::snprintf(faceAsset_,sizeof(faceAsset_),"%s",p.faceAsset.c_str());
        std::snprintf(hairAsset_,sizeof(hairAsset_),"%s",p.hairAsset.c_str());
        advanced_=!p.bodyAsset.empty();special_=!p.factory.empty();
    }
    static std::string PartLabel(const std::vector<Part>& values,const std::string& id) {
        const auto* p=FindPart(values,id);return p?p->name:id.empty()?"尚未选择":id;
    }
    void PartCombo(const char* label,const std::vector<Part>& values,std::string& selected) {
        const auto display=PartLabel(values,selected);ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo(label,display.c_str())) {
            for(const auto& p:values)if(crossCharacter_||p.character==character_) {
                const auto text=p.name+(crossCharacter_?" · "+p.character:"")+"##"+p.id;
                if(ImGui::Selectable(text.c_str(),selected==p.id))selected=p.id;
                if(ImGui::IsItemHovered())ImGui::SetTooltip("%s",p.id.c_str());
            }
            ImGui::EndCombo();
        }
    }
public:
    std::deque<Command> commands;
    void Draw(const UiSnapshot& s) {
        if(character_.empty()&&!s.catalog.characters.empty())character_=s.catalog.characters.front().id;
        if(std::none_of(s.actors.begin(),s.actors.end(),[&](const auto& a){return a.instance==primary_;}))primary_=s.actors.empty()?0:s.actors.front().instance;
        ImGui::TextUnformatted("场景人物管理");ImGui::SameLine();ImGui::TextDisabled("GakuActors · 独立插件");
        if(ImGui::Button("刷新游戏目录"))commands.push_back({Action::Scan});
        ImGui::SameLine();if(ImGui::Button("重新读取预设"))commands.push_back({Action::Reload});
        ImGui::TextWrapped("%s",s.catalog.notice.c_str());
        if(!s.notice.empty())ImGui::TextWrapped("%s",s.notice.c_str());
        ImGui::Spacing();ImGui::Separator();
        ImGui::SetNextItemWidth(-1);ImGui::InputTextWithHint("##actor_search","搜索人物名称或代号",search_,sizeof(search_));
        const bool columns=ImGui::GetContentRegionAvail().x>=670;
        const float leftWidth=columns?ImGui::GetContentRegionAvail().x*.29f:0;
        if(ImGui::BeginChild("人物目录",ImVec2(leftWidth,columns?350.0f:140.0f),true)) {
            ImGui::TextUnformatted("人物");
            for(const auto& c:s.catalog.characters) {
                if(search_[0]&&c.name.find(search_)==std::string::npos&&c.id.find(search_)==std::string::npos)continue;
                const auto text=c.name+(c.special?" · 特殊人物":"")+"##char_"+c.id;
                if(ImGui::Selectable(text.c_str(),character_==c.id)){character_=c.id;selection_.clear();editor_=false;}
            }
            if(s.catalog.characters.empty())ImGui::TextWrapped("目录就绪后显示可选人物，包括原生特殊人物。");
        }
        ImGui::EndChild();if(columns)ImGui::SameLine();
        if(ImGui::BeginChild("人物预设",ImVec2(0,columns?350.0f:330.0f),true)) {
            const auto* c=FindCharacter(s.catalog,character_);
            ImGui::TextUnformatted(c?c->name.c_str():"人物预设");
            ImGui::SameLine();ImGui::BeginDisabled(!c);
            if(ImGui::Button("＋ 新建预设")) {
                Preset p;p.character=character_;p.name=c?c->name+" · 自定义":"自定义";
                for(const auto& def:s.catalog.defaults)if(def.character==character_){p=def;p.name=(c?c->name:character_)+" · 自定义";break;}
                BeginEdit(p,true);
            }
            ImGui::EndDisabled();
            auto presetRow=[&](const Preset& p) {
                if(p.character!=character_)return;
                ImGui::PushID(p.id.c_str());
                if(ImGui::Selectable((p.name+"##preset").c_str(),selection_==p.id)){selection_=p.id;editor_=false;}
                ImGui::TextDisabled("%s",p.builtin?"游戏默认":"自定义预设");
                if(p.factory.empty()) {
                    ImGui::TextWrapped("身体：%s",PartLabel(s.catalog.bodies,p.body).c_str());
                    ImGui::TextWrapped("头部：%s",PartLabel(s.catalog.heads,p.head).c_str());
                } else ImGui::TextDisabled("使用原生人物组合");
                if(ImGui::Button(p.builtin?"另存为预设":"编辑"))BeginEdit(p,p.builtin);
                ImGui::SameLine();ImGui::BeginDisabled(!experimental_||!s.runtime||s.busy||s.playing||!primary_);
                if(ImGui::Button("加入场景"))commands.push_back({Action::Spawn,p,primary_});
                ImGui::EndDisabled();
                if(!p.builtin){ImGui::SameLine();if(ImGui::Button("删除预设")){selectedDelete_=p.id;ImGui::OpenPopup("删除人物预设");}}
                if(ImGui::BeginPopupModal("删除人物预设",nullptr,ImGuiWindowFlags_AlwaysAutoResize)) {
                    ImGui::TextUnformatted("删除保存的预设？场景中的人物不会被移除。");
                    if(ImGui::Button("删除")){Preset id;id.id=selectedDelete_;commands.push_back({Action::DeletePreset,id});ImGui::CloseCurrentPopup();}
                    ImGui::SameLine();if(ImGui::Button("取消"))ImGui::CloseCurrentPopup();ImGui::EndPopup();
                }
                ImGui::Spacing();ImGui::Separator();ImGui::PopID();
            };
            for(const auto& p:s.presets)presetRow(p);
            for(const auto& p:s.catalog.defaults)presetRow(p);
            if(editor_) {
                ImGui::Spacing();ImGui::TextUnformatted("新建／编辑人物预设");
                ImGui::SetNextItemWidth(-1);ImGui::InputText("预设名称",name_,sizeof(name_));
                if(!special_) {
                    ImGui::Checkbox("显示其他人物的头身组合",&crossCharacter_);
                    if(!advanced_){PartCombo("身体／服装",s.catalog.bodies,body_);PartCombo("头部／发型",s.catalog.heads,head_);}
                    ImGui::Checkbox("直接指定已知模型资源",&advanced_);
                    if(advanced_){
                        if(!s.catalog.models.empty()&&ImGui::BeginCombo("已读取的身体模型",bodyAsset_[0]?bodyAsset_:"选择模型资源")) {
                            for(const auto& m:s.catalog.models)if(ImGui::Selectable((m.character+" · "+m.body+"##body_"+m.factory).c_str()))std::snprintf(bodyAsset_,sizeof(bodyAsset_),"%s",m.body.c_str());
                            ImGui::EndCombo();
                        }
                        if(!s.catalog.models.empty()&&ImGui::BeginCombo("已读取的头部模型",faceAsset_[0]?faceAsset_:"选择脸部与头发组合")) {
                            for(const auto& m:s.catalog.models)if(ImGui::Selectable((m.character+" · "+m.face+"##head_"+m.factory).c_str())) {
                                std::snprintf(faceAsset_,sizeof(faceAsset_),"%s",m.face.c_str());std::snprintf(hairAsset_,sizeof(hairAsset_),"%s",m.hair.c_str());
                            }
                            ImGui::EndCombo();
                        }
                        ImGui::SetNextItemWidth(-1);ImGui::InputText("身体资源",bodyAsset_,sizeof(bodyAsset_));
                        ImGui::SetNextItemWidth(-1);ImGui::InputText("脸部资源",faceAsset_,sizeof(faceAsset_));
                        ImGui::SetNextItemWidth(-1);ImGui::InputText("头发资源",hairAsset_,sizeof(hairAsset_));}
                } else {
                    ImGui::TextWrapped("保存此原生特殊人物组合；其头身资源不从服装列表推测。");
                    for(const auto& model:s.catalog.models)if(model.factory==factory_&&!model.body.empty()&&!model.face.empty()&&!model.hair.empty()) {
                        if(ImGui::Button("使用已读取的模型资源编辑组合")) {
                            special_=false;advanced_=true;factory_.clear();body_.clear();head_.clear();
                            std::snprintf(bodyAsset_,sizeof(bodyAsset_),"%s",model.body.c_str());
                            std::snprintf(faceAsset_,sizeof(faceAsset_),"%s",model.face.c_str());
                            std::snprintf(hairAsset_,sizeof(hairAsset_),"%s",model.hair.c_str());
                        }
                        break;
                    }
                }
                Preset p;p.id=editId_.empty()?"user-"+std::to_string(std::chrono::system_clock::now().time_since_epoch().count())+"-"+std::to_string(serial_):editId_;
                p.name=name_;p.character=character_;
                if(special_)p.factory=factory_;else if(advanced_){p.bodyAsset=bodyAsset_;p.faceAsset=faceAsset_;p.hairAsset=hairAsset_;}else{p.body=body_;p.head=head_;}
                std::string error;const bool valid=ValidatePreset(p,error);
                ImGui::BeginDisabled(!s.writable||!valid);
                if(ImGui::Button("保存预设")){++serial_;commands.push_back({Action::Save,p});editor_=false;}
                ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("取消编辑"))editor_=false;
                if(!valid)ImGui::TextWrapped("%s",error.c_str());
            }
        }
        ImGui::EndChild();
        if(!s.storeNotice.empty())ImGui::TextWrapped("%s",s.storeNotice.c_str());
        ImGui::Spacing();ImGui::Checkbox("启用实验人物加载",&experimental_);
        ImGui::TextWrapped("新增人物与选定主角色重叠；加载和移除前请在动作插件中停止播放。当前加载流程尚未完成实机验收。");
        std::string primaryLabel="请选择主角色";
        for(const auto& a:s.actors)if(a.instance==primary_)primaryLabel=a.name;
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("新增参考主角色",primaryLabel.c_str())) {
            for(const auto& a:s.actors)if(ImGui::Selectable((a.name+"##primary"+std::to_string(a.instance)).c_str(),primary_==a.instance))primary_=a.instance;
            ImGui::EndCombo();
        }
        ImGui::Spacing();ImGui::Separator();ImGui::Text("当前场景人物（%d）",static_cast<int>(s.actors.size()));
        for(const auto& a:s.actors) {
            ImGui::PushID(a.instance);ImGui::TextWrapped("%s · %s",a.name.c_str(),a.owned?"本插件新增":"游戏人物");
            if(a.owned) {
                if(ImGui::Button("调整位置")){positionActor_=a.instance;std::memcpy(position_,a.position,sizeof(position_));}
                ImGui::SameLine();ImGui::BeginDisabled(s.playing||s.busy);
                if(ImGui::Button("移除人物"))commands.push_back({Action::Remove,{},a.instance});ImGui::EndDisabled();
                if(positionActor_==a.instance&&ImGui::DragFloat3("世界位置",position_,.01f)) {
                    Command cmd{Action::Move,{},a.instance};std::memcpy(cmd.position,position_,sizeof(position_));commands.push_back(cmd);
                }
            }
            ImGui::PopID();
        }
    }
};
}
