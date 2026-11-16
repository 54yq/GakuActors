#pragma once
#include <windows.h>
#include <filesystem>
#include <system_error>

// Read once during plugin activation. A marker file makes diagnostics an
// explicit per-installation opt-in; ordinary playback performs no file polling.
namespace GakuDiagnostics {
inline bool Enabled(const wchar_t* gameRoot) noexcept {
    try {
        if (!gameRoot || !*gameRoot) return false;
        std::error_code ec;
        const auto marker = std::filesystem::path(gameRoot) / L"GakuLoader" / L"Configs" / L"gaku-diagnostics.enabled";
        return std::filesystem::is_regular_file(marker, ec) && !ec;
    } catch (...) { return false; }
}
}
