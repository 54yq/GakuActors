// Included inside NativeApi. No global cache update is executed by this path.
const void* FindField(void* klass,const char* name) {
    if(!fields_||!fieldName_||!fieldType_)return nullptr;
    for(size_t depth=0;klass&&depth<24;++depth,klass=parent_(klass)) {
        void* it=nullptr;size_t count=0;
        while(const void* f=fields_(klass,&it)) {
            if(++count>4096)throw std::runtime_error("字段数量异常");
            const char* n=fieldName_(f);if(n&&!std::strcmp(n,name))return f;
        }
    }
    return nullptr;
}
void* FieldObject(void* object,const char* name) {
    if(!object||!fieldValue_)throw std::runtime_error("字段读取接口不可用");
    const auto f=FindField(objectClass_(object),name);
    if(!f)throw std::runtime_error(std::string("缺少字段：")+name);
    return fieldValue_(f,object);
}
bool IsBuildTuple(void* klass) {
    if(!klass||!isValue_(klass)||ClassName(klass)!="System.ValueTuple`2")return false;
    const auto a=FindField(klass,"Item1"),b=FindField(klass,"Item2");
    return a&&b&&typeCode_(fieldType_(a))==2&&
        ClassName(fromType_(fieldType_(b)))=="Campus.Common.ICampusActorController";
}
struct BuildResult {bool success{};void* actor{};};
BuildResult DecodeBuildResult(void* tuple,void* expected) {
    if(!tuple||objectClass_(tuple)!=expected||!IsBuildTuple(expected))throw std::runtime_error("构建返回值不是已确认的 (bool, ICampusActorController)");
    Pin success(*this,FieldObject(tuple,"Item1"));
    if(!success.Get())throw std::runtime_error("构建结果 Item1 为空");
    return {*static_cast<bool*>(unbox_(success.Get())),FieldObject(tuple,"Item2")};
}
void* FindActor(int instance,int scene) {
    for(void* a:Objects(Class("Campus.Common","CampusActorController")))
        if(Int(a,"GetInstanceID")==instance&&SceneOf(GameObject(a))==scene)return a;
    return nullptr;
}
void* Manager(int instance=0) {
    void* found=nullptr;
    for(void* m:Objects(Class("Campus.Common","CampusActorManager"))) {
        if(instance){if(Int(m,"GetInstanceID")==instance)return m;}
        else if(Bool(GameObject(m),"get_activeInHierarchy")){if(found)return nullptr;found=m;}
    }
    return instance?nullptr:found;
}
ActorIdentity Identify(void* actor) {
    ActorIdentity out;out.instance=Int(actor,"GetInstanceID");out.index=Int(actor,"get_index");
    void* go=GameObject(actor);out.scene=SceneOf(go);out.active=Bool(go,"get_activeInHierarchy");
    out.build=Bool(actor,"get_IsBuildSuccess");out.rendered=Bool(actor,"IsRendererActive");
    Pin descriptor(*this,Call(On(actor,"get_descriptor"),actor));out.unique=Text(descriptor.Get(),"get_uniqueId");return out;
}
std::vector<ActorIdentity> CaptureActors() {
    std::vector<ActorIdentity> result;
    for(void* a:Objects(Class("Campus.Common","CampusActorController")))result.push_back(Identify(a));
    return result;
}
void* AtIndex(void* manager,int index) {
    void* args[]{&index};
    const auto contains=Method(objectClass_(manager),"ContainsActor",1,false,{8});
    if(!contains||typeCode_(result_(contains))!=2)throw std::runtime_error("索引占用检查接口不匹配");
    Pin present(*this,Call(contains,manager,args));
    if(!present.Get()||!*static_cast<bool*>(unbox_(present.Get())))return nullptr;
    return Call(Method(objectClass_(manager),"GetActor",1,false,{8}),manager,args);
}
void SetPose(void* actor,const float* position,const float* rotation) {
    void* t=Call(On(actor,"get_transform"),actor);uint32_t align{};
    const auto p=Method(objectClass_(t),"set_position",1,false,{0x11}),r=Method(objectClass_(t),"set_rotation",1,false,{0x11});
    if(!p||!r||valueSize_(fromType_(param_(p,0)),&align)!=12||valueSize_(fromType_(param_(r,0)),&align)!=16)throw std::runtime_error("角色位置参数不匹配");
    void* pa[]{const_cast<float*>(position)},*ra[]{const_cast<float*>(rotation)};Call(p,t,pa);Call(r,t,ra);
}
void SetActorActive(void* actor,bool active) {
    void* go=GameObject(actor);void* args[]{&active};Call(Method(objectClass_(go),"SetActive",1,false,{2}),go,args);
}
bool SetPresetActorName(void* actor,const std::string& face,int index,const std::string& presetName) {
    const auto name=ActorNameForFace(face,index,presetName);if(name.empty())return false;
    void* go=GameObject(actor);const auto setter=Method(objectClass_(go),"set_name",1,false,{0x0e});
    if(!setter)return false;
    Pin value(*this,stringNew_(name.c_str()));void* args[]{value.Get()};Call(setter,go,args);
    Trace(("Preset actor name: "+name).c_str());return true;
}
void SyncPresetActorNames(const Preset& preset) {
    if(pending_.active&&pending_.lease.preset==preset.id)pending_.displayName=preset.name;
    if(!PerformanceScene())return;
    for(const auto& lease:owned_)if(lease.ready&&lease.preset==preset.id&&lease.identity.scene==PerformanceScene())try {
        void* actor=ResolveLease(lease);if(!actor){Trace("Preset name update skipped: actor ownership changed");continue;}
        Pin descriptor(*this,Call(On(actor,"get_descriptor"),actor));
        if(!SetPresetActorName(actor,Text(descriptor.Get(),"get_face"),lease.identity.index,preset.name))Trace("Preset name update skipped: actor name setter unavailable");
    }catch(const std::exception& e){Trace((std::string("Preset name update failed: ")+e.what()).c_str());}
}
void Fault(const std::string& reason) {
    faulted_=true;creationEnabled_=false;notice="人物操作已锁定："+reason;
    Trace(("Spawn fault LATCHED: "+reason).c_str());
}
void CancelPending() {
    if(!pending_.active||pending_.cancelled)return;
    pending_.cancelled=true;
    if(pending_.cancellation) {
        void* source=handleTarget_(pending_.cancellation);if(source)Call(On(source,"Cancel"),source);
    }
}
void DropPending(bool dispose=true) {
    if(pending_.cancellation&&dispose)try {
        void* source=handleTarget_(pending_.cancellation);if(source)Call(On(source,"Dispose"),source);
    }catch(...){}
    for(void* h:{pending_.awaiter,pending_.task,pending_.cancellation})if(h)freeHandle_(h);
    pending_={};
}
void* ResolveLease(const ActorLease& lease) {
    if(!lease.fresh||!lease.manager||!FreshIdentity(lease.identity,lease.identity.index,lease.identity.scene,lease.preexisting))return nullptr;
    void* manager=Manager(lease.manager);if(!manager)return nullptr;
    void* actor=AtIndex(manager,lease.identity.index);if(!actor)return nullptr;
    return LeaseMatches(lease,Identify(actor))?actor:nullptr;
}
bool DestroyLease(const ActorLease& lease) {
    if(!ResolveLease(lease)){Trace("Cleanup SKIPPED: exact actor lease/descriptor not confirmed; no destruction invoked");return false;}
    void* manager=Manager(lease.manager);if(!manager)return false;
    const auto destroy=Method(objectClass_(manager),"DestroyActor",1,false,{8});
    if(!destroy||typeCode_(result_(destroy))!=1)return false;
    Trace(("Cleanup owned actor: instance="+std::to_string(lease.identity.instance)+" index="+std::to_string(lease.identity.index)).c_str());
    int index=lease.identity.index;void* args[]{&index};Call(destroy,manager,args);return true;
}
bool ReleaseLease(const ActorLease& lease) {
    if(!lease.fresh||!lease.identity.instance)return false;
    if(DestroyLease(lease))return true;
    // Scene unload may have already destroyed this exact object. Only a fresh
    // all-object enumeration can retire the record without invoking cleanup.
    for(void* a:Objects(Class("Campus.Common","CampusActorController")))
        if(Int(a,"GetInstanceID")==lease.identity.instance)return false;
    Trace("Cleanup confirmed: owned instance already absent; no destruction invoked");return true;
}
bool StartSpawn(const Preset& p,int reference) {
    if(!MutationsEnabled())return RejectMutation("spawn");
    if(!Ready()){notice="游戏运行时尚未就绪";return false;}
    if(Busy()){notice="正在加载人物，请勿重复提交";return false;}
    std::string error;if(!ValidatePreset(p,error)){notice=error;return false;}
    const int scene=PerformanceScene();if(!scene){notice="请进入拍摄或 Live";return false;}
    if(owned_.size()>=8){notice="实验版最多保留 8 个独立人物记录";return false;}
    if(!fieldValue_||!fields_||!fieldType_){notice="当前版本缺少安全读取构建结果所需的字段接口";return false;}
    void* primary=FindActor(reference,scene);if(!primary||!Bool(GameObject(primary),"get_activeInHierarchy")||!Bool(primary,"get_IsBuildSuccess")){notice="参考主角色尚未就绪";return false;}
    void* manager=Manager();if(!manager){notice="无法唯一确认人物管理器";return false;}
    const auto create=Method(objectClass_(manager),"CreateActor",2,false,{8,0x11});
    const auto allocate=On(manager,"GetFreeActorIndex");
    const auto build=Method(objectClass_(primary),"BuildAsync",3,false,{0x12,0x12,0x11});
    uint32_t align{};
    if(!create||!allocate||typeCode_(result_(allocate))!=8||!build||valueSize_(fromType_(param_(create,1)),&align)!=4){notice="独立创建或完整构建接口不匹配";return false;}
    void* taskClass=fromType_(result_(build));const auto getAwaiter=Method(taskClass,"GetAwaiter",0);
    void* awaiterClass=getAwaiter?fromType_(result_(getAwaiter)):nullptr;
    const auto completed=Method(awaiterClass,"get_IsCompleted",0),getResult=Method(awaiterClass,"GetResult",0);
    void* tupleClass=getResult?fromType_(result_(getResult)):nullptr;
    if(!getAwaiter||!completed||typeCode_(result_(completed))!=2||!IsBuildTuple(tupleClass)){notice="完整构建任务的返回契约不匹配，未创建人物";return false;}
    const auto baseline=CaptureActors();
    Pin settings(*this,Call(On(primary,"get_settings"),primary)),descriptor(*this,Descriptor(p));
    if(!settings.Get()||!descriptor.Get()||!assignable_(fromType_(param_(build,0)),objectClass_(settings.Get()))||!assignable_(fromType_(param_(build,1)),objectClass_(descriptor.Get()))){notice="原生构建设置或描述类型不兼容";return false;}
    const auto setId=Method(objectClass_(descriptor.Get()),"set_uniqueId",1,false,{0x0e});
    if(!setId){notice="无法设置独立人物描述身份";return false;}
    // A descriptor factory must not hand us an existing actor's mutable object.
    // Check before assigning uniqueId, not after capturing a modified baseline.
    for(void* a:Objects(Class("Campus.Common","CampusActorController"))) {
        Pin existing(*this,Call(On(a,"get_descriptor"),a));
        if(existing.Get()&&existing.Get()==descriptor.Get()){Fault("描述工厂返回既有角色共享对象，拒绝修改身份");return false;}
    }
    Pending next;next.active=true;next.started=std::chrono::steady_clock::now();next.resultClass=tupleClass;next.displayName=p.name;
    next.lease.manager=Int(manager,"GetInstanceID");next.lease.identity.scene=scene;next.lease.preset=p.id;
    static uint64_t serial{};next.lease.identity.unique="gakuactors-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++serial);
    Pin id(*this,stringNew_(next.lease.identity.unique.c_str()));void* idArgs[]{id.Get()};Call(setId,descriptor.Get(),idArgs);
    next.requested={p.character,Text(descriptor.Get(),"get_body"),Text(descriptor.Get(),"get_face"),Text(descriptor.Get(),"get_hair"),p.factory};
    if(next.requested.body.empty()||next.requested.face.empty()||next.requested.hair.empty()){notice="原生描述的身体、脸或头发资源为空";return false;}
    next.baseline=baseline;for(const auto& a:next.baseline)next.lease.preexisting.push_back(a.instance);
    if(!BaselinePreserved(next.baseline,CaptureActors())){Fault("准备模型描述时既有角色状态变化，未创建人物");return false;}
    Pose(primary,next.position,next.rotation);
    void* sourceClass=Class("System.Threading","CancellationTokenSource");Pin source(*this,sourceClass?objectNew_(sourceClass):nullptr);
    if(!source.Get())throw std::runtime_error("取消源分配失败");Call(On(source.Get(),".ctor"),source.Get());
    Pin token(*this,Call(On(source.Get(),"get_Token"),source.Get()));
    void* tokenClass=fromType_(param_(build,2));const int tokenSize=valueSize_(tokenClass,&align);
    if(!token.Get()||objectClass_(token.Get())!=tokenClass||ClassName(tokenClass)!="System.Threading.CancellationToken"||tokenSize<1||tokenSize>64)throw std::runtime_error("取消令牌契约不匹配");
    next.cancellation=newHandle_(source.Get(),false);if(!next.cancellation)throw std::runtime_error("取消源保留失败");
    pending_=std::move(next);
    try {
        Pin indexBox(*this,Call(allocate,manager));if(!indexBox.Get())throw std::runtime_error("独立索引为空");
        int index=*static_cast<int*>(unbox_(indexBox.Get()));pending_.lease.identity.index=index;
        if(AtIndex(manager,index))throw std::runtime_error("原生分配的索引仍被占用，已拒绝创建");
        if(!BaselinePreserved(pending_.baseline,CaptureActors()))throw std::runtime_error("索引分配后既有角色状态变化，未执行创建");
        Trace(("Spawn isolated BEGIN: index="+std::to_string(index)+" scene="+std::to_string(scene)+" baseline="+std::to_string(pending_.baseline.size())+" body="+pending_.requested.body+" face="+pending_.requested.face+" hair="+pending_.requested.hair).c_str());
        void* args[]{&index,&pending_.lease.identity.scene};Pin actor(*this,Call(create,manager,args));
        void* expected=Class("Campus.Common","CampusActorController");
        if(!actor.Get()||!expected||!assignable_(expected,objectClass_(actor.Get())))throw std::runtime_error("创建结果不是原生人物控制器");
        ActorIdentity identity;identity.instance=Int(actor.Get(),"GetInstanceID");identity.index=Int(actor.Get(),"get_index");identity.scene=SceneOf(GameObject(actor.Get()));
        if(!FreshIdentity(identity,index,scene,pending_.lease.preexisting))throw std::runtime_error("创建结果复用了既有实例或场景索引不符，未修改该对象");
        pending_.lease.identity.instance=identity.instance;pending_.lease.fresh=true;
        void* registered=AtIndex(manager,index);
        if(!registered||Int(registered,"GetInstanceID")!=identity.instance)throw std::runtime_error("独立控制器未注册到预期索引");
        owned_.push_back(pending_.lease);
        // Keep only our new object inactive until the full native build ends;
        // ordinary active-only actor scans must not bind an empty stand-in.
        SetActorActive(actor.Get(),false);
        if(!BaselinePreserved(pending_.baseline,CaptureActors()))throw std::runtime_error("创建独立控制器后既有角色状态变化，未执行构建");
        const auto ownBuild=Method(objectClass_(actor.Get()),"BuildAsync",3,false,{0x12,0x12,0x11});
        if(ownBuild!=build)throw std::runtime_error("新人物构建方法与预检不一致");
        void* buildArgs[]{settings.Get(),descriptor.Get(),unbox_(token.Get())};Pin task(*this,Call(ownBuild,actor.Get(),buildArgs));
        if(!task.Get())throw std::runtime_error("完整构建任务为空");
        pending_.task=newHandle_(task.Get(),false);if(!pending_.task)throw std::runtime_error("构建任务保留失败");
        Pin awaiter(*this,Call(getAwaiter,task.Get()));pending_.awaiter=awaiter.Get()?newHandle_(awaiter.Get(),false):nullptr;
        if(!pending_.awaiter)throw std::runtime_error("构建完成观察对象不可用");
        Trace(("Spawn isolated task started: instance="+std::to_string(identity.instance)+" index="+std::to_string(index)+" (CreateActor + actor.BuildAsync; no cache update)").c_str());
        notice="正在构建独立人物；加载完成前保持隐藏";return true;
    } catch(const std::exception& e) {
        try{CancelPending();}catch(...){}
        Fault(e.what());
        // An unobservable build might still be using its own new actor. Do
        // not destroy objects or release its cancellation root speculatively.
        return false;
    }
}
ModelReadiness InspectBuiltModel(void* actor,const ModelAssets& expected,bool verbose=true) {
    ModelReadiness m;m.buildSuccess=Bool(actor,"get_IsBuildSuccess");m.rendererActive=Bool(actor,"IsRendererActive");
    Pin parts(*this,Call(On(actor,"GetModelPartsList"),actor));
    for(void* part:Items(parts.Get())) {
        ++m.parts;const auto asset=Text(part,"get_assetName"),kind=Text(part,"get_partsId");
        const int bones=Int(part,"get_boneCount");
        Pin renderers(*this,FieldObject(part,"renderers")),materials(*this,FieldObject(part,"materials")),skin(*this,FieldObject(part,"skinningModel"));
        const int rc=renderers.Get()?Int(renderers.Get(),"get_Count"):0,mc=materials.Get()?Int(materials.Get(),"get_Count"):0;
        if(bones<0||bones>16384||rc<0||rc>1024||mc<0||mc>1024)throw std::runtime_error("原生部件数量异常");
        // Faces/materials can be batched into another native part. Require
        // each requested part's bones/active flag, and rendering/material
        // evidence across the complete actor rather than per-part counts.
        const bool ready=bones>0&&Bool(part,"get_rendererActive");
        m.bodyPart|=kind=="body"&&asset==expected.body&&ready;m.facePart|=kind=="face"&&asset==expected.face&&ready;m.hairPart|=kind=="hair"&&asset==expected.hair&&ready;
        m.nativeBones+=bones;m.nativeMaterials+=mc;m.nativeRenderers+=rc+(skin.Get()?1:0);
        if(verbose)Trace(("Spawn native model evidence: asset="+asset+" kind="+kind+" bone_entries="+std::to_string(bones)+" renderers="+std::to_string(rc)+" material_infos="+std::to_string(mc)+" vl_skinning="+std::to_string(skin.Get()!=nullptr)+" ready="+std::to_string(ready)).c_str());
    }
    return m;
}
void PollSpawn() {
    if(!pending_.active||!pending_.awaiter)return;
    bool complete=false;
    try {
        if(PerformanceScene()!=pending_.lease.identity.scene)CancelPending();
        void* awaiter=handleTarget_(pending_.awaiter);if(!awaiter)throw std::runtime_error("构建观察对象已丢失");
        complete=Bool(awaiter,"get_IsCompleted");
        if(!complete) {
            if(std::chrono::steady_clock::now()-pending_.started>std::chrono::seconds(60)){CancelPending();if(!faulted_)Fault("构建超时；等待取消完成，禁止再次创建");}
            return;
        }
        Pin tuple(*this,Call(On(awaiter,"GetResult"),awaiter));const auto result=DecodeBuildResult(tuple.Get(),pending_.resultClass);Pin actor(*this,result.actor);
        Trace(("Spawn isolated result: Item1="+std::to_string(result.success)+" controller="+std::to_string(actor.Get()!=nullptr)).c_str());
        if(pending_.cancelled) {
            const int instance=pending_.lease.identity.instance;
            if(!ReleaseLease(pending_.lease))throw std::runtime_error("取消后独立实例身份无法确认，保留记录并锁定操作");
            owned_.erase(std::remove_if(owned_.begin(),owned_.end(),[&](const auto& o){return o.identity.instance==instance;}),owned_.end());
            DropPending();notice="人物加入已取消";return;
        }
        if(!result.success||!actor.Get())throw std::runtime_error("完整构建返回失败或空控制器");
        const auto current=Identify(actor.Get());
        if(!LeaseMatches(pending_.lease,current))throw std::runtime_error("构建返回控制器不属于本次独立实例，拒绝移动或销毁返回对象");
        if(!ResolveLease(pending_.lease))throw std::runtime_error("管理器索引的实例归属已变化");
        if(!BaselinePreserved(pending_.baseline,CaptureActors()))throw std::runtime_error("既有角色集合或身份发生变化，已锁定后续操作");
        if(!current.build)throw std::runtime_error("原生 IsBuildSuccess=false");
        bool active=true;void* args[]{&active};Call(Method(objectClass_(actor.Get()),"SetRendererActive",1,false,{2}),actor.Get(),args);
        const auto evidence=InspectBuiltModel(actor.Get(),pending_.requested);std::string error;
        if(!ValidateModelReadiness(evidence,error))throw std::runtime_error(error);
        // The motion plugin identifies active actors from the face model or
        // the GameObject name. Use the actual built face, not preset category.
        try {
            if(!SetPresetActorName(actor.Get(),pending_.requested.face,current.index,pending_.displayName))
                Trace("Spawn actor name unavailable: face identity or Unity name setter unavailable");
        }catch(const std::exception& e){Trace((std::string("Spawn actor name unavailable: ")+e.what()).c_str());}
        SetPose(actor.Get(),pending_.position,pending_.rotation);SetActorActive(actor.Get(),true);
        if(!BaselinePreserved(pending_.baseline,CaptureActors()))throw std::runtime_error("激活独立人物后既有角色状态发生变化");
        for(auto& o:owned_)if(o.identity.instance==current.instance){std::memcpy(o.spawnPosition,pending_.position,sizeof(o.spawnPosition));o.ready=true;}
        Trace(("Spawn isolated READY: instance="+std::to_string(current.instance)+" index="+std::to_string(current.index)+" parts="+std::to_string(evidence.parts)+" native_bone_entries="+std::to_string(evidence.nativeBones)+" existing_actors_preserved=1").c_str());
        notice="独立人物已完成原生构建并显示在参考主角色位置；动作绑定仍需在动作插件中操作";DropPending();
    }catch(const std::exception& e) {
        if(complete) {
            bool released=false;try{released=ReleaseLease(pending_.lease);}catch(...){}
            const int instance=pending_.lease.identity.instance;
            if(released)owned_.erase(std::remove_if(owned_.begin(),owned_.end(),[&](const auto& o){return o.identity.instance==instance;}),owned_.end());
            const bool cancelled=pending_.cancelled;DropPending();
            if(cancelled&&released&&!faulted_)notice="人物加入已取消";else Fault(e.what());
        }else {try{CancelPending();}catch(...){}Fault(e.what());}
    }
}
bool RemoveActor(int instance) {
    if(!MutationsEnabled())return RejectMutation("remove");
    if(Busy()){notice="请等待人物构建结束";return false;}
    const auto it=std::find_if(owned_.begin(),owned_.end(),[&](const auto& a){return a.ready&&a.identity.instance==instance&&a.identity.scene==PerformanceScene();});
    if(it==owned_.end())return RejectMutation("unowned remove");
    if(!DestroyLease(*it)){Fault("移除时无法确认独立人物的当前身份");return false;}
    owned_.erase(it);notice="已移除本插件的独立人物";return true;
}
bool MoveActor(int instance,const float* position) {
    if(!MutationsEnabled())return RejectMutation("move");
    if(Busy()||!position)return false;
    const auto it=std::find_if(owned_.begin(),owned_.end(),[&](const auto& a){return a.ready&&a.identity.instance==instance&&a.identity.scene==PerformanceScene();});
    if(it==owned_.end())return RejectMutation("unowned move");
    for(size_t i=0;i<3;++i)if(!std::isfinite(position[i])||std::fabs(position[i])>10000)return false;
    void* actor=ResolveLease(*it);if(!actor){Fault("定位时无法确认独立人物的当前身份");return false;}
    float old[3],rotation[4];Pose(actor,old,rotation);SetPose(actor,position,rotation);return true;
}

