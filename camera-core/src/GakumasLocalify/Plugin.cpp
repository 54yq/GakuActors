#include "Plugin.h"
#include "Hook.h"

namespace GakumasLocal {
    HookInstaller::~HookInstaller() {
    }

    Plugin &Plugin::GetInstance() {
        static Plugin instance;
        return instance;
    }

    void Plugin::InstallHook(std::unique_ptr<HookInstaller>&& hookInstaller)
    {
        m_HookInstaller = std::move(hookInstaller);
#ifndef GKMS_HOSTED
        Hook::Install();
#endif
        // Hosted: the loader already owns activation and runtime readiness. The
        // plugin's onRuntimeReady calls HookMain::StartInjectFunctions once the
        // runtime is resolvable, so installing the adapter must not start hooks.
    }

    HookInstaller* Plugin::GetHookInstaller() const
    {
        return m_HookInstaller.get();
    }

}
