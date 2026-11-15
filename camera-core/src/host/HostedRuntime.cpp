// Hosted-only translation unit: it supplies the platform globals that
// standalone main.cpp/windowsPlatform.cpp own. Compiling it together with those
// files defines every one of them twice, so it is a deliberate no-op unless the
// hosted target asked for it.
#ifdef GKMS_HOSTED

#include "HostedHost.hpp"
#include "HostedBridge.hpp"
#include "FreeCameraInputFence.hpp"

#include "ReloadPipeline.hpp"
#include "InitialTranslationWait.hpp"
#include "WindowSearch.hpp"
#include "platformDefine.hpp"
#include "windowsPlatform.hpp"
#include "GakumasLocalify/Plugin.h"
#include "GakumasLocalify/Log.h"
#include "GakumasLocalify/Local.h"
#include "GakumasLocalify/MasterLocal.h"
#include "GakumasLocalify/Hook.h"
#include "GakumasLocalify/Il2cppUtils.hpp"
#include "GakumasLocalify/camera/camera.hpp"
#include "GakumasLocalify/config/Config.hpp"
#include "deps/UnityResolve/UnityResolve.hpp"
#include "gkmsGUI/GUII18n.hpp"
#include "resourceUpdate/resourceUpdate.hpp"
#include "data/LocalifyData.hpp"

#include <rapidjson/document.h>
#include <rapidjson/istreamwrapper.h>

#include <windowsx.h>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <condition_variable>
#include <deque>
#include <exception>
#include <fstream>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

// Hosted platform layer: it replaces standalone main.cpp/windowsPlatform.cpp for
// the hosted target only. There is no DllMain, no console, no private hook
// engine, no private window/device, and no TerminateProcess: the loader owns all
// of those. What remains is the set of globals and short entry points the shared
// translation and GUI code already calls.
std::filesystem::path gakumasLocalPath = "./gakumas-local";
std::filesystem::path ProgramConfigJson = gakumasLocalPath / "config.json";
std::filesystem::path ConfigJson = gakumasLocalPath / "localizationConfig.json";

bool g_has_config_file = false;
bool g_enable_console = false; // the host owns logging; a second console is a separate process surface
bool g_useRemoteAssets = false;
bool g_useAPIAssets = false;
bool g_useAPITextureAssets = false;
bool g_delTextureRemoteAfterUpdate = true;
std::string g_remoteResourceUrl = "";
std::string g_useAPIAssetsURL = "";
std::string g_useAPITextureAssetsURL = "https://texture.gakumas.cn/api/gkms_texture_data";

void reload_all_data();
std::function<void()> g_reload_all_data = [] { GkmsHosted::RequestReloadAllData(); };

