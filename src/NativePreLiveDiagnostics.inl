// Read only the confirmation page's committed costume records. Unsupported or
// ambiguous layouts fail closed rather than inferring a slot from asset names.
const void* PreLiveLoadMethod() {
    if(!valueBox_)return nullptr;
    void* player=Class("Campus.Live","LivePlayerPresenter");
    const auto method=Method(player,"PreloadAssetsAsync",4,false,{0x12,0x12,0x15,0x11});
    if(!method||TypeName(param_(method,0))!="Campus.Live.Data.LiveFixedData"||
       TypeName(param_(method,1))!="Campus.Live.LiveModelHelper"||
       TypeName(param_(method,3))!="System.Threading.CancellationToken"||
       TypeName(result_(method))!="Cysharp.Threading.Tasks.UniTask")return nullptr;
    uint32_t alignment{};
    void* taskClass=fromType_(result_(method));const auto status=Method(taskClass,"get_Status",0);
    void* statusClass=status?fromType_(result_(status)):nullptr;
    if(valueSize_(taskClass,&alignment)!=16||valueSize_(fromType_(param_(method,3)),&alignment)!=8||
       !statusClass||!isEnum_(statusClass)||valueSize_(statusClass,&alignment)!=4)return nullptr;
    // The observed Windows x64 entry has a hidden UniTask result pointer and
    // an eight-byte CancellationToken. Refuse unknown compiler/game builds.
    const unsigned char prefix[]{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x57,0x48,0x81,0xec,0x90,0x00,0x00,0x00};
    const void* address=*static_cast<void* const*>(method);
    if(!address||std::memcmp(address,prefix,sizeof(prefix))!=0)return nullptr;
    return method;
}
void ReplacePreLiveDescriptor(void* fixed,void* helper,const std::string& expectedCharacter,const std::array<std::string,3>& expectedOriginal,const std::array<std::string,3>& replacement) {
    if(preLivePending_.active)throw std::runtime_error("上一次预加载尚未完成");
    if(!fixed||!helper||objectClass_(fixed)!=Class("Campus.Live.Data","LiveFixedData")||
       objectClass_(helper)!=Class("Campus.Live","LiveModelHelper"))throw std::runtime_error("预加载数据类型不匹配");
    Pin model(*this,DiagnosticReferenceField(helper,"_model",Class("Campus.Live","LiveModel")));
    Pin modelFixed(*this,model.Get()?DiagnosticReferenceField(model.Get(),"<FixedData>k__BackingField"):nullptr);
    if(!modelFixed.Get()||modelFixed.Get()!=fixed)throw std::runtime_error("预加载和人物生成数据不一致");
    if(expectedCharacter.empty()||Text(fixed,"get_CharacterId")!=expectedCharacter)throw std::runtime_error("即将进入的 Live 人物已改变");
    for(const char* name:{"_originalActorDescriptorsCache","_additionalActorDescriptorsCache"}) {
        const auto field=FindField(objectClass_(fixed),name);
        if(!field||!fieldType_||!fieldFlags_||!fieldValue_||fieldFlags_(field)&0x10)throw std::runtime_error("描述缓存字段不可用");
        Pin cache(*this,fieldValue_(field,fixed));if(cache.Get())throw std::runtime_error("描述缓存已经生成，拒绝替换");
    }
    Pin list(*this,DiagnosticReferenceField(fixed,"<Descriptors>k__BackingField"));
    if(!list.Get()||ClassName(objectClass_(list.Get()))!="System.Collections.Generic.List`1"||
       Int(list.Get(),"get_Count")!=1||Int(fixed,"get_OriginalDescriptorCount")!=1)throw std::runtime_error("当前仅支持单人 Live 描述");
    const auto get=Method(objectClass_(list.Get()),"get_Item",1,false,{8});
    const auto set=Method(objectClass_(list.Get()),"set_Item",2,false,{8,0x12});
    if(!get||!set||TypeName(param_(set,1))!="Campus.Common.CampusActorDescriptor")throw std::runtime_error("描述列表接口不匹配");
    int index=0;void* getArgs[]{&index};Pin original(*this,Call(get,list.Get(),getArgs));
    if(!original.Get())throw std::runtime_error("原始人物描述为空");
    if(Text(original.Get(),"get_body")!=expectedOriginal[0]||Text(original.Get(),"get_face")!=expectedOriginal[1]||Text(original.Get(),"get_hair")!=expectedOriginal[2])
        throw std::runtime_error("游戏当前服装已变化，拒绝使用过期选择");
    Pin copy(*this,PrepareModelDescriptor(original.Get(),replacement));
    if(!copy.Get())throw std::runtime_error("人物描述副本为空");
    preLivePending_.list=newHandle_(list.Get(),false);preLivePending_.original=newHandle_(original.Get(),false);
    preLivePending_.replacement=newHandle_(copy.Get(),false);preLivePending_.fixed=newHandle_(fixed,false);
    if(!preLivePending_.list||!preLivePending_.original||!preLivePending_.replacement||!preLivePending_.fixed){ClearPreLivePending();throw std::runtime_error("无法保留原始描述以便恢复");}
    void* putArgs[]{&index,copy.Get()};
    try {
        Call(set,list.Get(),putArgs);
        Pin readback(*this,Call(get,list.Get(),getArgs));
        if(readback.Get()!=copy.Get())throw std::runtime_error("描述写入校验失败");
    } catch(...) {
        try{void* restoreArgs[]{&index,original.Get()};Call(set,list.Get(),restoreArgs);Trace("PreLive preload: original descriptor restored after setup failure");}
        catch(...){Trace("PreLive preload: descriptor restoration failed; re-enter Live");}
        ClearPreLivePending();throw;
    }
    preLivePending_.active=true;
    Trace(("PreLive preload APPLIED: character="+expectedCharacter+" body="+replacement[0]+" face="+replacement[1]+" hair="+replacement[2]).c_str());
}
void ClearPreLivePending() {
    for(void* handle:{preLivePending_.list,preLivePending_.original,preLivePending_.replacement,preLivePending_.fixed,preLivePending_.task})if(handle)freeHandle_(handle);
    preLivePending_={};
}
bool ObservePreLiveTask(const void* method,void* output) {
    if(!preLivePending_.active||!method||!output||!valueBox_)return false;
    Pin task(*this,valueBox_(fromType_(result_(method)),output));
    if(!task.Get())return false;
    preLivePending_.task=newHandle_(task.Get(),false);return preLivePending_.task!=nullptr;
}
std::string PollPreLiveTask() {
    if(!preLivePending_.active||!preLivePending_.task)return {};
    try {
        void* task=handleTarget_(preLivePending_.task);const auto getStatus=On(task,"get_Status");
        Pin state(*this,Call(getStatus,task));if(!state.Get())throw std::runtime_error("任务状态为空");
        const int status=*static_cast<int*>(unbox_(state.Get()));
        if(status==0)return {};
        if(status==1){Trace("PreLive preload task SUCCEEDED");ClearPreLivePending();return "预加载完成，等待 Live 人物构建";}
        if(status!=2&&status!=3)throw std::runtime_error("未知的预加载任务状态");
        bool restored=false;void* fixed=handleTarget_(preLivePending_.fixed),*list=handleTarget_(preLivePending_.list);
        if(fixed&&list&&!DiagnosticReferenceField(fixed,"_originalActorDescriptorsCache")&&!DiagnosticReferenceField(fixed,"_additionalActorDescriptorsCache")) {
            int index=0;void* args[]{&index};Pin current(*this,Call(Method(objectClass_(list),"get_Item",1,false,{8}),list,args));
            if(current.Get()==handleTarget_(preLivePending_.replacement)) {
                void* original=handleTarget_(preLivePending_.original);void* restoreArgs[]{&index,original};
                Call(Method(objectClass_(list),"set_Item",2,false,{8,0x12}),list,restoreArgs);restored=true;
            }
        }
        Trace(restored?"PreLive preload FAILED: original descriptor restored":"PreLive preload FAILED: automatic restore unavailable; re-enter Live");
        ClearPreLivePending();return restored?"预加载失败，已恢复原始描述；请重新进入 Live":"预加载失败，请返回确认页重新进入 Live 恢复原服装";
    }catch(const std::exception& e){Trace((std::string("PreLive preload task observation unavailable: ")+e.what()).c_str());ClearPreLivePending();return "预加载状态无法确认；请查看日志并在异常时重新进入 Live";}
}