bool ResetActorPosition(int instance) {
    if(!MutationsEnabled())return RejectMutation("reset position");
    const auto it=std::find_if(owned_.begin(),owned_.end(),[&](const auto& a){return a.ready&&a.identity.instance==instance&&a.identity.scene==PerformanceScene();});
    if(it==owned_.end())return RejectMutation("unowned reset position");
    return MoveActor(instance,it->spawnPosition);
}
bool TurnActor(int instance,float degrees,bool absolute=false) {
    if(!MutationsEnabled())return RejectMutation("turn");
    if(Busy()||!std::isfinite(degrees)||std::fabs(degrees)>360.0f)return false;
    const auto it=std::find_if(owned_.begin(),owned_.end(),[&](const auto& a){return a.ready&&a.identity.instance==instance&&a.identity.scene==PerformanceScene();});
    if(it==owned_.end())return RejectMutation("unowned turn");
    void* actor=ResolveLease(*it);if(!actor){Fault("转向时无法确认独立人物的当前身份");return false;}
    float position[3],q[4];Pose(actor,position,q);
    if(absolute)degrees-=std::atan2(2.0f*(q[3]*q[1]+q[0]*q[2]),1.0f-2.0f*(q[1]*q[1]+q[2]*q[2]))*57.29577951308232f;
    const float half=degrees*0.008726646259971648f,c=std::cos(half),s=std::sin(half);
    const float turned[4]{c*q[0]+s*q[2],c*q[1]+s*q[3],c*q[2]-s*q[0],c*q[3]-s*q[1]};
    SetPose(actor,position,turned);notice="人物朝向已调整";return true;
}

