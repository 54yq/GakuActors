#include "PresetStore.hpp"
#include "NativeApi.hpp"
#include "ActorUi.hpp"
#include <cassert>
#include <iostream>
#include "native_boundary_tests.hpp"

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
    // This test owns exactly these files; no recursive deletion or user paths.
    std::filesystem::remove(file);std::filesystem::remove(root);
    std::cout<<checks<<" preset validation, Unicode persistence, corruption and catalogue checks passed\n";
}