bool ReadConfirmedPreLiveCostumes(void* confirmModel,std::vector<PreLiveCostume>& output,bool verbose) {
    output.clear();if(!confirmModel)return false;
    Pin collection(*this,DiagnosticReferenceField(confirmModel,"<Costumes>k__BackingField"));
    if(!collection.Get())return false;
    void* klass=objectClass_(collection.Get());const int code=typeCode_(const_cast<void*>(classType_(klass)));
    const void* item=nullptr;int count=0;
    if(code==0x1d) {
        const auto length=arrayLength_(collection.Get());if(length>16)return false;
        count=static_cast<int>(length);item=Method(klass,"GetValue",1,false,{8});
    } else if(ClassName(klass)=="System.Collections.Generic.List`1") {
        const auto countMethod=On(collection.Get(),"get_Count");item=Method(klass,"get_Item",1,false,{8});
        if(!countMethod||typeCode_(result_(countMethod))!=8)return false;
        count=Int(collection.Get(),"get_Count");
    } else {
        if(verbose)Trace(("PreLive confirmed costumes: unsupported type="+TypeName(const_cast<void*>(classType_(klass)))).c_str());
        return false;
    }
    if(count<1||count>16||!item)return false;
    void* bodyClass=Class("Campus.Common.Proto.Client.Master","Costume");
    void* headClass=Class("Campus.Common.Proto.Client.Master","CostumeHead");
    void* characterClass=Class("Campus.Common.Proto.Client.Master","Character");
    if(!bodyClass||!headClass)return false;
    std::vector<PreLiveCostume> values;
    for(int index=0;index<count;++index) {
        void* args[]{&index};Pin entry(*this,Call(item,collection.Get(),args));if(!entry.Get())return false;
        if(verbose)Trace(("PreLive confirmed costume entry: index="+std::to_string(index)+" type="+TypeName(const_cast<void*>(classType_(objectClass_(entry.Get()))))).c_str());
        PreLiveCostume value;bool conflict=false;
        auto capture=[&](void* part) {
            if(!part)return;
            void* type=objectClass_(part);std::string* destination=nullptr;
            const bool isBody=assignable_(bodyClass,type),isHead=assignable_(headClass,type);
            const bool isCharacter=characterClass&&assignable_(characterClass,type);
            if(static_cast<int>(isBody)+static_cast<int>(isHead)+static_cast<int>(isCharacter)>1){conflict=true;return;}
            if(isBody)destination=&value.body;
            else if(isHead)destination=&value.head;
            else if(isCharacter)destination=&value.character;
            if(!destination)return;
            const auto getter=On(part,"get_Id");
            if(!getter||typeCode_(result_(getter))!=0x0e)return;
            const auto id=String(Call(getter,part));if(!TextValid(id,256))return;
            if(!destination->empty()&&*destination!=id)conflict=true;else *destination=id;
        };
        capture(entry.Get());
        for(const char* field:{"Item1","Item2","Item3","Item4","<Body>k__BackingField","<Head>k__BackingField","<Character>k__BackingField"}) {
            Pin part(*this,DiagnosticReferenceField(entry.Get(),field));if(part.Get())capture(part.Get());
        }
        if(conflict||value.body.empty()||value.head.empty()) {
            if(verbose){Trace("PreLive confirmed costumes: incomplete or ambiguous record");TraceTypeShape(objectClass_(entry.Get()));}
            return false;
        }
        values.push_back(std::move(value));
    }
    output=std::move(values);return true;
}

