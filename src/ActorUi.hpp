#pragma once
#include "DesktopModel.hpp"
#include "ActorModelAssets.hpp"
#include "ResourceCatalog.hpp"
#include "UiThumbnail.hpp"
#include <imgui.h>
#include <cstdio>
#include <deque>
#include <chrono>
#include <cmath>
#include <unordered_map>

namespace GakuActors {
class ActorUi {
    char search_[160]{},name_[192]{},bodyAsset_[320]{},faceAsset_[320]{},hairAsset_[320]{};
    std::string character_,selection_,editId_,body_,head_,factory_;
    bool editor_{},advanced_{},special_{},crossCharacter_{};
    int primary_{},positionActor_{},resetWaitActor_{};float position_[3]{},yaw_{};
    uint64_t resetWaitRevision_{};
    std::string selectedDelete_,thumbnailId_,thumbnailBodyData_,thumbnailHeadData_,thumbnailBodyStatus_,thumbnailHeadStatus_;
    std::string preLivePreset_,preLiveManualCharacter_,preLiveEditCharacter_,preLiveImportStatus_;size_t preLiveSlot_{};
    char preLiveBody_[320]{},preLiveFace_[320]{},preLiveHair_[320]{};
    ui_thumbnail::Image thumbnailBody_,thumbnailHead_;
    uint64_t serial_{},thumbnailRevision_{};
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
    void ResourceCombo(const char* label,const std::vector<ModelResource>& values,char* selected,size_t size) {
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo(label,selected[0]?selected:"选择已发现的模型部件")) {
            for(const auto& r:values) {
                const bool active=r.resource==selected;
                const auto display=(r.source.empty()?r.resource:r.source+" · "+r.resource)+"##"+r.kind+r.resource;
                if(ImGui::Selectable(display.c_str(),active))std::snprintf(selected,size,"%s",r.resource.c_str());
                if(active)ImGui::SetItemDefaultFocus();
                if(ImGui::IsItemHovered())ImGui::SetTooltip("来源：%s\n资源包：%s\n索引存在不代表已下载或构建成功",r.source.c_str(),r.bundle.c_str());
            }
            if(values.empty())ImGui::TextDisabled("暂无条目；请等待底层目录完成，或显示其他人物");
            ImGui::EndCombo();
        }
    }
    using SceneTree=std::unordered_map<int,std::vector<const SceneResource*>>;
    void DrawSceneNode(const UiSnapshot& s,const SceneResource& resource,int depth,const SceneTree& tree) {
        if(depth>32)return;
        const auto found=tree.find(resource.instance);const bool hasChildren=found!=tree.end()&&!found->second.empty();
        ImGui::PushID(resource.instance);
        ImGuiTreeNodeFlags flags=ImGuiTreeNodeFlags_OpenOnArrow;
        if(!hasChildren)flags|=ImGuiTreeNodeFlags_Leaf|ImGuiTreeNodeFlags_NoTreePushOnOpen;
        const bool open=ImGui::TreeNodeEx("##node",flags,"%s%s",resource.name.c_str(),resource.locked?" [锁定]":"");
        ImGui::SameLine();bool hidden=resource.hidden;
        ImGui::BeginDisabled(!s.resourceControls||resource.locked);
        if(ImGui::Checkbox("隐藏##nodehide",&hidden)){Command c{Action::HideResource};c.resource=resource;c.hidden=hidden;commands.push_back(std::move(c));}
        ImGui::EndDisabled();
        if(open&&hasChildren){for(const auto* child:found->second)DrawSceneNode(s,*child,depth+1,tree);ImGui::TreePop();}
        ImGui::PopID();
    }
public:
    std::deque<Command> commands;
    uint64_t ThumbnailRevision()const{return thumbnailRevision_;}
    void SetThumbnail(uint64_t revision,const std::string& id,const std::string& body,const std::string& bodyStatus,const std::string& head,const std::string& headStatus) {
        thumbnailRevision_=revision;
        if(id!=thumbnailId_||body!=thumbnailBodyData_){thumbnailId_=id;thumbnailBodyData_=body;thumbnailBody_=ui_thumbnail::Decode(body);}
        if(id!=thumbnailId_||head!=thumbnailHeadData_){thumbnailHeadData_=head;thumbnailHead_=ui_thumbnail::Decode(head);}
        thumbnailBodyStatus_=bodyStatus;thumbnailHeadStatus_=headStatus;
    }
    void DrawPreLive(const UiSnapshot& s) {
        if(s.inLive) {
            ImGui::TextUnformatted("当前 Live 人物模型（只读）");
            ImGui::TextDisabled("每两秒自动更新；此处仅查看当前模型与加载状态。");
            if(!s.preLiveApplyStatus.empty())ImGui::TextWrapped("换装状态：%s",s.preLiveApplyStatus.c_str());
            if(s.liveModels.empty())ImGui::TextDisabled("等待场景人物加载或识别。");
            for(const auto& model:s.liveModels) {
                ImGui::PushID(model.instance);ImGui::Separator();
                ImGui::TextWrapped("人物：%s",model.name.empty()?"未命名":model.name.c_str());
                ImGui::TextWrapped("身体：%s",model.body.empty()?"尚未读取":model.body.c_str());
                ImGui::TextWrapped("脸部：%s",model.face.empty()?"尚未读取":model.face.c_str());
                ImGui::TextWrapped("头发：%s",model.hair.empty()?"尚未读取":model.hair.c_str());
                ImGui::TextWrapped("加载状态：%s",model.status.c_str());ImGui::PopID();
            }
            return;
        }
        ImGui::TextWrapped("打开游戏的现场播放确认页；此页面会自动读取已确认的服装，也可手动刷新。");
        ImGui::TextDisabled("选择模型并准备换装，再从游戏确认进入 Live。");
        ImGui::BeginDisabled(!s.runtime);
        if(ImGui::Button("检测当前服装选择")){preLivePreset_.clear();commands.push_back({Action::InspectPreLive});}
        ImGui::EndDisabled();
        if(!s.preLiveStatus.empty())ImGui::TextWrapped("%s",s.preLiveStatus.c_str());
        if(!s.preLiveDetected)return;
        if(preLiveSlot_>=s.preLiveCostumes.size())preLiveSlot_=0;
        if(s.preLiveCostumes.size()>1) {
            const auto label="位置 "+std::to_string(preLiveSlot_+1);
            if(ImGui::BeginCombo("演出位置",label.c_str())) {
                for(size_t i=0;i<s.preLiveCostumes.size();++i)if(ImGui::Selectable(("位置 "+std::to_string(i+1)).c_str(),preLiveSlot_==i)){preLiveSlot_=i;preLiveManualCharacter_.clear();preLivePreset_.clear();}
                ImGui::EndCombo();
            }
        }
        const PreLiveCostume* slot=s.preLiveCostumes.empty()?nullptr:&s.preLiveCostumes[preLiveSlot_];
        const std::string detected=slot?PreLiveCharacterFromCostume(s.catalog,*slot):std::string{};
        ImGui::TextWrapped("当前人物：%s",detected.empty()?"尚未识别":detected.c_str());
        ImGui::TextWrapped("确认页身体服装：%s",!slot||slot->body.empty()?"尚未读取":slot->body.c_str());
        ImGui::TextWrapped("确认页头部服装：%s",!slot||slot->head.empty()?"尚未读取":slot->head.c_str());
        ImGui::TextDisabled("确认游戏换装后，确认页数据更新；选择页内临时点选不作为最终结果。");
        if(ImGui::Button("读取人物与服装目录"))commands.push_back({Action::ReadPreLiveCharacters});
        if(!s.preLiveCatalogStatus.empty())ImGui::TextWrapped("%s",s.preLiveCatalogStatus.c_str());
        if(!s.catalog.resourceNotice.empty())ImGui::TextWrapped("%s",s.catalog.resourceNotice.c_str());
        const auto choices=PreLiveCharacterChoices(s.catalog,s.presets);
        const auto character=PreLiveEffectiveCharacter(detected,preLiveManualCharacter_);
        const auto* current=FindCharacter(s.catalog,character);
        const std::string preview=character.empty()?"请选择人物":(current?current->name:character)+(preLiveManualCharacter_.empty()?"（自动识别）":"（手动选择）");
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("人物分类",preview.c_str())) {
            if(ImGui::Selectable("使用自动识别",preLiveManualCharacter_.empty())){preLiveManualCharacter_.clear();preLivePreset_.clear();}
            for(const auto& choice:choices) {
                const auto label=choice.name+" · "+choice.id+"##prelive_"+choice.id;
                if(ImGui::Selectable(label.c_str(),preLiveManualCharacter_==choice.id)){preLiveManualCharacter_=choice.id;preLivePreset_.clear();}
            }
            if(choices.empty())ImGui::TextDisabled("请先读取游戏人物列表，或保存人物预设。");
            ImGui::EndCombo();
        }
        if(character.empty()){ImGui::TextDisabled("请选择人物分类；不依赖自动识别。");return;}
        if(preLiveEditCharacter_!=character){preLiveEditCharacter_=character;preLiveBody_[0]=preLiveFace_[0]=preLiveHair_[0]='\0';preLiveImportStatus_.clear();}
        ImGui::Separator();ImGui::TextUnformatted("身体、脸和头发");
        ResourceCombo("身体##prelive",PreLiveResourceChoices(s.catalog,character,"body"),preLiveBody_,sizeof(preLiveBody_));
        ResourceCombo("脸##prelive",PreLiveResourceChoices(s.catalog,character,"face"),preLiveFace_,sizeof(preLiveFace_));
        ResourceCombo("头发##prelive",PreLiveResourceChoices(s.catalog,character,"hair"),preLiveHair_,sizeof(preLiveHair_));
        ImGui::BeginDisabled(!slot||detected.empty()||!preLiveBody_[0]||!preLiveFace_[0]||!preLiveHair_[0]);
        if(ImGui::Button("准备下一场 Live 换装")) {
            Command command{Action::ArmPreLive};command.instance=static_cast<int>(preLiveSlot_);command.actorScene=s.preLiveScene;
            command.preset.character=detected;command.preset.body=slot->body;command.preset.head=slot->head;
            command.preset.bodyAsset=preLiveBody_;command.preset.faceAsset=preLiveFace_;command.preset.hairAsset=preLiveHair_;commands.push_back(std::move(command));
        }
        ImGui::EndDisabled();ImGui::SameLine();if(ImGui::Button("取消待应用换装"))commands.push_back({Action::CancelPreLive});
        if(!s.preLiveApplyStatus.empty())ImGui::TextWrapped("%s",s.preLiveApplyStatus.c_str());
        if(!s.preLiveModelStatus.empty())ImGui::TextWrapped("%s",s.preLiveModelStatus.c_str());
        ImGui::TextUnformatted("载入这个人物的已有预设");
        bool any=false;
        for(const auto& preset:s.presets)if(preset.character==character) {
            any=true;ImGui::PushID(preset.id.c_str());
            if(ImGui::Selectable(preset.name.c_str(),preLivePreset_==preset.id)) {
                ModelAssets model;std::string error;
                if(ResolvePreLivePreset(s.catalog,preset,character,model,error)) {
                    preLivePreset_=preset.id;std::snprintf(preLiveBody_,sizeof(preLiveBody_),"%s",model.body.c_str());
                    std::snprintf(preLiveFace_,sizeof(preLiveFace_),"%s",model.face.c_str());std::snprintf(preLiveHair_,sizeof(preLiveHair_),"%s",model.hair.c_str());
                    preLiveImportStatus_="预设已载入编辑器";
                } else preLiveImportStatus_=error;
            }
            if(preLivePreset_==preset.id) {
                ImGui::TextWrapped("身体：%s",preset.bodyAsset.empty()?preset.body.c_str():preset.bodyAsset.c_str());
                ImGui::TextWrapped("脸部：%s",preset.faceAsset.empty()?"由头部组合决定":preset.faceAsset.c_str());
                ImGui::TextWrapped("头发：%s",preset.hairAsset.empty()?preset.head.c_str():preset.hairAsset.c_str());
            }
            ImGui::PopID();
        }
        if(!any)ImGui::TextDisabled("当前没有属于这个人物的自定义预设。");
        if(!preLiveImportStatus_.empty())ImGui::TextWrapped("%s",preLiveImportStatus_.c_str());
    }
    void Draw(const UiSnapshot& s) {
        if(!s.preLiveApplyStatus.empty())ImGui::TextWrapped("换装状态：%s",s.preLiveApplyStatus.c_str());
        if(!s.preLiveModelStatus.empty())ImGui::TextWrapped("%s",s.preLiveModelStatus.c_str());
        ImGui::BeginDisabled(!s.admitted&&!s.freeCameraActive);
        if(ImGui::Button(s.freeCameraActive?"关闭自由视角":"开启自由视角"))commands.push_back({Action::FreeCamera,{},s.freeCameraActive?0:1});
        ImGui::EndDisabled();
        if(!s.freeCameraStatus.empty())ImGui::TextWrapped("%s",s.freeCameraStatus.c_str());
        bool enabled=s.resourceControls||s.resourceEnablePending;
        ImGui::BeginDisabled(!s.admitted||(s.motionConflict&&!s.motionHandoffAvailable&&!s.resourceControls));
        if(ImGui::Checkbox("启用资源显隐（自动交接 Motion）",&enabled))commands.push_back({Action::EnableResourceControls,{},enabled?1:0});
        ImGui::EndDisabled();
        ImGui::TextDisabled(s.resourceEnablePending?"正在等待 Motion 释放；可取消勾选停止等待。":s.resourceControls?"资源显隐已启用；关闭后恢复并交还 Motion。":s.motionConflict&&!s.motionHandoffAvailable?"当前 Motion 版本不支持自动交接。":"启用时自动交接；切换场景后需重新启用。");
        if(!s.resourceStatus.empty())ImGui::TextWrapped("%s",s.resourceStatus.c_str());
        if(!ImGui::BeginTabBar("resource-tabs"))return;
        if(ImGui::BeginTabItem("人物")){DrawActors(s);ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("场景")){DrawResources(s,false);ImGui::EndTabItem();}
        if(ImGui::BeginTabItem("道具")){DrawResources(s,true);ImGui::EndTabItem();}
        ImGui::EndTabBar();
    }
    void DrawResources(const UiSnapshot& s,bool props) {
        if(!props) {
            const char* stages[]{"游戏原生摄影场景","通用 Live 舞台（实验）","初星舞台（实验）","中庭舞台（日中）"};
            ImGui::SetNextItemWidth(-1);
            if(ImGui::BeginCombo("下次摄影背景",stages[(s.stageChoice>=0&&s.stageChoice<4)?s.stageChoice:0])) {
                for(int i=0;i<4;++i)if(ImGui::Selectable(stages[i],s.stageChoice==i))commands.push_back({Action::SelectStage,{},i});
                ImGui::EndCombo();
            }
            if(!s.stageStatus.empty())ImGui::TextWrapped("%s",s.stageStatus.c_str());
            ImGui::TextDisabled("选择后在下一次摄影背景载入时生效，非立即替换。");
            bool solid=s.cameraColorEnabled;ImGui::BeginDisabled(!s.resourceControls);
            if(ImGui::Checkbox("相机纯色背景",&solid)) {
                Command c{Action::CameraColor};c.hidden=solid;std::memcpy(c.color,s.cameraColor,sizeof(c.color));commands.push_back(c);
            }
            float color[4];std::memcpy(color,s.cameraColor,sizeof(color));
            ImGui::BeginDisabled(!solid);
            if(ImGui::ColorEdit3("背景颜色",color)) {
                Command c{Action::CameraColor};c.hidden=true;std::memcpy(c.color,color,sizeof(c.color));commands.push_back(c);
            }
            ImGui::EndDisabled();ImGui::EndDisabled();
            ImGui::TextDisabled("相机底色需要配合隐藏背景节点；退出时尝试恢复。");
        }
        if(ImGui::Button("扫描当前场景资源"))commands.push_back({Action::ScanResources});
        if(!s.resourceStatus.empty())ImGui::TextWrapped("%s",s.resourceStatus.c_str());
        ImGui::TextDisabled(props?"角色下匹配的道具节点":"当前场景节点；人物、碰撞和系统节点受保护");
        if(!props) {
            SceneTree tree;for(const auto& node:s.resources)if(node.kind!=SceneResourceKind::ActorModel&&node.kind!=SceneResourceKind::Prop)tree[node.parent].push_back(&node);
            ImGui::BeginChild("##scene_tree",ImVec2(0,420),true);
            const auto roots=tree.find(0);if(roots!=tree.end())for(const auto* node:roots->second)DrawSceneNode(s,*node,0,tree);
            ImGui::EndChild();
        }
        for(const auto& resource:s.resources) {
            if(!props)break;
            if(props!=(resource.kind==SceneResourceKind::Prop))continue;
            ImGui::PushID(resource.instance);bool hidden=resource.hidden;
            ImGui::BeginDisabled(!s.resourceControls);
            if(ImGui::Checkbox(resource.name.c_str(),&hidden)) {
                Command c{Action::HideResource};c.resource=resource;c.hidden=hidden;commands.push_back(std::move(c));
            }
            ImGui::EndDisabled();
            ImGui::PopID();
        }
        ImGui::TextDisabled("勾选隐藏，取消勾选恢复；仅影响扫描确认的节点。");
    }
    void DrawActors(const UiSnapshot& s) {
        if(character_.empty()&&!s.catalog.characters.empty())character_=s.catalog.characters.front().id;
        if(std::none_of(s.actors.begin(),s.actors.end(),[&](const auto& a){return a.instance==primary_;}))primary_=s.actors.empty()?0:s.actors.front().instance;
        ImGui::TextUnformatted("场景人物管理");ImGui::SameLine();ImGui::TextDisabled("GakuActors · 独立插件");
        bool enabled=s.mutationsEnabled;ImGui::BeginDisabled(!s.admitted||s.busy||s.playing||s.faulted);
        if(ImGui::Checkbox("启用独立人物加载（实验）",&enabled))commands.push_back({Action::EnableCreation,{},enabled?1:0});
        ImGui::EndDisabled();
        if(s.faulted)ImGui::TextWrapped("本次运行已锁定人物操作。请保留日志；重复读取目录不会解除锁定。");
        else if(!s.mutationsEnabled)ImGui::TextWrapped("独立人物加载已关闭；现有人物的模型编辑不需要开启此选项。");
        ImGui::BeginDisabled(!s.admitted);
        if(ImGui::Button("读取游戏目录"))commands.push_back({Action::Scan});
        ImGui::EndDisabled();
        ImGui::SameLine();if(ImGui::Button("重新读取预设"))commands.push_back({Action::Reload});
        ImGui::TextWrapped("读取目录时同时记录只读诊断：原生接口签名和现有角色模型／骨骼状态，可在同一次运行中重复读取。");
        ImGui::TextWrapped("%s",s.catalog.notice.c_str());
        if(!s.catalog.resourceNotice.empty())ImGui::TextWrapped("%s",s.catalog.resourceNotice.c_str());
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
            const auto rawBodies=ResourceChoices(s.catalog,character_,"body"),rawFaces=ResourceChoices(s.catalog,character_,"face"),rawHair=ResourceChoices(s.catalog,character_,"hair");
            ImGui::TextDisabled("已发现模型：身体 %d · 脸 %d · 头发 %d",static_cast<int>(rawBodies.size()),static_cast<int>(rawFaces.size()),static_cast<int>(rawHair.size()));
            ImGui::SameLine();ImGui::BeginDisabled(!c);
            if(ImGui::Button("＋ 新建预设")) {
                Preset p;p.character=character_;p.name=c?c->name+" · 自定义":"自定义";
                bool formal=false;
                for(const auto& def:s.catalog.defaults)if(def.character==character_){p=def;p.name=(c?c->name:character_)+" · 自定义";formal=true;break;}
                if(!formal) {
                    if(!rawBodies.empty())p.bodyAsset=rawBodies.front().resource;
                    if(rawFaces.size()==1)p.faceAsset=rawFaces.front().resource;
                    if(rawHair.size()==1)p.hairAsset=rawHair.front().resource;
                }
                BeginEdit(p,true);
                if(!formal&&(!rawBodies.empty()||!rawFaces.empty()||!rawHair.empty()))advanced_=true;
            }
            ImGui::EndDisabled();
            auto presetRow=[&](const Preset& p) {
                if(p.character!=character_)return;
                ImGui::PushID(p.id.c_str());
                if(ImGui::Selectable((p.name+"##preset").c_str(),selection_==p.id)){selection_=p.id;editor_=false;commands.push_back({Action::LoadThumbnail,p});}
                if(selection_==p.id) {
                    ImGui::TextDisabled("身体 / 服装图");
                    if(thumbnailId_==p.id) {ui_thumbnail::Draw(thumbnailBody_);ImGui::TextWrapped("%s",thumbnailBodyStatus_.c_str());
                        ImGui::TextDisabled("头部组合图");ui_thumbnail::Draw(thumbnailHead_);ImGui::TextWrapped("%s",thumbnailHeadStatus_.c_str());}
                    else ImGui::TextDisabled("等待获取图片…");
                    ImGui::TextDisabled("游戏目录图仅作部件参考，不是实时 3D 模型预览。");
                }
                ImGui::TextDisabled("%s",p.builtin?"游戏默认":"自定义预设");
                if(p.factory.empty()) {
                    const auto bodyLabel=p.bodyAsset.empty()?PartLabel(s.catalog.bodies,p.body):p.bodyAsset;
                    const auto headLabel=p.faceAsset.empty()?PartLabel(s.catalog.heads,p.head):p.faceAsset+" + "+p.hairAsset;
                    ImGui::TextWrapped("身体：%s",bodyLabel.c_str());
                    ImGui::TextWrapped("头部：%s",headLabel.c_str());
                } else ImGui::TextDisabled("使用原生人物组合");
                if(ImGui::Button(p.builtin?"另存为预设":"编辑"))BeginEdit(p,p.builtin);
                ImGui::SameLine();ImGui::BeginDisabled(!s.mutationsEnabled||!s.runtime||s.busy||s.playing||!primary_);
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
                        const auto heads=ModelChoices(s.catalog,character_,true,crossCharacter_);
                        ResourceCombo("已发现的身体模型",ResourceChoices(s.catalog,character_,"body",crossCharacter_),bodyAsset_,sizeof(bodyAsset_));
                        const auto headPreview=faceAsset_[0]?std::string(faceAsset_)+" + "+hairAsset_:"选择脸部与头发组合";
                        if(ImGui::BeginCombo("已读取的头部模型",headPreview.c_str())) {
                            for(const auto& m:heads) {
                                const bool selected=m.face==faceAsset_&&m.hair==hairAsset_;
                                if(ImGui::Selectable((m.label+"##head_"+m.face+"|"+m.hair).c_str(),selected)) {
                                    std::snprintf(faceAsset_,sizeof(faceAsset_),"%s",m.face.c_str());std::snprintf(hairAsset_,sizeof(hairAsset_),"%s",m.hair.c_str());
                                }
                                if(selected)ImGui::SetItemDefaultFocus();
                            }
                            if(heads.empty())ImGui::TextDisabled("该分类暂无已解析的脸部与头发组合");
                            ImGui::EndCombo();
                        }
                        ResourceCombo("已发现的脸部模型",ResourceChoices(s.catalog,character_,"face",crossCharacter_),faceAsset_,sizeof(faceAsset_));
                        ResourceCombo("已发现的头发模型",ResourceChoices(s.catalog,character_,"hair",crossCharacter_),hairAsset_,sizeof(hairAsset_));
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
        ImGui::Spacing();
        ImGui::TextWrapped("目录和资源名称仅代表定义可读取，不代表身体、头部或骨骼已加载到场景。");
        std::string primaryLabel="请选择主角色";
        for(const auto& a:s.actors)if(a.instance==primary_)primaryLabel=a.name;
        ImGui::SetNextItemWidth(-1);
        if(ImGui::BeginCombo("新增参考主角色",primaryLabel.c_str())) {
            for(const auto& a:s.actors)if(ImGui::Selectable((a.name+"##primary"+std::to_string(a.instance)).c_str(),primary_==a.instance))primary_=a.instance;
            ImGui::EndCombo();
        }
        ImGui::Spacing();ImGui::Separator();ImGui::Text("当前场景人物（%d）",static_cast<int>(s.actors.size()));
        if(ImGui::Button("扫描人物显隐和场景资源"))commands.push_back({Action::ScanResources});
        for(const auto& a:s.actors) {
            ImGui::PushID(a.instance);ImGui::TextWrapped("%s",a.name.c_str());
            for(const auto& r:s.resources)if(r.kind==SceneResourceKind::ActorModel&&r.instance==a.instance) {
                bool hidden=r.hidden;
                ImGui::BeginDisabled(!s.resourceControls);
                if(ImGui::Checkbox("隐藏人物模型",&hidden)){Command c{Action::HideResource};c.resource=r;c.hidden=hidden;commands.push_back(std::move(c));}
                ImGui::EndDisabled();
            }
            {
                ImGui::BeginDisabled(!s.admitted||s.faulted||!a.identityValid);
                if(ImGui::Button("调整位置与朝向")){
                    positionActor_=a.instance;resetWaitActor_=0;std::memcpy(position_,a.position,sizeof(position_));
                    const auto* q=a.rotation;yaw_=std::atan2(2.0f*(q[3]*q[1]+q[0]*q[2]),1.0f-2.0f*(q[1]*q[1]+q[2]*q[2]))*57.2957795f;
                    if(yaw_<0)yaw_+=360.0f;
                }
                if(a.owned){ImGui::SameLine();ImGui::BeginDisabled(!s.mutationsEnabled||s.playing||s.busy);
                if(ImGui::Button("移除人物"))commands.push_back({Action::Remove,{},a.instance});ImGui::EndDisabled();}
                if(positionActor_==a.instance) {
                    if(resetWaitActor_==a.instance&&s.poseResetActor==a.instance&&s.poseResetRevision>resetWaitRevision_) {
                        std::memcpy(position_,s.poseResetPosition,sizeof(position_));resetWaitActor_=0;
                    }
                    ImGui::BeginDisabled(s.playing||s.busy);
                    if(ImGui::DragFloat3("世界位置",position_,.01f,-10000.0f,10000.0f)) {
                        resetWaitActor_=0;
                        Command cmd{Action::Move,{},a.instance};std::memcpy(cmd.position,position_,sizeof(position_));commands.push_back(cmd);
                    }
                    if(ImGui::Button("重置位置")){resetWaitActor_=a.instance;resetWaitRevision_=s.poseResetRevision;commands.push_back({Action::ResetPosition,{},a.instance});}
                    ImGui::SetNextItemWidth(220);ImGui::SliderFloat("朝向（0–360°）",&yaw_,0.0f,360.0f,"%.1f°");
                    if(ImGui::Button("设置朝向")){Command cmd{Action::SetYaw,{},a.instance};cmd.degrees=yaw_;commands.push_back(cmd);}
                    ImGui::SameLine();if(ImGui::Button("左转 15°")){Command cmd{Action::Turn,{},a.instance};cmd.degrees=-15;commands.push_back(cmd);yaw_=std::fmod(yaw_+345.0f,360.0f);}
                    ImGui::SameLine();if(ImGui::Button("右转 15°")){Command cmd{Action::Turn,{},a.instance};cmd.degrees=15;commands.push_back(cmd);yaw_=std::fmod(yaw_+15.0f,360.0f);}
                    ImGui::EndDisabled();
                }
                ImGui::EndDisabled();
                if(!a.identityValid)ImGui::TextDisabled("人物身份尚未确认，位置调整不可用；可重新扫描。 ");
            }
            ImGui::PopID();
        }
    }
};
}
