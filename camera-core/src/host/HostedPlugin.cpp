// Hosted-only translation unit. The standalone project also globs ./src/**, so
// this file must be a no-op there: without the guard it would need
// ActorEntryCallback (declared only for the hosted ABI), duplicate the
// standalone platform globals and define a second plugin entry point. The
// standalone premake manifest also excludes src/host; the guard keeps a naive
// "compile every .cpp" build working instead of turning it into a link error.
#ifdef GKMS_HOSTED

#include "HostedHost.hpp"
#include "GakuDiagnostics.hpp"
#include "HostedBridge.hpp"
#include "CameraWriteProvider.hpp"
#include "LanguageConsumer.hpp"

#include "platformDefine.hpp"
#include "windowsPlatform.hpp"
#include "GakumasLocalify/Plugin.h"
#include "GakumasLocalify/Hook.h"
#include "GakumasLocalify/Log.h"
#include "GakumasLocalify/Local.h"
#include "GakumasLocalify/MasterLocal.h"
#include "GakumasLocalify/Misc.hpp"
#include "GakumasLocalify/camera/camera.hpp"
#include "GakumasLocalify/config/Config.hpp"
#include "gkmsGUI/gkmsGUILoop.hpp"
#include "resourceUpdate/resourceUpdate.hpp"
#include "data/LocalifyData.hpp"
#include "imgui/imgui.h"

#include <atomic>
#include <chrono>
#include <memory>

namespace {
std::atomic_bool g_guiBindFailureLogged{false};
std::atomic_bool g_injected{false};
std::atomic_bool g_firstPanelFrameLogged{false};
GkmsHosted::CameraWriteProvider g_cameraWrites;
GkmsHosted::CameraRenderProvider g_cameraRenders;
}