namespace {
std::atomic_bool g_windowHookRequested{false};
std::atomic_bool g_windowHookInstalled{false};
std::atomic_bool g_managedBootstrapDone{false};
std::atomic_bool g_managedBootstrapStarted{false};
GkmsHosted::FreeCameraInputApi g_freeInputApi;
GkmsHosted::FreeInputFence g_freeInputFence;
std::atomic_bool g_freeInputActive{false},g_freeInputScene{true},g_freeInputText{false};
std::atomic_uint64_t g_freeInputWindowKeys{0};
bool g_freeInputConfigured=false;

bool IsFreeCameraKey(UINT key) noexcept {
    switch(key){case 'W':case 'A':case 'S':case 'D':case 'Q':case 'E':case 'R':case 'F':case 'V':case 'I':case 'J':case 'K':case 'L':case VK_UP:case VK_DOWN:case VK_LEFT:case VK_RIGHT:case VK_SPACE:case VK_CONTROL:case VK_LCONTROL:case VK_RCONTROL:case VK_SHIFT:case VK_LSHIFT:case VK_RSHIFT:return true;default:return false;}
}
void ResetFreeCameraKeys(){
    static constexpr int keys[]{'W','A','S','D','Q','E','R','F','V','I','J','K','L',VK_UP,VK_DOWN,VK_LEFT,VK_RIGHT,VK_SPACE,VK_CONTROL,VK_SHIFT};
    for(int key:keys)
        GKCamera::on_cam_rawinput_keyboard(WM_KEYUP,key);
}

// What a Save command needs, captured on the thread that owns those values.
// The worker must never read Config or the resource URLs itself: the panel keeps
// writing them after the command is queued, and a std::string being edited is
// not safe to read from another thread.
struct SaveRequest {
    std::string localizationConfig;
    std::string programConfig;
};

struct Command {
    int kind{0};
    std::shared_ptr<const SaveRequest> save;
};

std::mutex g_commandMutex;
std::condition_variable g_commandReady;
std::deque<Command> g_commands; // 1 = reload, 2 = save, 0 = stop
std::thread g_commandThread;
// Closed at shutdown before the stop marker is appended, so a resource update
// finishing during the join cannot post a Reload into a queue with no reader.
std::atomic_bool g_commandsOpen{true};

// Parsing and publication are separated by the pipeline: a worker only stages a
// snapshot after both parsers returned, and a parse that throws leaves the
// previously published (and any already staged) snapshot untouched.
using TranslationReloadPipeline = GkmsHosted::ReloadPipeline<
    GakumasLocal::Local::Bundle,
    std::shared_ptr<const GakumasLocal::MasterLocal::Snapshot>>;
TranslationReloadPipeline g_reloads;

// The first snapshot has no live readers until RuntimeReady enables the
// translation detours. Publish it before that point without waiting for the
// first Unity frame; later reloads keep their main-thread publication rule.
GkmsHosted::InitialTranslationWait g_initialReload;
std::atomic_bool g_initialTranslationsPublished{false};
std::atomic_bool g_initialStartupRecovery{false};

bool BeginInitialReload() { return g_initialReload.BeginParse(); }
void FinishInitialReload(bool initial, bool parsed) { g_initialReload.FinishParse(initial, parsed); }

constexpr int kCommandReload = 1;
constexpr int kCommandSave = 2;
constexpr int kCommandStop = 0;

void Enqueue(int kind, std::shared_ptr<const SaveRequest> save = nullptr) {
    {
        std::lock_guard<std::mutex> lock(g_commandMutex);
        if (!g_commandsOpen.load(std::memory_order_acquire)) return;
        if (!g_commandThread.joinable()) return;
        if (g_commands.size() >= 8) g_commands.pop_front();
        g_commands.push_back(Command{kind, std::move(save)});
    }
    g_commandReady.notify_one();
}

void RequestPublishOnMainThread() {
    if (!GkmsHosted::g_services.mainThread.enqueue) return;
    if (GkmsHosted::g_services.mainThread.enqueue(GkmsHosted::g_services.mainThread.context,
            [](void*) { GkmsHosted::ApplyPendingReloadOnMainThread(); }, nullptr) != GAKU_OK) {
        GkmsHosted::HostLog(GAKU_LOG_WARNING, "reload parsed but main-thread publication was refused");
    }
}

void CommandLoop() {
    for (;;) {
        Command command;
        {
            std::unique_lock<std::mutex> lock(g_commandMutex);
            g_commandReady.wait(lock, [] { return !g_commands.empty(); });
            command = std::move(g_commands.front());
            g_commands.pop_front();
        }
        if (command.kind == kCommandStop) return;
        // Everything below is file I/O or parsing on a plugin-private thread.
        // A std::thread entry point cannot let an exception escape: a
        // filesystem_error would call std::terminate and take the game down,
        // and the loader's callback isolation never sees this thread. Each
        // command is isolated, the last published snapshot stays live, and the
        // next command still runs.
        bool initialReload = false;
        try {
            if (command.kind == kCommandReload) {
                // Slow part: file I/O and parsing. No host gate, no Unity call.
                // ParseSnapshot reports a source it cannot read by throwing;
                // the pipeline turns that into "keep everything as it was"
                // rather than staging the partially filled bundle.
                const auto started = std::chrono::steady_clock::now();
                initialReload = BeginInitialReload();
                const bool parsed = g_reloads.Reload(
                    [initialReload] {
                        TranslationReloadPipeline::Snapshot snapshot;
                        const auto view = LocalifyData::ReadDisk();
                        LocalifyData::ReadScope resourceScope(view);
                        snapshot.resources = view;
                        snapshot.local = GakumasLocal::Local::ParseSnapshot(initialReload);
                        snapshot.master = GakumasLocal::MasterLocal::ParseSnapshot();
                        return snapshot;
                    },
                    [](const char* what) {
                        GakumasLocal::Log::ErrorFmt(
                            "Translation snapshot parse failed: %s; the published snapshot is unchanged",
                            what);
                    });
                FinishInitialReload(initialReload, parsed);
                if (!parsed) continue;
                const auto parsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                GakumasLocal::Log::InfoFmt(
                    "Translation snapshot parsed in %lld ms on the plugin worker; waiting for the Unity main thread",
                    static_cast<long long>(parsedMs));
                RequestPublishOnMainThread();
            } else if (command.kind == kCommandSave) {
                const auto started = std::chrono::steady_clock::now();
                if (!command.save) {
                    GakumasLocal::Log::Error("Save command carried no captured configuration; nothing written");
                    continue;
                }
                // Only the captured text and the fixed activation-time paths are
                // touched here; the configuration globals stay on the main thread.
                GakumasLocal::Config::WriteConfigFile(ConfigJson, command.save->localizationConfig);
                GkmsResourceUpdate::WriteProgramConfig(command.save->programConfig);
                const auto savedMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                    std::chrono::steady_clock::now() - started).count();
                GakumasLocal::Log::InfoFmt("Configuration saved on the plugin worker in %lld ms (config_exists=%d)",
                    static_cast<long long>(savedMs), std::filesystem::exists(ConfigJson) ? 1 : 0);
            }
        } catch (const std::exception& error) {
            FinishInitialReload(initialReload, false);
            GakumasLocal::Log::ErrorFmt(
                "Hosted command %d failed: %s; the published snapshot is unchanged and later commands continue",
                command.kind, error.what());
        } catch (...) {
            FinishInitialReload(initialReload, false);
            GakumasLocal::Log::ErrorFmt(
                "Hosted command %d failed with a non-standard exception; later commands continue", command.kind);
        }
    }
}

