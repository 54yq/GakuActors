// Read the game's resource database; never load bundles or create actors here.
bool ResourceScanActive()const{return resourceScan_.iterator!=nullptr;}
void StopResourceScan(bool dispose=true) {
    if(resourceScan_.iterator) {
        if(dispose)try {
            void* object=handleTarget_(resourceScan_.iterator);
            if(object)if(const auto m=On(object,"Dispose"))Call(m,object);
        }catch(...){}
        freeHandle_(resourceScan_.iterator);
    }
    resourceScan_={};
}
const void* EnumerableMethod(void* object,const char* plain,const char* explicitName) {
    if(const auto m=On(object,plain))return m;
    return On(object,explicitName);
}
std::pair<std::string,std::string> AssetNamePair(const void* converter,const std::string& resource) {
    Pin name(*this,stringNew_(resource.c_str()));void* args[]{name.Get()};
    Pin pair(*this,Call(converter,nullptr,args));
    if(!pair.Get())throw std::runtime_error("模型名称转换返回为空");
    if(ClassName(objectClass_(pair.Get()))=="System.String[]") {
        if(arrayLength_(pair.Get())!=2)throw std::runtime_error("模型名称数组不是两个字符串");
        auto values=reinterpret_cast<void**>(static_cast<char*>(pair.Get())+4*sizeof(void*));
        return {String(values[0]),String(values[1])};
    }
    const auto a=FindField(objectClass_(pair.Get()),"Item1"),b=FindField(objectClass_(pair.Get()),"Item2");
    if(!a||!b||typeCode_(fieldType_(a))!=0x0e||typeCode_(fieldType_(b))!=0x0e) {
        TraceTypeShape(objectClass_(pair.Get()));throw std::runtime_error("模型名称转换不是已确认的两个字符串");
    }
    Pin first(*this,fieldValue_(a,pair.Get())),second(*this,fieldValue_(b,pair.Get()));
    return {String(first.Get()),String(second.Get())};
}
bool StartResourceScan(Catalog& catalog,bool preLive=false) {
    if(!PerformanceScene()&&!preLive)return false;
    StopResourceScan();
    for(const auto& model:catalog.models)AddDescriptorResources(catalog,model);
    try {
        if(!fieldValue_||!fields_)throw std::runtime_error("缺少资源名称字段读取接口");
        void* octo=Class("Octo","OctoManager");
        Pin database(*this,Call(Method(octo,"get_Database",0,true)));
        if(!database.Get())throw std::runtime_error("Octo 目录尚未初始化");
        const auto namesMethod=On(database.Get(),"GetAllAssetBundleNames");
        TraceSignature("Octo database",namesMethod);
        Pin names(*this,Call(namesMethod,database.Get()));
        if(!names.Get())throw std::runtime_error("Octo 资源名称集合为空");
        const auto getEnumerator=EnumerableMethod(names.Get(),"GetEnumerator","System.Collections.IEnumerable.GetEnumerator");
        Pin iterator(*this,Call(getEnumerator,names.Get()));if(!iterator.Get())throw std::runtime_error("资源目录枚举器为空");
        const auto next=On(iterator.Get(),"MoveNext");
        auto current=EnumerableMethod(iterator.Get(),"get_Current","System.Collections.IEnumerator.get_Current");
        if(!current) {
            void* it=nullptr;size_t count=0;
            while(const void* m=methods_(objectClass_(iterator.Get()),&it)) {
                if(++count>4096)break;const char* n=methodName_(m);
                if(n&&std::string(n).ends_with(".get_Current")&&methodCount_(m)==0&&typeCode_(result_(m))==0x0e){current=m;break;}
            }
        }
        if(!next||typeCode_(result_(next))!=2||!current)throw std::runtime_error("资源枚举器签名不匹配");
        const auto converter=Method(Class("Campus.Common.Octo","SimpleAssetKeeper"),"GetAssetNames",1,true,{0x0e});
        if(!converter)throw std::runtime_error("原生模型名称转换接口不可用");
        TraceSignature("resource converter",converter);
        const auto exists=Method(octo,"IsAssetBundleExists",1,true,{0x0e});
        if(!exists||typeCode_(result_(exists))!=2)throw std::runtime_error("原生资源存在性检查接口不匹配");
        ResourceScan nextScan;nextScan.converter=converter;nextScan.moveNext=next;nextScan.current=current;
        // Seed conversion with exact Master/head/body resources as well as
        // scene-resolved descriptors. The desktop can start this scan before
        // entering a scene, when no descriptor has been created yet.
        std::vector<std::string> samples;
        for(const auto& model:catalog.models)for(const auto& resource:{model.body,model.face,model.hair})if(ParseModelResource(resource))samples.push_back(resource);
        for(const auto& resource:catalog.resources)if(ParseModelResource(resource.resource))samples.push_back(resource.resource);
        for(const auto& body:catalog.bodies)if(ParseModelResource(body.resource))samples.push_back(body.resource);
        for(const auto& head:catalog.heads)for(const auto& resource:{head.faceResource,head.resource})if(ParseModelResource(resource))samples.push_back(resource);
        std::sort(samples.begin(),samples.end());samples.erase(std::unique(samples.begin(),samples.end()),samples.end());
        size_t learned=0;
        for(const auto& resource:samples) {
            const auto pair=AssetNamePair(converter,resource);
            for(int slot:{1,2}) {
                const auto& bundle=slot==1?pair.first:pair.second;if(bundle.empty())continue;
                Pin key(*this,stringNew_(bundle.c_str()));void* args[]{key.Get()};Pin known(*this,Call(exists,nullptr,args));
                if(!known.Get()||!*static_cast<bool*>(unbox_(known.Get())))continue;
                if(auto pattern=LearnResourcePattern(resource,bundle)) {
                    pattern->slot=slot;
                    if(std::none_of(nextScan.patterns.begin(),nextScan.patterns.end(),[&](const auto& p){return p.before==pattern->before&&p.after==pattern->after&&p.resourcePrefix==pattern->resourcePrefix&&p.slot==slot;})) {
                        nextScan.patterns.push_back(*pattern);
                        Trace(("Resource index mapping: model="+resource+" bundle="+bundle+" tuple_slot="+std::to_string(slot)).c_str());
                    }
                }
            }
            if(++learned>=12)break;
        }
        nextScan.iterator=newHandle_(iterator.Get(),false);if(!nextScan.iterator)throw std::runtime_error("无法保留资源枚举器");
        resourceScan_=std::move(nextScan);
        catalog.resourceNotice="正在分帧读取 Octo 模型资源目录；不依赖可穿戴服装表";
        Trace("Resource index BEGIN: explicit scan, read-only Octo names + verified native name conversion");return true;
    }catch(const std::exception& e) {
        StopResourceScan();catalog.resourceNotice=std::string("底层资源目录未完成：")+e.what();Trace(catalog.resourceNotice.c_str());return false;
    }
}
void StepResourceScan(Catalog& catalog,bool preLive=false) {
    if(!ResourceScanActive())return;
    if(!PerformanceScene()&&!preLive){StopResourceScan();catalog.resourceNotice="离开支持场景，资源目录扫描已暂停";return;}
    try {
        void* iterator=handleTarget_(resourceScan_.iterator);if(!iterator)throw std::runtime_error("资源枚举器已丢失");
        for(size_t step=0;step<(preLive?128u:512u);++step) {
            Pin more(*this,Call(resourceScan_.moveNext,iterator));
            if(!more.Get())throw std::runtime_error("资源枚举状态为空");
            if(!*static_cast<bool*>(unbox_(more.Get()))) {
                const auto visited=resourceScan_.visited,added=resourceScan_.added;
                for(const auto& character:catalog.characters) {
                    const auto body=ResourceChoices(catalog,character.id,"body"),face=ResourceChoices(catalog,character.id,"face"),hair=ResourceChoices(catalog,character.id,"hair");
                    Trace(("Resource catalog character: id="+character.id+" body="+std::to_string(body.size())+" face="+std::to_string(face.size())+" hair="+std::to_string(hair.size())).c_str());
                }
                Trace(("Resource index COMPLETE: visited="+std::to_string(visited)+" added="+std::to_string(added)+" candidates="+std::to_string(resourceScan_.candidates)).c_str());
                catalog.resourceNotice="底层资源目录完成：检查 "+std::to_string(visited)+" 条，新增 "+std::to_string(added)+" 个模型部件；索引记录不代表文件已下载或人物已构建";
                StopResourceScan();return;
            }
            if(++resourceScan_.visited>1000000)throw std::runtime_error("资源目录超过百万条检查上限");
            const auto bundle=String(Call(resourceScan_.current,iterator));if(bundle.empty())continue;
            if(resourceScan_.samples.size()<12&&bundle.find("chr")!=std::string::npos) {
                resourceScan_.samples.push_back(bundle);Trace(("Resource index sample: "+bundle).c_str());
            }
            std::optional<ModelResource> candidate;int slot=0;
            for(const auto& p:resourceScan_.patterns)if(auto value=ResourceFromBundle(p,bundle)){candidate=std::move(value);slot=p.slot;break;}
            if(!candidate) {
                const auto start=bundle.find("mdl_chr_");
                if(start!=std::string::npos) {
                    auto end=start;while(end<bundle.size()&&((bundle[end]>='a'&&bundle[end]<='z')||(bundle[end]>='0'&&bundle[end]<='9')||bundle[end]=='_'||bundle[end]=='-'))++end;
                    candidate=ParseModelResource(bundle.substr(start,end-start));
                }
            }
            if(!candidate)continue;
            ++resourceScan_.candidates;
            // Verify every extracted identifier using the game's converter.
            // A texture or similarly named unrelated bundle is never added.
            const auto mapped=AssetNamePair(resourceScan_.converter,candidate->resource);
            const bool valid=slot==1?mapped.first==bundle:slot==2?mapped.second==bundle:(mapped.first==bundle||mapped.second==bundle);
            if(!valid)continue;
            candidate->bundle=bundle;candidate->source="Octo 游戏资源索引";
            if(AddModelResource(catalog,*candidate)) {
                ++resourceScan_.added;
                if(!FindCharacter(catalog,candidate->character))catalog.characters.push_back({candidate->character,candidate->character,true});
                if(candidate->character=="krnh"||candidate->character=="hkne")Trace(("Resource NPC discovered: character="+candidate->character+" kind="+candidate->kind+" model="+candidate->resource+" bundle="+bundle).c_str());
            }
        }
        catalog.resourceNotice="底层资源目录读取中：已检查 "+std::to_string(resourceScan_.visited)+" 条，新增 "+std::to_string(resourceScan_.added)+" 个部件";
    }catch(const std::exception& e) {
        const auto message=std::string("底层资源目录读取中断：")+e.what();StopResourceScan();catalog.resourceNotice=message;Trace(message.c_str());
    }
}