namespace GkmsHosted {
Services g_services{};
bool g_servicesReady = false;
bool g_diagnosticsEnabled = false;
bool DiagnosticsEnabled() { return g_diagnosticsEnabled; }

unsigned long long ProcessUptimeMs() {
    FILETIME created{}, exited{}, kernel{}, user{};
    if (!GetProcessTimes(GetCurrentProcess(), &created, &exited, &kernel, &user)) return 0;
    FILETIME now{};
    GetSystemTimeAsFileTime(&now);
    ULARGE_INTEGER start{}, current{};
    start.LowPart = created.dwLowDateTime;
    start.HighPart = created.dwHighDateTime;
    current.LowPart = now.dwLowDateTime;
    current.HighPart = now.dwHighDateTime;
    return current.QuadPart > start.QuadPart ? (current.QuadPart - start.QuadPart) / 10000ULL : 0;
}

void HostLog(GakuLogLevel level, const std::string& message) {
    if (level == GAKU_LOG_INFO && !g_diagnosticsEnabled) return;
    if (g_services.log.write) {
        g_services.log.write(g_services.log.context, level, message.c_str());
        return;
    }
    OutputDebugStringA(message.c_str());
}

std::filesystem::path LocalRoot() {
    return g_services.runtime.localRoot ? std::filesystem::path(g_services.runtime.localRoot)
                                        : std::filesystem::path(L"gakumas-local");
}

void* GameAssembly() {
    return g_services.runtime.getGameAssembly ? g_services.runtime.getGameAssembly() : nullptr;
}

bool HostIsStopping() {
    return g_services.lifecycle.isStopping &&
        g_services.lifecycle.isStopping(g_services.lifecycle.context) != 0;
}

bool QueryServices(const GakuHostApi* host) {
    if (!host || host->abiVersion != GAKU_ABI_VERSION || !host->queryService) return false;
    g_services.host = *host;
    const auto query = [&](const char* name, uint32_t size, void* table) {
        return host->queryService(host->hostContext, name, GAKU_SERVICE_VERSION, size, table) == GAKU_OK;
    };
    const bool required =
        query(GAKU_SERVICE_LOG, sizeof(g_services.log), &g_services.log) &&
        query(GAKU_SERVICE_RUNTIME, sizeof(g_services.runtime), &g_services.runtime) &&
        query(GAKU_SERVICE_MAIN_THREAD, sizeof(g_services.mainThread), &g_services.mainThread) &&
        query(GAKU_SERVICE_HOOKS, sizeof(g_services.hooks), &g_services.hooks) &&
        query(GAKU_SERVICE_HOTKEYS, sizeof(g_services.hotkeys), &g_services.hotkeys) &&
        query(GAKU_SERVICE_ACTOR_ENTRY, sizeof(g_services.actorEntry), &g_services.actorEntry);
    if (!required) return false;
    g_diagnosticsEnabled = GakuDiagnostics::Enabled(g_services.runtime.gameRoot);
    // The panel is optional in the sense that a headless host is still valid;
    // without it Localify keeps translating and simply has no window.
    query(GAKU_SERVICE_DESKTOP_UI, sizeof(g_services.desktopUi), &g_services.desktopUi);
    query(GAKU_SERVICE_DESKTOP_WINDOW, sizeof(g_services.desktopWindow), &g_services.desktopWindow);
    g_services.language = QueryUiLanguage(host);
    RefreshUiLanguage(g_services.language);
    // An older host can still load the plugin. Paired hosts expose cancellation
    // before shutdown waits for this runtime-ready callback to release its gate.
    g_services.lifecycle = {};
    GakuLifecycleServiceV1 lifecycle{};
    if (host->queryService(host->hostContext, GAKU_SERVICE_LIFECYCLE,
            GAKU_LIFECYCLE_VERSION, sizeof(lifecycle), &lifecycle) == GAKU_OK &&
        lifecycle.abiVersion == GAKU_LIFECYCLE_VERSION &&
        lifecycle.structSize >= sizeof(lifecycle) && lifecycle.isStopping) {
        g_services.lifecycle = lifecycle;
    }
    g_services.paths = {};
    GakuPathsV1 paths{};
    const auto pathStatus = host->queryService(host->hostContext, GAKU_SERVICE_PATHS,
        GAKU_PATHS_VERSION, sizeof(paths), &paths);
    if (pathStatus == GAKU_OK) {
        if (paths.abiVersion != GAKU_PATHS_VERSION || paths.structSize < sizeof(paths) ||
            !paths.getRoot || !paths.ensureRoot) return false;
        g_services.paths = paths;
    } else if (pathStatus != GAKU_E_UNAVAILABLE) {
        return false;
    }
    g_servicesReady = true;
    return true;
}

bool RegisterActorEntryBefore(const char* name, ActorEntryCallback callback) {
    if (!g_servicesReady || !g_services.actorEntry.registerEntry) return false;
    GakuActorEntryBinding binding{GAKU_SERVICE_VERSION, sizeof(GakuActorEntryBinding),
        GAKU_ACTOR_ENTRY_BEFORE, name, callback, nullptr};
    const auto status = g_services.actorEntry.registerEntry(g_services.actorEntry.context, &binding);
    HostLog(status == GAKU_OK ? GAKU_LOG_INFO : GAKU_LOG_ERROR,
        std::string("actor-entry before registration ") + (status == GAKU_OK ? "ok" : "failed") +
        " (status=" + std::to_string(status) + ")");
    return status == GAKU_OK;
}

void RecordCameraWriteTarget(uint32_t kind, void* target) {
    g_cameraWrites.SetInstalledTarget(kind, target);
}

void NotifyCameraWrite(uint32_t kind, void* transformSelf) {
    try {
        g_cameraWrites.Notify(kind, transformSelf);
    } catch (...) {
        static std::atomic_bool reported{false};
        if (!reported.exchange(true)) HostLog(GAKU_LOG_ERROR,
            "camera write-after observer threw; game original already completed");
    }
}

void RecordCameraRenderTarget(uint32_t kind, void* target) {
    g_cameraRenders.SetInstalledTarget(kind, target);
}

bool NotifyMainCamera(void** camera) {
    return g_cameraRenders.DispatchMain(camera);
}

CameraRenderPushDispatch BeforeCameraRenderPush(void* self, void* state) {
    return g_cameraRenders.DispatchBeforePush(self, state);
}

void AfterCameraRenderPush(const CameraRenderPushDispatch& dispatch, void* self) {
    g_cameraRenders.DispatchAfterPush(dispatch, self);
}

bool NotifyEndCamera(void* camera) {
    return g_cameraRenders.DispatchEnd(camera);
}
}