// Explicitly requested, read-only inspection of the game's Live confirmation.
// Never changes the page, descriptor, selection, scene, or game data.
PreLiveObservation InspectPreLiveSelection(bool verbose=true) {
    PreLiveObservation out;out.status="未检测到 Live 现场播放确认页";
    if(!Ready()){out.status="游戏运行时尚未就绪";return out;}
    if(verbose)Trace("PreLive inspection BEGIN: explicit read-only request");
    void* component=Class("UnityEngine","Component");
    void* confirm=nullptr;int confirmMatches=0;
    for(const char* name:{"PictureBookLivePlayConfirmSheetPresenter"}) {
        void* klass=Class("Campus.OutGame",name);
        if(!klass){if(verbose)Trace((std::string("PreLive type unavailable: ")+name).c_str());continue;}
        if(verbose){size_t depth=0;for(void* parent=klass;parent&&depth++<16;parent=parent_(parent))TraceTypeShape(parent);}
        if(!component||!assignable_(component,klass)) {if(verbose)Trace((std::string("PreLive type is not Unity component: ")+name).c_str());continue;}
        size_t visited=0;int active=0;
        for(void* candidate:Objects(klass))try {
            if(++visited>64){if(verbose)Trace("PreLive inspection truncated at 64 objects");break;}
            if(objectClass_(candidate)!=klass)continue;
            void* go=GameObject(candidate);if(!Bool(go,"get_activeInHierarchy"))continue;
            const int scene=SceneOf(go);if(!scene)continue;++active;
            if(verbose)Trace((std::string("PreLive active page: type=")+name+" instance="+std::to_string(Int(candidate,"GetInstanceID"))+" scene="+std::to_string(scene)).c_str());
            confirm=candidate;out.scene=scene;++confirmMatches;
        }catch(const std::exception& e){if(verbose)Trace((std::string("PreLive page unavailable: ")+e.what()).c_str());}
        if(verbose)Trace((std::string("PreLive active count: type=")+name+" count="+std::to_string(active)).c_str());
    }
    if(confirmMatches!=1||!confirm){if(confirmMatches>1)out.status="检测到多个 Live 确认页，已停止识别";if(verbose)Trace("PreLive inspection END: no unique confirmation page");return out;}
    out.found=true;
    void* confirmModelClass=Class("Campus.OutGame","PictureBookLivePlayConfirmSheetModel");
    Pin confirmModel(*this,confirmModelClass?DiagnosticReferenceField(confirm,"_model",confirmModelClass):nullptr);
    if(!confirmModel.Get()){out.status="已检测到 Live 确认页，但页面数据尚未读取到";return out;}
    if(verbose){
        TraceTypeShape(objectClass_(confirmModel.Get()));TraceSignature("PreLive confirm model",On(confirmModel.Get(),"get_Costumes"));
        void* fixed=Class("Campus.Live.Data","LiveFixedData");
        void* helper=Class("Campus.Live","LiveModelHelper");
        void* player=Class("Campus.Live","LivePlayerPresenter");
        for(void* type:{fixed,helper,player})if(type)TraceTypeShape(type);
        if(fixed){TraceSignature("PreLive fixed data",Method(fixed,"get_Descriptors",0));TraceSignature("PreLive fixed data",Method(fixed,"GetOriginalActorDescriptors",0));}
        if(player){TraceSignature("PreLive player",Method(player,"PreloadAssetsAsync",4));TraceSignature("PreLive player",Method(player,"CreateDescriptors",2));}
    }
    if(!ReadConfirmedPreLiveCostumes(confirmModel.Get(),out.costumes,verbose)) {
        out.status="已检测到 Live 确认页，但服装数据结构尚未确认";
        if(verbose){Pin costumes(*this,DiagnosticReferenceField(confirmModel.Get(),"<Costumes>k__BackingField"));DiagnosticActorCollection("prelive.confirm.costumes",costumes.Get());}
        return out;
    }
    if(out.costumes.size()==1){out.character=out.costumes[0].character;out.body=out.costumes[0].body;out.head=out.costumes[0].head;}
    out.status="已读取确认页中的服装；当前仅观察，尚未修改游戏数据";
    if(verbose)Trace(("PreLive confirmation: scene="+std::to_string(out.scene)+" slots="+std::to_string(out.costumes.size())).c_str());
    if(verbose)Trace("PreLive inspection END: read-only");return out;
}
