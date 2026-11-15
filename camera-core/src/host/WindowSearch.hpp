#pragma once
#include <windows.h>

namespace GkmsHosted {

// Returns this process's Unity window, or null when the game has not created
// one yet.
//
// The `hwndChildAfter` argument must always be the window found by the previous
// iteration. Passing NULL restarts the enumeration from the first top-level
// window, so a first UnityWndClass window that belongs to a different Unity
// process would be returned forever: the PID never matches, the loop never
// ends, and it runs on the Unity main thread, which hangs the game.
inline HWND FindOwnUnityWindow(const wchar_t* className = L"UnityWndClass") {
    HWND window = nullptr;
    while ((window = FindWindowExW(nullptr, window, className, nullptr)) != nullptr) {
        DWORD process = 0;
        GetWindowThreadProcessId(window, &process);
        if (process == GetCurrentProcessId()) return window;
    }
    return nullptr;
}

}
