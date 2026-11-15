#pragma once

// The archive and the installer share this contract. Package names are UTF-8;
// filesystem operations use native wide paths, never the process code page.
#include <windows.h>
#include <algorithm>
#include <filesystem>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace GkmsResourceUpdate {
namespace SafeResource {
enum class PackageKind { Translation, Texture };

inline std::string Utf8(const std::filesystem::path& path) {
    const auto value = path.generic_u8string();
    return std::string(value.begin(), value.end());
}

inline std::filesystem::path NativePath(const std::string& value) {
    if (value.empty()) return {};
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), nullptr, 0);
    if (!count) throw std::runtime_error("Invalid UTF-8 resource path");
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
        static_cast<int>(value.size()), wide.data(), count))
        throw std::runtime_error("Cannot decode resource path");
    return std::filesystem::path(wide).make_preferred();
}

// Do not depend on the host game's manifest/registry opt-in for long paths.
// Windows' extended syntax needs absolute UTF-16 paths and native separators.
inline size_t ExtendedRootLength(const std::wstring& value) {
    const auto letter = [](wchar_t c) { return (c >= L'A' && c <= L'Z') || (c >= L'a' && c <= L'z'); };
    if (value.size() >= 7 && value.rfind(L"\\\\?\\", 0) == 0 && letter(value[4]) && value[5] == L':' && value[6] == L'\\') return 7;
    if (value.rfind(L"\\\\?\\UNC\\", 0) == 0) {
        const auto serverEnd = value.find(L'\\', 8);
        if (serverEnd == std::wstring::npos || serverEnd == 8 || serverEnd + 1 == value.size())
            throw std::runtime_error("UNC resource path needs a server and share");
        const auto shareEnd = value.find(L'\\', serverEnd + 1);
        if (shareEnd == serverEnd + 1) throw std::runtime_error("Empty UNC resource share");
        return shareEnd == std::wstring::npos ? value.size() : shareEnd + 1;
    }
    throw std::runtime_error("Unsupported resource device namespace");
}
inline std::wstring Fold(const std::wstring& value);
inline std::filesystem::path ExtendedAbsolute(const std::filesystem::path& path) {
    if (path.empty()) throw std::runtime_error("Empty absolute resource path");
    auto ordinary = path.native();
    if (ordinary.find(L'\0') != std::wstring::npos) throw std::runtime_error("Embedded NUL in absolute resource path");
    std::replace(ordinary.begin(), ordinary.end(), L'/', L'\\');
    if (ordinary.rfind(L"\\\\?\\", 0) == 0) {
        // Decompose/normalise ordinary DOS/UNC syntax first. MSVC deliberately
        // treats only \\? as root_name; root_path() is not a drive/share root.
        if (ordinary.size() >= 8 && Fold(ordinary.substr(4, 4)) == L"UNC\\") ordinary = L"\\\\" + ordinary.substr(8);
        else if (ordinary.size() >= 7 && ordinary[5] == L':' && ordinary[6] == L'\\') ordinary = ordinary.substr(4);
        else throw std::runtime_error("Unsupported resource device namespace");
    } else if (ordinary.rfind(L"\\\\.\\", 0) == 0 || ordinary.rfind(L"\\??\\", 0) == 0) {
        throw std::runtime_error("Device paths are not resource directories");
    }
    auto absolute = std::filesystem::path(ordinary);
    if (!absolute.is_absolute()) absolute = std::filesystem::absolute(absolute);
    absolute = absolute.lexically_normal().make_preferred();
    const auto normalized = absolute.native();
    auto value = normalized.rfind(L"\\\\", 0) == 0 ? L"\\\\?\\UNC\\" + normalized.substr(2) : L"\\\\?\\" + normalized;
    const auto rootLength = ExtendedRootLength(value);
    while (value.size() > rootLength && value.back() == L'\\') value.pop_back();
    if (value.size() == rootLength && value.back() != L'\\') value += L'\\';
    return std::filesystem::path(value).make_preferred();
}
inline bool IsFilesystemRoot(const std::filesystem::path& path) {
    const auto value = ExtendedAbsolute(path).native();
    return value.size() == ExtendedRootLength(value);
}

inline std::wstring Fold(const std::wstring& value) {
    std::wstring result(value.size(), L'\0');
    if (!value.empty() && !LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE,
        value.data(), static_cast<int>(value.size()), result.data(),
        static_cast<int>(result.size()), nullptr, nullptr, 0))
        throw std::runtime_error("Cannot normalize resource filename");
    return result;
}

