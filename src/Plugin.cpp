#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GakuPlugin.h>
#include <GakuPaths.h>
#include "PresetStore.hpp"
#include "NativeApi.hpp"
#include "ActorUi.hpp"
#include "SceneGate.hpp"
#include <atomic>
#include <mutex>
#include <cmath>

namespace GakuActors {
struct State {
    GakuHostApi host{};GakuLogService log{};GakuRuntimeService runtime{};
    GakuDesktopUiService desktop{};GakuDesktopWindowService window{};GakuHotkeyService hotkeys{};
    GakuPathsV1 paths{};NativeApi native;PresetStore store;ActorUi ui;UiSnapshot snapshot;
    std::mutex mutex;std::deque<Command> commands;
    std::atomic<bool> ready{},stopped{};bool catalogRead{},catalogRequested{},sceneRequested{},wasAdmitted{};
    SceneGate gate;
    std::vector<Preset> modelQueue;size_t modelCursor{},modelFailures{};
    bool dialogOpen{false};
    std::chrono::steady_clock::time_point nextScan{},nextCatalog{};
    struct Playback {uint32_t abiVersion,structSize;uint32_t(*isPlaying)();} playback{};
};
static State state;
template<class T> bool Query(State& s,const char* name,T& value) {
    T candidate{};if(s.host.queryService(s.host.hostContext,name,1,sizeof(candidate),&candidate)!=GAKU_OK||
        candidate.abiVersion!=1||candidate.structSize<sizeof(candidate))return false;
    value=candidate;return true;
}
static void Log(State& s,const std::string& message,GakuLogLevel level=GAKU_LOG_INFO) {
    if(s.log.write)s.log.write(s.log.context,level,("[GakuActors] "+message).c_str());
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
    Query(s,GAKU_SERVICE_DESKTOP_UI,s.desktop);Query(s,GAKU_SERVICE_DESKTOP_WINDOW,s.window);
    Query(s,GAKU_SERVICE_HOTKEYS,s.hotkeys);Query(s,GAKU_SERVICE_PATHS,s.paths);
    s.native.trace=[](const char* text){Log(state,text);};
    const auto root=ConfigRoot(s);if(!root.empty())s.store.SetFile(root/L"actor_presets.json");
    s.store.Load();s.snapshot.writable=s.store.Writable();s.snapshot.presets=s.store.presets;s.snapshot.storeNotice=s.store.notice;
    if(s.hotkeys.registerHotkey) {
        const GakuHotkeyBinding key{1,sizeof(key),"show-actors","场景人物管理",'J',GAKU_HOTKEY_MOD_CTRL,Show,nullptr};
        s.hotkeys.registerHotkey(s.hotkeys.context,&key);
    }
    *output=&s;Log(s,"独立人物插件已激活；不依赖 GakuMotion，不接管角色帧更新");return GAKU_OK;
}
static void RuntimeReadyImpl(void* context) {
    auto& s=*static_cast<State*>(context);if(s.stopped)return;
    const bool ready=s.native.Resolve(s.runtime.getGameAssembly());s.ready.store(ready,std::memory_order_release);
    Log(s,ready?"IL2CPP exports resolved; dormant until a Photography/Live actor is observed; catalog requires explicit request":"IL2CPP exports unavailable",ready?GAKU_LOG_INFO:GAKU_LOG_WARNING);
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
    auto& s=*static_cast<State*>(context);if(s.stopped||!s.ready.load(std::memory_order_acquire))return;
    std::deque<Command> commands;
    {std::lock_guard<std::mutex> lock(s.mutex);commands.swap(s.commands);}
    UiSnapshot next;
    {std::lock_guard<std::mutex> lock(s.mutex);next=s.snapshot;}
    next.runtime=true;
    const auto now=std::chrono::steady_clock::now();
    auto publish=[&]{
        next.busy=false;next.mutationsEnabled=s.native.MutationsEnabled();next.notice=s.native.notice;
        next.presets=s.store.presets;next.storeNotice=s.store.notice;next.writable=s.store.Writable();
        std::lock_guard<std::mutex> lock(s.mutex);s.snapshot=std::move(next);
    };
    try {
        // Idle startup takes no Unity/Master/scene-enumeration path. Only an
        // existing actor callback can open the performance-scene gate.
        next.admitted=s.gate.Fresh(MonotonicMs());
        if(next.admitted&&!s.gate.MatchesActive(s.native.ActiveSceneName())){s.gate.Reset();next.admitted=false;}
        s.native.SetAdmittedScene(next.admitted?s.gate.Scene():0);
        next.admissionNotice=next.admitted?(s.gate.Mode()==PerformanceMode::Live?"Live 场景已确认":"拍摄场景已确认"):"当前页面休眠；仅在拍摄或 Live 场景启用";
        if(!next.admitted) {
            s.catalogRequested=false;s.sceneRequested=false;s.modelQueue.clear();s.modelCursor=0;next.actors.clear();next.playing=false;
            if(s.wasAdmitted){s.native.SuspendScene();Log(s,"Performance scene left; catalog/roster scanning suspended");}
            s.wasAdmitted=false;
        } else {
            if(!s.wasAdmitted){Log(s,"Performance scene admitted; waiting for explicit Read Catalog request");s.native.notice="场景已就绪，点击读取游戏目录后使用人物管理";}
            s.wasAdmitted=true;
            if(!s.playback.isPlaying)Query(s,"gakumotion.playback-state",s.playback);
            next.playing=s.playback.isPlaying&&s.playback.isPlaying();
        }
        for(const auto& command:commands) {
            switch(command.action) {
            case Action::Scan:if(next.admitted){s.catalogRequested=true;s.sceneRequested=true;}break;
            case Action::Reload:s.store.Load();break;
            case Action::Save:s.store.Save(command.preset);break;
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
                s.native.RemoveActor(command.instance);s.nextScan={};break;
            case Action::Move:s.native.MoveActor(command.instance,command.position);break;
            }
        }
        if(next.admitted&&s.catalogRequested) {
            s.catalogRequested=false;
            // Explicit, admitted-scene diagnostics describe metadata and read
            // existing models only. Never run cache updates to learn a type.
            try{s.native.DiagnoseContracts();}catch(const std::exception& e){Log(s,std::string("Diagnostic contracts unavailable: ")+e.what(),GAKU_LOG_WARNING);}
            try{s.native.DiagnoseActors();}catch(const std::exception& e){Log(s,std::string("Diagnostic actors unavailable: ")+e.what(),GAKU_LOG_WARNING);}
            Log(s,"Catalog request BEGIN (explicit user action, admitted scene)");
            try{auto catalog=s.native.ScanCatalog();catalog.revision=next.catalog.revision+1;next.catalog=std::move(catalog);s.catalogRead=true;Log(s,"Game master catalog read: "+std::to_string(next.catalog.characters.size())+" characters, "+std::to_string(next.catalog.bodies.size())+" bodies, "+std::to_string(next.catalog.heads.size())+" heads");
                s.modelQueue.clear();s.modelCursor=0;s.modelFailures=0;
                for(const auto& p:next.catalog.defaults)if(p.factory.empty())s.modelQueue.push_back(p);
            }
            catch(const std::exception& e){next.catalog.notice=std::string("目录读取未完成：")+e.what();Log(s,next.catalog.notice,GAKU_LOG_WARNING);}
            Log(s,"Catalog request END; no automatic retries");
        }
        if(next.admitted&&s.modelCursor<s.modelQueue.size()) {
            // Resolve definitions, not assets, at most one outfit per frame.
            // This work is scheduled only by an explicit admitted-scene scan.
            try{next.catalog.models.push_back(s.native.ResolveModelAssets(s.modelQueue[s.modelCursor]));}
            catch(const std::exception& e){if(s.modelFailures++<3)Log(s,std::string("Model definition resolve skipped: ")+e.what(),GAKU_LOG_WARNING);}
            ++s.modelCursor;
            next.catalog.notice="目录已读取；模型资源解析 "+std::to_string(s.modelCursor)+"/"+std::to_string(s.modelQueue.size())+"；资源定义不代表已加载人物";
            if(s.modelCursor==s.modelQueue.size())Log(s,"Model definitions complete: resolved="+std::to_string(next.catalog.models.size())+" skipped="+std::to_string(s.modelFailures));
        }
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
    struct RestoreContext {ImGuiContext* value;~RestoreContext(){ImGui::SetCurrentContext(value);}} restore{previous};
    UiSnapshot snapshot;{std::lock_guard<std::mutex> lock(s.mutex);snapshot=s.snapshot;}
    // A separate floating dialog in the loader desktop, with no Photography UI
    // templates, event injection or changes to the existing motion plugin.
    ImGui::TextUnformatted("GakuActors · 场景人物管理");
    ImGui::TextWrapped("%s",snapshot.admissionNotice.c_str());
    ImGui::BeginDisabled(!snapshot.admitted);
    if(ImGui::Button("打开人物预设弹窗"))s.dialogOpen=true;
    ImGui::EndDisabled();
    ImGui::TextWrapped("保护版：人物目录与头身预设；场景人物操作已停用。");
    if(s.dialogOpen&&snapshot.admitted) {
        const auto display=ImGui::GetIO().DisplaySize;
        ImGui::SetNextWindowSize(ImVec2((std::min)(960.0f,(std::max)(280.0f,display.x-32)),
            (std::min)(820.0f,(std::max)(240.0f,display.y-32))),ImGuiCond_FirstUseEver);
        if(ImGui::Begin("场景人物管理##gakuactors-dialog",&s.dialogOpen))s.ui.Draw(snapshot);
        ImGui::End();
    }
    if(!s.ui.commands.empty()) {
        std::lock_guard<std::mutex> lock(s.mutex);
        for(auto& cmd:s.ui.commands)if(s.commands.size()<128)s.commands.push_back(std::move(cmd));
        s.ui.commands.clear();
    }
}
static void ShutdownImpl(void* context) {
    auto& s=*static_cast<State*>(context);s.stopped=true;
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
    try{TickImpl(context,delta);}catch(...){state.native.EndFrame();BoundaryError("[GakuActors] Unity callback failed");}
}
static void ActorTick(void* context,void* actor,float delta) noexcept {
    try{ActorTickImpl(context,actor,delta);}catch(...){state.gate.Reset();}
}
static void Desktop(void* context) noexcept {
    try{DesktopImpl(context);}catch(...){BoundaryError("[GakuActors] Desktop callback failed");}
}
static void Shutdown(void* context) noexcept {
    try{ShutdownImpl(context);}catch(...){BoundaryError("[GakuActors] Shutdown cleanup failed");}
}
}
extern "C" GAKU_PLUGIN_EXPORT GakuStatus GakuPlugin_Query(uint32_t version,uint32_t bytes,GakuPluginDescriptor* output) {
    if(version!=GAKU_ABI_VERSION)return GAKU_E_VERSION;
    if(!output)return GAKU_E_INVALID_ARGUMENT;if(bytes<sizeof(*output))return GAKU_E_SIZE;
    *output={1,sizeof(*output),"gakuactors","GakuActors · 场景人物管理","0.1.4-diagnostics",
        GakuActors::Activate,GakuActors::RuntimeReady,GakuActors::Tick,GakuActors::ActorTick,GakuActors::Desktop,GakuActors::Shutdown};
    return GAKU_OK;
}