void ReadProgramConfigFile() {
    std::ifstream config_stream{ProgramConfigJson};
    if (!config_stream.is_open()) return;
    rapidjson::IStreamWrapper wrapper{config_stream};
    rapidjson::Document document;
    document.ParseStream(wrapper);
    if (document.HasParseError()) return;
    g_has_config_file = true;
    if (document.HasMember("useRemoteAssets") && document["useRemoteAssets"].IsBool())
        g_useRemoteAssets = document["useRemoteAssets"].GetBool();
    if (document.HasMember("transRemoteZipUrl") && document["transRemoteZipUrl"].IsString())
        g_remoteResourceUrl = document["transRemoteZipUrl"].GetString();
    if (document.HasMember("useAPIAssets") && document["useAPIAssets"].IsBool())
        g_useAPIAssets = document["useAPIAssets"].GetBool();
    if (document.HasMember("useAPIAssetsURL") && document["useAPIAssetsURL"].IsString())
        g_useAPIAssetsURL = document["useAPIAssetsURL"].GetString();
    if (document.HasMember("useAPITextureAssets") && document["useAPITextureAssets"].IsBool())
        g_useAPITextureAssets = document["useAPITextureAssets"].GetBool();
    if (document.HasMember("useAPITextureAssetsURL") && document["useAPITextureAssetsURL"].IsString())
        g_useAPITextureAssetsURL = document["useAPITextureAssetsURL"].GetString();
    if (document.HasMember("delTextureRemoteAfterUpdate") && document["delTextureRemoteAfterUpdate"].IsBool())
        g_delTextureRemoteAfterUpdate = document["delTextureRemoteAfterUpdate"].GetBool();
}


void SwitchFullScreen(HWND hWnd) {
    static auto Screen_SetResolution = reinterpret_cast<void (*)(UINT, UINT, UINT, void*)>(
        Il2cppUtils::il2cpp_resolve_icall("UnityEngine.Screen::SetResolution_Injected(System.Int32,System.Int32,UnityEngine.FullScreenMode,UnityEngine.RefreshRate&)"));
    static auto get_Height = reinterpret_cast<int (*)()>(Il2cppUtils::il2cpp_resolve_icall("UnityEngine.Screen::get_height()"));
    static auto get_Width = reinterpret_cast<int (*)()>(Il2cppUtils::il2cpp_resolve_icall("UnityEngine.Screen::get_width()"));
    if (!Screen_SetResolution || !get_Height || !get_Width) return;

    LONG style = GetWindowLong(hWnd, GWL_STYLE);
    const bool currentFullScreen = (style & WS_POPUP) != 0;
    static int savedWidth = -1;
    static int savedHeight = -1;
    int64_t refreshRate[3];
    refreshRate[0] = 0x100000000LL;

    if (currentFullScreen) {
        if (savedWidth == -1) {
            savedWidth = 542;
            savedHeight = 990;
        }
        Screen_SetResolution(savedWidth, savedHeight, 2 * !false + 1, refreshRate);
    } else {
        savedWidth = get_Width();
        savedHeight = get_Height();
        Screen_SetResolution(GetSystemMetrics(SM_CXSCREEN), GetSystemMetrics(SM_CYSCREEN), 2 * !true + 1, refreshRate);
    }
}
} // namespace