namespace {

class HostHookInstaller final : public GakumasLocal::HookInstaller {
public:
    HostHookInstaller(const std::string& il2cppLibraryPath, const std::string& localizationFilesDir) {
        m_il2cppLibraryPath = il2cppLibraryPath;
        this->localizationFilesDir = localizationFilesDir;
    }

    // The loader service already enables the detour; a null return means
    // success, exactly like the standalone MinHook wrapper's MH_OK.
    void* InstallHook(void* addr, void* hook, void** orig) override {
        const auto started = GetTickCount64();
        const auto status = GkmsHosted::g_services.hooks.acquire(
            GkmsHosted::g_services.hooks.context, addr, hook, orig);
        const auto elapsed = GetTickCount64() - started;
        ++m_acquireCount;
        m_acquireMs += elapsed;
        if (elapsed >= 50) {
            ++m_slowAcquireCount;
            if (m_slowAcquireCount <= 8) {
                GkmsHosted::HostLog(GAKU_LOG_INFO, "[StartupTiming] slow hook acquire target=" +
                    std::to_string(reinterpret_cast<uintptr_t>(addr)) + " elapsed_ms=" +
                    std::to_string(elapsed) + " status=" + std::to_string(status) +
                    " uptime_ms=" + std::to_string(GkmsHosted::ProcessUptimeMs()) +
                    " thread=" + std::to_string(GetCurrentThreadId()));
            }
        }
        if (status == GAKU_OK) return nullptr;
        return reinterpret_cast<void*>(static_cast<intptr_t>(status));
    }

    void ReportStartupTiming() const {
        GkmsHosted::HostLog(GAKU_LOG_INFO, "[StartupTiming] hook acquire summary count=" +
            std::to_string(m_acquireCount) + " total_ms=" + std::to_string(m_acquireMs) +
            " slow_count=" + std::to_string(m_slowAcquireCount) + " uptime_ms=" +
            std::to_string(GkmsHosted::ProcessUptimeMs()) + " thread=" +
            std::to_string(GetCurrentThreadId()));
    }

