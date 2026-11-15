// Included in NativeApi; every method runs on the Unity callback thread.
bool ModelTargetMatches() {
    if(!modelEdit_.manager||!modelEdit_.identity.instance||!PerformanceScene()||PerformanceScene()!=modelEdit_.identity.scene)return false;
    void* manager=Manager(modelEdit_.manager);if(!manager)return false;
    void* actor=AtIndex(manager,modelEdit_.identity.index);if(!actor)return false;
    const auto now=Identify(actor);const auto& expected=modelEdit_.identity;
    return now.instance==expected.instance&&now.index==expected.index&&now.scene==expected.scene&&now.unique==expected.unique;
}
void ClearModelTask() {
    for(void* h:{modelEdit_.task,modelEdit_.awaiter,modelEdit_.cancellation})if(h)freeHandle_(h);
    modelEdit_.task=modelEdit_.awaiter=modelEdit_.cancellation=nullptr;modelEdit_.active=false;
}
void DropModelEdit() {
    ClearModelTask();if(modelEdit_.original)freeHandle_(modelEdit_.original);modelEdit_={};
}
void CancelModelEdit() {
    if(modelEdit_.active) {
        modelEdit_.cancelled=true;
        if(modelEdit_.cancellation)try {void* source=handleTarget_(modelEdit_.cancellation);if(source)Call(On(source,"Cancel"),source);}catch(...){}
        Trace("Model edit: scene ended; cancellation requested, awaiting completion");
    } else if(modelEdit_.original)DropModelEdit();
}
bool StartActorModel(int instance,int expectedScene,const std::string& expectedUnique,const std::array<std::string,3>& parts,bool restore) {
    // UpdateActorAsync changed the game's actor identity in a real session.
    // Neither reapplying nor restoring by the former identity is safe until
    // its ownership and lifecycle are understood; reject both at the boundary.
    if(!ActorModelUpdatesAvailable()) {
        notice="人物模型替换暂时停用：上次更新导致人物实例变化，请重新进入场景并查看日志";
        Trace((std::string("Model edit BLOCKED: safety lock mode=")+(restore?"restore":"apply")+" instance="+std::to_string(instance)).c_str());
        return false;
    }
    if(Busy()){notice="人物操作正在进行";return false;}
    if(!PerformanceScene()||!Ready()){notice="请先进入拍摄或 Live 场景";return false;}
    if(faulted_)return RejectMutation("model edit");
    if(modelEdit_.original&&modelEdit_.identity.instance!=instance){notice="请先恢复先前编辑的人物";return false;}
    try {
        void* actor=FindActor(instance,PerformanceScene());void* manager=Manager();
        if(!actor||!manager)throw std::runtime_error("无法确认人物或唯一管理器");
        auto identity=Identify(actor);
        if(identity.scene!=expectedScene||identity.unique!=expectedUnique)throw std::runtime_error("选中人物已变化，请重新选择");
        if(!identity.instance||identity.unique.empty()||(!restore&&(!identity.active||!identity.build)))throw std::runtime_error("人物尚未构建完成或身份为空");
        void* registered=AtIndex(manager,identity.index);
        if(!registered||Int(registered,"GetInstanceID")!=identity.instance)throw std::runtime_error("人物与管理器索引不一致");
        if(modelEdit_.original&&!ModelTargetMatches())throw std::runtime_error("原人物身份已经改变，拒绝操作");
        if(restore&&!modelEdit_.original)throw std::runtime_error("没有可恢复的原始模型记录");
        Pin current(*this,Call(On(actor,"get_descriptor"),actor));
        const std::array<std::string,3> before{Text(current.Get(),"get_body"),Text(current.Get(),"get_face"),Text(current.Get(),"get_hair")};
        Pin target(*this,restore?handleTarget_(modelEdit_.original):PrepareModelDescriptor(current.Get(),parts));
        if(!target.Get())throw std::runtime_error("目标模型描述为空");
        const std::array<std::string,3> targetParts{Text(target.Get(),"get_body"),Text(target.Get(),"get_face"),Text(target.Get(),"get_hair")};
        if(!restore&&targetParts!=parts)throw std::runtime_error("目标模型描述不匹配");
        const auto update=Method(objectClass_(manager),"UpdateActorAsync",3,false,{8,0x12,0x11});
        if(!update||!assignable_(fromType_(param_(update,1)),objectClass_(target.Get())))throw std::runtime_error("游戏人物更新接口不匹配");
        void* taskClass=fromType_(result_(update));const auto getAwaiter=Method(taskClass,"GetAwaiter",0);
        void* awaiterClass=getAwaiter?fromType_(result_(getAwaiter)):nullptr;
        const auto completed=Method(awaiterClass,"get_IsCompleted",0),getResult=Method(awaiterClass,"GetResult",0);
        void* tupleClass=getResult?fromType_(result_(getResult)):nullptr;
        if(!getAwaiter||!completed||typeCode_(result_(completed))!=2||!IsBuildTuple(tupleClass))throw std::runtime_error("人物更新任务契约不匹配");
        void* sourceClass=Class("System.Threading","CancellationTokenSource");Pin source(*this,sourceClass?objectNew_(sourceClass):nullptr);
        if(!source.Get())throw std::runtime_error("取消源分配失败");Call(On(source.Get(),".ctor"),source.Get());
        Pin token(*this,Call(On(source.Get(),"get_Token"),source.Get()));uint32_t align{};
        void* tokenClass=fromType_(param_(update,2));const int tokenSize=valueSize_(tokenClass,&align);
        if(!token.Get()||objectClass_(token.Get())!=tokenClass||ClassName(tokenClass)!="System.Threading.CancellationToken"||tokenSize<1||tokenSize>64)throw std::runtime_error("取消令牌不匹配");
        if(!modelEdit_.original) {
            Pin originalCopy(*this,PrepareModelDescriptor(current.Get(),before));
            modelEdit_.original=newHandle_(originalCopy.Get(),false);if(!modelEdit_.original)throw std::runtime_error("无法保留原始模型描述");
            modelEdit_.manager=Int(manager,"GetInstanceID");modelEdit_.identity=identity;
        }
        modelEdit_.baseline=CaptureActors();modelEdit_.baseline.erase(std::remove_if(modelEdit_.baseline.begin(),modelEdit_.baseline.end(),[&](const auto& a){return a.instance==identity.instance;}),modelEdit_.baseline.end());
        modelEdit_.requested=targetParts;modelEdit_.restoring=restore;modelEdit_.cancelled=false;modelEdit_.resultClass=tupleClass;
        modelEdit_.cancellation=newHandle_(source.Get(),false);if(!modelEdit_.cancellation)throw std::runtime_error("无法保留取消源");
        modelEdit_.started=std::chrono::steady_clock::now();modelEdit_.active=true;
        Trace(("Model edit BEGIN: mode="+std::string(restore?"restore":"apply")+" instance="+std::to_string(instance)+" index="+std::to_string(identity.index)+" scene="+std::to_string(identity.scene)+" unique="+identity.unique+" old="+before[0]+","+before[1]+","+before[2]+" requested="+targetParts[0]+","+targetParts[1]+","+targetParts[2]).c_str());
        int index=identity.index;void* args[]{&index,target.Get(),unbox_(token.Get())};
        Pin task(*this,Call(update,manager,args));if(!task.Get())throw std::runtime_error("人物更新任务为空");
        modelEdit_.task=newHandle_(task.Get(),false);if(!modelEdit_.task)throw std::runtime_error("无法保留更新任务");
        Pin awaiter(*this,Call(getAwaiter,task.Get()));modelEdit_.awaiter=awaiter.Get()?newHandle_(awaiter.Get(),false):nullptr;
        if(!modelEdit_.awaiter)throw std::runtime_error("无法观察更新任务");
        notice=restore?"正在恢复人物模型":"正在更新人物模型";return true;
    }catch(const std::exception& e) {
        Trace((std::string("Model edit ERROR: ")+e.what()).c_str());
        if(modelEdit_.active){Fault(std::string("人物更新任务状态不明：")+e.what());}
        else {if(modelEdit_.cancellation){freeHandle_(modelEdit_.cancellation);modelEdit_.cancellation=nullptr;}notice=std::string("人物模型操作未开始：")+e.what();}
        return false;
    }
}
void PollModelEdit() {
    if(!modelEdit_.active||!modelEdit_.awaiter)return;
    bool complete=false;
    try {
        if(PerformanceScene()!=modelEdit_.identity.scene&&!modelEdit_.cancelled)CancelModelEdit();
        void* awaiter=handleTarget_(modelEdit_.awaiter);if(!awaiter)throw std::runtime_error("更新任务观察对象丢失");
        complete=Bool(awaiter,"get_IsCompleted");
        if(!complete){if(std::chrono::steady_clock::now()-modelEdit_.started>std::chrono::seconds(60)&&!modelEdit_.cancelled){CancelModelEdit();Fault("人物模型更新超时，等待任务结束");}return;}
        Pin tuple(*this,Call(On(awaiter,"GetResult"),awaiter));const auto result=DecodeBuildResult(tuple.Get(),modelEdit_.resultClass);Pin actor(*this,result.actor);
        if(modelEdit_.cancelled)throw std::runtime_error("更新已取消；需要重新进入场景确认人物状态");
        if(!result.success||!actor.Get())throw std::runtime_error("游戏更新返回失败或空人物");
        if(!ModelTargetMatches()||Int(actor.Get(),"GetInstanceID")!=modelEdit_.identity.instance)throw std::runtime_error("更新后人物实例或管理器索引改变");
        if(!BaselinePreserved(modelEdit_.baseline,CaptureActors()))throw std::runtime_error("其他场景人物状态发生变化");
        ModelAssets expected{"",modelEdit_.requested[0],modelEdit_.requested[1],modelEdit_.requested[2],""};
        const auto readiness=InspectBuiltModel(actor.Get(),expected);std::string error;
        if(!ValidateModelReadiness(readiness,error))throw std::runtime_error(error);
        const bool restored=modelEdit_.restoring;ClearModelTask();
        Trace((std::string("Model edit READY: mode=")+(restored?"restore":"apply")+" instance="+std::to_string(modelEdit_.identity.instance)).c_str());
        if(restored){DropModelEdit();notice="已恢复人物原始模型";}else notice="人物模型已更新；可使用恢复原模型";
    }catch(const std::exception& e) {
        Trace((std::string("Model edit FAILED: ")+e.what()).c_str());
        if(complete) {
            const bool cancelled=modelEdit_.cancelled,wasRestoring=modelEdit_.restoring;const auto identity=modelEdit_.identity;
            ClearModelTask();
            if(cancelled&&PerformanceScene()!=identity.scene){DropModelEdit();notice="场景已离开，人物模型更新已取消";}
            else {
                bool safe=false;try{const bool target=ModelTargetMatches();const bool preserved=target&&!wasRestoring&&!cancelled&&BaselinePreserved(modelEdit_.baseline,CaptureActors());safe=ModelRecoveryPermitted(true,wasRestoring,cancelled,target,preserved);}catch(...){}
                if(safe) {
                    Trace("Model edit: attempting automatic restore after completed failure");
                    if(StartActorModel(identity.instance,identity.scene,identity.unique,{},true)){notice="模型更新失败，正在尝试恢复原模型";return;}
                }
                notice=std::string("人物模型更新失败，请检查日志并尝试恢复原模型：")+e.what();
            }
        }
        else Fault(std::string("人物模型任务状态不明：")+e.what());
    }
}