namespace GkmsHosted {
uint32_t FreeCameraShortcutBlockReady(){return g_freeInputActive.load(std::memory_order_acquire)?1u:0u;}
void FreeCameraShortcutBlockStats(uint64_t* windowKeys,uint64_t* unityQueries,uint64_t* cameraPushes){if(windowKeys)*windowKeys=g_freeInputWindowKeys.load(std::memory_order_relaxed);if(unityQueries)*unityQueries=g_freeInputFence.BlockedCount();if(cameraPushes)*cameraPushes=GakumasLocal::HookMain::FreeCameraPushCount();}
void StopFreeCameraShortcutBlock(){
    if(g_freeInputActive.exchange(false,std::memory_order_acq_rel))ResetFreeCameraKeys();
    if(g_freeInputFence.HasLease()&&!g_freeInputFence.Release())HostLog(GAKU_LOG_WARNING,"Free camera shortcut block: some hooks could not be released");
}
void TickFreeCameraShortcutBlock(){
    const bool wanted=GakumasLocal::Config::enableFreeCamera&&GakumasLocal::Config::blockFreeCameraShortcuts;
    if(!wanted){if(g_freeInputActive.load(std::memory_order_acquire)||g_freeInputFence.HasLease())StopFreeCameraShortcutBlock();return;}
    if(g_freeInputActive.load(std::memory_order_acquire)){
        static auto nextReport=std::chrono::steady_clock::time_point{};const auto now=std::chrono::steady_clock::now();
        if(now>=nextReport){nextReport=now+std::chrono::seconds(5);HostLog(GAKU_LOG_INFO,"Free camera shortcut block: forwarded_window_keys="+std::to_string(g_freeInputWindowKeys.load(std::memory_order_relaxed))+" blocked_unity_queries="+std::to_string(g_freeInputFence.BlockedCount()));}
        return;
    }
    if(!g_windowHookInstalled.load(std::memory_order_acquire))InstallWindowHookOnMainThread();
    if(!g_windowHookInstalled.load(std::memory_order_acquire)){GakumasLocal::Config::enableFreeCamera=false;HostLog(GAKU_LOG_ERROR,"Free camera shortcut block: game keyboard chain unavailable; free camera disabled");return;}
    HWND window=FindWindowW(L"UnityWndClass",L"gakumas");DWORD pid{};if(window)GetWindowThreadProcessId(window,&pid);
    if(!window||pid!=GetCurrentProcessId())return;
    if(!g_freeInputConfigured){
        if(!g_freeInputApi.Resolve(GameAssembly())){GakumasLocal::Config::enableFreeCamera=false;HostLog(GAKU_LOG_ERROR,"Free camera shortcut block: IL2CPP input exports unavailable; free camera disabled");return;}
        g_freeInputFence.Configure(&g_freeInputApi,g_services.hooks,&g_freeInputActive,&g_freeInputScene,&g_freeInputText);g_freeInputConfigured=true;
    }
    if(!g_freeInputFence.Install(window)){GakumasLocal::Config::enableFreeCamera=false;HostLog(GAKU_LOG_ERROR,"Free camera shortcut block: Unity input hooks unavailable; free camera disabled");return;}
    g_freeInputWindowKeys.store(0,std::memory_order_relaxed);g_freeInputActive.store(true,std::memory_order_release);
    HostLog(GAKU_LOG_INFO,"Free camera shortcut block enabled; Localify camera input preserved");
}
}