inline std::string NormalizeEntry(std::string value) {
    if (value.empty() || value.find('\0') != std::string::npos || value.size() > 32760)
        throw std::runtime_error("Empty, embedded-NUL or overlong archive entry");
    std::replace(value.begin(), value.end(), '\\', '/');
    if (value.front() == '/' || value.find(':') != std::string::npos || value.find("//") != std::string::npos)
        throw std::runtime_error("Absolute, drive or alternate-stream archive entry");
    if (value.back() == '/') value.pop_back();
    if (value.empty()) throw std::runtime_error("Empty archive entry");
    const auto native = NativePath(value);
    for (const auto& part : native) {
        const auto name = part.native();
        if (name.empty() || name == L"." || name == L".." || name.size() > 255 ||
            name.back() == L'.' || name.back() == L' ')
            throw std::runtime_error("Unsafe archive path component");
        for (const wchar_t ch : name)
            if (ch < 32 || ch == L'<' || ch == L'>' || ch == L'"' || ch == L'|' || ch == L'?' || ch == L'*')
                throw std::runtime_error("Invalid Windows resource filename");
        const auto stem = Fold(name.substr(0, name.find(L'.')));
        if (stem == L"CON" || stem == L"PRN" || stem == L"AUX" || stem == L"NUL" ||
            stem == L"CONIN$" || stem == L"CONOUT$" ||
            (stem.size() == 4 && (stem.substr(0, 3) == L"COM" || stem.substr(0, 3) == L"LPT") &&
                ((stem[3] >= L'0' && stem[3] <= L'9') || stem[3] == L'\u00b9' || stem[3] == L'\u00b2' || stem[3] == L'\u00b3')))
            throw std::runtime_error("Reserved Windows resource filename");
    }
    return Utf8(native);
}

// Checks the entire existing chain, including the leaf. A lexical descendant
// check alone cannot contain a write through a pre-existing junction.
inline void CheckNoReparse(const std::filesystem::path& path) {
    const auto absolute = ExtendedAbsolute(path);
    const auto rootLength = ExtendedRootLength(absolute.native());
    auto current = std::filesystem::path(absolute.native().substr(0, rootLength));
    for (const auto& part : std::filesystem::path(absolute.native().substr(rootLength))) {
        current /= part;
        const DWORD attributes = GetFileAttributesW(current.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            const DWORD error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND) continue;
            throw std::runtime_error("Cannot inspect resource path: " + std::to_string(error));
        }
        if (attributes & FILE_ATTRIBUTE_REPARSE_POINT)
            throw std::runtime_error("Resource path contains a reparse point");
    }
}

inline bool IsAllowedTarget(const std::string& path, PackageKind kind) {
    const auto normalized = NormalizeEntry(path);
    if (normalized != path) return false;
    if (kind == PackageKind::Translation)
        return path == "version.txt" || path.rfind("local-files/", 0) == 0;
    return path.rfind("texture2d/", 0) == 0;
}

struct ArchiveEntry {
    std::string name;
    bool directory{};
    std::string target; // Empty means a safe, unrelated package file: ignored.
};

inline void CheckUniquePaths(const std::vector<ArchiveEntry>& entries, bool mapped) {
    std::set<std::wstring> files, directories, explicitNames;
    for (const auto& entry : entries) {
        const auto& value = mapped ? entry.target : entry.name;
        if (value.empty()) continue;
        const auto path = NativePath(value);
        const auto key = Fold(path.generic_wstring());
        if (!explicitNames.insert(key).second)
            throw std::runtime_error("Duplicate or case-colliding archive entry");
        if (entry.directory) {
            if (files.count(key)) throw std::runtime_error("File/directory archive collision");
            directories.insert(key);
        } else {
            if (directories.count(key)) throw std::runtime_error("File/directory archive collision");
            files.insert(key);
        }
        for (auto parent = path.parent_path(); !parent.empty(); parent = parent.parent_path()) {
            const auto parentKey = Fold(parent.generic_wstring());
            if (files.count(parentKey)) throw std::runtime_error("File/directory archive collision");
            directories.insert(parentKey);
        }
    }
}

inline void MapArchive(std::vector<ArchiveEntry>& entries, PackageKind kind) {
    CheckUniquePaths(entries, false);
    std::set<std::string> prefixes;
    for (const auto& entry : entries) {
        const auto& name = entry.name;
        if (kind == PackageKind::Translation) {
            if (name.rfind("local-files/", 0) == 0 || name == "local-files") prefixes.insert("");
            const auto slash = name.find('/');
            if (slash != std::string::npos && (name.substr(slash + 1).rfind("local-files/", 0) == 0 ||
                name.substr(slash + 1) == "local-files")) prefixes.insert(name.substr(0, slash + 1));
        } else if (!entry.directory && NativePath(name).filename() == L"texture_version.txt") {
            prefixes.insert(name.substr(0, name.size() - std::string("texture_version.txt").size()));
        }
    }
    if (prefixes.size() != 1) throw std::runtime_error("Package has no unique resource root");
    const auto prefix = *prefixes.begin();
    bool hasFile = false;
    for (auto& entry : entries) {
        if (entry.name.rfind(prefix, 0) != 0) continue;
        auto relative = entry.name.substr(prefix.size());
        if (relative.empty()) continue;
        if (kind == PackageKind::Translation) {
            if (relative == "version.txt" || relative.rfind("local-files/", 0) == 0)
                entry.target = NormalizeEntry(relative);
        } else {
            entry.target = NormalizeEntry("texture2d/" + relative);
        }
        if (!entry.target.empty() && !IsAllowedTarget(entry.target, kind))
            throw std::runtime_error("Package maps outside its resource namespace");
        hasFile = hasFile || (!entry.directory && !entry.target.empty());
    }
    if (!hasFile) throw std::runtime_error("Package contains no resources");
    CheckUniquePaths(entries, true);
}
} // namespace SafeResource
} // namespace GkmsResourceUpdate
