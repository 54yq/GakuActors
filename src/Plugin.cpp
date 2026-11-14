#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <GakuPlugin.h>
#include <GakuPaths.h>
#include "PresetStore.hpp"
#include "NativeApi.hpp"
#include "ActorUi.hpp"
#include <atomic>
#include <mutex>
#include <cmath>

namespace GakuActors {
struct State {
    GakuHostApi host{};GakuLogService log{};GakuRuntimeService runtime{};
    GakuDesktopUiService desktop{};GakuDesktopWindowService window{};GakuHotkeyService hotkeys{};
    GakuPathsV1 paths{};NativeApi native;PresetStore store;ActorUi ui;UiSnapshot snapshot;
    std::mutex mutex;std::deque<Command> commands;
    std::atomic<bool> ready{},stopped{};bool catalogRead{};
    bool dialogOpen{true};
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
    Log(s,ready?"IL2CPP exports resolved; managed work deferred to Unity thread":"IL2CPP exports unavailable",ready?GAKU_LOG_INFO:GAKU_LOG_WARNING);
}
static void TickImpl(void* context,float) {
    auto& s=*static_cast<State*>(context);if(s.stopped||!s.ready.load(std::memory_order_acquire))return;
    std::deque<Command> commands;
    {std::lock_guard<std::mutex> lock(s.mutex);commands.swap(s.commands);}
    UiSnapshot next;
    {std::lock_guard<std::mutex> lock(s.mutex);next=s.snapshot;}
    next.runtime=true;
    const auto now=std::chrono::steady_clock::now();
    try {
        if(!s.playback.isPlaying)Query(s,"gakumotion.playback-state",s.playback);
        next.playing=s.playback.isPlaying&&s.playback.isPlaying();
        for(const auto& command:commands) {
            switch(command.action) {
            case Action::Scan:s.catalogRead=false;s.nextCatalog={};break;
            case Action::Reload:s.store.Load();break;
            case Action::Save:s.store.Save(command.preset);break;
            case Action::DeletePreset:s.store.Remove(command.preset.id);break;
            case Action::Spawn:
                if(next.playing){s.native.notice="请先在动作插件中停止播放";break;}
                s.native.StartSpawn(command.preset,command.instance);s.nextScan={};break;
            case Action::Remove:
                if(next.playing){s.native.notice="请先在动作插件中停止播放";break;}
                s.native.RemoveActor(command.instance);s.nextScan={};break;
            case Action::Move: {
                const auto own=std::find_if(s.native.owned.begin(),s.native.owned.end(),[&](const auto& o){return o.instance==command.instance;});
                if(own==s.native.owned.end()||own->scene!=s.native.PhotographyScene())break;
                bool finite=true;for(float v:command.position)finite&=std::isfinite(v)&&std::fabs(v)<10000;
                if(!finite)break;
                if(void* actor=s.native.FindActor(own->instance,own->scene)){float pos[3],rotation[4];s.native.Pose(actor,pos,rotation);s.native.SetPose(actor,command.position,rotation);}
                break;
            }
            }
        }
        if(!s.catalogRead&&now>=s.nextCatalog) {
            try{auto catalog=s.native.ScanCatalog();catalog.revision=next.catalog.revision+1;next.catalog=std::move(catalog);s.catalogRead=true;Log(s,"Game master catalog read: "+std::to_string(next.catalog.characters.size())+" characters, "+std::to_string(next.catalog.bodies.size())+" bodies, "+std::to_string(next.catalog.heads.size())+" heads");}
            catch(const std::exception& e){next.catalog.notice=std::string("等待游戏目录就绪：")+e.what();s.nextCatalog=now+std::chrono::seconds(5);}
        }
        s.native.PollSpawn();
        if(now>=s.nextScan){next.actors=s.native.ScanScene();s.nextScan=now+std::chrono::milliseconds(500);}
    } catch(const std::exception& e){s.native.notice=e.what();Log(s,e.what(),GAKU_LOG_WARNING);}
    s.native.EndFrame();
    next.busy=s.native.pending.task!=nullptr;next.notice=s.native.notice;
    next.presets=s.store.presets;next.storeNotice=s.store.notice;next.writable=s.store.Writable();
    {std::lock_guard<std::mutex> lock(s.mutex);s.snapshot=std::move(next);}
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
    if(ImGui::Button("打开人物预设弹窗"))s.dialogOpen=true;
    ImGui::TextWrapped("人物目录、头身预设、加入和移除场景人物。");
    if(s.dialogOpen) {
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
    *output={1,sizeof(*output),"gakuactors","GakuActors · 场景人物管理","0.1.0-dev",
        GakuActors::Activate,GakuActors::RuntimeReady,GakuActors::Tick,nullptr,GakuActors::Desktop,GakuActors::Shutdown};
    return GAKU_OK;
}
