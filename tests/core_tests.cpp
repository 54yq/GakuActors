#include "PresetStore.hpp"
#include "NativeApi.hpp"
#include "ActorUi.hpp"
#include <cassert>
#include <iostream>
#include "native_boundary_tests.hpp"
#include "SceneGate.hpp"
#include "ModelReadiness.hpp"

int main() {
    using namespace GakuActors;
    int checks=0;auto check=[&](bool result){++checks;if(!result){std::cerr<<"failed check "<<checks<<"\n";std::abort();}};
    Preset normal{"user-1","咲季 · 制服","hski","hski-cstm-0000","costume_head_hski-hair-0000"};std::string error;
    check(ValidatePreset(normal,error));
    Preset special{"user-2","老师","teacher",{}, {},"CreateTeacherDescriptor"};check(ValidatePreset(special,error));
    auto invalid=special;invalid.body="body";check(!ValidatePreset(invalid,error));
    invalid=special;invalid.factory="SomeArbitraryMethod";check(!ValidatePreset(invalid,error));
    invalid=normal;invalid.name="bad\nname";check(!ValidatePreset(invalid,error));
    invalid=normal;invalid.bodyAsset="raw_body";check(!ValidatePreset(invalid,error));
    Preset raw{"raw","原生模型组合","npc",{},{},{},"body","face","hair"};check(ValidatePreset(raw,error));
    auto root=std::filesystem::temp_directory_path()/(L"gakuactors-test-"+std::to_wstring(GetCurrentProcessId()));
    std::filesystem::create_directories(root);
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
    check(gate.Mode()==PerformanceMode::Photography);check(gate.MatchesActive("env_3d_adv_rooftop-00-00-noon"));
    check(!gate.MatchesActive("OutGame"));check(!gate.Fresh(1701));
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
    check(ResourceChoices(npc,"hkne","hair").size()==1);check(ResourceChoices(npc,"hkne","hair",true).size()==2);
    check(!ParseModelResource("mdl_chr_krnh-base-0000_face_diffuse"));check(!ParseModelResource("../mdl_chr_krnh-base-0000_face"));
    check(ActorNameForFace("mdl_chr_krnh-base-0000_face",2)=="krnh | GakuActors[2]");
    check(ActorNameForFace("mdl_chr_hume-base-0000_face",3)=="hume | GakuActors[3]");
    check(ActorNameForFace("mdl_chr_hume-base-0000_hair",0).empty());
    check(ActorNameForFace("mdl_chr_unknown-base-0000_face",0).empty());
    check(ActorNameForFace("mdl_chr_myu0-base-0000_face",0)=="myu0 | GakuActors[0]");
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
    std::filesystem::remove(file);std::filesystem::remove(root);
    std::cout<<checks<<" preset validation, Unicode persistence, corruption and catalogue checks passed\n";
}