// Position controls for a roster snapshot. A fresh scene/index/unique-id
// match is required for game-owned actors; owned actors retain lease checks.
void* ResolveScenePoseActor(const SceneActor& expected) {
    if(!PerformanceScene()||expected.scene!=PerformanceScene()||!expected.identityValid||expected.uniqueId.empty())return nullptr;
    void* actor=FindActor(expected.instance,expected.scene);if(!actor)return nullptr;
    const auto identity=Identify(actor);
    if(identity.instance!=expected.instance||identity.scene!=expected.scene||identity.index!=expected.actorIndex||identity.unique!=expected.uniqueId||!identity.active)return nullptr;
    if(expected.owned) {
        const auto it=std::find_if(owned_.begin(),owned_.end(),[&](const auto& lease){return lease.ready&&lease.identity.instance==expected.instance&&lease.identity.scene==expected.scene;});
        if(it==owned_.end()||!LeaseMatches(*it,identity)||ResolveLease(*it)!=actor)return nullptr;
    }
    return actor;
}
bool ReadScenePose(const SceneActor& actor,float* position,float* rotation) {
    void* object=ResolveScenePoseActor(actor);if(!object)return false;Pose(object,position,rotation);return true;
}
bool CaptureScenePoseBaseline(const SceneActor& expected,void* actor) {
    auto it=std::find_if(poseBaselines_.begin(),poseBaselines_.end(),[&](const auto& b){return b.instance==expected.instance&&b.scene==expected.scene&&b.index==expected.actorIndex&&b.unique==expected.uniqueId;});
    if(it!=poseBaselines_.end())return true;
    PoseBaseline baseline;baseline.instance=expected.instance;baseline.scene=expected.scene;baseline.index=expected.actorIndex;baseline.unique=expected.uniqueId;
    float rotation[4];Pose(actor,baseline.position,rotation);
    if(expected.owned)for(const auto& lease:owned_)if(lease.ready&&lease.identity.instance==expected.instance&&lease.identity.scene==expected.scene){std::memcpy(baseline.position,lease.spawnPosition,sizeof(baseline.position));break;}
    poseBaselines_.push_back(std::move(baseline));return true;
}
bool MoveSceneActor(const SceneActor& expected,const float* position) {
    if(Faulted()||Busy()||!position){notice="人物操作繁忙或被锁定";return false;}
    for(size_t i=0;i<3;++i)if(!std::isfinite(position[i])||std::fabs(position[i])>10000){notice="位置数值无效";return false;}
    void* actor=ResolveScenePoseActor(expected);if(!actor){notice="人物身份或场景已变化，请重新扫描";return false;}
    CaptureScenePoseBaseline(expected,actor);float old[3],rotation[4];Pose(actor,old,rotation);SetPose(actor,position,rotation);notice="人物位置已提交";return true;
}
bool ResetSceneActor(const SceneActor& expected,float* target=nullptr) {
    if(Faulted()||Busy()){notice="人物操作繁忙或被锁定";return false;}
    void* actor=ResolveScenePoseActor(expected);if(!actor){notice="人物身份或场景已变化，请重新扫描";return false;}
    if(!CaptureScenePoseBaseline(expected,actor))return false;
    const auto it=std::find_if(poseBaselines_.begin(),poseBaselines_.end(),[&](const auto& b){return b.instance==expected.instance&&b.scene==expected.scene&&b.index==expected.actorIndex&&b.unique==expected.uniqueId;});
    if(it==poseBaselines_.end())return false;
    if(target)std::memcpy(target,it->position,sizeof(it->position));
    float current[3],rotation[4];Pose(actor,current,rotation);SetPose(actor,it->position,rotation);notice="人物位置已重置";return true;
}
bool TurnSceneActor(const SceneActor& expected,float degrees,bool absolute) {
    if(Faulted()||Busy()||!std::isfinite(degrees)||std::fabs(degrees)>360){notice="人物操作繁忙或朝向数值无效";return false;}
    void* actor=ResolveScenePoseActor(expected);if(!actor){notice="人物身份或场景已变化，请重新扫描";return false;}
    CaptureScenePoseBaseline(expected,actor);float position[3],q[4];Pose(actor,position,q);
    if(absolute)degrees-=std::atan2(2.0f*(q[3]*q[1]+q[0]*q[2]),1.0f-2.0f*(q[1]*q[1]+q[2]*q[2]))*57.29577951308232f;
    const float half=degrees*0.008726646259971648f,c=std::cos(half),s=std::sin(half);
    const float turned[4]{c*q[0]+s*q[2],c*q[1]+s*q[3],c*q[2]-s*q[0],c*q[3]-s*q[1]};
    SetPose(actor,position,turned);notice="人物朝向已提交";return true;
}
