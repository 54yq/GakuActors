#ifndef GAKUMAS_LOCALIFY_HOOK_H
#define GAKUMAS_LOCALIFY_HOOK_H

#include <string>

namespace GakumasLocal::Hook
{
    void Install();
}

namespace GakumasLocal::HookMain
{
    // Resolves exports/metadata and installs every owned detour. The standalone
    // build reaches it through il2cpp_init; the hosted build calls it from its
    // own runtime-ready phase. Metadata only: no managed object creation.
    void StartInjectFunctions();
    // Camera-only hosted build. Returns false if any required lease failed.
    bool StartCameraInjectFunctions();
    // Unity/asset work split out of StartInjectFunctions. Call on the Unity main
    // thread only; the hosted build queues it from onRuntimeReady.
    void StartManagedBootstrap();
    // Main-thread refresh after a resource-set publication. No hook reinstallation.
    void ReloadResourceAssets();
    // Re-applies the translation to every live TMP_Text. A label that was built
    // before the snapshot was published keeps the Japanese string forever, and a
    // reload has the same problem, so publication sweeps the live scene instead
    // of waiting for the game to set the same text again. Main thread only.
    void RetranslateAllTexts(const char* reason);
    // One sweep is not enough: the title screen is built in the same frames the
    // first snapshot lands, so its labels are set after the sweep ran. Schedule
    // a short bounded series and drain it from the main-thread tick.
    void ScheduleRetranslateSweeps();
    // Unity main thread, once per frame. Cheap no-op until a sweep is due.
    void TickRetranslateSweeps();
    // Detour evidence: seen counts intercepted text writes, translated the
    // subset that matched. seen == 0 means the detours never ran; seen without
    // translated means the keys did not match.
    void TextDetourStats(unsigned long long* seen, unsigned long long* translated,
                         unsigned long long* i18nSeen, unsigned long long* i18nTranslated);
    unsigned long long FreeCameraPushCount();
}

#endif //GAKUMAS_LOCALIFY_HOOK_H