namespace GakumasLocal::WinHooks {
namespace Keyboard {
WNDPROC g_pfnOldWndProc = nullptr;

void keyboardEvents(int action, int key_code) {
#ifndef GKMS_CAMERA_ONLY
    GKCamera::on_cam_rawinput_keyboard(action, key_code);
    const auto message = GakumasLocal::Local::OnKeyDown(action, key_code);
    if (!message.empty()) GakumasLocal::Log::Info(message.c_str());
#endif
}

LRESULT CALLBACK WndProcCallback(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_SYSKEYUP:
        case WM_KEYUP:
        case WM_SYSKEYDOWN:
        case WM_KEYDOWN: {
            if(g_freeInputActive.load(std::memory_order_acquire)&&GetForegroundWindow()==hWnd&&IsFreeCameraKey(static_cast<UINT>(wParam))){
                g_freeInputWindowKeys.fetch_add(1,std::memory_order_relaxed);
                GKCamera::on_cam_rawinput_keyboard(static_cast<int>(uMsg),static_cast<int>(wParam));
                return 0;
            }
            // Ctrl+U and F11 are owner-local loader hotkeys now, and WM_CLOSE
            // belongs to the loader's quit path. Only per-key game input that a
            // hotkey registry cannot express stays here.
            keyboardEvents(uMsg, static_cast<int>(wParam));
        } break;
        case WM_NCACTIVATE:
        case WM_KILLFOCUS: {
            if(uMsg==WM_KILLFOCUS&&g_freeInputActive.load(std::memory_order_acquire))ResetFreeCameraKeys();
            if (!wParam) return FALSE;
        } break;
        case WM_CHAR: {
            const auto c=static_cast<UINT>(wParam);
            if(g_freeInputActive.load(std::memory_order_acquire)&&GetForegroundWindow()==hWnd&&IsFreeCameraKey(c>='a'&&c<='z'?c-32:c))return 0;
        } break;
        case WM_INPUT: {
            if(g_freeInputActive.load(std::memory_order_acquire)&&GetForegroundWindow()==hWnd){
                RAWINPUT raw{};UINT size=sizeof(raw);const UINT read=GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER));
                if(read!=static_cast<UINT>(-1)&&read>=offsetof(RAWINPUT,data)+sizeof(RAWKEYBOARD)&&raw.header.dwType==RIM_TYPEKEYBOARD&&IsFreeCameraKey(raw.data.keyboard.VKey)){
                    if(GET_RAWINPUT_CODE_WPARAM(wParam)==RIM_INPUT)DefWindowProcW(hWnd,uMsg,wParam,lParam);
                    return 0;
                }
            }
        } break;
        case WM_NCHITTEST: {
            if (GakumasLocal::Config::dmmUnlockSize) {
                POINT point = {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
                ScreenToClient(hWnd, &point);
                RECT client{};
                GetClientRect(hWnd, &client);
                const int border = 8;
                const bool left = point.x < border;
                const bool right = point.x >= client.right - border;
                const bool top = point.y < border;
                const bool bottom = point.y >= client.bottom - border;
                if (top && left) return HTTOPLEFT;
                if (top && right) return HTTOPRIGHT;
                if (bottom && left) return HTBOTTOMLEFT;
                if (bottom && right) return HTBOTTOMRIGHT;
                if (left) return HTLEFT;
                if (right) return HTRIGHT;
                if (top) return HTTOP;
                if (bottom) return HTBOTTOM;
                return HTCLIENT;
            }
        } break;
        case WM_GETMINMAXINFO: {
            if (GakumasLocal::Config::dmmUnlockSize) {
                auto* info = reinterpret_cast<LPMINMAXINFO>(lParam);
                info->ptMaxTrackSize.x = GetSystemMetrics(SM_CXSCREEN) * 3;
                info->ptMaxTrackSize.y = GetSystemMetrics(SM_CYSCREEN) * 3;
                info->ptMinTrackSize.x = 200;
                info->ptMinTrackSize.y = 200;
                return 1;
            }
        } break;
        case WM_SYSCOMMAND: {
            if (GakumasLocal::Config::dmmUnlockSize && (wParam & 0xFFF0) == SC_MAXIMIZE) {
                SwitchFullScreen(hWnd);
                return 1;
            }
        } break;
        case WM_NCPAINT: {
            if (GakumasLocal::Config::dmmUnlockSize) {
                LONG style = GetWindowLong(hWnd, GWL_STYLE);
                if (!(style & WS_POPUP)) {
                    style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
                    SetWindowLong(hWnd, GWL_STYLE, style);
                }
            }
        } break;
        default: break;
    }
    return CallWindowProc(g_pfnOldWndProc, hWnd, uMsg, wParam, lParam);
}