    GakumasLocal::OpaqueFunctionPointer LookupSymbol(const char* name) override {
        auto* module = static_cast<HMODULE>(GkmsHosted::GameAssembly());
        return reinterpret_cast<GakumasLocal::OpaqueFunctionPointer>(
            module ? GetProcAddress(module, name) : nullptr);
    }

private:
    unsigned int m_acquireCount{};
    unsigned int m_slowAcquireCount{};
    ULONGLONG m_acquireMs{};
};

#ifdef GKMS_CAMERA_ONLY
GakuStatus Activate(const GakuHostApi* host, void**) {
    if (!GkmsHosted::QueryServices(host) || !host->registerService) return GAKU_E_UNAVAILABLE;
    struct CameraControl {uint32_t abiVersion,structSize;GakuStatus(*setEnabled)(uint32_t);uint32_t(*isEnabled)();uint32_t(*protectionReady)();void(*stats)(uint64_t*,uint64_t*,uint64_t*);};
    const CameraControl control{1u,sizeof(CameraControl),
        [](uint32_t enabled)->GakuStatus {
            if (enabled) {
                if (!g_injected.load(std::memory_order_acquire)) return GAKU_E_UNAVAILABLE;
                static bool cameraInitialized = false;
                if (!cameraInitialized) { GKCamera::initCameraSettings(); cameraInitialized = true; }
                GakumasLocal::Config::blockFreeCameraShortcuts = true;
                GakumasLocal::Config::enableFreeCamera = true;
                GkmsHosted::TickFreeCameraShortcutBlock();
                if (!GkmsHosted::FreeCameraShortcutBlockReady()) {
                    GakumasLocal::Config::enableFreeCamera = false;
                    return GAKU_E_UNAVAILABLE;
                }
            } else {
                GakumasLocal::Config::enableFreeCamera = false;
                GkmsHosted::StopFreeCameraShortcutBlock();
            }
            return GAKU_OK;
        },
        []()->uint32_t{return GakumasLocal::Config::enableFreeCamera?1u:0u;},
        GkmsHosted::FreeCameraShortcutBlockReady,GkmsHosted::FreeCameraShortcutBlockStats};
    return host->registerService(host->hostContext,"gakuactors.camera-core",sizeof(control),&control);
}

void RuntimeReady(void*) {
    auto& services = GkmsHosted::g_services;
    // All plugins have registered their services by this phase. Never acquire
    // competing targets if a Localify camera provider is already present.
    GakuLocalifyCameraWriteService existing{};
    GakuLocalifyCameraRenderService existingRender{};
    if (services.host.queryService(services.host.hostContext, GAKULOCALIFY_SERVICE_CAMERA_WRITE,
            GAKULOCALIFY_CAMERA_WRITE_VERSION, sizeof(existing), &existing) == GAKU_OK ||
        services.host.queryService(services.host.hostContext, GAKULOCALIFY_SERVICE_CAMERA_RENDER,
            GAKULOCALIFY_CAMERA_RENDER_VERSION, sizeof(existingRender), &existingRender) == GAKU_OK) {
        GkmsHosted::HostLog(GAKU_LOG_INFO,"Camera core dormant: another camera provider is installed");
        return;
    }
    if (!GkmsHosted::GameAssembly()) return;
    const auto writes = g_cameraWrites.Service();
    const auto renders = g_cameraRenders.Service();
    if (services.host.registerService(services.host.hostContext,GAKULOCALIFY_SERVICE_CAMERA_WRITE,sizeof(writes),&writes)!=GAKU_OK ||
        services.host.registerService(services.host.hostContext,GAKULOCALIFY_SERVICE_CAMERA_RENDER,sizeof(renders),&renders)!=GAKU_OK) {
        GkmsHosted::HostLog(GAKU_LOG_ERROR,"Camera core could not publish shared camera providers");
        return;
    }
    auto installer=std::make_unique<HostHookInstaller>("GameAssembly.dll", "");
    GakumasLocal::Plugin::GetInstance().InstallHook(std::move(installer));
    if (!GakumasLocal::HookMain::StartCameraInjectFunctions()) {
        GkmsHosted::HostLog(GAKU_LOG_ERROR,"Camera core hook installation incomplete; free camera disabled");
        return;
    }
    GakumasLocal::WinHooks::Keyboard::InstallWndProcHook();
    g_injected.store(true,std::memory_order_release);
    GkmsHosted::HostLog(GAKU_LOG_INFO,"Standalone camera core ready; shared Motion providers published");
}

void Tick(void*, float) {
    if (g_injected.load(std::memory_order_acquire)) GkmsHosted::TickFreeCameraShortcutBlock();
}
void Shutdown(void*) {
    GakumasLocal::Config::enableFreeCamera=false;
    GkmsHosted::StopFreeCameraShortcutBlock();
    g_cameraRenders.Close();
    g_cameraWrites.Close();
}
#else
GakuStatus Activate(const GakuHostApi* host, void**) {
    if (!GkmsHosted::QueryServices(host)) {
        return GAKU_E_UNAVAILABLE;
    }
    if (!host->registerService) return GAKU_E_INVALID_ARGUMENT;
    const auto cameraWrites = g_cameraWrites.Service();
    const auto cameraServiceStatus = host->registerService(host->hostContext,
        GAKULOCALIFY_SERVICE_CAMERA_WRITE, sizeof(cameraWrites), &cameraWrites);
    if (cameraServiceStatus != GAKU_OK) return cameraServiceStatus;
    const auto cameraRenders = g_cameraRenders.Service();
    const auto renderServiceStatus = host->registerService(host->hostContext,
        GAKULOCALIFY_SERVICE_CAMERA_RENDER, sizeof(cameraRenders), &cameraRenders);
    if (renderServiceStatus != GAKU_OK) return renderServiceStatus;
    // GakuActors presents the button; Localify retains its existing camera
    // implementation and owns the keyboard-isolation lifecycle.
    struct FreeCameraControl {uint32_t abiVersion,structSize;GakuStatus(*setEnabled)(uint32_t);uint32_t(*isEnabled)();uint32_t(*protectionReady)();void(*stats)(uint64_t*,uint64_t*,uint64_t*);};
    const FreeCameraControl freeCamera{1u,sizeof(FreeCameraControl),
        [](uint32_t enabled)->GakuStatus {
            if(enabled){
                if(!g_injected.load(std::memory_order_acquire)||!GkmsHosted::ManagedBootstrapDone()||!g_cameraRenders.GetTarget(GAKULOCALIFY_CAMERA_RENDER_PUSH))return GAKU_E_UNAVAILABLE;
                for(uint32_t kind=0;kind<GAKULOCALIFY_CAMERA_WRITE_COUNT;++kind)if(!g_cameraWrites.GetTarget(kind))return GAKU_E_UNAVAILABLE;
            }
            GakumasLocal::Config::blockFreeCameraShortcuts=true;GakumasLocal::Config::enableFreeCamera=enabled!=0;
            if(enabled){GkmsHosted::TickFreeCameraShortcutBlock();if(!GkmsHosted::FreeCameraShortcutBlockReady()){GakumasLocal::Config::enableFreeCamera=false;return GAKU_E_UNAVAILABLE;}}
            else GkmsHosted::StopFreeCameraShortcutBlock();
            return GAKU_OK;
        },
        []()->uint32_t{return GakumasLocal::Config::enableFreeCamera?1u:0u;},
        GkmsHosted::FreeCameraShortcutBlockReady,GkmsHosted::FreeCameraShortcutBlockStats};
    const auto freeCameraStatus=host->registerService(host->hostContext,"gakulocalify.free-camera-control",sizeof(freeCamera),&freeCamera);
    if(freeCameraStatus!=GAKU_OK)GkmsHosted::HostLog(GAKU_LOG_WARNING,"Free camera control service unavailable");
    gakumasLocalPath = GkmsHosted::LocalRoot();
    try {
        const auto category = [](GakuPathKind kind) -> std::filesystem::path {
            const auto& paths = GkmsHosted::g_services.paths;
            if (!paths.getRoot) return {};
            uint32_t count{};
            if (paths.getRoot(paths.context, kind, nullptr, 0, &count) != GAKU_E_SIZE || !count || count > 32768)
                throw std::runtime_error("Cannot query Localify data category");
            std::vector<wchar_t> value(count);
            const auto capacity = count;
            if (paths.getRoot(paths.context, kind, value.data(), capacity, &count) != GAKU_OK ||
                !count || count > capacity || value[count - 1] != L'\0' ||
                std::find(value.begin(), value.begin() + count, L'\0') != value.begin() + count - 1)
                throw std::runtime_error("Cannot resolve Localify data category");
            const auto result = std::filesystem::path(value.data());
            if (!result.is_absolute()) throw std::runtime_error("Localify data category must be absolute");
            return result;
        };
        const auto configs = category(GAKU_PATH_CONFIGS);
        const auto users = category(GAKU_PATH_USER_DATA);
        const auto plugin = category(GAKU_PATH_PLUGIN_DATA);
        auto view = GkmsHosted::g_services.paths.getRoot
            ? LocalifyData::InitializeFreshHosted(gakumasLocalPath, configs, users, plugin)
            : LocalifyData::Load(gakumasLocalPath, configs, users, plugin);
        if (view->migrated && !GkmsHosted::g_services.paths.getRoot)
            throw std::runtime_error("Migrated Localify data requires the Loader paths service");
        LocalifyData::Publish(view);
        ProgramConfigJson = view->ProgramConfig();
        ConfigJson = view->LocalizationConfig();
    } catch (const std::exception& error) {
        GkmsHosted::HostLog(GAKU_LOG_ERROR, std::string("Localify data layout refused: ") + error.what());
        return GAKU_E_UNAVAILABLE;
    }
    GkmsResourceUpdate::RecoverInterruptedUpdates();

    // Native bootstrap only: paths, configuration, hotkeys, worker. No Unity or
    // IL2CPP call is allowed here, and the game may already be running.
    readProgramConfig();
    loadConfig(ConfigJson);
    GkmsHosted::StartRuntime();
    // Start parsing during the game's own boot instead of after the runtime is
    // resolvable. The worker only reads files; RuntimeReady publishes the first
    // native snapshot before enabling any detour, independently of frame ticks.
    GkmsHosted::RequestReloadAllData();

    GakuHotkeyBinding showGui{GAKU_SERVICE_VERSION, sizeof(GakuHotkeyBinding), "show-gui",
        "Localify panel", 'U', GAKU_HOTKEY_MOD_CTRL,
        [](void*) {
            GkmsHosted::HostLog(GAKU_LOG_INFO, "hotkey show-gui (Ctrl+U) fired");
            if (GkmsHosted::g_services.desktopWindow.requestShow) {
                GkmsHosted::g_services.desktopWindow.requestShow(GkmsHosted::g_services.desktopWindow.context);
            } else {
                GkmsHosted::HostLog(GAKU_LOG_WARNING, "show-gui: no desktop window service; panel unavailable");
            }
        }, nullptr};
    const auto showStatus = GkmsHosted::g_services.hotkeys.registerHotkey(
        GkmsHosted::g_services.hotkeys.context, &showGui);

    GakuHotkeyBinding fullscreen{GAKU_SERVICE_VERSION, sizeof(GakuHotkeyBinding), "toggle-fullscreen",
        "Unlock window size / fullscreen", VK_F11, 0, [](void*) {
            GkmsHosted::HostLog(GAKU_LOG_INFO, "hotkey toggle-fullscreen (F11) fired");
            GkmsHosted::ToggleGameFullScreen();
        }, nullptr};
    const auto fullscreenStatus = GkmsHosted::g_services.hotkeys.registerHotkey(
        GkmsHosted::g_services.hotkeys.context, &fullscreen);

    // Activation evidence from inside the plugin: this must be pre-runtime, and
    // a mapped GameAssembly here is the documented race, not an error.
    GkmsHosted::HostLog(GAKU_LOG_INFO, "GakuLocalify activate: thread=" +
        std::to_string(GetCurrentThreadId()) + " uptime_ms=" + std::to_string(GkmsHosted::ProcessUptimeMs()) +
        " gameAssembly=" + (GkmsHosted::GameAssembly() ? "present" : "absent") +
        " localRoot=" + LocalifyData::Utf8(gakumasLocalPath) +
        " enabled=" + (GakumasLocal::Config::enabled ? "true" : "false") +
        " useMasterTrans=" + (GakumasLocal::Config::useMasterTrans ? "true" : "false") +
        " replaceFont=" + (GakumasLocal::Config::replaceFont ? "true" : "false") +
        " hotkeys(show=" + std::to_string(showStatus) + ",fullscreen=" + std::to_string(fullscreenStatus) + ")");
    return GAKU_OK;
}

void RuntimeReady(void*) {
    const auto started = GetTickCount64();
    auto* gameAssembly = static_cast<HMODULE>(GkmsHosted::GameAssembly());
    // This is a loader worker, never the Unity main thread. Keep the entry
    // timestamp before metadata resolution and detour installation.
    GkmsHosted::HostLog(GAKU_LOG_INFO, "[StartupTiming] GakuLocalify onRuntimeReady uptime_ms=" +
        std::to_string(GkmsHosted::ProcessUptimeMs()) + " thread=" +
        std::to_string(GetCurrentThreadId()) + " gameAssembly=" +
        (gameAssembly ? "present" : "absent") + " onUnityMainThread=" +
        std::to_string(GkmsHosted::g_services.mainThread.isMainThread
            ? GkmsHosted::g_services.mainThread.isMainThread(GkmsHosted::g_services.mainThread.context) : 0));
    if (!GakumasLocal::Config::enabled) {
        GkmsHosted::HostLog(GAKU_LOG_WARNING,
            "localizationConfig.json has enabled=false; no detours installed");
        return;
    }
    if (!gameAssembly) {
        GkmsHosted::HostLog(GAKU_LOG_ERROR, "GameAssembly is not resolvable; injection skipped");
        return;
    }
    if (g_injected.load(std::memory_order_acquire)) return;
    if (!GkmsHosted::PrepareInitialTranslationsBeforeHooks()) return;
    if (GkmsHosted::HostIsStopping()) return;
    // Metadata and detour installation only.
    auto installer = std::make_unique<HostHookInstaller>("GameAssembly.dll", LocalifyData::Utf8(gakumasLocalPath));
    auto* installerTiming = installer.get();
    GakumasLocal::Plugin::GetInstance().InstallHook(std::move(installer));
    GakumasLocal::HookMain::StartInjectFunctions();
    // Runtime-ready callbacks are serialized by the host. Only a completed
    // install may latch this flag; a pending parse must never look injected.
    g_injected.store(true, std::memory_order_release);
    installerTiming->ReportStartupTiming();
    // Asset bundles allocate Il2Cpp strings and start a Unity request, and the
    // camera reset reads live Unity objects; both are queued for the Unity main
    // thread instead of running on this worker. A later reload can call the
    // same entry point, so the flag inside it decides who runs.
    if (GkmsHosted::g_services.mainThread.enqueue) {
        const auto queued = GkmsHosted::g_services.mainThread.enqueue(
            GkmsHosted::g_services.mainThread.context, [](void*) {
                GkmsHosted::EnsureManagedBootstrapOnMainThread();
            }, nullptr);
        if (queued != GAKU_OK) {
            GkmsHosted::HostLog(GAKU_LOG_WARNING,
                "managed bootstrap could not be queued (status=" + std::to_string(queued) + ")");
        }
    }
    // Initial native translations already exist before these detours become
    // visible; managed bootstrap still waits for the Unity main-thread queue.
    GkmsHosted::HostLog(GAKU_LOG_INFO, "Localify detours installed; initial translations published; managed bootstrap queued");
    GkmsHosted::HostLog(GAKU_LOG_INFO, "[StartupTiming] GakuLocalify onRuntimeReady finished uptime_ms=" +
        std::to_string(GkmsHosted::ProcessUptimeMs()) + " thread=" + std::to_string(GetCurrentThreadId()) +
        " elapsed_ms=" + std::to_string(GetTickCount64() - started));
}

void Tick(void*, float) {
    GkmsHosted::InstallWindowHookOnMainThread();
    GkmsHosted::TickFreeCameraShortcutBlock();
    // Later reload publication retains its existing bounded retranslation
    // behavior, including labels constructed during a slow initial parse.
    if (GkmsHosted::ManagedBootstrapDone()) {
        GakumasLocal::HookMain::TickRetranslateSweeps();
    }
    // Detour evidence, reported only when it moves: a run whose counters stay at
    // zero has no text path to translate, which is a different failure from a
    // text path that runs but never matches a key.
    if (!GkmsHosted::DiagnosticsEnabled()) return;
    static std::chrono::steady_clock::time_point nextReport{};
    static unsigned long long reportedSeen = 0;
    static unsigned long long reportedI18nSeen = 0;
    const auto now = std::chrono::steady_clock::now();
    if (nextReport.time_since_epoch().count() == 0) nextReport = now + std::chrono::seconds(5);
    if (now < nextReport) return;
    nextReport = now + std::chrono::seconds(5);
    unsigned long long seen = 0, translated = 0, i18nSeen = 0, i18nTranslated = 0;
    GakumasLocal::HookMain::TextDetourStats(&seen, &translated, &i18nSeen, &i18nTranslated);
    if (seen == reportedSeen && i18nSeen == reportedI18nSeen) return;
    reportedSeen = seen;
    reportedI18nSeen = i18nSeen;
    GkmsHosted::HostLog(GAKU_LOG_INFO, "text detours: tmp seen=" + std::to_string(seen) +
        " translated=" + std::to_string(translated) + ", i18n seen=" + std::to_string(i18nSeen) +
        " translated=" + std::to_string(i18nTranslated));
}

void DesktopUi(void*) {
    if (!GkmsHosted::g_services.desktopUi.bind) return;
    const GakuImGuiAbi abi{IMGUI_VERSION_NUM, sizeof(ImGuiIO), sizeof(ImGuiStyle),
        sizeof(ImDrawVert), sizeof(ImDrawIdx), sizeof(ImWchar)};
    GakuDesktopUiBinding binding{};
    const auto status = GkmsHosted::g_services.desktopUi.bind(&abi, sizeof(binding), &binding);
    if (status != GAKU_OK) {
        if (!g_guiBindFailureLogged.exchange(true)) {
            GkmsHosted::HostLog(GAKU_LOG_ERROR, "ImGui binding refused (status=" +
                std::to_string(status) + "); Localify panel disabled");
        }
        return;
    }
    if (!binding.imguiContext || !binding.alloc || !binding.free) {
        if (!g_guiBindFailureLogged.exchange(true)) {
            GkmsHosted::HostLog(GAKU_LOG_ERROR,
                "ImGui binding is incomplete (context/allocator missing); Localify panel disabled");
        }
        return;
    }
    // The context and its draw lists were built by the host module. Every
    // plugin-side push/free of those shared vectors must therefore hit the host
    // heap: with this plugin's own allocator, ImDrawList::AddPolyline freed a
    // host allocation through ucrtbase and took the process down inside ntdll
    // on the first panel frame. Same negotiation as GakuMotion.
    ImGui::SetAllocatorFunctions(binding.alloc, binding.free, binding.allocatorUserData);
    auto* previous = ImGui::GetCurrentContext();
    ImGui::SetCurrentContext(static_cast<ImGuiContext*>(binding.imguiContext));
    // The host wraps this callback in its plugin content child window and
    // advertises that with presentationKind. Draw into that page instead of
    // opening a floating "Gakumas Plugin Config" window; GakuMotion makes the
    // same decision from the same field.
    const bool embedded = GkmsHosted::g_services.desktopWindow.presentationKind ==
        GAKU_DESKTOP_PRESENTATION_PLUGIN_PANEL;
    if (!g_firstPanelFrameLogged.exchange(true)) {
        ImGuiIO& io = ImGui::GetIO();
        GkmsHosted::HostLog(GAKU_LOG_INFO, "panel frame 1: imgui=" + std::string(IMGUI_VERSION) +
            " fonts=" + std::to_string(io.Fonts->Fonts.Size) +
            " atlas=" + std::to_string(io.Fonts->TexWidth) + "x" + std::to_string(io.Fonts->TexHeight) +
            " display=" + std::to_string(static_cast<int>(io.DisplaySize.x)) + "x" +
            std::to_string(static_cast<int>(io.DisplaySize.y)) + " allocator=host mode=" +
            (embedded ? "embedded" : "window") + " presentationKind=" +
            std::to_string(GkmsHosted::g_services.desktopWindow.presentationKind));
    }
    GkmsHosted::RefreshUiLanguage(GkmsHosted::g_services.language);
    GkmsGUILoop::mainLoop(embedded);
    ImGui::SetCurrentContext(previous);
}

void Shutdown(void*) {
    GkmsHosted::StopFreeCameraShortcutBlock();
    g_cameraRenders.Close();
    g_cameraWrites.Close();
    // Ordering is the contract: close and cancel background resource updates
    // first (they post Reload commands and own the live-tree commit gate),
    // capture the configuration while this thread still owns it, then close the
    // command queue and wait for both. A task still extracting or copying only
    // touches staging; BeginShutdown has already closed the commit gate, so even
    // one that outlives the join deadline cannot write installed resources.
    GkmsResourceUpdate::BeginShutdown();
    GkmsHosted::RequestSaveAllData();
    GkmsHosted::StopRuntime();
    // Bounded: a task stuck inside an uninterruptible call is reported and left
    // detached instead of blocking the loader's exit path indefinitely.
    GkmsResourceUpdate::WaitForJobs(2000);
    GakumasLocal::Local::FlushPendingDumps(2000);
    GkmsHosted::HostLog(GAKU_LOG_INFO, "GakuLocalify shutdown complete; loader retires hooks and bindings");
}
#endif

}

