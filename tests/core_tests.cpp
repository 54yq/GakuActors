#include "PresetStore.hpp"
#include "NativeApi.hpp"
#include "ActorUi.hpp"
#include <cassert>
#include <iostream>
#include <source_location>
#include "native_boundary_tests.hpp"
#include "SceneGate.hpp"
#include "ModelReadiness.hpp"
#include "ResourceOwnership.hpp"
#include "StageLoadHook.hpp"
#include "StageChoiceStore.hpp"
#include "DesktopProtocol.hpp"
#include "DesktopTransport.hpp"
#include "ActorModelAssets.hpp"

namespace GakuActors {
struct StageLoadHookTestAccess {
    static inline bool failRelease{};static inline int releases{};
    static GakuStatus Release(void*,void*){++releases;return failRelease?GAKU_E_CONFLICT:GAKU_OK;}
    template<class Check> static void Run(Check check) {
        StageLoadHook hook;hook.hooks_.release=Release;hook.target_=reinterpret_cast<void*>(1);
        hook.enabled_=true;hook.choice_=3;releases=0;failRelease=true;
        check(!hook.Select(0)&&hook.enabled_.load()&&hook.choice_.load()==3&&hook.target_!=nullptr);
        failRelease=false;check(hook.Select(0)&&!hook.enabled_.load()&&hook.choice_.load()==0&&hook.target_==nullptr);
        check(releases==2);
    }
};
}

