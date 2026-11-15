#pragma once
#ifdef GKMS_HOSTED
// MinHook is standalone-only, and it used to be what pulled in <windows.h>.
// The hosted target still needs the Win32 types for its keyboard and window
// helpers, so include it directly instead.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <minhook.h>
#endif
#include <vector>

#define ANDROID_LOG_VERBOSE 0
#define ANDROID_LOG_DEBUG 1
#define ANDROID_LOG_INFO 2
#define ANDROID_LOG_ERROR 3

#define GKMS_WINDOWS

#define LogMinVersion ANDROID_LOG_DEBUG

#define PLUGIN_VERSION "v3.6.2"

#ifdef GKMS_HOSTED
// The hosted build routes detours through the loader's hook service, so a
// failure carries a GakuStatus, not a MinHook status.
#define LOG_HOOK_FAILURE(name, addr, stub)                                                         \
    GakumasLocal::Log::ErrorFmt("ADD_HOOK: %s at %p failed: status %d", name, addr,                \
        static_cast<int>(reinterpret_cast<intptr_t>(stub)))
#else
#define LOG_HOOK_FAILURE(name, addr, stub)                                                         \
    GakumasLocal::Log::ErrorFmt("ADD_HOOK: %s at %p failed: %s", name, addr,                       \
        MH_StatusToString(static_cast<MH_STATUS>(reinterpret_cast<intptr_t>(stub))))
#endif

#define ADD_HOOK(name, addr)                                                                       \
	name##_Addr = reinterpret_cast<name##_Type>(addr);                                             \
	if (addr) {                                                                                    \
    	auto stub = hookInstaller->InstallHook(reinterpret_cast<void*>(addr),                      \
                                               reinterpret_cast<void*>(name##_Hook),               \
                                               reinterpret_cast<void**>(&name##_Orig));            \
        if (stub) {                                                                                \
            LOG_HOOK_FAILURE(#name, addr, stub);                                                   \
        }                                                                                          \
        else {                                                                                     \
            hookedStubs.emplace(stub);                                                             \
            GakumasLocal::Log::InfoFmt("ADD_HOOK: %s at %p", #name, addr);                         \
        }                                                                                          \
    }                                                                                              \
    else GakumasLocal::Log::ErrorFmt("Hook failed: %s is NULL", #name, addr);                      \
    if (Config::lazyInit) UnityResolveProgress::classProgress.current++


#ifdef GKMS_HOSTED
// The hosted build forwards every diagnostic into the loader's private log. A
// per-translation-unit static FILE* would scatter output into the process CWD.
void __android_log_write(int prio, const char* tag, const char* msg);
#else
static void __android_log_write(int prio, const char* tag, const char* msg) {
    if (prio < LogMinVersion) return;

	static const char* logLevels[4] = { "VERBOSE", "DEBUG", "INFO", "ERROR" };

	if (prio < 0 || prio > 3) {
		prio = 1;
	}
	printf("[%s] %s: %s\n", logLevels[prio], tag, msg);  
	// ponytail: 同步落盘，方便无控制台时排查 hook 是否装上
	static FILE* logFile = fopen("gakumas-local/log.txt", "w");
	if (logFile) { fprintf(logFile, "[%s] %s: %s\n", logLevels[prio], tag, msg); fflush(logFile); }
}
#endif