#ifdef GKMS_ACTORS_INTEGRATED
extern "C" GakuStatus GakuActors_CameraCoreQuery(uint32_t requestedAbi, uint32_t outputSize,
#else
extern "C" __declspec(dllexport) GakuStatus GakuPlugin_Query(uint32_t requestedAbi, uint32_t outputSize,
#endif
    GakuPluginDescriptor* output) {
    if (requestedAbi != GAKU_ABI_VERSION) return GAKU_E_VERSION;
    if (!output) return GAKU_E_INVALID_ARGUMENT;
    if (outputSize < sizeof(GakuPluginDescriptor)) return GAKU_E_SIZE;
#ifdef GKMS_CAMERA_ONLY
    GakuPluginDescriptor descriptor{GAKU_ABI_VERSION, sizeof(GakuPluginDescriptor),
        "gakuactors.camera-core", "GakuActors Camera Core", PLUGIN_VERSION};
#else
    GakuPluginDescriptor descriptor{GAKU_ABI_VERSION, sizeof(GakuPluginDescriptor),
        "gakumas.localify", "Gakumas Localify (hosted)", PLUGIN_VERSION};
#endif
    descriptor.activate = Activate;
    descriptor.onRuntimeReady = RuntimeReady;
    descriptor.onTick = Tick;
#ifndef GKMS_CAMERA_ONLY
    descriptor.onDesktopUi = DesktopUi;
#endif
    descriptor.shutdown = Shutdown;
    *output = descriptor;
    return GAKU_OK;
}

#endif // GKMS_HOSTED
