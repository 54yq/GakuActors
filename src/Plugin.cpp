#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GakuPlugin.h>
#include <GakuPaths.h>
#include "PresetStore.hpp"
#include "NativeApi.hpp"
#include "ActorUi.hpp"
#include "SceneGate.hpp"
#include "StageLoadHook.hpp"
#include "PreLiveLoadHook.hpp"
#include "StageChoiceStore.hpp"
#include "DesktopProtocol.hpp"
#include "DesktopListener.hpp"
#include "FreeCamera.hpp"
#include "GakuDiagnostics.hpp"
#include "CatalogStartupRead.hpp"
#include "ResourceOwnership.hpp"
#include <atomic>
#include <mutex>
#include <memory>
#include <cmath>

#ifdef GKMS_ACTORS_INTEGRATED
extern "C" GakuStatus GakuActors_CameraCoreQuery(uint32_t,uint32_t,GakuPluginDescriptor*);
#endif

namespace GakuActors {
struct State {
    GakuHostApi host{};GakuLogService log{};GakuRuntimeService runtime{};
    GakuDesktopUiService desktop{};GakuDesktopWindowService window{};GakuHotkeyService hotkeys{};
    GakuHookService hooks{};GakuMainThreadService mainThread{};StageLoadHook stageHook;PreLiveLoadHook preLiveHook;StageChoiceStore stageStore;
    GakuPathsV1 paths{};NativeApi native;PresetStore store;ActorUi ui;UiSnapshot snapshot;FreeCamera freeCamera;
    DesktopListener desktopListener;
    std::string thumbnailId,thumbnailData,thumbnailStatus,thumbnailHeadData,thumbnailHeadStatus;
    std::string legacyThumbnailId,legacyThumbnailData,legacyThumbnailStatus,legacyHeadData,legacyHeadStatus;
    uint64_t legacyThumbnailRevision{};
    std::mutex mutex;std::deque<Command> commands;
    std::atomic<bool> ready{},stopped{};bool catalogRead{},catalogRequested{},sceneRequested{},wasAdmitted{},diagnostics{};
    SceneGate gate;
    int livePauseScene{},livePauseOwner{};bool ownsLivePause{};std::chrono::steady_clock::time_point nextLivePauseObserve{};
    CatalogStartupRead catalogStartup;bool catalogManualRequest{};
    int resourceScene{},stageChoice{};bool stageAttempted{};std::string resourceActiveScene;
    std::vector<Preset> modelQueue;size_t modelCursor{},modelFailures{};
    bool dialogOpen{false},preLiveDialogOpen{false},preLiveCatalogAttempted{false},preLiveLogged{false},preLiveResourceScan{false},desktopResourceScan{false};
    std::atomic<uint64_t> preLiveUiSeenMs{},desktopUiSeenMs{};
    std::chrono::steady_clock::time_point nextScan{},nextCatalog{};
    std::chrono::steady_clock::time_point nextPreLiveProbe{},lastPreLiveSlowLog{},nextPreLiveModelMonitor{};
    std::string lastPreLiveModelLog;
    struct Playback {uint32_t abiVersion,structSize;uint32_t(*isPlaying)();} playback{};
    struct VisualHandoff {uint32_t abiVersion,structSize;uint32_t(*request)(uint32_t);uint32_t(*isReleased)();} handoff{};
    VisualHandoff cameraHandoff{};
    bool handoffRequested{},resourceEnablePending{};
    std::chrono::steady_clock::time_point resourceEnableDeadline{};
    bool cameraHandoffRequested{},freeCameraPending{};
    std::chrono::steady_clock::time_point freeCameraDeadline{};
    struct PoseCheck {SceneActor actor;float position[3]{},rotation[4]{};std::chrono::steady_clock::time_point due;std::string operation;};
    std::vector<PoseCheck> poseChecks;
    std::chrono::steady_clock::time_point lastMoveLog{},lastFreeCameraLog{};
#ifdef GKMS_ACTORS_INTEGRATED
    GakuPluginDescriptor cameraCore{};void* cameraCoreState{};bool cameraCoreActivated{};
#endif
};
static State state;
template<class T> bool Query(State& s,const char* name,T& value) {
    T candidate{};if(s.host.queryService(s.host.hostContext,name,1,sizeof(candidate),&candidate)!=GAKU_OK||
        candidate.abiVersion!=1||candidate.structSize<sizeof(candidate))return false;
    value=candidate;return true;
}
static void Log(State& s,const std::string& message,GakuLogLevel level=GAKU_LOG_INFO) {
    if(level==GAKU_LOG_INFO&&!s.diagnostics)return;
    if(s.log.write)s.log.write(s.log.context,level,("[GakuActors] "+message).c_str());
}
// Only adjacent placement or yaw updates for the same actor may coalesce. A reset,
// actor change, or any other command remains an ordering barrier.
static void QueueCommand(State& s,Command&& command) {
    if((command.action==Action::Move||command.action==Action::SetYaw)&&!s.commands.empty()&&s.commands.back().action==command.action&&
       s.commands.back().instance==command.instance) s.commands.back()=std::move(command);
    else s.commands.push_back(std::move(command));
}
static std::string DesktopMessage(State& s,const std::string& request) {
    try {
        if(request.size()>desktop_protocol::kMaxRequestBytes)return {};
        const auto payload=Json::parse(request,nullptr,false);
        if(!payload.is_object()||!payload.contains("version")||payload["version"]!=desktop_protocol::kVersion||
           !payload.contains("action")||!payload["action"].is_string())return {};
        s.desktopUiSeenMs.store(static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count()),std::memory_order_release);
        UiSnapshot snapshot;std::string thumbnailId,thumbnailData,thumbnailStatus,thumbnailHeadData,thumbnailHeadStatus;
        const bool thumbnailRequest=payload["action"]=="thumbnail_status";
        bool accepted=payload["action"]=="snapshot"||thumbnailRequest;
        {
            std::lock_guard<std::mutex> lock(s.mutex);
            if(!accepted&&!s.stopped&&s.commands.size()<128) {
                Command command{Action::Reload};
                if(desktop_protocol::Decode(request,s.snapshot,command)){
                    if(command.action==Action::LoadThumbnail){s.thumbnailId=command.preset.id;s.thumbnailData.clear();s.thumbnailStatus="读取中";s.thumbnailHeadData.clear();s.thumbnailHeadStatus="读取中";}
                    QueueCommand(s,std::move(command));accepted=true;
                }
            }
            snapshot=s.snapshot;
            if(thumbnailRequest){thumbnailId=s.thumbnailId;thumbnailData=s.thumbnailData;thumbnailStatus=s.thumbnailStatus;thumbnailHeadData=s.thumbnailHeadData;thumbnailHeadStatus=s.thumbnailHeadStatus;}
        }
        Json result{{"version",desktop_protocol::kVersion},{"accepted",accepted},{"snapshot",desktop_protocol::Snapshot(snapshot)}};
        if(thumbnailRequest)result["thumbnail"]={{"id",thumbnailId},{"status",thumbnailStatus},{"data",thumbnailData},{"head_status",thumbnailHeadStatus},{"head_data",thumbnailHeadData}};
        return result.dump();
    } catch(...) {return {};}
}
static std::string DesktopSessionMessage(State& s,bool& resourceSession,const std::string& input) {
    auto result=DesktopMessage(s,input);
    const auto request=Json::parse(input,nullptr,false),reply=Json::parse(result,nullptr,false);
    if(request.is_object()&&reply.is_object()&&reply.value("accepted",false)) {
        const auto action=request.value("action",std::string{});
        if((action=="enable_resources"&&request.value("enabled",false))||action=="hide_resource"||action=="camera_color")resourceSession=true;
    }
    return result;
}
static void DesktopSessionDisconnected(State& s,bool& resourceSession) {
    if(!resourceSession)return;
    resourceSession=false;
    std::lock_guard<std::mutex> lock(s.mutex);
    if(!s.stopped)QueueCommand(s,Command{Action::EnableResourceControls,{},0});
}
static std::filesystem::path ConfigRoot(State& s) {
    if(s.paths.getRoot&&s.paths.ensureRoot) {
        uint32_t count{};const auto result=s.paths.getRoot(s.paths.context,GAKU_PATH_CONFIGS,nullptr,0,&count);
        if(result!=GAKU_E_SIZE||!count||count>32768)return {};
        std::wstring path(count,L'\0');
        if(s.paths.getRoot(s.paths.context,GAKU_PATH_CONFIGS,path.data(),count,&count)!=GAKU_OK)return {};
        path.resize(count-1);return path;
    }
    return s.runtime.localRoot?std::filesystem::path(s.runtime.localRoot)/L"Configs"/L"gakuactors":std::filesystem::path{};
}
static void Show(void*) {
    if(!state.stopped&&state.window.requestShow)state.window.requestShow(state.window.context);
}
static GakuStatus ActivateImpl(const GakuHostApi* host,void** output) {
    if(!host||!output||host->abiVersion!=1||host->structSize<sizeof(*host)||!host->queryService)return GAKU_E_INVALID_ARGUMENT;
    auto& s=state;s.host=*host;
    if(!Query(s,GAKU_SERVICE_LOG,s.log)||!Query(s,GAKU_SERVICE_RUNTIME,s.runtime)||!s.runtime.getGameAssembly)return GAKU_E_UNAVAILABLE;
    s.diagnostics=GakuDiagnostics::Enabled(s.runtime.gameRoot);
    Query(s,GAKU_SERVICE_DESKTOP_UI,s.desktop);Query(s,GAKU_SERVICE_DESKTOP_WINDOW,s.window);
    Query(s,GAKU_SERVICE_HOTKEYS,s.hotkeys);Query(s,GAKU_SERVICE_PATHS,s.paths);
    Query(s,GAKU_SERVICE_HOOKS,s.hooks);Query(s,GAKU_SERVICE_MAIN_THREAD,s.mainThread);
    s.stageHook.Configure(&s.native,&s.gate,s.hooks,s.mainThread);
    s.preLiveHook.Configure(&s.native,s.hooks,s.mainThread);
    s.freeCamera.Configure(&s.native,s.hooks,s.mainThread);
    s.native.trace=s.diagnostics?+[](const char* text){Log(state,text);}:nullptr;
    const auto root=ConfigRoot(s);if(!root.empty()){s.store.SetFile(root/L"actor_presets.json");s.stageStore.SetFile(root/L"stage_choice.json");}
    if(!s.stageStore.Load(s.stageChoice))s.snapshot.stageStatus="摄影背景设置不可读或损坏，已保留原文件";
    s.snapshot.stageChoice=s.stageChoice;
    s.store.Load();s.snapshot.writable=s.store.Writable();s.snapshot.presets=s.store.presets;s.snapshot.storeNotice=s.store.notice;
    if(s.hotkeys.registerHotkey) {
        const GakuHotkeyBinding key{1,sizeof(key),"show-actors","场景人物管理",'J',GAKU_HOTKEY_MOD_CTRL,Show,nullptr};
        s.hotkeys.registerHotkey(s.hotkeys.context,&key);
    }
#ifdef GKMS_ACTORS_INTEGRATED
    if(GakuActors_CameraCoreQuery(GAKU_ABI_VERSION,sizeof(s.cameraCore),&s.cameraCore)==GAKU_OK&&s.cameraCore.activate){
        try{s.cameraCoreActivated=s.cameraCore.activate(host,&s.cameraCoreState)==GAKU_OK;}catch(...){s.cameraCoreActivated=false;}
    }
    Log(s,s.cameraCoreActivated?"Integrated camera core activated":"Integrated camera core unavailable; other resource controls remain available",s.cameraCoreActivated?GAKU_LOG_INFO:GAKU_LOG_WARNING);
#endif
    // The listener is single-client. Record resource mutations on its thread;
    // enqueue cleanup on disconnect so Unity and handoff calls stay on the game thread.
    auto desktopResources=std::make_shared<bool>(false);
    if(!s.desktopListener.Start([&s,desktopResources](const std::string& input){return DesktopSessionMessage(s,*desktopResources,input);},
        [&s,desktopResources]{DesktopSessionDisconnected(s,*desktopResources);}))Log(s,"独立窗口连接服务不可用",GAKU_LOG_WARNING);
    *output=&s;Log(s,"独立人物插件已激活；不依赖 GakuMotion，不接管角色帧更新");return GAKU_OK;
}
static void RuntimeReadyImpl(void* context) {
    auto& s=*static_cast<State*>(context);if(s.stopped)return;
#ifdef GKMS_ACTORS_INTEGRATED
    if(s.cameraCoreActivated&&s.cameraCore.onRuntimeReady){
        try{s.cameraCore.onRuntimeReady(s.cameraCoreState);}catch(...){Log(s,"Integrated camera startup failed",GAKU_LOG_ERROR);}
    }
#endif
    const bool ready=s.native.Resolve(s.runtime.getGameAssembly());s.ready.store(ready,std::memory_order_release);
    s.catalogStartup={};s.catalogRead=false;
    Log(s,ready?"IL2CPP exports resolved; catalog auto-read queued for the first settled Photography/Live scene":"IL2CPP exports unavailable",ready?GAKU_LOG_INFO:GAKU_LOG_WARNING);
}
static uint64_t MonotonicMs(){return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count());}
static void ActorTickImpl(void* context,void* actor,float) {
    auto& s=*static_cast<State*>(context);if(s.stopped||!s.ready.load(std::memory_order_acquire)||!actor)return;
    // Read only this actual callback actor's Unity owner/active scene. Never
    // enumerate models, request Master data or create game objects here.
    const auto scene=s.native.ObserveActorScene(actor);
    s.gate.Observe(scene.owner,scene.handle,scene.active,MonotonicMs());
}
static void TickImpl(void* context,float) {
    auto& s=*static_cast<State*>(context);if(s.stopped)return;
    std::deque<Command> commands;
    {std::lock_guard<std::mutex> lock(s.mutex);commands.swap(s.commands);}
    UiSnapshot next;
    {std::lock_guard<std::mutex> lock(s.mutex);next=s.snapshot;}
    if(!s.ready.load(std::memory_order_acquire)) {
        // Storage is independent of IL2CPP. Never defer scene commands to a later scene.
        for(const auto& command:commands) {
            switch(command.action) {
            case Action::Reload:s.store.Load();break;
            case Action::Save:s.store.Save(command.preset);break;
            case Action::DeletePreset:s.store.Remove(command.preset.id);break;
            default:next.notice="游戏尚未就绪，场景操作未执行";break;
            }
        }
        next.runtime=false;next.admitted=false;next.activeSceneName.clear();next.presets=s.store.presets;
        next.storeNotice=s.store.notice;next.writable=s.store.Writable();
        std::lock_guard<std::mutex> lock(s.mutex);s.snapshot=std::move(next);return;
    }
    next.runtime=true;
    const auto now=std::chrono::steady_clock::now();
    auto publish=[&]{
        next.busy=s.native.Busy();next.mutationsEnabled=s.native.MutationsEnabled();next.faulted=s.native.Faulted();next.notice=s.native.notice;
        next.catalogAutoPending=s.catalogStartup.Pending();
        next.freeCameraActive=s.freeCamera.Active()||s.freeCameraPending;next.freeCameraStatus=s.freeCamera.Active()&&!s.freeCamera.FrameReady()?"正在等待快捷键屏蔽就绪":s.freeCamera.status;
        next.presets=s.store.presets;next.storeNotice=s.store.notice;next.writable=s.store.Writable();
        std::lock_guard<std::mutex> lock(s.mutex);s.snapshot=std::move(next);
    };
    try {
        // Idle startup takes no Unity/Master/scene-enumeration path. Only an
        // existing actor callback can open the performance-scene gate.
        next.admitted=s.gate.Fresh(MonotonicMs());
        // A paused Live can stop the actor callback used to refresh the gate.
        // Keep it admitted only while our own pause still belongs to the same
        // active scene and a freshly re-discovered presenter confirms it.
        if(!next.admitted&&s.ownsLivePause&&s.gate.Mode()==PerformanceMode::Live&&s.livePauseScene==s.gate.Scene()) {
            try {
                const auto active=s.native.ActiveSceneName();
                if(s.gate.MatchesActive(active)) {
                    s.native.SetAdmittedScene(s.livePauseScene);
                    NativeApi::LivePlaybackObservation observed;
                    next.admitted=s.native.ObserveLivePlayback(observed)&&observed.presenter==s.livePauseOwner&&observed.paused;
                }
            }catch(const std::exception&){}
        }
        const std::string activeScene=next.admitted?s.native.ActiveSceneName():std::string{};
        if(next.admitted&&!s.gate.MatchesActive(activeScene)){s.gate.Reset();next.admitted=false;}
        next.activeSceneName=next.admitted?activeScene:std::string{};
        next.inLive=next.admitted&&s.gate.Mode()==PerformanceMode::Live;
        if(!next.inLive){next.liveModels.clear();next.preLiveModelStatus.clear();}
        s.freeCamera.Scene(next.admitted);
        if(s.cameraHandoffRequested&&!s.freeCameraPending&&!s.freeCamera.Active()){
            if(s.cameraHandoff.request)s.cameraHandoff.request(0);s.cameraHandoffRequested=false;
            Log(s,"Free camera ended; Motion camera handoff released");
        }
        if(s.diagnostics&&s.freeCamera.Active()&&now-s.lastFreeCameraLog>=std::chrono::seconds(5)){Log(s,s.freeCamera.Diagnostics());s.lastFreeCameraLog=now;}
        const int currentScene=next.admitted?s.gate.Scene():0;
        if(s.resourceScene!=currentScene||s.resourceActiveScene!=(next.admitted?activeScene:std::string{})){
            if(s.resourceScene&&(s.freeCamera.Active()||s.freeCameraPending)){
                s.freeCameraPending=false;s.freeCamera.Disable();s.freeCamera.status="场景发生变化，自由视角已关闭";
                if(s.cameraHandoffRequested&&s.cameraHandoff.request)s.cameraHandoff.request(0);s.cameraHandoffRequested=false;
                Log(s,"Free camera stopped on scene change");
            }
            if(s.resourceScene)s.native.EndResourceSession();s.resourceScene=currentScene;s.resourceActiveScene=next.admitted?activeScene:std::string{};next.resources.clear();next.resourceControls=false;next.cameraColorEnabled=false;
        }
        s.native.SetAdmittedScene(currentScene);
        if(!next.inLive||s.livePauseScene!=currentScene){s.ownsLivePause=false;s.livePauseOwner=0;s.livePauseScene=currentScene;next.livePauseAvailable=false;next.livePaused=false;next.livePauseOwned=false;s.nextLivePauseObserve={};}
        const auto desktopSeen=s.desktopUiSeenMs.load(std::memory_order_acquire),desktopNow=MonotonicMs();
        const bool observingDesktop=desktopSeen&&desktopNow>=desktopSeen&&desktopNow-desktopSeen<=3000;
        if(next.inLive&&(observingDesktop||s.dialogOpen||s.ownsLivePause)&&now>=s.nextLivePauseObserve){
            s.nextLivePauseObserve=now+std::chrono::seconds(2);
            try{NativeApi::LivePlaybackObservation obs;next.livePauseAvailable=s.native.ObserveLivePlayback(obs);if(next.livePauseAvailable){next.livePaused=obs.paused;if(s.ownsLivePause&&(s.livePauseOwner!=obs.presenter||!obs.paused)){s.ownsLivePause=false;next.livePauseStatus=obs.paused?"Live 场景已变化，已释放暂停记录":"Live 已在游戏中恢复播放";}next.livePauseOwned=s.ownsLivePause;}else{next.livePauseOwned=false;}}
            catch(const std::exception&){next.livePauseAvailable=false;next.livePauseOwned=false;}
        }

        next.admissionNotice=next.admitted?(s.gate.Mode()==PerformanceMode::Live?"Live 场景已确认":"拍摄场景已确认"):"当前页面休眠；仅在拍摄或 Live 场景启用";
        if(!next.admitted) {
            next.preLiveModelStatus.clear();
            if(s.cameraHandoffRequested&&s.cameraHandoff.request)s.cameraHandoff.request(0);
            s.cameraHandoffRequested=false;s.freeCameraPending=false;
            s.catalogRequested=false;s.catalogManualRequest=false;s.sceneRequested=false;s.modelQueue.clear();s.modelCursor=0;next.actors.clear();next.playing=false;
            s.resourceEnablePending=false;next.resourceEnablePending=false;s.poseChecks.clear();
            if(s.wasAdmitted){const bool resumeDesktop=s.desktopResourceScan;s.native.SuspendScene();s.preLiveResourceScan=false;s.desktopResourceScan=false;if(resumeDesktop)s.desktopResourceScan=s.native.StartResourceScan(next.catalog,true);Log(s,"Performance scene left; catalog/roster scanning suspended");}
            s.wasAdmitted=false;
            s.stageAttempted=false;
        } else {
            if(!s.wasAdmitted){Log(s,"Performance scene admitted; automatic catalog read will run when settled");s.native.notice=s.catalogRead?"场景已就绪，人物目录已读取":"场景已就绪，正在准备自动读取人物目录";}
            s.wasAdmitted=true;
            if(!s.playback.isPlaying)Query(s,"gakumotion.playback-state",s.playback);
            next.playing=s.playback.isPlaying&&s.playback.isPlaying();
            if(!s.cameraHandoff.request||!s.cameraHandoff.isReleased)Query(s,"gakumotion.camera-handoff",s.cameraHandoff);
            if(s.freeCameraPending) {
                if(s.cameraHandoff.isReleased&&s.cameraHandoff.isReleased()) {
                    s.freeCameraPending=false;
                    if(!s.freeCamera.Enable(s.host)){if(s.cameraHandoff.request)s.cameraHandoff.request(0);s.cameraHandoffRequested=false;}
                    Log(s,std::string("Free camera handoff completed: ")+s.freeCamera.status);
                } else if(now>=s.freeCameraDeadline) {
                    s.freeCameraPending=false;if(s.cameraHandoff.request)s.cameraHandoff.request(0);s.cameraHandoffRequested=false;
                    s.freeCamera.status="等待 Motion 释放相机超时，自由视角未启用";Log(s,s.freeCamera.status,GAKU_LOG_WARNING);
                }
            }
            if(!s.handoff.request||!s.handoff.isReleased)Query(s,"gakumotion.visual-handoff",s.handoff);
            next.motionHandoffAvailable=s.handoff.request&&s.handoff.isReleased;
            next.motionHandoffRequested=s.handoffRequested;
            next.motionHandoffReleased=next.motionHandoffAvailable&&s.handoff.isReleased()!=0;
            const bool motionDetected=MotionResourceDetected(s.playback.isPlaying!=nullptr,s.handoff.request!=nullptr,s.handoff.isReleased!=nullptr);
            next.motionConflict=MotionResourceConflict(motionDetected,next.motionHandoffAvailable,next.motionHandoffReleased);
            // No controller means our resource operations are available by
            // default. Do not enumerate scene objects until the user scans.
            if(ResourceMayAutoEnable(next.admitted,motionDetected,s.resourceEnablePending,next.resourceControls)) {
                next.resourceControls=true;next.resourceStatus="资源控制已就绪（未检测到 Motion）";
            }
            next.resourceEnablePending=s.resourceEnablePending;
            if(s.resourceEnablePending&&now>=s.resourceEnableDeadline&&!next.motionHandoffReleased) {
                if(s.handoff.request)s.handoff.request(0);
                s.handoffRequested=false;s.resourceEnablePending=false;next.motionHandoffRequested=false;next.resourceEnablePending=false;
                next.resourceStatus="等待 Motion 释放超时，未启用资源控制；请检查日志";
                Log(s,"Resource enable timed out waiting for Motion handoff",GAKU_LOG_WARNING);
            } else if(s.resourceEnablePending&&(!motionDetected||next.motionHandoffReleased)) {
                try {
                    next.resources=s.native.ScanResources();
                    s.resourceEnablePending=false;next.resourceEnablePending=false;next.resourceControls=true;
                    s.sceneRequested=true;s.nextScan={};
                    next.resourceStatus="资源控制已启用；Motion 交接完成";Log(s,"Resource control enabled after Motion handoff");
                } catch(const std::exception& e) {
                    s.resourceEnablePending=false;next.resourceEnablePending=false;next.resourceControls=false;
                    if(s.handoffRequested&&s.handoff.request)s.handoff.request(0);
                    s.handoffRequested=false;next.motionHandoffRequested=false;
                    next.resourceStatus=std::string("资源扫描失败，未启用控制：")+e.what();
                    Log(s,next.resourceStatus,GAKU_LOG_WARNING);
                }
            }
            if(next.motionConflict&&next.resourceControls) {
                const bool cameraRestored=s.native.RestoreCamera(),resourcesRestored=s.native.RestoreResources();
                if(cameraRestored&&resourcesRestored){next.resourceControls=false;next.cameraColorEnabled=false;next.resources.clear();}
                next.resourceStatus=cameraRestored&&resourcesRestored?"检测到动作插件，已恢复并停用资源显隐":"检测到动作插件，但资源恢复未完成；已停止新的显隐操作";
            }
            if(!s.stageAttempted&&s.stageChoice&&s.gate.Mode()==PerformanceMode::Photography){
                s.stageAttempted=true;next.stageStatus=s.stageHook.Select(s.stageChoice)?"已启用摄影背景载入替换；等待下一次载入":"摄影背景替换不可用：接口缺失或载入钩子已被其他插件占用";
            }
        }
        auto inspectPreLive=[&](bool verbose) {
            const auto before=next.preLiveStatus+"|"+next.preLiveCharacter+"|"+next.preLiveBody+"|"+next.preLiveHead;
            const auto previousCostumes=next.preLiveCostumes;
            next.preLiveDetected=false;next.preLiveScene=0;next.preLiveCharacter.clear();next.preLiveBody.clear();next.preLiveHead.clear();next.preLiveCostumes.clear();
            if(next.admitted){next.preLiveStatus="请在进入演出前打开游戏的服装选择页面";return;}
            try{const auto observed=s.native.InspectPreLiveSelection(verbose);next.preLiveDetected=observed.found;next.preLiveScene=observed.scene;
                next.preLiveCharacter=observed.character;next.preLiveBody=observed.body;next.preLiveHead=observed.head;next.preLiveCostumes=observed.costumes;next.preLiveStatus=observed.status;}
            catch(const std::exception& e){next.preLiveStatus=std::string("服装页面检测失败：")+e.what();if(verbose)Log(s,next.preLiveStatus,GAKU_LOG_WARNING);}
            const auto after=next.preLiveStatus+"|"+next.preLiveCharacter+"|"+next.preLiveBody+"|"+next.preLiveHead;
            if(!verbose&&(before!=after||previousCostumes!=next.preLiveCostumes))Log(s,"PreLive observation changed: uptime_ms="+std::to_string(MonotonicMs())+" "+after+" confirmed_count="+std::to_string(next.preLiveCostumes.size()));
        };
        for(const auto& command:commands) {
            switch(command.action) {
            case Action::Scan:if(next.admitted){s.catalogRequested=true;s.catalogManualRequest=true;s.sceneRequested=true;}break;
            case Action::InspectPreLive: {
                inspectPreLive(true);s.nextPreLiveProbe=now+std::chrono::milliseconds(500);
                break;
            }
            case Action::ReadDesktopCatalog: {
                ++next.preLiveCatalogRevision;
                try {
                    if(!s.native.Ready())throw std::runtime_error("游戏运行时尚未就绪，请稍后手动更新");
                    // Runtime metadata can be ready before the game's master database.
                    // A desktop connection alone is not evidence that it is safe to read.
                    if(!MayReadDesktopCatalog(next.admitted,next.preLiveDetected))throw std::runtime_error("等待进入拍摄、Live 或已确认的入场前页面；当前显示缓存目录");
                    if(s.native.Busy())throw std::runtime_error("游戏资源操作进行中，请稍后手动更新");
                    // Read Master data only. Optional descriptor factories and
                    // scene enumeration remain restricted to admitted scenes.
                    auto catalog=s.native.ScanCatalog(false);
                    if(catalog.characters.empty()||catalog.bodies.empty()||catalog.heads.empty())throw std::runtime_error("游戏人物或服装数据尚未就绪，请稍后手动更新");
                    for(const auto& resource:next.catalog.resources)AddModelResource(catalog,resource);
                    for(const auto& model:next.catalog.models)catalog.models.push_back(model);
                    // Preserve previously verified special characters and
                    // factory presets without invoking factories outside scenes.
                    for(const auto& c:next.catalog.characters)if(c.special&&!FindCharacter(catalog,c.id))catalog.characters.push_back(c);
                    for(const auto& p:next.catalog.defaults)if(!p.factory.empty())catalog.defaults.push_back(p);
                    catalog.revision=next.catalog.revision+1;next.catalog=std::move(catalog);
                    next.preLiveCatalogStatus="人物、服装和头部目录已读取";
                    // Read-only bounded Octo index; independent of photo/Live.
                    // No bundles are loaded and no actor is instantiated.
                    s.preLiveResourceScan=false;s.desktopResourceScan=s.native.StartResourceScan(next.catalog,true);
                } catch(const std::exception& e){next.preLiveCatalogStatus=std::string("目录读取失败：")+e.what();Log(s,next.preLiveCatalogStatus,GAKU_LOG_WARNING);}
                break;
            }
            case Action::ReadPreLiveCharacters: {
                if(next.admitted){next.preLiveCatalogStatus="请先回到 Live 入场前的播放确认页";break;}
                try {
                    const auto observed=s.native.InspectPreLiveSelection(false);
                    if(!observed.found){next.preLiveCatalogStatus="请先打开 Live 现场播放确认页";break;}
                    auto catalog=s.native.ScanCatalog();
                    if(catalog.characters.empty()){next.preLiveCatalogStatus="游戏没有返回人物目录，请稍后重试";break;}
                    for(const auto& resource:next.catalog.resources)AddModelResource(catalog,resource);
                    catalog.revision=next.catalog.revision+1;next.catalog=std::move(catalog);
                    next.preLiveCatalogStatus="人物和服装目录已读取，可手动选择人物分类";
                    s.desktopResourceScan=false;s.preLiveResourceScan=s.native.StartResourceScan(next.catalog,true);
                    Log(s,"PreLive catalog loaded: characters="+std::to_string(next.catalog.characters.size())+" bodies="+std::to_string(next.catalog.bodies.size())+" heads="+std::to_string(next.catalog.heads.size()));
                } catch(const std::exception& e) {next.preLiveCatalogStatus=std::string("人物和服装目录读取失败：")+e.what();Log(s,next.preLiveCatalogStatus,GAKU_LOG_WARNING);}
                break;
            }
            case Action::ArmPreLive: {
                s.preLiveHook.Cancel();
                try {
                    if(next.admitted)throw std::runtime_error("请在进入 Live 前操作");
                    const auto observed=s.native.InspectPreLiveSelection(false);
                    if(!observed.found||observed.scene!=command.actorScene||command.instance<0||static_cast<size_t>(command.instance)>=observed.costumes.size())throw std::runtime_error("确认页面或人物位置已经改变");
                    const auto& slot=observed.costumes[command.instance];
                    if(slot.body!=command.preset.body||slot.head!=command.preset.head||PreLiveCharacterFromCostume(next.catalog,slot)!=command.preset.character)throw std::runtime_error("确认页人物或服装已经改变");
                    if(observed.costumes.size()!=1||command.instance!=0)throw std::runtime_error("当前实验版仅支持单人 Live");
                    for(const auto& [asset,kind]:std::vector<std::pair<std::string,std::string>>{{command.preset.bodyAsset,"body"},{command.preset.faceAsset,"face"},{command.preset.hairAsset,"hair"}}){auto parsed=ParseModelResource(asset);if(!parsed||parsed->kind!=kind)throw std::runtime_error("请选择完整有效的身体、脸和头发");}
                    Preset original;original.character=command.preset.character;original.body=slot.body;original.head=slot.head;
                    const auto model=s.native.ResolveModelAssets(original);
                    const std::array<std::string,3> before{model.body,model.face,model.hair},after{command.preset.bodyAsset,command.preset.faceAsset,command.preset.hairAsset};
                    s.preLiveHook.Arm(command.preset.character,before,after);s.lastPreLiveModelLog.clear();
                } catch(const std::exception& e){s.preLiveHook.status=std::string("无法准备换装：")+e.what();Log(s,s.preLiveHook.status,GAKU_LOG_WARNING);}
                next.preLiveApplyStatus=s.preLiveHook.status;break;
            }
            case Action::CancelPreLive:s.preLiveHook.Cancel();next.preLiveApplyStatus=s.preLiveHook.status;break;
            case Action::Reload:s.store.Load();break;
            case Action::FreeCamera:
                if(command.instance) {
                    if(!next.admitted){s.freeCamera.status="请进入拍摄 / Live 场景";break;}
                    if(s.freeCamera.Active()||s.freeCameraPending)break;
                    if(s.playback.isPlaying||s.cameraHandoff.request){
                        if(!s.cameraHandoff.request||!s.cameraHandoff.isReleased){s.freeCamera.status="Motion 版本不支持相机交接，请更新配套版本";break;}
                        if(!s.cameraHandoff.request(1)){s.freeCamera.status="Motion 拒绝释放相机";break;}
                        s.cameraHandoffRequested=true;s.freeCameraPending=true;s.freeCameraDeadline=now+std::chrono::seconds(15);
                        s.freeCamera.status="正在等待 Motion 释放相机";
                    } else s.freeCamera.Enable(s.host);
                } else {
                    s.freeCameraPending=false;s.freeCamera.Disable();
                    if(s.cameraHandoffRequested&&s.cameraHandoff.request)s.cameraHandoff.request(0);s.cameraHandoffRequested=false;
                }
                Log(s,std::string("Free camera: ")+s.freeCamera.status);break;
            case Action::Save:if(s.store.Save(command.preset)&&next.admitted)s.native.SyncPresetActorNames(command.preset);break;
            case Action::DeletePreset:s.store.Remove(command.preset.id);break;
            case Action::Spawn:
                Log(s,"Spawn command: preset="+command.preset.id+" category="+command.preset.character+" body="+command.preset.bodyAsset+" face="+command.preset.faceAsset+" hair="+command.preset.hairAsset);
                if(!next.admitted){s.native.notice="当前页面不支持加入人物";break;}
                if(next.playing){s.native.notice="请先在动作插件中停止播放";break;}
                if(!s.native.StartSpawn(command.preset,command.instance))Log(s,s.native.notice,GAKU_LOG_WARNING);
                s.nextScan={};break;
            case Action::Remove:
                if(!next.admitted)break;
                if(next.playing){s.native.notice="请先在动作插件中停止播放";break;}
                if(!s.native.RestoreResources()){s.native.notice="资源状态未能恢复，已取消移除人物";break;}
                s.native.RemoveActor(command.instance);s.nextScan={};break;
            case Action::Move:
            case Action::ResetPosition:
            case Action::Turn:
            case Action::SetYaw:
            case Action::ResetYaw: {
                const char* operation=command.action==Action::Move?"move":command.action==Action::ResetPosition?"reset-position":command.action==Action::Turn?"turn-relative":command.action==Action::ResetYaw?"reset-yaw":"set-yaw";
                const bool logMove=command.action!=Action::Move||now-s.lastMoveLog>=std::chrono::milliseconds(250);
                if(logMove){Log(s,std::string("Pose requested op=")+operation+" instance="+std::to_string(command.instance));if(command.action==Action::Move)s.lastMoveLog=now;}
                auto actor=std::find_if(next.actors.begin(),next.actors.end(),[&](const SceneActor& a){return a.instance==command.instance&&a.scene==currentScene;});
                if(!next.admitted||next.playing||actor==next.actors.end()) {
                    s.native.notice=next.playing?"请先停止动作播放":"人物未在当前场景名单中，请扫描";
                    Log(s,std::string("Pose BLOCKED op=")+operation+" instance="+std::to_string(command.instance)+" reason="+s.native.notice,GAKU_LOG_WARNING);break;
                }
                float before[3]{},rotationBefore[4]{};
                if(!s.native.ReadScenePose(*actor,before,rotationBefore)) {s.native.notice="人物身份已变化，请重新扫描";Log(s,std::string("Pose BLOCKED op=")+operation+" instance="+std::to_string(command.instance)+" reason=identity-mismatch",GAKU_LOG_WARNING);break;}
                bool ok=false;float resetTarget[3]{},resetRotation[4]{};
                if(command.action==Action::Move)ok=s.native.MoveSceneActor(*actor,command.position);
                else if(command.action==Action::ResetPosition)ok=s.native.ResetSceneActor(*actor,resetTarget);
                else if(command.action==Action::ResetYaw)ok=s.native.ResetSceneActorRotation(*actor,resetRotation);
                else ok=s.native.TurnSceneActor(*actor,command.degrees,command.action==Action::SetYaw);
                float after[3]{},rotationAfter[4]{};const bool readback=ok&&s.native.ReadScenePose(*actor,after,rotationAfter);
                bool matches=readback;
                if(readback&&(command.action==Action::Move||command.action==Action::ResetPosition)) {
                    const float* target=command.action==Action::Move?command.position:resetTarget;
                    for(int i=0;i<3;++i)if(std::fabs(after[i]-target[i])>0.01f)matches=false;
                } else if(readback&&command.action==Action::ResetYaw) {
                    double dot=0;for(int i=0;i<4;++i)dot+=resetRotation[i]*rotationAfter[i];matches=std::fabs(std::fabs(dot)-1.0)<0.001;
                } else if(readback&&(command.action==Action::Turn||command.action==Action::SetYaw)) {
                    const float* q=rotationBefore;float degrees=command.degrees;
                    if(command.action==Action::SetYaw)degrees-=std::atan2(2.0f*(q[3]*q[1]+q[0]*q[2]),1.0f-2.0f*(q[1]*q[1]+q[2]*q[2]))*57.2957795f;
                    const float half=degrees*0.00872664626f,c=std::cos(half),sn=std::sin(half);
                    const float expected[4]{c*q[0]+sn*q[2],c*q[1]+sn*q[3],c*q[2]-sn*q[0],c*q[3]-sn*q[1]};
                    double dot=0;for(int i=0;i<4;++i)dot+=expected[i]*rotationAfter[i];matches=std::fabs(std::fabs(dot)-1.0)<0.001;
                }
                const float* requestedPosition=command.action==Action::ResetPosition?resetTarget:command.position;
                char buffer[700]{};std::snprintf(buffer,sizeof(buffer),"Pose op=%s instance=%d scene=%d owned=%d requested_pos=(%.3f,%.3f,%.3f) degrees=%.2f before=(%.3f,%.3f,%.3f) after=(%.3f,%.3f,%.3f) quat=(%.4f,%.4f,%.4f,%.4f) submitted=%d readback=%d matched=%d",operation,actor->instance,actor->scene,actor->owned?1:0,requestedPosition[0],requestedPosition[1],requestedPosition[2],command.degrees,before[0],before[1],before[2],after[0],after[1],after[2],rotationAfter[0],rotationAfter[1],rotationAfter[2],rotationAfter[3],ok?1:0,readback?1:0,matches?1:0);
                if(logMove||!ok||!matches)Log(s,buffer,ok&&matches?GAKU_LOG_INFO:GAKU_LOG_WARNING);
                if(ok&&!matches)s.native.notice="人物变换未通过回读，请查看日志";
                if(command.action==Action::ResetPosition&&matches) {
                    next.poseResetActor=actor->instance;
                    std::memcpy(next.poseResetPosition,after,sizeof(after));
                    ++next.poseResetRevision;
                }
                if(command.action==Action::ResetYaw&&matches) {
                    SceneActor resetActor=*actor;std::memcpy(resetActor.rotation,rotationAfter,sizeof(rotationAfter));
                    const auto yaw=desktop_protocol::ActorYaw(resetActor);
                    if(yaw.is_number()){next.poseResetYawActor=actor->instance;next.poseResetYaw=static_cast<float>(yaw.get<double>());++next.poseResetYawRevision;}
                }
                if(readback) {
                    s.poseChecks.erase(std::remove_if(s.poseChecks.begin(),s.poseChecks.end(),[&](const auto& check){return check.actor.instance==actor->instance&&check.actor.scene==actor->scene;}),s.poseChecks.end());
                    State::PoseCheck check;check.actor=*actor;std::memcpy(check.position,after,sizeof(after));std::memcpy(check.rotation,rotationAfter,sizeof(rotationAfter));check.due=now+std::chrono::milliseconds(500);check.operation=operation;s.poseChecks.push_back(std::move(check));
                }
                break;
            }
            case Action::SetLivePause: {
                if(!next.inLive||!next.admitted){next.livePauseStatus="仅在 Live 场景可暂停播放";break;}
                if(next.playing){next.livePauseStatus="Motion 正在播放，请先在 Motion 暂停动作";break;}
                try {
                    NativeApi::LivePlaybackObservation obs;
                    if(!s.native.ObserveLivePlayback(obs)){next.livePauseStatus="无法确认 Live 播放状态";break;}
                    if(command.instance) {
                        if(obs.paused){next.livePauseStatus="Live 原本已暂停；本插件不会接管恢复";break;}
                        s.ownsLivePause=true;s.livePauseOwner=obs.presenter;s.livePauseScene=currentScene;
                        if(!s.native.SetLivePlaybackPaused(obs.presenter,true)){next.livePauseStatus="暂停结果未确认，请等待状态刷新后尝试恢复";next.livePauseOwned=true;s.nextLivePauseObserve={};break;}
                        next.livePauseStatus="Live 已暂停（包含音乐）；可调整人物";
                    } else {
                        if(!s.ownsLivePause||s.livePauseOwner!=obs.presenter||s.livePauseScene!=currentScene){next.livePauseStatus="没有本插件持有的暂停，未改变播放状态";break;}
                        if(!s.native.SetLivePlaybackPaused(obs.presenter,false)){next.livePauseStatus="未能确认 Live 恢复，请检查游戏播放状态";break;}
                        s.ownsLivePause=false;s.livePauseOwner=0;next.livePauseStatus="Live 已从暂停位置继续播放";
                    }
                    next.livePaused=command.instance!=0;next.livePauseOwned=s.ownsLivePause;next.livePauseAvailable=true;s.nextLivePauseObserve={};
                }catch(const std::exception& e){next.livePauseStatus=std::string("Live 播放控制失败：")+e.what();}
                break;
            }
            case Action::SetVisualHandoff:
                if(!next.admitted||!s.handoff.request||!s.handoff.isReleased){next.resourceStatus="动作插件未提供资源交接接口，请更新配套版本";break;}
                if(!command.instance) {
                    s.resourceEnablePending=false;next.resourceEnablePending=false;
                    if(next.resourceControls&&(!s.native.RestoreCamera()||!s.native.RestoreResources())){next.resourceStatus="本插件资源未能恢复，无法交还 Motion";break;}
                    if(!s.stageHook.Select(0)){next.resourceStatus="摄影背景钩子未能释放，无法交还 Motion";break;}
                    next.stageStatus="已释放摄影背景钩子；保存的舞台选择不变";
                    next.resourceControls=false;next.cameraColorEnabled=false;
                }
                if(s.handoff.request(command.instance?1u:0u)){s.handoffRequested=command.instance!=0;next.motionHandoffRequested=s.handoffRequested;
                    next.motionHandoffReleased=false;next.motionConflict=true;
                    next.resourceStatus=s.handoffRequested?"正在等待 Motion 恢复并释放资源控制":"已交还 Motion 资源控制";}
                break;
            case Action::EnableCreation:
                if(next.admitted&&!next.playing)s.native.EnableCreation(command.instance!=0);
                break;
            case Action::ApplyActorModel:case Action::RestoreActorModel:
                Log(s,std::string("Model edit REQUEST: mode=")+(command.action==Action::RestoreActorModel?"restore":"apply")+" instance="+std::to_string(command.instance)+" scene="+std::to_string(command.actorScene));
                if(!next.admitted||next.playing){s.native.notice="请进入场景并停止动作播放";Log(s,"Model edit BLOCKED: unsupported scene or motion playing",GAKU_LOG_WARNING);break;}
                {const auto actor=std::find_if(next.actors.begin(),next.actors.end(),[&](const SceneActor& a){return a.instance==command.instance&&a.scene==command.actorScene&&a.identityValid&&a.uniqueId==command.actorUnique;});
                if(actor==next.actors.end()||(command.action==Action::RestoreActorModel&&!actor->modelRestorable)||(command.action==Action::ApplyActorModel&&!ActorModelChoiceAllowed(next.catalog,*actor,command.preset))){s.native.notice="人物或模型部件已变化，请重新选择";Log(s,"Model edit BLOCKED: stale actor identity or invalid model choice",GAKU_LOG_WARNING);break;}}
                if(!s.native.RestoreResources()){s.native.notice="资源状态未能恢复，已取消人物模型操作";Log(s,"Model edit BLOCKED: failed to restore scene resources",GAKU_LOG_WARNING);break;}
                s.native.StartActorModel(command.instance,command.actorScene,command.actorUnique,
                    {command.preset.bodyAsset,command.preset.faceAsset,command.preset.hairAsset},command.action==Action::RestoreActorModel);
                s.nextScan={};break;
            case Action::ScanResources:
                if(next.admitted){next.resources=s.native.ScanResources();s.sceneRequested=true;s.nextScan={};next.resourceStatus="已扫描当前场景；勾选可隐藏资源";}
                break;
            case Action::HideResource:
                if(next.admitted&&next.resourceControls&&!next.motionConflict) {
                    const auto found=std::find_if(next.resources.begin(),next.resources.end(),[&](const auto& r){return r.generation==command.resource.generation&&r.instance==command.resource.instance&&r.scene==command.resource.scene&&r.kind==command.resource.kind;});
                    if(found!=next.resources.end()&&s.native.SetResourceHidden(*found,command.hidden))next.resourceStatus="操作完成，已保存可恢复状态";
                    else next.resourceStatus="操作未完成：资源已变化或接口不可用";
                    next.resources=s.native.ScanResources();
                }
                break;
            case Action::EnableResourceControls:
                if(!next.admitted){next.resourceStatus="请进入拍摄或 Live 场景";break;}
                if(command.instance) {
                    if(next.motionConflict) {
                        if(!s.handoff.request||!s.handoff.isReleased){next.resourceStatus="Motion 版本不支持自动交接，请更新配套版本";Log(s,"Resource enable blocked: Motion handoff service unavailable",GAKU_LOG_WARNING);break;}
                        if(!s.handoff.request(1u)){next.resourceStatus="Motion 拒绝交接";Log(s,"Resource enable blocked: handoff request rejected",GAKU_LOG_WARNING);break;}
                    s.handoffRequested=true;s.resourceEnablePending=true;s.resourceEnableDeadline=now+std::chrono::seconds(15);next.motionHandoffRequested=true;next.resourceEnablePending=true;
                        next.resourceStatus="正在等待 Motion 释放资源，完成后自动启用";Log(s,"Resource enable requested: waiting for Motion handoff");
                    } else {
                        try {
                            next.resources=s.native.ScanResources();next.resourceControls=true;s.sceneRequested=true;s.nextScan={};
                            next.resourceStatus="资源控制已启用";Log(s,"Resource control enabled");
                        } catch(const std::exception& e) {
                            next.resourceControls=false;next.resourceStatus=std::string("资源扫描失败，未启用控制：")+e.what();
                            Log(s,next.resourceStatus,GAKU_LOG_WARNING);
                        }
                    }
                } else {
                    if(!s.native.RestoreCamera()||!s.native.RestoreResources()){next.resourceStatus="资源状态未能恢复，已保留控制权";Log(s,"Resource disable blocked: restore failed",GAKU_LOG_WARNING);break;}
                    if(s.handoffRequested&&!s.stageHook.Select(0)){next.resourceStatus="摄影背景钩子未能释放，无法交还 Motion";Log(s,"Resource disable blocked: stage hook release failed",GAKU_LOG_WARNING);break;}
                    if(s.handoffRequested&&s.handoff.request&&!s.handoff.request(0u)){next.resourceStatus="Motion 未接受交还请求";break;}
                    s.handoffRequested=false;s.resourceEnablePending=false;next.resourceEnablePending=false;next.motionHandoffRequested=false;
                    next.resourceControls=false;next.cameraColorEnabled=false;next.resourceStatus="资源已恢复并交还 Motion";Log(s,"Resource control disabled and handed back");
                }
                break;
            case Action::CameraColor:
                if(next.admitted&&next.resourceControls&&!next.motionConflict) {
                    const bool ok=command.hidden?s.native.ApplyCameraColor(command.color):s.native.RestoreCamera();
                    if(ok){next.cameraColorEnabled=command.hidden;std::memcpy(next.cameraColor,command.color,sizeof(next.cameraColor));next.resourceStatus="相机状态已更新";}
                    else next.resourceStatus="相机状态未更新：对象或接口不可用，或状态已被其他功能修改";
                }
                break;
            case Action::SelectStage:
                if(next.admitted&&s.gate.Mode()==PerformanceMode::Photography) {
                    const int old=s.stageChoice;
                    if(!s.stageHook.Select(command.instance)){next.stageStatus="无法设置：游戏接口不可用或载入钩子被其他插件占用";break;}
                    if(!s.stageStore.Save(command.instance)){s.stageHook.Select(old);next.stageStatus="无法保存摄影背景设置，原设置已保留";break;}
                    s.stageChoice=command.instance;next.stageChoice=s.stageChoice;s.stageAttempted=true;
                    next.stageStatus=s.stageChoice?"选择已保存；下次摄影背景载入时生效":"已选择游戏原生背景；下次摄影背景载入时生效";
                } else next.stageStatus="请先进入摄影场景再修改摄影背景";
                break;
            case Action::LoadThumbnail: {
                std::string data,status="没有可确认的服装图",headData,headStatus="没有可确认的头部组合图";
                if(next.admitted) {
                    const Part* part=FindPart(next.catalog.bodies,command.preset.body);
                    if(!part&&!command.preset.bodyAsset.empty()) {
                        for(const auto& candidate:next.catalog.bodies)if(candidate.resource==command.preset.bodyAsset) {
                            if(part){part=nullptr;break;} // Ambiguous image association fails closed.
                            part=&candidate;
                        }
                    }
                    if(part&&!part->thumbnail.empty())data=s.native.ReadLoadedThumbnail(part->thumbnail,status);
                    const Part* head=FindPart(next.catalog.heads,command.preset.head);
                    if(!head&&!command.preset.faceAsset.empty()&&!command.preset.hairAsset.empty()) {
                        for(const auto& candidate:next.catalog.heads)if(candidate.resource==command.preset.hairAsset&&candidate.faceResource==command.preset.faceAsset) {
                            if(head){head=nullptr;break;}
                            head=&candidate;
                        }
                    }
                    if(head&&!head->thumbnail.empty())headData=s.native.ReadLoadedThumbnail(head->thumbnail,headStatus);
                } else {status="请先进入拍摄或 Live 场景";headStatus=status;}
                std::lock_guard<std::mutex> lock(s.mutex);
                if(s.legacyThumbnailId==command.preset.id){s.legacyThumbnailData=data;s.legacyThumbnailStatus=status;s.legacyHeadData=headData;s.legacyHeadStatus=headStatus;++s.legacyThumbnailRevision;}
                if(s.thumbnailId==command.preset.id){s.thumbnailData=std::move(data);s.thumbnailStatus=std::move(status);s.thumbnailHeadData=std::move(headData);s.thumbnailHeadStatus=std::move(headStatus);}
                break;
            }
            }
        }
        for(auto it=s.poseChecks.begin();it!=s.poseChecks.end();) {
            if(now<it->due||s.native.Busy()){++it;continue;}
            float position[3]{},rotation[4]{};const bool read=s.native.ReadScenePose(it->actor,position,rotation);
            double dist=0,dot=0;for(int i=0;i<3;++i){const double delta=position[i]-it->position[i];dist+=delta*delta;}for(int i=0;i<4;++i)dot+=rotation[i]*it->rotation[i];
            const bool stable=read&&dist<0.0001&&std::fabs(std::fabs(dot)-1.0)<0.001;
            char buffer[420]{};std::snprintf(buffer,sizeof(buffer),"Pose follow-up op=%s instance=%d scene=%d readback=%d stable=%d position=(%.3f,%.3f,%.3f) quat=(%.4f,%.4f,%.4f,%.4f)",it->operation.c_str(),it->actor.instance,it->actor.scene,read?1:0,stable?1:0,position[0],position[1],position[2],rotation[0],rotation[1],rotation[2],rotation[3]);
            Log(s,buffer,stable?GAKU_LOG_INFO:GAKU_LOG_WARNING);it=s.poseChecks.erase(it);
        }
        const auto preLiveUi=s.preLiveUiSeenMs.load(std::memory_order_acquire),tickMs=MonotonicMs();
        if(ShouldObservePreLive(next.admitted,preLiveUi,tickMs)&&now>=s.nextPreLiveProbe){
            const auto started=std::chrono::steady_clock::now();inspectPreLive(false);
            const auto elapsed=std::chrono::steady_clock::now()-started;
            const bool slow=elapsed>std::chrono::milliseconds(50);
            s.nextPreLiveProbe=std::chrono::steady_clock::now()+(slow?std::chrono::milliseconds(2000):std::chrono::milliseconds(500));
            if(slow&&now-s.lastPreLiveSlowLog>std::chrono::seconds(10)){s.lastPreLiveSlowLog=now;Log(s,"PreLive observation exceeded 50 ms; reduced polling frequency",GAKU_LOG_WARNING);}
            if(!next.preLiveDetected){s.preLiveCatalogAttempted=false;s.preLiveLogged=false;}
            else if(!s.preLiveLogged){s.preLiveLogged=true;inspectPreLive(true);}
            else if(!s.preLiveCatalogAttempted) {
                s.preLiveCatalogAttempted=true;
                if(next.catalog.bodies.empty()&&next.catalog.heads.empty()) {
                    std::lock_guard<std::mutex> lock(s.mutex);
                    if(s.commands.size()<128)QueueCommand(s,Command{Action::ReadPreLiveCharacters});
                } else if(!s.native.ResourceScanActive()&&next.catalog.resourceNotice.find("底层资源目录完成")==std::string::npos) {
                    s.desktopResourceScan=false;s.preLiveResourceScan=s.native.StartResourceScan(next.catalog,true);
                }
            }
        }
        if(next.admitted&&next.preLiveDetected){next.preLiveDetected=false;next.preLiveCharacter.clear();next.preLiveBody.clear();next.preLiveHead.clear();next.preLiveCostumes.clear();next.preLiveStatus="已进入演出，入场前观察已停止";}
        s.preLiveHook.DrainSkip();if(next.admitted&&s.gate.Mode()==PerformanceMode::Live)s.preLiveHook.SceneEntered();
        if(const auto result=s.native.PollPreLiveTask();!result.empty()) {
            s.preLiveHook.status=result;
            if(result.starts_with("预加载失败")||result.starts_with("预加载状态无法确认"))s.preLiveHook.submitted_=false;
        }
        if(!s.preLiveHook.status.empty())next.preLiveApplyStatus=s.preLiveHook.status;
        if(next.admitted&&s.gate.Mode()==PerformanceMode::Live&&now>=s.nextPreLiveModelMonitor) {
            s.nextPreLiveModelMonitor=now+std::chrono::seconds(2);
            try {
                const auto actors=s.native.ScanScene();const auto& wanted=s.preLiveHook.Requested();
                std::vector<LiveModelObservation> observations;std::string summary;
                for(const auto& actor:actors) {
                    LiveModelObservation observed{actor.instance,actor.name,actor.bodyAsset,actor.faceAsset,actor.hairAsset,{}};
                    if(observed.body.empty()||observed.face.empty()||observed.hair.empty())observed.status="模型信息尚未读取完整";
                    else try {
                        void* current=s.native.FindActor(actor.instance,actor.scene);std::string error;
                        if(!current)throw std::runtime_error("人物已离开场景");
                        ModelAssets expected{"",observed.body,observed.face,observed.hair,""};
                        const auto evidence=s.native.InspectBuiltModel(current,expected,false);
                        observed.status=ValidateModelReadiness(evidence,error)?"已构建":!evidence.buildSuccess?"正在构建或尚未就绪":"加载中或未就绪："+error;
                    }catch(const std::exception& e){observed.status=std::string("加载状态暂不可用：")+e.what();}
                    if(s.preLiveHook.submitted_&&!actor.owned&&
                       (observed.body!=wanted[0]||observed.face!=wanted[1]||observed.hair!=wanted[2]))observed.status+="；与目标组合不一致";
                    if(!summary.empty())summary+=" | ";
                    summary+=observed.name+" 身体："+observed.body+"；脸："+observed.face+"；头发："+observed.hair+"；"+observed.status;
                    observations.push_back(std::move(observed));
                }
                next.liveModels=std::move(observations);next.preLiveModelStatus=summary.empty()?"等待场景人物加载":"当前 Live："+summary;
                if(next.preLiveModelStatus!=s.lastPreLiveModelLog){Log(s,"PreLive live monitor: "+next.preLiveModelStatus);s.lastPreLiveModelLog=next.preLiveModelStatus;}
            }catch(const std::exception& e){Log(s,std::string("PreLive live monitor error: ")+e.what(),GAKU_LOG_WARNING);}
        }
        s.native.PollSpawn(); // Only our own pending task; no cache or catalog load.
        s.native.PollModelEdit();
        const auto catalogNow=MonotonicMs();
        const bool catalogDue=s.catalogStartup.Due(next.admitted,s.native.Busy(),catalogNow);
        if(!s.catalogRead&&!s.catalogRequested&&catalogDue){
            s.catalogStartup.Begin(catalogNow);s.catalogRequested=true;s.catalogManualRequest=false;s.sceneRequested=true;
        }
        if(next.admitted&&s.catalogRequested&&!s.native.Busy()) {
            s.catalogRequested=false;
            const bool manual=s.catalogManualRequest;s.catalogManualRequest=false;
            // Expensive diagnostics remain an explicit action. Automatic reads
            // use the same catalog path without probing live actor contracts.
            if(manual&&s.diagnostics){
                try{s.native.DiagnoseContracts();}catch(const std::exception& e){Log(s,std::string("Diagnostic contracts unavailable: ")+e.what(),GAKU_LOG_WARNING);}
                try{s.native.DiagnoseActorManagerRelations();}catch(const std::exception& e){Log(s,std::string("Diagnostic manager relationships unavailable: ")+e.what(),GAKU_LOG_WARNING);}
                try{s.native.DiagnoseLiveBindings();}catch(const std::exception& e){Log(s,std::string("Diagnostic live bindings unavailable: ")+e.what(),GAKU_LOG_WARNING);}
                try{s.native.DiagnoseActors();}catch(const std::exception& e){Log(s,std::string("Diagnostic actors unavailable: ")+e.what(),GAKU_LOG_WARNING);}
            }
            Log(s,manual?"Catalog request BEGIN (manual refresh)":"Catalog request BEGIN (automatic startup read)");
            try{auto catalog=s.native.ScanCatalog();if(catalog.characters.empty())throw std::runtime_error("游戏尚未返回人物目录");catalog.revision=next.catalog.revision+1;next.catalog=std::move(catalog);s.catalogRead=true;s.catalogStartup.Complete();s.native.notice=manual?"人物目录已重新读取":"人物目录已自动读取，可选择人物与预设";Log(s,"Game master catalog read: "+std::to_string(next.catalog.characters.size())+" characters, "+std::to_string(next.catalog.bodies.size())+" bodies, "+std::to_string(next.catalog.heads.size())+" heads");
                s.desktopResourceScan=false;s.preLiveResourceScan=false;s.native.StartResourceScan(next.catalog);
                s.modelQueue.clear();s.modelCursor=0;s.modelFailures=0;
                for(const auto& p:next.catalog.defaults)if(p.factory.empty())s.modelQueue.push_back(p);
            }
            catch(const std::exception& e){next.catalog.notice=std::string("目录读取未完成：")+e.what();s.native.notice=next.catalog.notice+(s.catalogStartup.Pending()?"；将稍后自动重试":"；可点击重新读取目录重试");Log(s,s.native.notice,GAKU_LOG_WARNING);}
            Log(s,"Catalog request END");
        }
        if(next.admitted&&s.modelCursor<s.modelQueue.size()) {
            // Resolve definitions, not assets, at most one outfit per frame.
            // Scheduled after a successful automatic or manual catalog read.
            try{auto model=s.native.ResolveModelAssets(s.modelQueue[s.modelCursor]);model.costumeId=s.modelQueue[s.modelCursor].body;model.headId=s.modelQueue[s.modelCursor].head;AddDescriptorResources(next.catalog,model);next.catalog.models.push_back(std::move(model));}
            catch(const std::exception& e){if(s.modelFailures++<3)Log(s,std::string("Model definition resolve skipped: ")+e.what(),GAKU_LOG_WARNING);}
            ++s.modelCursor;
            next.catalog.notice="目录已读取；模型资源解析 "+std::to_string(s.modelCursor)+"/"+std::to_string(s.modelQueue.size())+"；资源定义不代表已加载人物";
            if(s.modelCursor==s.modelQueue.size())Log(s,"Model definitions complete: resolved="+std::to_string(next.catalog.models.size())+" skipped="+std::to_string(s.modelFailures));
        }
        if(s.preLiveResourceScan&&(!next.preLiveDetected||!ShouldObservePreLive(next.admitted,preLiveUi,tickMs))){
            s.native.StopResourceScan();s.preLiveResourceScan=false;
            next.catalog.resourceNotice="离开确认页，模型目录扫描已暂停";
        }
        // Completion changes the revision once so the desktop can persist the
        // final verified resource list. During the scan snapshots still expose
        // incremental results and progress without repeated cache writes.
        auto stepCatalogResources=[&](bool outside) {
            const bool wasActive=s.native.ResourceScanActive();
            if(wasActive&&!s.native.Busy())s.native.StepResourceScan(next.catalog,outside);
            if(wasActive&&!s.native.ResourceScanActive()&&next.catalog.resourceNotice.find("底层资源目录完成")==0)++next.catalog.revision;
        };
        if(s.desktopResourceScan) {stepCatalogResources(true);if(!s.native.ResourceScanActive())s.desktopResourceScan=false;}
        else if(s.preLiveResourceScan){stepCatalogResources(true);if(!s.native.ResourceScanActive())s.preLiveResourceScan=false;}
        else if(next.admitted)stepCatalogResources(false);
        if(next.admitted&&s.sceneRequested&&now>=s.nextScan){next.actors=s.native.ScanScene();s.nextScan=now+std::chrono::milliseconds(500);}
    } catch(const std::exception& e){s.native.notice=e.what();Log(s,e.what(),GAKU_LOG_WARNING);}
    s.native.EndFrame();
    publish();
}
static void DesktopImpl(void* context) {
    auto& s=*static_cast<State*>(context);if(s.stopped||!s.desktop.bind)return;
    const GakuImGuiAbi abi{IMGUI_VERSION_NUM,sizeof(ImGuiIO),sizeof(ImGuiStyle),sizeof(ImDrawVert),sizeof(ImDrawIdx),sizeof(ImWchar)};
    GakuDesktopUiBinding binding{};
    if(s.desktop.bind(&abi,sizeof(binding),&binding)!=GAKU_OK||!binding.imguiContext||!binding.alloc||!binding.free)return;
    ImGui::SetAllocatorFunctions(binding.alloc,binding.free,binding.allocatorUserData);
    auto* previous=ImGui::GetCurrentContext();ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.imguiContext));
    s.freeCamera.SetTextInput(ImGui::GetIO().WantTextInput);
    struct RestoreContext {ImGuiContext* value;~RestoreContext(){ImGui::SetCurrentContext(value);}} restore{previous};
    UiSnapshot snapshot;std::string legacyId,legacyBody,legacyBodyStatus,legacyHead,legacyHeadStatus;uint64_t legacyRevision{};
    {std::lock_guard<std::mutex> lock(s.mutex);snapshot=s.snapshot;legacyRevision=s.legacyThumbnailRevision;
        if(legacyRevision!=s.ui.ThumbnailRevision()){legacyId=s.legacyThumbnailId;legacyBody=s.legacyThumbnailData;legacyBodyStatus=s.legacyThumbnailStatus;legacyHead=s.legacyHeadData;legacyHeadStatus=s.legacyHeadStatus;}}
    if(legacyRevision!=s.ui.ThumbnailRevision())s.ui.SetThumbnail(legacyRevision,legacyId,legacyBody,legacyBodyStatus,legacyHead,legacyHeadStatus);
    // Render the embedded tools in the loader panel.
    ImGui::TextUnformatted("GakuActors · 场景资源管理");
    ImGui::TextWrapped("%s",snapshot.admissionNotice.c_str());
    if(ImGui::Button(s.dialogOpen?"收起模型界面":"模型界面")){s.dialogOpen=!s.dialogOpen;s.preLiveDialogOpen=false;}
    ImGui::SameLine();ImGui::BeginDisabled(!snapshot.runtime);
    if(ImGui::Button(s.preLiveDialogOpen?"收起 Live 入场前换装":"Live 入场前换装")){s.preLiveDialogOpen=!s.preLiveDialogOpen;s.dialogOpen=false;}
    ImGui::EndDisabled();
    ImGui::TextWrapped("人物目录与头身预设；独立加载默认关闭，可显式启用实验。");
    if(s.dialogOpen) {
        ImGui::Separator();
        ImGui::TextUnformatted("场景资源管理 · 模型界面");
        s.ui.Draw(snapshot);
    }
    if(s.preLiveDialogOpen) {
        s.preLiveUiSeenMs.store(MonotonicMs(),std::memory_order_release);
        ImGui::Separator();
        ImGui::TextUnformatted("Live 入场前换装");
        s.ui.DrawPreLive(snapshot);
    }
    if(!s.preLiveDialogOpen)s.preLiveUiSeenMs.store(0,std::memory_order_release);
    if(!s.ui.commands.empty()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        for(auto& cmd:s.ui.commands)if(s.commands.size()<128) {
            if(cmd.action==Action::LoadThumbnail){s.legacyThumbnailId=cmd.preset.id;s.legacyThumbnailData.clear();s.legacyHeadData.clear();s.legacyThumbnailStatus="读取中";s.legacyHeadStatus="读取中";++s.legacyThumbnailRevision;}
            QueueCommand(s,std::move(cmd));
        }
        s.ui.commands.clear();
    }
}
static void ShutdownImpl(void* context) {
    auto& s=*static_cast<State*>(context);s.stopped=true;
    s.freeCamera.Disable();
    if(s.cameraHandoffRequested&&s.cameraHandoff.request)s.cameraHandoff.request(0);
    s.desktopListener.Stop();
    s.stageHook.Shutdown();
    s.preLiveHook.Shutdown();
    if(s.hotkeys.unregisterHotkey)s.hotkeys.unregisterHotkey(s.hotkeys.context,"show-actors");
    try{s.native.Shutdown();}catch(...){}
}
static void BoundaryError(const char* message) noexcept {
    if(state.log.write)state.log.write(state.log.context,GAKU_LOG_ERROR,message);
}
static GakuStatus Activate(const GakuHostApi* host,void** output) noexcept {
    try{return ActivateImpl(host,output);}catch(...){BoundaryError("[GakuActors] Activation failed");return GAKU_E_INTERNAL;}
}
static void RuntimeReady(void* context) noexcept {
    try{RuntimeReadyImpl(context);}catch(...){state.ready=false;BoundaryError("[GakuActors] Runtime resolution failed");}
}
static void Tick(void* context,float delta) noexcept {
    try{
#ifdef GKMS_ACTORS_INTEGRATED
        if(state.cameraCoreActivated&&state.cameraCore.onTick)state.cameraCore.onTick(state.cameraCoreState,delta);
#endif
        TickImpl(context,delta);
    }catch(...){state.native.EndFrame();BoundaryError("[GakuActors] Unity callback failed");}
}
static void ActorTick(void* context,void* actor,float delta) noexcept {
    try{ActorTickImpl(context,actor,delta);}catch(...){state.gate.Reset();}
}
static void Desktop(void* context) noexcept {
    try{DesktopImpl(context);}catch(...){BoundaryError("[GakuActors] Desktop callback failed");}
}
static void Shutdown(void* context) noexcept {
    try{ShutdownImpl(context);}catch(...){BoundaryError("[GakuActors] Shutdown cleanup failed");}
#ifdef GKMS_ACTORS_INTEGRATED
    try{if(state.cameraCoreActivated&&state.cameraCore.shutdown)state.cameraCore.shutdown(state.cameraCoreState);}catch(...){BoundaryError("[GakuActors] Camera shutdown failed");}
#endif
}
}
extern "C" GAKU_PLUGIN_EXPORT GakuStatus GakuPlugin_Query(uint32_t version,uint32_t bytes,GakuPluginDescriptor* output) {
    if(version!=GAKU_ABI_VERSION)return GAKU_E_VERSION;
    if(!output)return GAKU_E_INVALID_ARGUMENT;if(bytes<sizeof(*output))return GAKU_E_SIZE;
    *output={1,sizeof(*output),"gakuactors","GakuActors · 场景资源管理","0.5.1-native-test",
        GakuActors::Activate,GakuActors::RuntimeReady,GakuActors::Tick,GakuActors::ActorTick,GakuActors::Desktop,GakuActors::Shutdown};
    return GAKU_OK;
}
