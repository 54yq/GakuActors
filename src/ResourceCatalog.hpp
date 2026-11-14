#pragma once
#include "Catalog.hpp"
#include <optional>
namespace GakuActors {
inline std::optional<ModelResource> ParseModelResource(const std::string& name) {
    constexpr auto prefix="mdl_chr_";
    if(!name.starts_with(prefix)||name.size()>256)return {};
    const auto dash=name.find('-',8),part=name.rfind('_');
    if(dash==std::string::npos||dash<=8||part==std::string::npos||part<=dash+1)return {};
    for(char c:name)if(!((c>='a'&&c<='z')||(c>='0'&&c<='9')||c=='_'||c=='-'))return {};
    const auto kind=name.substr(part+1);
    if(kind!="body"&&kind!="face"&&kind!="hair")return {};
    return ModelResource{name.substr(8,dash-8),kind,name,{},{}};
}
inline std::string ActorNameForFace(const std::string& face,int index,const std::string& presetName) {
    const auto model=ParseModelResource(face);
    if(!TextValid(presetName,160))return {};
    std::string code="gakuactors";
    if(model&&model->kind=="face"&&!model->character.empty()&&model->character.size()<=32&&
        std::all_of(model->character.begin(),model->character.end(),[](char c){return (c>='a'&&c<='z')||(c>='0'&&c<='9');}))code=model->character;
    return code+" | GakuActors["+std::to_string(index)+"] | preset:"+presetName;
}
inline bool AddModelResource(Catalog& c,ModelResource resource) {
    if(!ParseModelResource(resource.resource))return false;
    auto it=std::find_if(c.resources.begin(),c.resources.end(),[&](const auto& a){return a.resource==resource.resource;});
    if(it!=c.resources.end()) {
        if(it->bundle.empty()&&!resource.bundle.empty()){it->bundle=resource.bundle;it->source=resource.source;}
        return false;
    }
    c.resources.push_back(std::move(resource));return true;
}
inline void AddDescriptorResources(Catalog& c,const ModelAssets& model) {
    for(const auto& value:{model.body,model.face,model.hair})if(auto r=ParseModelResource(value)) {
        r->source="原生模型描述";AddModelResource(c,std::move(*r));
    }
}
inline std::vector<ModelResource> ResourceChoices(const Catalog& c,const std::string& character,const std::string& kind,bool all=false) {
    std::vector<ModelResource> out;
    for(const auto& r:c.resources)if(r.kind==kind&&(all||r.character==character))out.push_back(r);
    std::sort(out.begin(),out.end(),[](const auto& a,const auto& b){return a.resource<b.resource;});return out;
}
struct ResourceNamePattern {std::string before,after,resourcePrefix;int slot{1};};
inline std::optional<ResourceNamePattern> LearnResourcePattern(const std::string& resource,const std::string& bundle) {
    if(!ParseModelResource(resource))return {};
    // Learn only from the game's own ex-asset-name converter. Never synthesize
    // a variant or character name that was absent from the resource index.
    auto pos=bundle.find(resource);
    if(pos!=std::string::npos)return ResourceNamePattern{bundle.substr(0,pos),bundle.substr(pos+resource.size()),{}};
    const auto stem=resource.substr(8);pos=bundle.find(stem);
    if(pos!=std::string::npos)return ResourceNamePattern{bundle.substr(0,pos),bundle.substr(pos+stem.size()),"mdl_chr_"};
    return {};
}
inline std::optional<ModelResource> ResourceFromBundle(const ResourceNamePattern& p,const std::string& bundle) {
    if(!bundle.starts_with(p.before)||!bundle.ends_with(p.after)||bundle.size()<=p.before.size()+p.after.size())return {};
    auto r=ParseModelResource(p.resourcePrefix+bundle.substr(p.before.size(),bundle.size()-p.before.size()-p.after.size()));
    if(r){r->bundle=bundle;r->source="Octo 游戏资源索引";}return r;
}
}
