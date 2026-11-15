#include "PurchaseGuard.h"

#include "Il2cppUtils.hpp"
#include "Log.h"
#include "Plugin.h"
#include "config/Config.hpp"

#include <cstddef>
#include <cstdint>

namespace GakumasLocal::PurchaseGuard {
    namespace {
        // IL2CPP returns UniTask<ICheckPurchaseResult> by value. The compiler
        // supplies the hidden return buffer according to the target ABI.
        struct PurchaseCheckTask {
            void* source;
            void* result;
            std::int16_t token;
        };

        static_assert(sizeof(PurchaseCheckTask) == 24);
        static_assert(offsetof(PurchaseCheckTask, result) == 8);
        static_assert(offsetof(PurchaseCheckTask, token) == 16);

        constexpr auto purchaseAssembly = "quaunity-inapp-purchase-manager.dll";
        constexpr auto purchaseNamespace = "Qua.InAppPurchasing";
        using CheckPurchase = PurchaseCheckTask (*)(void*, void*, void*, void*);
        bool installed = false;
        CheckPurchase originalCheck = nullptr;
        UnityResolve::Class* resultClass = nullptr;
        UnityResolve::Method* resultCtor = nullptr;
        UnityResolve::Method* setCanPurchase = nullptr;
        UnityResolve::Method* gameToast = nullptr;

        void ShowPurchaseBlockedToast() {
            if (gameToast) {
                using Il2cppString = UnityResolve::UnityType::String;
                gameToast->Invoke<void>(Il2cppString::New("充值前请先关闭 iOS 登录"), gameToast->address);
            }
        }

        PurchaseCheckTask CheckPurchaseHook(void* self, void* item, void* token, void* method) {
            if (!Config::loginAsIOS) {
                return originalCheck(self, item, token, method);
            }

            auto result = resultClass->New<void>();
            if (!result) return {};
            resultCtor->Invoke<void>(result, resultCtor->address);
            setCanPurchase->Invoke<void>(result, false, setCanPurchase->address);
            ShowPurchaseBlockedToast();
            // An inline result with no source is an already completed UniTask.
            return {nullptr, result, 0};
        }

        template <typename Function>
        bool InstallOne(HookInstaller& installer, const char* name, void* address, Function hook, Function& original) {
            const auto status = installer.InstallHook(address, reinterpret_cast<void*>(hook),
                                                      reinterpret_cast<void**>(&original));
#ifdef GKMS_HOSTED
            if (status) {
                Log::ErrorFmt("iOS purchase guard: %s hook failed: status %d", name,
                              static_cast<int>(reinterpret_cast<intptr_t>(status)));
                return false;
            }
#elif defined(GKMS_WINDOWS)
            if (status) {
                const auto error = static_cast<MH_STATUS>(reinterpret_cast<uintptr_t>(status));
                Log::ErrorFmt("iOS purchase guard: %s hook failed: %s", name, MH_StatusToString(error));
                return false;
            }
            // The Windows installer returns creation status, so check activation separately.
            const auto enabled = MH_EnableHook(address);
            if (enabled != MH_OK && enabled != MH_ERROR_ENABLED) {
                MH_RemoveHook(address);
                original = nullptr;
                Log::ErrorFmt("iOS purchase guard: %s hook failed: %s", name, MH_StatusToString(enabled));
                return false;
            }
#else
            if (!status) {
                Log::ErrorFmt("iOS purchase guard: %s hook failed: %s", name,
                              shadowhook_to_errmsg(shadowhook_get_errno()));
                return false;
            }
#endif
            Log::InfoFmt("iOS purchase guard: installed %s at %p", name, address);
            return true;
        }
    }

    void Install(HookInstaller& installer) {
        const auto toast = Il2cppUtils::GetMethod(
            "Assembly-CSharp.dll", "Campus.Common", "ToastManager", "Show", {"System.String"});
        if (toast && toast->static_function && toast->function && toast->return_type->name == "System.Void" &&
            toast->args.size() == 1 && toast->args.front()->pType->name == "System.String") {
            gameToast = toast;
        }
        if (installed) return;

        resultClass = Il2cppUtils::GetClass(purchaseAssembly, purchaseNamespace, "CheckPurchaseResult");
        resultCtor = Il2cppUtils::GetMethod(purchaseAssembly, purchaseNamespace, "CheckPurchaseResult", ".ctor");
        setCanPurchase = Il2cppUtils::GetMethod(purchaseAssembly, purchaseNamespace, "CheckPurchaseResult",
                                               "set_CanPurchase", {"System.Boolean"});
        if (!resultClass || !resultCtor || !resultCtor->function || !setCanPurchase || !setCanPurchase->function) {
            Log::Error("iOS purchase guard: required purchase result methods are unavailable");
            return;
        }

        // Use the same lookup as PR #12, including the resolver's same-name fallback.
        const auto address = Il2cppUtils::GetMethodPointer("Assembly-CSharp.dll", "Campus.Common.Purchase",
            "InAppPurchaseManager", "CheckPurchaseAsync",
            {"Qua.InAppPurchasing.IPurchaseItem", "System.Threading.CancellationToken"});
        if (!address) {
            Log::Error("iOS purchase guard: CheckPurchaseAsync is unavailable");
            return;
        }

        installed = InstallOne(installer, "CheckPurchaseAsync", address, CheckPurchaseHook, originalCheck);
    }
}