void InstallWndProcHook() {
    // Called from the loader worker during injection; the actual subclass must
    // happen on the game window's owning thread.
    g_windowHookRequested.store(true, std::memory_order_release);
}
}

void* LoadAssetBundle(const std::filesystem::path& path) {
    using Il2cppString = UnityResolve::UnityType::String;
    const std::filesystem::path absolutePath = std::filesystem::absolute(path).lexically_normal();
    Il2cppString* bundlePath = Il2cppString::New(LocalifyData::EnginePath(absolutePath));

    static auto LoadFromFileAsync = Il2cppUtils::GetMethod("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundle", "LoadFromFileAsync");
    static auto get_assetBundle = Il2cppUtils::GetMethod("UnityEngine.AssetBundleModule.dll", "UnityEngine", "AssetBundleCreateRequest", "get_assetBundle");

    auto bundleTask = LoadFromFileAsync->Invoke<void*>(bundlePath);
    if (!bundleTask) return nullptr;
    return get_assetBundle->Invoke<void*>(bundleTask);
}
}

void readProgramConfig() {
    ReadProgramConfigFile();
}

void loadConfig(const std::string& configJson) {
    GakumasLocal::Config::LoadConfig(configJson);
}

void loadConfig(const std::filesystem::path& filePath) {
    if (!std::filesystem::exists(ProgramConfigJson)) {
        // A new hosted installation needs a discoverable first translation.
        // Existing configuration and imported legacy layouts retain their choice.
        if (LocalifyData::UseFreshHostedApiDefault(*LocalifyData::Capture()))
            g_useAPIAssets = true;
        g_useAPIAssetsURL = GkmsGUII18n::ts("default_assets_check_api");
        GkmsResourceUpdate::CreateProgramConfigIfMissing();
        readProgramConfig();
    }
    if (!std::filesystem::exists(filePath)) {
        GakumasLocal::Config::CreateDefaultFile(filePath);
    }
    std::ifstream file(filePath);
    if (!file.is_open()) {
        GakumasLocal::Log::ErrorFmt("Load config %s failed.", LocalifyData::Utf8(filePath).c_str());
        loadConfig(std::string("{}"));
        return;
    }
    std::string fileContent((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    file.close();
    loadConfig(fileContent);
}

void reload_all_data() {
    GkmsHosted::RequestReloadAllData();
}

bool getCurrentLodingProgress(int* stepTotal, int* stepCurrent, int* currTotal, int* currCurrent) {
    *stepTotal = UnityResolveProgress::assembliesProgress.total;
    *stepCurrent = UnityResolveProgress::assembliesProgress.current;
    *currTotal = UnityResolveProgress::classProgress.total;
    *currCurrent = UnityResolveProgress::classProgress.current;
    return UnityResolveProgress::startInit;
}

// Hosted logging: every plugin diagnostic lands in the loader's private log
// instead of a per-translation-unit file or stdout.
void __android_log_write(int prio, const char* tag, const char* msg) {
    const auto level = prio >= ANDROID_LOG_ERROR ? GAKU_LOG_ERROR :
        prio == ANDROID_LOG_INFO ? GAKU_LOG_INFO : GAKU_LOG_WARNING;
    std::string line;
    if (tag && tag[0]) {
        line.append(tag).append(": ");
    }
    line.append(msg ? msg : "");
    GkmsHosted::HostLog(level, line);
}

namespace GkmsHosted {

void StartRuntime() {
    g_initialReload.Reset();
    g_initialTranslationsPublished.store(false, std::memory_order_release);
    g_initialStartupRecovery.store(false, std::memory_order_release);
    g_commandsOpen.store(true, std::memory_order_release);
    g_commandThread = std::thread(CommandLoop);
    GakumasLocal::Log::Info("Hosted Localify runtime started");
}

void StopRuntime() {
    {
        std::lock_guard<std::mutex> lock(g_commandMutex);
        if (!g_commandThread.joinable()) return;
        // Close the queue before appending the stop marker: a resource update
        // task that finishes while we are joining must not enqueue a Reload
        // behind it. The shutdown handler queues its save before this call, so
        // that save is already in front of the marker.
        g_commandsOpen.store(false, std::memory_order_release);
        g_commands.push_back(Command{kCommandStop, nullptr});
    }
    g_commandReady.notify_all();
    g_commandThread.join();
}

void RequestReloadAllData() {
    Enqueue(kCommandReload);
}

void RequestSaveAllData() {
    // Capture now, on the caller's thread (the panel or the shutdown callback).
    // The worker only writes the captured text, so editing the panel while a
    // save is in flight cannot race a read of the same std::string.
    auto request = std::make_shared<SaveRequest>();
    request->localizationConfig = GakumasLocal::Config::SerializeConfig();
    request->programConfig = GkmsResourceUpdate::SerializeProgramConfig();
    Enqueue(kCommandSave, std::move(request));
}

bool ManagedBootstrapDone() {
    return g_managedBootstrapDone.load(std::memory_order_acquire);
}

bool PrepareInitialTranslationsBeforeHooks() {
    // RuntimeReady holds the host callback gate and no Localify detour exists
    // yet. The parser may finish earlier or later; do not expose empty maps to
    // the first intercepted UI construction in either case.
    const auto started = GetTickCount64();
    bool slow = false;
    const auto result = g_initialReload.Wait(std::chrono::seconds(10), std::chrono::milliseconds(100),
        [] { return !g_commandsOpen.load(std::memory_order_acquire) || HostIsStopping(); },
        [&] {
            slow = true;
            HostLog(GAKU_LOG_WARNING, "[StartupTiming] initial translation parse exceeded startup timeout; "
                "continuing to wait for automatic recovery uptime_ms=" + std::to_string(ProcessUptimeMs()));
        });
    if (result == InitialTranslationWait::Result::Cancelled || HostIsStopping()) {
        HostLog(GAKU_LOG_INFO, "[StartupTiming] initial translation wait cancelled by host shutdown");
        return false;
    }
    if (result == InitialTranslationWait::Result::Failed) {
        HostLog(GAKU_LOG_ERROR, "[StartupTiming] initial translation parse failed; detours stay disabled "
            "uptime_ms=" + std::to_string(ProcessUptimeMs()) + " thread=" +
            std::to_string(GetCurrentThreadId()));
        return false;
    }
    if (slow) {
        HostLog(GAKU_LOG_INFO, "[StartupTiming] initial translation parse completed after startup timeout; "
            "resuming initialization uptime_ms=" + std::to_string(ProcessUptimeMs()));
    }
    TranslationReloadPipeline::Snapshot snapshot;
    if (!g_reloads.TakePending(snapshot)) {
        HostLog(GAKU_LOG_ERROR, "Initial translation snapshot unavailable before hooks; detours stay disabled");
        return false;
    }
    // Native maps and version caches only. Asset bundles, fonts, camera state
    // and any retranslation remain Unity main-thread work.
    LocalifyData::Publish(std::static_pointer_cast<const LocalifyData::Snapshot>(snapshot.resources));
    GakumasLocal::Local::PublishSnapshot(std::move(snapshot.local));
    GakumasLocal::MasterLocal::PublishSnapshot(std::move(snapshot.master));
    GkmsResourceUpdate::GetCurrentResourceVersion(false);
    GkmsResourceUpdate::GetCurrentTextureVersion(false);
    g_initialStartupRecovery.store(slow, std::memory_order_release);
    g_initialTranslationsPublished.store(true, std::memory_order_release);
    HostLog(GAKU_LOG_INFO, "[StartupTiming] initial native translation snapshot published uptime_ms=" +
        std::to_string(ProcessUptimeMs()) + " thread=" + std::to_string(GetCurrentThreadId()) +
        " wait_ms=" + std::to_string(GetTickCount64() - started));
    return true;
}

void EnsureManagedBootstrapOnMainThread() {
    // Idempotent because the runtime-ready task and later reloads share this
    // entry. Reloads require the replacement font before retranslation runs.
    if (g_managedBootstrapStarted.exchange(true)) return;
    const auto started = GetTickCount64();
    HostLog(GAKU_LOG_INFO, "[StartupTiming] managed bootstrap begin uptime_ms=" +
        std::to_string(ProcessUptimeMs()) + " thread=" + std::to_string(GetCurrentThreadId()));
    GakumasLocal::HookMain::StartManagedBootstrap();
    GKCamera::initCameraSettings();
    // A slow parse can outlive the startup boundary. Repair existing labels
    // after maps/hooks/fonts are ready, without adding sweeps to fast startup.
    if (g_initialStartupRecovery.exchange(false, std::memory_order_acq_rel)) {
        GakumasLocal::HookMain::RetranslateAllTexts("slow initial translation startup completed");
        GakumasLocal::HookMain::ScheduleRetranslateSweeps();
    }
    g_managedBootstrapDone.store(true, std::memory_order_release);
    HostLog(GAKU_LOG_INFO, "[StartupTiming] managed bootstrap done uptime_ms=" +
        std::to_string(ProcessUptimeMs()) + " thread=" + std::to_string(GetCurrentThreadId()) +
        " elapsed_ms=" + std::to_string(GetTickCount64() - started));
}

void ApplyPendingReloadOnMainThread() {
    // A real initial parse failure/cancellation disables injection. In that state an
    // already queued publication must not invoke managed bootstrap against an
    // uninitialized resolver. Initial publication has its own pre-detour path.
    if (!g_initialTranslationsPublished.load(std::memory_order_acquire)) return;
    TranslationReloadPipeline::Snapshot snapshot;
    if (!g_reloads.TakePending(snapshot)) return;
    LocalifyData::Publish(std::static_pointer_cast<const LocalifyData::Snapshot>(snapshot.resources));
    // The replacement font lives in an asset bundle. Publication now re-applies
    // text to labels that are already on screen, so the bundle has to be in
    // place first or those labels would render Chinese with the original atlas.
    EnsureManagedBootstrapOnMainThread();
    GakumasLocal::HookMain::ReloadResourceAssets();
    // Short work only: config re-read, snapshot publication, cached versions.
    ReadProgramConfigFile();
    loadConfig(ConfigJson);
    GakumasLocal::Local::PublishSnapshot(std::move(snapshot.local));
    GakumasLocal::MasterLocal::PublishSnapshot(std::move(snapshot.master));
    GkmsResourceUpdate::GetCurrentResourceVersion(false);
    GkmsResourceUpdate::GetCurrentTextureVersion(false);
    // Text that was built before this snapshot existed still shows the source
    // string; the title screen is created before the runtime-ready phase, so it
    // would otherwise stay Japanese for the whole session.
    GakumasLocal::HookMain::RetranslateAllTexts("translation snapshot published");
    // Labels built after this call (title screen, menus) need their own sweeps.
    GakumasLocal::HookMain::ScheduleRetranslateSweeps();
    GakumasLocal::Log::Info("Translation snapshot published on the Unity main thread");
}

void InstallWindowHookOnMainThread() {
    if (!g_windowHookRequested.load(std::memory_order_acquire)) return;
    if (g_windowHookInstalled.exchange(true)) return;
    using namespace GakumasLocal::WinHooks::Keyboard;
    HWND window = FindOwnUnityWindow();
    if (!window) {
        g_windowHookInstalled.store(false);
        return;
    }
    g_pfnOldWndProc = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    if(!g_pfnOldWndProc){g_windowHookInstalled.store(false);GakumasLocal::Log::Error("Game window keyboard chain: previous procedure unavailable");return;}
    SetLastError(0);
    const LONG_PTR previous=SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(WndProcCallback));
    if(!previous&&GetLastError()!=0){g_windowHookInstalled.store(false);GakumasLocal::Log::Error("Game window keyboard chain: installation failed");return;}
#ifndef GKMS_CAMERA_ONLY
    LONG style = GetWindowLongW(window, GWL_STYLE);
    style |= WS_THICKFRAME | WS_MAXIMIZEBOX;
    SetWindowLongW(window, GWL_STYLE, style);
#endif
    GakumasLocal::Log::Info("Game window keyboard chain installed on the window thread");
}

void ToggleGameFullScreen() {
    if (!GakumasLocal::Config::dmmUnlockSize) return;
    if (HWND window = FindOwnUnityWindow()) SwitchFullScreen(window);
}

}

#endif // GKMS_HOSTED
