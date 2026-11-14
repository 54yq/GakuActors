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
    ModelReadiness complete{true,true,true,true,3,2,5000,4,65};check(ValidateModelReadiness(complete,error));
    auto incomplete=complete;incomplete.facePart=false;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.vertices=0;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.materials=0;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.bones=0;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.rendererActive=false;check(!ValidateModelReadiness(incomplete,error));
    incomplete=complete;incomplete.buildSuccess=false;check(!ValidateModelReadiness(incomplete,error));
    Catalog models;models.characters={{"hski","花海咲季"},{"nasr","老师"}};
    models.models={{"nasr","teacher_body","teacher_face","hair_a","factory_a"},{"nasr","teacher_body","teacher_face","hair_b","factory_b"},
        {"hski","idol_body","idol_face","idol_hair","outfit"}};
    check(ModelChoices(models,"nasr",false,false).size()==1);
    const auto headChoices=ModelChoices(models,"nasr",true,false);
    check(headChoices.size()==2);check(headChoices[0].label!=headChoices[1].label);
    check(ModelChoices(models,"hski",false,false).size()==1);
    check(ModelChoices(models,"hski",false,true).size()==2);
    raw.character="hski";raw.bodyAsset="mdl_chr_nasr-casl-0000_body";
    check(ValidatePreset(raw,error)); // classification never restricts model identity
    // This test owns exactly these files; no recursive deletion or user paths.
    std::filesystem::remove(file);std::filesystem::remove(root);
    std::cout<<checks<<" preset validation, Unicode persistence, corruption and catalogue checks passed\n";
}