int main() {
    using namespace GakuActors;
    int checks=0;auto check=[&](bool result,std::source_location loc=std::source_location::current()){++checks;if(!result){std::cerr<<"failed check "<<checks<<" at "<<loc.file_name()<<":"<<loc.line()<<"\n";std::abort();}};
    check(ShouldObservePreLive(false,1000,2000));
    check(!ShouldObservePreLive(true,1000,2000));
    check(!ShouldObservePreLive(false,0,2000));
    check(!ShouldObservePreLive(false,1000,4001));
    check(!ShouldObservePreLive(false,2000,1000));
    check(PreLiveEffectiveCharacter("hski","")=="hski");
    check(PreLiveEffectiveCharacter("hski","fktn")=="fktn");
    Catalog preLiveCatalog;preLiveCatalog.characters.push_back({"hski","咲季",false});
    std::vector<Preset> preLivePresets(2);preLivePresets[0].character="hski";preLivePresets[1].character="fktn";
    const auto categoryChoices=PreLiveCharacterChoices(preLiveCatalog,preLivePresets);
    check(categoryChoices.size()==2&&categoryChoices[0].name=="咲季"&&categoryChoices[1].id=="fktn");
    const std::vector<std::string> initialAssets{"prop-before","body-a","face-a","prop-between","hair-a"};
    const std::array<std::string,3> originalParts{"body-a","face-a","hair-a"},newParts{"body-b","face-b","hair-b"};
    std::vector<std::string> swapped;
    check(ReplaceActorModelAssets(initialAssets,originalParts,newParts,swapped));
    check(swapped==std::vector<std::string>({"prop-before","body-b","face-b","prop-between","hair-b"}));
    std::vector<std::string> restored;
    check(ReplaceActorModelAssets(swapped,newParts,originalParts,restored)&&restored==initialAssets);
    auto duplicateAssets=initialAssets;duplicateAssets.push_back("body-a");
    restored={"unchanged"};check(!ReplaceActorModelAssets(duplicateAssets,originalParts,newParts,restored)&&restored==std::vector<std::string>({"unchanged"}));
    check(!ReplaceActorModelAssets(initialAssets,{"body-a","body-a","hair-a"},newParts,restored));
    check(!ReplaceActorModelAssets(initialAssets,originalParts,{"body-b","","hair-b"},restored));
    check(!ReplaceActorModelAssets(initialAssets,{"missing","face-a","hair-a"},newParts,restored));
    check(!ReplaceActorModelAssets(initialAssets,originalParts,{"prop-before","face-b","hair-b"},restored));
    check(ModelRecoveryPermitted(true,false,false,true,true));
    check(!ModelRecoveryPermitted(false,false,false,true,true));
    check(!ModelRecoveryPermitted(true,true,false,true,true));
    check(!ModelRecoveryPermitted(true,false,true,true,true));
    check(!ModelRecoveryPermitted(true,false,false,false,true));
    check(!ModelRecoveryPermitted(true,false,false,true,false));
    DesktopTransport transport;std::string transportError;
    check(!transport.Start(L"Z:\\definitely-missing-gakuactors-test.exe",[](const std::string&){return std::string("{}");},transportError));
    check(!transport.Running()&&!transportError.empty());transport.Stop();
    ResourceSession session;session.Reset(-50);
    ResourceIdentity resource{session.generation,-50,-101,-201};
    check(session.Accept(resource));
    ResourceChange hidden{resource,true,false};
    check(hidden.Decide(resource,false)==RestoreDecision::RestoreOriginal);
    check(hidden.Decide(resource,true)==RestoreDecision::AlreadyRestored);
    auto reusedResource=resource;reusedResource.owner=-102;
    check(hidden.Decide(reusedResource,false)==RestoreDecision::RejectIdentity);
    reusedResource=resource;reusedResource.object=-202;
    check(hidden.Decide(reusedResource,false)==RestoreDecision::RejectIdentity);
    reusedResource=resource;reusedResource.property=1;
    check(hidden.Decide(reusedResource,false)==RestoreDecision::RejectIdentity);
    session.Reset(-50);
    check(!session.Accept(resource));
    reusedResource=resource;reusedResource.generation=session.generation;
    check(hidden.Decide(reusedResource,false)==RestoreDecision::RejectIdentity);
    session.Reset(0);check(!session.Accept(reusedResource));
    struct ResourceBackend {
        ResourceIdentity identity;bool value{true},readable{true},failWrite{};
        bool Read(const ResourceIdentity& id,bool& out) {
            if(!readable||!SameResource(identity,id))return false;out=value;return true;
        }
        bool Write(const ResourceIdentity& id,bool v) {
            if(!SameResource(identity,id))return false;
            value=v;return !failWrite; // simulate a setter failing after a partial write
        }
    } backend{resource};
    ResourceChanges changes;backend.failWrite=true;
    check(!changes.Apply(backend,resource,false));
    check(!backend.value&&changes.Size()==1);
    backend.identity.owner=-102;check(!changes.Restore(backend));
    check(changes.Size()==1&&!backend.value);
    backend.identity=resource;backend.failWrite=false;
    check(changes.Restore(backend)&&backend.value&&changes.Size()==0);
    check(changes.Apply(backend,resource,false)&&changes.Size()==1);
    check(!changes.Apply(backend,resource,false));
    check(changes.Restore(backend)&&backend.value);
    check(changes.Apply(backend,resource,true)&&changes.Size()==0);
    check(changes.Apply(backend,resource,false));
    backend.readable=false;check(!changes.Restore(backend)&&changes.Size()==1);
    backend.readable=true;backend.value=true; // game restored the bit itself
    check(changes.Restore(backend)&&changes.Size()==0&&backend.value);
    Preset normal{"user-1","咲季 · 制服","hski","hski-cstm-0000","costume_head_hski-hair-0000"};std::string error;
    UiSnapshot wire;wire.admitted=true;wire.presets.push_back(normal);
    SceneActor observed;observed.instance=-4;observed.bodyAsset="body-a";observed.faceAsset="face-b";observed.hairAsset="hair-c";wire.actors.push_back(observed);
    wire.resources.push_back({SceneResourceKind::Prop,-12,-12,-7,9,"道具",false});
    Command decoded{Action::Reload};
    auto request=[](const Json& value){return value.dump();};
    check(desktop_protocol::Snapshot(wire)["presets"].size()==1);
    wire.liveModels.push_back({-4,"人物","body-a","face-a","hair-a","已构建"});
    const auto liveWire=desktop_protocol::Snapshot(wire)["live_models"];
    check(liveWire.size()==1&&liveWire[0]["body"]=="body-a"&&liveWire[0]["face"]=="face-a"&&liveWire[0]["hair"]=="hair-a"&&liveWire[0]["status"]=="已构建");
    const auto actorWire=desktop_protocol::Snapshot(wire)["actors"][0];
    check(actorWire["bodyAsset"]=="body-a"&&actorWire["faceAsset"]=="face-b"&&actorWire["hairAsset"]=="hair-c");
    wire.actors[0].scene=-7;wire.actors[0].uniqueId="actor-one";wire.actors[0].identityValid=true;
    wire.catalog.resources.push_back({"hski","body","body-b","",""});
    wire.catalog.resources.push_back({"hski","face","face-new","",""});
    wire.catalog.resources.push_back({"hski","hair","hair-new","",""});
    check(!desktop_protocol::Decode(request({{"version",1},{"action","apply_actor_model"},{"instance",-4},{"scene",-7},{"unique","actor-one"},{"body","body-b"},{"face","face-new"},{"hair","hair-new"}}),wire,decoded));
    check(!desktop_protocol::Decode(request({{"version",1},{"action","apply_actor_model"},{"instance",-4},{"scene",-7},{"unique","wrong"},{"body","body-b"},{"face","face-new"},{"hair","hair-new"}}),wire,decoded));
    check(!desktop_protocol::Decode(request({{"version",1},{"action","apply_actor_model"},{"instance",-4},{"scene",-7},{"unique","actor-one"},{"body","unknown"},{"face","face-new"},{"hair","hair-new"}}),wire,decoded));
    check(!desktop_protocol::Decode(request({{"version",1},{"action","restore_actor_model"},{"instance",-4},{"scene",-7},{"unique","actor-one"}}),wire,decoded));
    wire.actors[0].modelRestorable=true;
    check(!desktop_protocol::Decode(request({{"version",1},{"action","restore_actor_model"},{"instance",-4},{"scene",-7},{"unique","actor-one"}}),wire,decoded));
    check(desktop_protocol::Decode(request({{"version",1},{"action","scan"}}),wire,decoded)&&decoded.action==Action::Scan);
    check(!desktop_protocol::Decode(request({{"version",2},{"action","scan"}}),wire,decoded));
    check(!desktop_protocol::Decode(std::string(desktop_protocol::kMaxRequestBytes+1,'x'),wire,decoded));
    check(desktop_protocol::Decode(request({{"version",1},{"action","spawn"},{"preset_id",normal.id},{"instance",-4}}),wire,decoded)&&decoded.preset.id==normal.id);
    check(!desktop_protocol::Decode(request({{"version",1},{"action","spawn"},{"preset_id","unknown"},{"instance",-4}}),wire,decoded));
    check(desktop_protocol::Decode(request({{"version",1},{"action","load_thumbnail"},{"preset_id",normal.id}}),wire,decoded)&&decoded.action==Action::LoadThumbnail);
    check(!desktop_protocol::Decode(request({{"version",1},{"action","load_thumbnail"},{"preset_id","unknown"}}),wire,decoded));
    wire.catalog.bodies.push_back({"body-1","制服","hski","body-asset",false,"native-body-icon"});
    wire.catalog.heads.push_back({"head-1","发型","hski","hair-asset",true,"native-head-icon","face-asset"});
    check(desktop_protocol::Snapshot(wire)["heads"][0]["face_resource"]=="face-asset");
    check(desktop_protocol::Decode(request({{"version",1},{"action","load_part_thumbnail"},{"kind","body"},{"part_id","body-1"}}),wire,decoded)&&decoded.action==Action::LoadThumbnail&&decoded.preset.body=="body-1"&&decoded.preset.id=="part:body:body-1");
    check(desktop_protocol::Decode(request({{"version",1},{"action","load_part_thumbnail"},{"kind","head"},{"part_id","head-1"}}),wire,decoded)&&decoded.preset.head=="head-1"&&decoded.preset.body.empty());
    check(!desktop_protocol::Decode(request({{"version",1},{"action","load_part_thumbnail"},{"kind","body"},{"part_id","head-1"}}),wire,decoded));
    check(!desktop_protocol::Decode(request({{"version",1},{"action","load_part_thumbnail"},{"kind","texture"},{"part_id","arbitrary-name"}}),wire,decoded));
    check(!desktop_protocol::Decode(request({{"version",1},{"action","load_part_thumbnail"},{"kind","body"},{"part_id",42}}),wire,decoded));
    check(desktop_protocol::Decode(request({{"version",1},{"action","move"},{"instance",-4},{"position",{1,2,3}}}),wire,decoded));
    check(!desktop_protocol::Decode(request({{"version",1},{"action","move"},{"instance",-4},{"position",{1,2,10001}}}),wire,decoded));
    check(desktop_protocol::Decode(request({{"version",1},{"action","hide_resource"},{"kind",1},{"scene",-7},{"instance",-12},{"generation",9},{"hidden",true}}),wire,decoded)&&decoded.resource.name=="道具");
    check(!desktop_protocol::Decode(request({{"version",1},{"action","hide_resource"},{"kind",1},{"scene",-7},{"instance",-12},{"generation",10},{"hidden",true}}),wire,decoded));
    wire.resources[0].locked=true;
    check(!desktop_protocol::Decode(request({{"version",1},{"action","hide_resource"},{"kind",1},{"scene",-7},{"instance",-12},{"generation",9},{"hidden",true}}),wire,decoded));
    check(ValidatePreset(normal,error));
    Preset special{"user-2","老师","teacher",{}, {},"CreateTeacherDescriptor"};check(ValidatePreset(special,error));
    auto invalid=special;invalid.body="body";check(!ValidatePreset(invalid,error));
    invalid=special;invalid.factory="SomeArbitraryMethod";check(!ValidatePreset(invalid,error));
    invalid=normal;invalid.name="bad\nname";check(!ValidatePreset(invalid,error));
    invalid=normal;invalid.bodyAsset="raw_body";check(!ValidatePreset(invalid,error));
    Preset raw{"raw","原生模型组合","npc",{},{},{},"body","face","hair"};check(ValidatePreset(raw,error));
    auto root=std::filesystem::temp_directory_path()/(L"gakuactors-test-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
    StageChoiceStore stageStore;stageStore.SetFile(root/L"stage_choice.json");int stageChoice=-1;
    check(stageStore.Load(stageChoice)&&stageChoice==0);check(stageStore.Save(3));
    StageChoiceStore reloadedStage;reloadedStage.SetFile(root/L"stage_choice.json");
    check(reloadedStage.Load(stageChoice)&&stageChoice==3);check(!reloadedStage.Save(4));
    {std::ofstream bad(root/L"stage_bad.json");bad<<"{\"version\":\"bad\",\"choice\":2}";}
    StageChoiceStore badStage;badStage.SetFile(root/L"stage_bad.json");check(!badStage.Load(stageChoice));check(!badStage.Save(1));
    StageLoadHook hook;check(hook.Select(0));check(!hook.Select(1));check(StageLoadHook::Key(4)==nullptr);
    StageLoadHookTestAccess::Run(check);
    auto file=root/L"人物预设.json";
    PresetStore store;store.SetFile(file);check(store.Load());check(store.presets.empty());check(store.Writable());
    check(store.Save(normal));check(store.Save(special));check(store.Save(raw));check(store.presets.size()==3);
    PresetStore loaded;loaded.SetFile(file);check(loaded.Load());check(loaded.presets[0].name==normal.name);
    normal.name="改名后的预设";check(loaded.Save(normal));check(loaded.presets.size()==3);check(loaded.presets[0].name==normal.name);
    check(loaded.Remove(special.id));check(loaded.presets.size()==2);
    auto prior=loaded.presets;
    {std::ofstream corrupt(file,std::ios::binary);corrupt<<"{broken";}
    check(!loaded.Load());check(loaded.presets.size()==prior.size());check(!loaded.Writable());check(!loaded.Save(normal));
    {std::ifstream unchanged(file,std::ios::binary);std::string text((std::istreambuf_iterator<char>(unchanged)),{});check(text=="{broken");}
    {std::ofstream duplicate(file);duplicate<<Json{{"version",1},{"presets",Json::array({PresetJson(normal),PresetJson(normal)})}}.dump();}
    check(!loaded.Load());check(!loaded.Writable());
    Catalog catalog;catalog.characters.push_back({"hski","花海咲季"});catalog.bodies.push_back({normal.body,"制服","hski",{},false});
    catalog.heads.push_back({normal.head,"默认发型","hski",{},true});
    check(CatalogContains(catalog,normal));check(!CatalogContains(catalog,raw));check(!CatalogContains(catalog,special));
    catalog.defaults.push_back(special);check(CatalogContains(catalog,special));
    auto parsed=Preset{};check(ReadPreset(PresetJson(raw),parsed,error));check(parsed.faceAsset=="face");
    check(!ReadPreset(Json{{"id",5}},parsed,error));
    NativeApiTestAccess::Run(check);
    ActorIdentity original{-101,0,-50,"game-original",true,true,true};
    ActorIdentity created{-102,1,-50,"gakuactors-owned",false,false,false};
    std::vector<int> originalInstances{-101};
    check(FreshIdentity(created,1,-50,originalInstances));check(!FreshIdentity(original,0,-50,originalInstances));
    check(!FreshIdentity(created,0,-50,originalInstances));check(!FreshIdentity(created,1,-51,originalInstances));
    ActorLease lease{7,created,"preset",originalInstances,true,false};check(LeaseMatches(lease,created));
    auto reused=created;reused.instance=-103;check(!LeaseMatches(lease,reused));
    reused=created;reused.unique="game-reused";check(!LeaseMatches(lease,reused));
    reused=created;reused.index=0;check(!LeaseMatches(lease,reused));
    auto unproven=lease;unproven.fresh=false;check(!LeaseMatches(unproven,created));
    unproven=lease;unproven.preexisting.push_back(created.instance);check(!LeaseMatches(unproven,created));
    check(BaselinePreserved({original},{created,original}));check(!BaselinePreserved({original},{created}));
    reused=original;reused.unique="changed";check(!BaselinePreserved({original},{reused}));
    reused=original;reused.rendered=false;check(!BaselinePreserved({original},{reused}));
    SceneGate gate;
    check(!gate.Fresh(0));
    gate.Observe("OutGame",1,"OutGame",100);check(!gate.Fresh(100));
    gate.Observe("Photography",7,"Photography",200);check(gate.Fresh(200));check(gate.Scene()==7);
    check(!gate.AuthorizeReplacement(8,"env_3d_live_courtyard-00-00-noon"));
    check(gate.AuthorizeReplacement(7,"env_3d_live_courtyard-00-00-noon"));
    gate.Observe("Photography",7,"env_3d_live_courtyard-00-00-noon",210);
    check(gate.Fresh(210)&&gate.MatchesActive("env_3d_live_courtyard-00-00-noon"));
    check(gate.Mode()==PerformanceMode::Photography);check(gate.MatchesActive("env_3d_adv_rooftop-00-00-noon"));
    check(!gate.MatchesActive("OutGame"));check(!gate.Fresh(1711));
    gate.Observe("Photography",7,"OutGame",300);check(!gate.Fresh(300));
    gate.Observe("Live",8,"env_3d_live_test",400);check(gate.Fresh(400));check(gate.Mode()==PerformanceMode::Live);
    gate.Observe("OutGame",1,"OutGame",500);check(!gate.Fresh(500));
    gate.Observe("Photography",0,"Photography",600);check(!gate.Fresh(600));
    gate.Observe("Live",9,"Photography",700);check(!gate.Fresh(700));
    gate.Observe("Photography",7,"env_3d_dress_dressroom-00-00-noon",800);check(!gate.Fresh(800));
    ModelReadiness emptyActor{};check(!ValidateModelReadiness(emptyActor,error));
    ModelReadiness complete{true,true,true,true,3,2,5000,4,65};complete.hairPart=true;check(ValidateModelReadiness(complete,error));
    auto incomplete=complete;incomplete.facePart=false;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.vertices=0;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.materials=0;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.bones=0;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.rendererActive=false;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.buildSuccess=false;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.hairPart=false;check(!ValidateModelReadiness(incomplete,error));
    ModelReadiness native{true,true,true,true,3};native.hairPart=true;
    native.nativeBones=387;native.nativeRenderers=3;native.nativeMaterials=4;check(ValidateModelReadiness(native,error));
    native.nativeMaterials=0;check(!ValidateModelReadiness(native,error));
    Catalog models;models.characters={{"hski","花海咲季"},{"nasr","老师"}};
    models.models={{"nasr","teacher_body","teacher_face","hair_a","factory_a"},{"nasr","teacher_body","teacher_face","hair_b","factory_b"},
        {"hski","idol_body","idol_face","idol_hair","outfit"}};
    check(ModelChoices(models,"nasr",false,false).size()==1);
    const auto headChoices=ModelChoices(models,"nasr",true,false);
    check(headChoices.size()==2);check(headChoices[0].label!=headChoices[1].label);
    check(ModelChoices(models,"hski",false,false).size()==1);
    check(ModelChoices(models,"hski",false,true).size()==2);
    Catalog npc;
    for(const auto& asset:{"mdl_chr_krnh-schl-0000_body","mdl_chr_krnh-base-0000_face","mdl_chr_krnh-base-0000_hair",
                          "mdl_chr_hkne-casl-0000_body","mdl_chr_hkne-base-0000_face","mdl_chr_hkne-base-0000_hair"}) {
        auto r=ParseModelResource(asset);check(r.has_value());check(AddModelResource(npc,*r));
    }
    check(npc.defaults.empty()&&npc.models.empty()); // NPC has no wearable costume or pre-paired head.
    check(ResourceChoices(npc,"krnh","body").size()==1);check(ResourceChoices(npc,"krnh","face").size()==1);
    Catalog preLiveParts;
    preLiveParts.bodies.push_back({"b","衣服","krnh","mdl_chr_krnh-base-0000_body",false});
    preLiveParts.heads.push_back({"h","头","krnh","mdl_chr_krnh-base-0000_hair",true,{},"mdl_chr_krnh-base-0000_face"});
    check(PreLiveCharacterFromCostume(preLiveParts,{"","b","h"})=="krnh");
    preLiveParts.heads[0].character="other";
    check(PreLiveCharacterFromCostume(preLiveParts,{"","b","h"}).empty());
    preLiveParts.heads[0].character="krnh";
    check(PreLiveResourceChoices(preLiveParts,"krnh","body").size()==1);
    check(PreLiveResourceChoices(preLiveParts,"krnh","face").size()==1);
    check(PreLiveResourceChoices(preLiveParts,"other","hair").empty());
    Preset preLivePreset{"p","预设","krnh","b","h"};ModelAssets resolved;std::string preLiveError;
    check(ResolvePreLivePreset(preLiveParts,preLivePreset,"krnh",resolved,preLiveError));
    check(resolved.body=="mdl_chr_krnh-base-0000_body"&&resolved.face=="mdl_chr_krnh-base-0000_face"&&resolved.hair=="mdl_chr_krnh-base-0000_hair");
    check(!ResolvePreLivePreset(preLiveParts,preLivePreset,"other",resolved,preLiveError));
    check(resolved.body=="mdl_chr_krnh-base-0000_body");
    check(ResourceChoices(npc,"hkne","hair").size()==1);check(ResourceChoices(npc,"hkne","hair",true).size()==2);
    check(!ParseModelResource("mdl_chr_krnh-base-0000_face_diffuse"));check(!ParseModelResource("../mdl_chr_krnh-base-0000_face"));
    check(ActorNameForFace("mdl_chr_krnh-base-0000_face",2,"我的燐羽")=="krnh | GakuActors[2] | preset:我的燐羽");
    check(ActorNameForFace("mdl_chr_hume-base-0000_face",3,"自定义")=="hume | GakuActors[3] | preset:自定义");
    check(ActorNameForFace("mdl_chr_hume-base-0000_hair",0,"名字")=="gakuactors | GakuActors[0] | preset:名字");
    check(ActorNameForFace("mdl_chr_unknown-base-0000_face",0,"名字")=="unknown | GakuActors[0] | preset:名字");
    check(ActorNameForFace("mdl_chr_myu0-base-0000_face",0,"优")=="myu0 | GakuActors[0] | preset:优");
    check(ActorNameForFace("other-face",0,"自定义")=="gakuactors | GakuActors[0] | preset:自定义");
    check(ActorNameForFace("other-face",0,"").empty());
    check(!ParseModelResource("mdl_chr_krnh-base-0000_face.prefab"));check(!ParseModelResource("mdl_prp_krnh_body"));
    auto part=*ParseModelResource("mdl_chr_krnh-base-0000_face");part.bundle="registered/face";part.source="Octo";
    check(!AddModelResource(npc,part));check(ResourceChoices(npc,"krnh","face")[0].bundle=="registered/face");
    const auto exact=LearnResourcePattern("mdl_chr_krnh-base-0000_face","3d/mdl_chr_krnh-base-0000_face.bundle");check(exact.has_value());
    const auto found=ResourceFromBundle(*exact,"3d/mdl_chr_hkne-base-0000_face.bundle");check(found&&found->character=="hkne");
    check(!ResourceFromBundle(*exact,"texture/mdl_chr_hkne-base-0000_face.bundle"));
    check(!ResourceFromBundle(*exact,"3d/mdl_chr_hkne-base-0000_face_diffuse.bundle"));
    const auto stem=LearnResourcePattern("mdl_chr_krnh-schl-0000_body","3d/model/krnh-schl-0000_body");check(stem.has_value());
    check(ResourceFromBundle(*stem,"3d/model/hkne-casl-0000_body")->resource=="mdl_chr_hkne-casl-0000_body");
    check(!LearnResourcePattern("mdl_chr_krnh-schl-0000_body","unrelated/hash"));
    raw.character="hski";raw.bodyAsset="mdl_chr_nasr-casl-0000_body";
    check(ValidatePreset(raw,error)); // classification never restricts model identity
    // This test owns exactly these files; no recursive deletion or user paths.
    std::filesystem::remove(file);std::filesystem::remove(root/L"stage_choice.json");std::filesystem::remove(root/L"stage_bad.json");std::filesystem::remove(root);
    std::cout<<checks<<" preset validation, Unicode persistence, corruption and catalogue checks passed\n";
}
