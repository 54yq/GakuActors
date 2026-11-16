// Live presenter pause path follows GakuMotion's verified Live control flow.
// The operation pauses the whole Live timeline (including its audio), and
// always re-discovers the presenter in the current scene on the Unity thread.
struct LivePlaybackObservation { int presenter{}; bool paused{}; float time{},duration{}; };
bool ObserveLivePlayback(LivePlaybackObservation& result,void** presenterOut=nullptr) {
    const int scene=PerformanceScene();if(!scene)return false;
    void* sceneClass=Class("Campus.Live","LiveScenePresenter"),*presenterClass=Class("Campus.Live","LivePresenter"),*playerClass=Class("Campus.Live","LivePlayerPresenter"),*executionClass=Class("Campus.Live.Data","LiveExecutionData");
    void* componentClass=Class("UnityEngine","Component");
    if(!sceneClass||!presenterClass||!playerClass||!executionClass||!componentClass||!assignable_(componentClass,sceneClass))return false;
    void* owner=nullptr;
    for(void* candidate:Objects(sceneClass)) {
        if(objectClass_(candidate)!=sceneClass)continue;
        void* go=GameObject(candidate);if(SceneOf(go)!=scene||!Bool(go,"get_activeInHierarchy"))continue;
        if(owner)return false;owner=candidate;
    }
    if(!owner)return false;
    Pin presenter(*this,DiagnosticReferenceField(owner,"_live",presenterClass));
    Pin player(*this,DiagnosticReferenceField(owner,"_livePlayer",playerClass));
    if(!presenter.Get()||objectClass_(presenter.Get())!=presenterClass||!player.Get()||objectClass_(player.Get())!=playerClass)return false;
    const auto executionMethod=On(player.Get(),"get_ExecutionData");
    if(!executionMethod||!result_(executionMethod)||typeCode_(result_(executionMethod))!=0x12||fromType_(result_(executionMethod))!=executionClass)return false;
    Pin execution(*this,Call(executionMethod,player.Get()));if(!execution.Get()||objectClass_(execution.Get())!=executionClass)return false;
    auto number=[&](const char* name,float& out){const auto m=On(player.Get(),name);if(!m||!result_(m)||typeCode_(result_(m))!=0x0c)return false;Pin value(*this,Call(m,player.Get()));if(!value.Get())return false;out=*static_cast<float*>(unbox_(value.Get()));return std::isfinite(out);};
    result.paused=Bool(execution.Get(),"get_IsPause");
    if(!number("get_TimelineTime",result.time)||!number("get_Duration",result.duration)||result.time<0||result.duration<=0||result.time>result.duration+1)return false;
    result.presenter=Int(owner,"GetInstanceID");if(!result.presenter)return false;
    if(presenterOut)*presenterOut=presenter.Get();
    return true;
}
bool SetLivePlaybackPaused(int expectedPresenter,bool paused) {
    LivePlaybackObservation before;void* target=nullptr;
    if(!ObserveLivePlayback(before,&target)||before.presenter!=expectedPresenter||!target)return false;
    if(before.paused==paused)return true;
    Pin targetPin(*this,target);if(!targetPin.Get())return false;
    const auto method=On(targetPin.Get(),paused?"Pause":"UnPause");
    if(!method||!result_(method)||typeCode_(result_(method))!=0x01)return false;
    Call(method,targetPin.Get());
    LivePlaybackObservation after;
    return ObserveLivePlayback(after)&&after.presenter==expectedPresenter&&after.paused==paused;
}
