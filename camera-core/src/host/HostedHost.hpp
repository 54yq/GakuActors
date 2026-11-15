#pragma once
#include "GakuPlugin.h"
#include "GakuPaths.h"
#include "GakuLanguage.h"
#include "GakuLifecycle.h"
#include <filesystem>
#include <functional>
#include <string>

// Platform-layer globals. Standalone main.cpp used to own these; the hosted
// target defines them in HostedRuntime.cpp instead. Declared once here so the
// plugin entry point and the platform layer cannot drift apart.
extern std::filesystem::path gakumasLocalPath;
extern std::filesystem::path ProgramConfigJson;
extern std::filesystem::path ConfigJson;
extern std::function<void()> g_reload_all_data;
void readProgramConfig();

// Hosted Localify: the loader owns the process, the window, the ImGui context,
// input edges and every game detour. This layer only adapts Localify's existing
// translation/gui code onto those services. It must never create a second
// window, device, ImGui context, MinHook instance or process-wide hotkey.
namespace GkmsHosted {

struct Services {
    GakuHostApi host{};
    GakuLogService log{};
    GakuRuntimeService runtime{};
    GakuMainThreadService mainThread{};
    GakuHookService hooks{};
    GakuHotkeyService hotkeys{};
    GakuDesktopUiService desktopUi{};
    GakuDesktopWindowService desktopWindow{};
    GakuActorEntryService actorEntry{};
    GakuPathsV1 paths{};
    GakuLanguageServiceV1 language{};
    GakuLifecycleServiceV1 lifecycle{};
};
extern Services g_services;
extern bool g_servicesReady;

// activate: copy every service table the hosted target depends on.
bool QueryServices(const GakuHostApi* host);
void HostLog(GakuLogLevel level, const std::string& message);
std::filesystem::path LocalRoot();
void* GameAssembly();
// Optional host cancellation, available without entering the callback gate.
bool HostIsStopping();

// Plugin worker that parses translations. Requests are cheap: they only enqueue.
void StartRuntime();
void StopRuntime();
void RequestReloadAllData();
void RequestSaveAllData();
// RuntimeReady, before any Localify detour: wait for the initial native parse
// and publish its maps while there are no readers. A slow parse keeps waiting;
// false means a real parse failure or host cancellation, never a timeout.
// This never creates managed objects or performs retranslation. Later reloads
// remain main-thread publications.
bool PrepareInitialTranslationsBeforeHooks();
// Unity main thread: publish a parsed snapshot, re-read config (short) and
// update cached resource versions. Never parses translation files.
void ApplyPendingReloadOnMainThread();
// Unity main thread, idempotent: load the extra asset bundle (replacement font),
// refresh fonts/textures and apply camera settings. Publication calls it first
// because the retranslation sweep needs the replacement font in place.
void EnsureManagedBootstrapOnMainThread();
// True once the asset bundle/font bootstrap ran on the Unity main thread. The
// sweep tick stays off until then because it resolves managed classes.
bool ManagedBootstrapDone();
// Unity main thread, idempotent: install the game window's keyboard chain once
// the window is known to exist.
void InstallWindowHookOnMainThread();
void TickFreeCameraShortcutBlock();
void StopFreeCameraShortcutBlock();
uint32_t FreeCameraShortcutBlockReady();
void FreeCameraShortcutBlockStats(uint64_t* windowKeys,uint64_t* unityQueries,uint64_t* cameraPushes);
// Owner-local F11 action: the game window, never the host control window.
void ToggleGameFullScreen();

}
