#pragma once

#include "resourceUpdate/ResourceInstall.hpp"
#include <map>
#include <limits>
#include <optional>
#include <wincrypt.h>
#pragma comment(lib, "advapi32.lib")

namespace LocalifyData {
namespace fs = std::filesystem;
using Json = GkmsResourceUpdate::SafeResource::Json;
using Kind = GkmsResourceUpdate::SafeResource::PackageKind;
using namespace GkmsResourceUpdate::SafeResource;
using GkmsResourceUpdate::SafeResource::ExtendedAbsolute;
using GkmsResourceUpdate::SafeResource::ExtendedRootLength;
using GkmsResourceUpdate::SafeResource::IsFilesystemRoot;

inline std::string Sha256(const fs::path& raw) {
    const auto file = ExtendedAbsolute(raw);
    CheckNoReparse(file);
    Handle input(CreateFileW(file.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    if (input.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Open resource for SHA256");
    HCRYPTPROV provider{}; HCRYPTHASH hash{};
    WinCheck(CryptAcquireContextW(&provider, nullptr, nullptr, PROV_RSA_AES, CRYPT_VERIFYCONTEXT), "Create SHA256 provider");
    try {
        WinCheck(CryptCreateHash(provider, CALG_SHA_256, 0, 0, &hash), "Create SHA256 hash");
        unsigned char buffer[65536]; DWORD count{};
        do {
            WinCheck(ReadFile(input.value, buffer, sizeof(buffer), &count, nullptr), "Read resource for SHA256");
            if (count) WinCheck(CryptHashData(hash, buffer, count, 0), "Hash resource");
        } while (count);
        unsigned char digest[32]; DWORD size = sizeof(digest);
        WinCheck(CryptGetHashParam(hash, HP_HASHVAL, digest, &size, 0), "Finish SHA256");
        static const char alphabet[] = "0123456789abcdef";
        std::string result; result.reserve(64);
        for (const auto value : digest) { result += alphabet[value >> 4]; result += alphabet[value & 15]; }
        CryptDestroyHash(hash); CryptReleaseContext(provider, 0);
        return result;
    } catch (...) {
        if (hash) CryptDestroyHash(hash);
        CryptReleaseContext(provider, 0); throw;
    }
}

inline bool SafeId(const std::string& id) {
    if (id.empty() || id.size() > 64) return false;
    for (const char c : id) if (!(c >= 'a' && c <= 'z') && !(c >= '0' && c <= '9') && c != '-') return false;
    return true;
}
inline std::wstring EnginePath(const fs::path& file) {
    const auto extended = ExtendedAbsolute(file).native();
    const auto ordinary = extended.rfind(L"\\\\?\\UNC\\", 0) == 0 ? L"\\\\" + extended.substr(8) : extended.substr(4);
    // Preserve Unity's already-used ordinary absolute syntax for normal paths.
    // Very long paths retain the native prefix; engine acceptance needs its own
    // real-game check and is not inferred from the offline filesystem tests.
    return ordinary.size() < MAX_PATH ? ordinary : extended;
}

struct Snapshot {
    fs::path localRoot, configRoot, userRoot, pluginRoot;
    bool migrated{};
    std::string migrationId;
    Json index;

    fs::path ProgramConfig() const { return migrated ? configRoot / "program.json" : localRoot / "config.json"; }
    fs::path LocalizationConfig() const { return migrated ? configRoot / "localization.json" : localRoot / "localizationConfig.json"; }
    fs::path Exports() const { return migrated ? userRoot / "Exports" : localRoot / "dump-files"; }
    fs::path SetRoot(const Json& set) const {
        const auto id = set.at("id").get<std::string>();
        const auto source = set.at("source").get<std::string>();
        if (!SafeId(id)) throw std::runtime_error("Invalid Localify resource-set id");
        fs::path root;
        if (source == "legacy") root = userRoot / "legacy-import" / NativePath(id);
        else if (source == "managed") root = pluginRoot / "packs" / NativePath(id);
        else throw std::runtime_error("Invalid Localify resource-set source");
        CheckNoReparse(root);
        if (!fs::is_directory(root)) throw std::runtime_error("Localify selected resource set is missing");
        return root;
    }
    fs::path ActiveBase(Kind kind) const {
        if (!migrated) return localRoot;
        return SetRoot(index.at(kind == Kind::Translation ? "activeTranslation" : "activeTexture"));
    }
    std::vector<fs::path> Roots(Kind kind, bool lowToHigh = false) const {
        if (!migrated) return {localRoot};
        std::vector<fs::path> result{userRoot / "overrides", ActiveBase(kind)};
        if (lowToHigh) std::reverse(result.begin(), result.end());
        return result;
    }
};
using View = std::shared_ptr<const Snapshot>;
inline std::mutex contextMutex;
inline View current;
inline thread_local View scopedView;

inline View Capture() {
    if (scopedView) return scopedView;
    std::lock_guard<std::mutex> lock(contextMutex);
    if (!current) throw std::runtime_error("Localify data paths are not initialized");
    return current;
}
template<class Result, class Work, class OnError>
inline Result ReadOrFallback(Result fallback, Work&& work, OnError&& onError) noexcept {
    try { return work(); }
    catch (const std::exception& error) { try { onError(error.what()); } catch (...) {} }
    catch (...) { try { onError("Unknown resource lookup failure"); } catch (...) {} }
    return fallback;
}
inline void Publish(View view) {
    if (!view) throw std::runtime_error("Missing Localify data snapshot");
    std::lock_guard<std::mutex> lock(contextMutex); current = std::move(view);
}
class ReadScope {
    View previous_;
public:
    explicit ReadScope(View view) : previous_(std::move(scopedView)) { scopedView = std::move(view); }
    ~ReadScope() { scopedView = std::move(previous_); }
};

struct SelectionToken {
    std::string source, id;
    uint64_t generation{};
    bool operator==(const SelectionToken&) const = default;
};
inline SelectionToken CaptureSelection(const Snapshot& view, Kind kind) {
    const auto& selected = view.index.at(kind == Kind::Translation ? "activeTranslation" : "activeTexture");
    return {selected.at("source").get<std::string>(), selected.at("id").get<std::string>(),
        selected.value("selectionGeneration", uint64_t{})};
}

inline void ValidateIndex(const Snapshot& snapshot) {
    const auto& index = snapshot.index;
    if (index.at("owner") != "gakumas.localify" || index.at("schemaVersion") != 1 ||
        !index.at("revision").is_number_unsigned() || !index.at("sets").is_array())
        throw std::runtime_error("Unsupported Localify resource index");
    std::set<std::string> names;
    for (const auto& set : index.at("sets")) {
        const auto id = set.at("id").get<std::string>();
        const auto source = set.at("source").get<std::string>();
        if (!SafeId(id) || (source != "legacy" && source != "managed") ||
            !names.insert(source + ":" + id).second)
            throw std::runtime_error("Invalid or duplicate Localify resource set");
        if (source == "managed" && set.at("kind") != "translation" && set.at("kind") != "texture")
            throw std::runtime_error("Invalid managed Localify package kind");
        snapshot.SetRoot(set);
    }
    for (const auto kind : {Kind::Translation, Kind::Texture}) {
        const auto& selected = index.at(kind == Kind::Translation ? "activeTranslation" : "activeTexture");
        if (selected.contains("selectionGeneration") && !selected.at("selectionGeneration").is_number_unsigned())
            throw std::runtime_error("Invalid Localify selection generation");
        bool found = false;
        for (const auto& set : index.at("sets")) {
            if (set.at("source") == selected.at("source") && set.at("id") == selected.at("id") &&
                (set.at("source") == "legacy" || set.at("kind") == (kind == Kind::Translation ? "translation" : "texture"))) found = true;
        }
        if (!found) throw std::runtime_error("Active Localify resource set is not registered");
        snapshot.ActiveBase(kind);
    }
}

inline View Load(const fs::path& local, const fs::path& configs = {}, const fs::path& users = {}, const fs::path& plugin = {}) {
    auto view = std::make_shared<Snapshot>();
    view->localRoot = ExtendedAbsolute(local);
    view->configRoot = ExtendedAbsolute(configs.empty() ? view->localRoot / "Configs/gakumas.localify" : configs);
    view->userRoot = ExtendedAbsolute(users.empty() ? view->localRoot / "UserData/gakumas.localify" : users);
    view->pluginRoot = ExtendedAbsolute(plugin.empty() ? view->localRoot / "PluginData/gakumas.localify" : plugin);
    for (const auto& root : {view->localRoot, view->configRoot, view->userRoot, view->pluginRoot}) CheckNoReparse(root);
    const auto marker = view->pluginRoot / "layout.json";
    CheckNoReparse(marker);
    if (fs::exists(marker)) {
        const auto receipt = ReadJournal(marker);
        if (receipt.at("owner") != "gakumas.localify" || receipt.at("schemaVersion") != 1 ||
            receipt.at("layoutVersion") != 2) throw std::runtime_error("Unsupported Localify data layout");
        view->migrationId = receipt.at("migrationId").get<std::string>();
        if (!SafeId(view->migrationId)) throw std::runtime_error("Invalid Localify migration id");
        view->migrated = true;
        view->index = ReadJournal(view->pluginRoot / "resource-index.json");
        ValidateIndex(*view);
    }
    return view;
}
// A new hosted root has no legacy resources to migrate. Create a pair of
// empty, immutable baseline packs and publish the layout marker last. Existing
// legacy or partially initialized user data is never replaced.
inline View InitializeFreshHosted(const fs::path& local, const fs::path& configs,
    const fs::path& users, const fs::path& plugin) {
    auto view = Load(local, configs, users, plugin);
    if (view->migrated) return view;
    for (const auto* name : {"config.json", "localizationConfig.json", "local-files",
                             "texture2d", "version.txt", ".localify-updates"}) {
        const auto path = view->localRoot / name;
        CheckNoReparse(path);
        if (fs::exists(path)) return view;
    }
    const auto marker = view->pluginRoot / "layout.json";
    const auto init = view->pluginRoot / "fresh-initializing.json";
    const auto index = view->pluginRoot / "resource-index.json";
    const auto packs = view->pluginRoot / "packs";
    const auto translation = packs / "fresh-translation";
    const auto texture = packs / "fresh-texture";
    const Json initRecord{{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"purpose", "fresh-empty-baselines"}};
    const Json manifest{{"owner", "gakumas.localify"}, {"schemaVersion", 1},
        {"files", Json::array()}, {"directories", Json::array()}};
    const Json expectedIndex{{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"revision", uint64_t{1}},
        {"activeTranslation", {{"source", "managed"}, {"id", "fresh-translation"}}},
        {"activeTexture", {{"source", "managed"}, {"id", "fresh-texture"}}},
        {"sets", Json::array({
            {{"source", "managed"}, {"id", "fresh-translation"}, {"kind", "translation"}, {"label", "Empty translation"}},
            {{"source", "managed"}, {"id", "fresh-texture"}, {"kind", "texture"}, {"label", "Empty texture"}}
        })}};
    for (const auto& path : {marker, init, index, packs, translation, texture}) CheckNoReparse(path);
    if (!fs::exists(init)) {
        if (fs::exists(index) || fs::exists(packs))
            throw std::runtime_error("Unmarked Localify data exists; fresh layout not initialized");
        EnsureDirectory(view->pluginRoot);
        const auto temporary = view->pluginRoot / NativePath("fresh-init-" + UniqueId() + ".tmp");
        SaveJournal(temporary, initRecord);
        if (!MoveFileExW(temporary.c_str(), init.c_str(), MOVEFILE_WRITE_THROUGH)) {
            const auto error = GetLastError();
            fs::remove(temporary);
            if (!fs::exists(init)) throw std::runtime_error("Cannot claim fresh layout: " + std::to_string(error));
        }
    }
    if (ReadJournal(init) != initRecord) throw std::runtime_error("Unknown Localify initialization record");
    // Resume only our exact empty initialization. Unexpected content is never
    // overwritten, even if an initialization was interrupted previously.
    if (fs::exists(packs)) {
        if (!fs::is_directory(packs)) throw std::runtime_error("Fresh packs path is not a directory");
        for (const auto& item : fs::directory_iterator(packs)) {
            CheckNoReparse(item.path());
            if ((item.path().filename() != "fresh-translation" && item.path().filename() != "fresh-texture") || !item.is_directory())
                throw std::runtime_error("Unexpected content in fresh packs");
        }
    }
    for (const auto& path : {translation, texture}) {
        if (fs::exists(path)) for (const auto& item : fs::directory_iterator(path)) {
            CheckNoReparse(item.path());
            if (item.path().filename() != "pack-manifest.json" || !item.is_regular_file() || ReadJournal(item.path()) != manifest)
                throw std::runtime_error("Unexpected content in fresh baseline");
        }
    }
    if (fs::exists(index) && ReadJournal(index) != expectedIndex)
        throw std::runtime_error("Unexpected unmarked Localify resource index");
    EnsureDirectory(translation); EnsureDirectory(texture);
    for (const auto& path : {translation, texture})
        if (!fs::exists(path / "pack-manifest.json")) SaveJournal(path / "pack-manifest.json", manifest);
    if (!fs::exists(index)) SaveJournal(index, expectedIndex);
    SaveJournal(marker, {{"owner", "gakumas.localify"}, {"schemaVersion", 1},
        {"layoutVersion", 2}, {"migrationId", "fresh-install"}});
    fs::remove(init);
    return Load(local, configs, users, plugin);
}
inline bool UseFreshHostedApiDefault(const Snapshot& view) {
    return view.migrated && view.migrationId == "fresh-install";
}
inline View ReadDisk() {
    const auto view = Capture();
    return Load(view->localRoot, view->configRoot, view->userRoot, view->pluginRoot);
}
inline void Initialize(const fs::path& local, const fs::path& configs = {}, const fs::path& users = {}, const fs::path& plugin = {}) {
    Publish(Load(local, configs, users, plugin));
}

inline std::vector<fs::path> Roots(Kind kind, bool lowToHigh = false) { return Capture()->Roots(kind, lowToHigh); }
inline fs::path Find(const std::string& relative, Kind kind = Kind::Translation) {
    const auto name = NativePath(NormalizeEntry(relative));
    for (const auto& root : Roots(kind)) {
        const auto file = root / name; CheckNoReparse(file);
        if (fs::is_regular_file(file)) return file;
    }
    return {};
}
inline std::vector<fs::path> Files(const std::string& relative, Kind kind, bool recursive) {
    const auto name = NativePath(NormalizeEntry(relative));
    std::map<std::wstring, fs::path> selected;
    for (const auto& base : Roots(kind, true)) {
        const auto directory = base / name; CheckNoReparse(directory);
        if (!fs::exists(directory)) continue;
        if (!fs::is_directory(directory)) throw std::runtime_error("Localify resource directory is a file");
        const auto insert = [&](const auto& item) {
            CheckNoReparse(item.path());
            if (item.is_regular_file()) selected[Fold(item.path().lexically_relative(directory).generic_wstring())] = item.path();
        };
        if (recursive) for (const auto& item : fs::recursive_directory_iterator(directory)) insert(item);
        else for (const auto& item : fs::directory_iterator(directory)) insert(item);
    }
    std::vector<fs::path> result;
    for (const auto& item : selected) result.push_back(item.second);
    return result;
}
inline fs::path Exports() { return Capture()->Exports(); }
inline std::vector<fs::path> PreviousExports() {
    const auto view = Capture();
    if (!view->migrated) return {view->Exports()};
    return {view->Exports() / NativePath("legacy-" + view->migrationId), view->Exports()};
}

inline bool SaveConfig(const fs::path& raw, const std::string& knownContent) {
    const auto file = ExtendedAbsolute(raw);
    auto known = Json::parse(knownContent);
    if (!known.is_object()) throw std::runtime_error("Localify config must be an object");
    Json merged = Json::object();
    if (fs::exists(file)) {
        merged = ReadJournal(file);
        if (!merged.is_object()) throw std::runtime_error("Existing Localify config is not an object; preserved");
    }
    for (const auto& item : known.items()) merged[item.key()] = item.value();
    SaveJournal(file, merged);
    return true;
}
inline bool SaveConfigNew(const fs::path& raw, const std::string& content,
    const std::function<void()>& beforePublish = {}) {
    const auto file = ExtendedAbsolute(raw);
    CheckNoReparse(file);
    if (fs::exists(file)) {
        if (!fs::is_regular_file(file)) throw std::runtime_error("Localify config target is not a regular file");
        return false;
    }
    const auto json = Json::parse(content);
    if (!json.is_object()) throw std::runtime_error("Localify config must be an object");
    EnsureDirectory(file.parent_path());
    const auto prepared = file.parent_path() / NativePath("default-" + UniqueId() + ".tmp");
    SaveJournal(prepared, json);
    if (beforePublish) beforePublish();
    CheckNoReparse(file);
    if (MoveFileExW(prepared.c_str(), file.c_str(), MOVEFILE_WRITE_THROUGH)) return true;
    const auto error = GetLastError();
    if (fs::is_regular_file(file)) {
        fs::remove(prepared); // Our unselected generated defaults only.
        return false;
    }
    throw std::runtime_error("Cannot create Localify defaults without replacing data: " + std::to_string(error));
}

class IndexLock {
    Handle lock_;
public:
    explicit IndexLock(const fs::path& raw) : lock_() {
        const auto pluginRoot = ExtendedAbsolute(raw);
        EnsureDirectory(pluginRoot); CheckNoReparse(pluginRoot / "resource-index.lock");
        lock_.value = CreateFileW((pluginRoot / "resource-index.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock_.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Lock Localify resource index");
    }
};
inline bool SelectSet(Kind kind, const std::string& source, const std::string& id,
    std::optional<uint64_t> expectedRevision = {}, std::optional<SelectionToken> expectedSelection = {}) {
    const auto view = ReadDisk();
    if (!view->migrated) throw std::runtime_error("Resource sets require the new Localify layout");
    IndexLock lock(view->pluginRoot);
    auto next = std::make_shared<Snapshot>(*view);
    next->index = ReadJournal(view->pluginRoot / "resource-index.json");
    ValidateIndex(*next);
    if (expectedRevision && next->index.at("revision").get<uint64_t>() != *expectedRevision)
        throw std::runtime_error("Resource selection changed; downloaded package retained for later selection");
    const auto previous = CaptureSelection(*next, kind);
    if (expectedSelection && previous != *expectedSelection) return false;
    if (previous.generation == (std::numeric_limits<uint64_t>::max)())
        throw std::runtime_error("Localify selection generation exhausted");
    next->index[kind == Kind::Translation ? "activeTranslation" : "activeTexture"] = {
        {"source", source}, {"id", id}, {"selectionGeneration", previous.generation + 1}};
    next->index["revision"] = next->index.at("revision").get<uint64_t>() + 1;
    ValidateIndex(*next);
    SaveJournal(view->pluginRoot / "resource-index.json", next->index);
    // Main-thread snapshot publication switches readers after maps are parsed.
    return true;
}

inline std::string InstallPack(const fs::path& raw, Kind kind, const std::string& version) {
    const auto incoming = ExtendedAbsolute(raw);
    const auto view = ReadDisk();
    if (!view->migrated) throw std::runtime_error("Managed packs require the new Localify layout");
    IndexLock lock(view->pluginRoot);
    const auto id = std::string(kind == Kind::Translation ? "translation-" : "texture-") + UniqueId();
    const auto target = view->pluginRoot / "packs" / NativePath(id);
    EnsureDirectory(target.parent_path()); CheckNoReparse(incoming); CheckNoReparse(target);
    Json manifest{{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"files", Json::array()}, {"directories", Json::array()}};
    for (const auto& item : fs::recursive_directory_iterator(incoming)) {
        CheckNoReparse(item.path());
        const auto relative = Utf8(item.path().lexically_relative(incoming));
        if (item.is_directory()) { manifest["directories"].push_back(relative); continue; }
        if (!item.is_regular_file()) throw std::runtime_error("Unexpected managed resource file type");
        if (!IsAllowedTarget(relative, kind)) throw std::runtime_error("Unexpected managed package resource");
        manifest["files"].push_back({{"path", relative}, {"sha256", Sha256(item.path())}, {"size", item.file_size()}});
    }
    if (manifest["files"].empty()) throw std::runtime_error("Empty managed package");
    SaveJournal(incoming / "pack-manifest.json", manifest);
    WinCheck(MoveFileExW(incoming.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH), "Publish immutable Localify resource pack");
    auto next = std::make_shared<Snapshot>(*view);
    next->index = ReadJournal(view->pluginRoot / "resource-index.json");
    ValidateIndex(*next);
    next->index["sets"].push_back({{"source", "managed"}, {"id", id},
        {"kind", kind == Kind::Translation ? "translation" : "texture"}, {"version", version},
        {"label", std::string(kind == Kind::Translation ? "Translation " : "Texture ") + version}, {"origin", "downloaded-package"}});
    next->index["revision"] = next->index.at("revision").get<uint64_t>() + 1;
    ValidateIndex(*next);
    SaveJournal(view->pluginRoot / "resource-index.json", next->index);
    return id; // A crash before indexing leaves a complete unselected pack; no data is deleted.
}
inline bool PackUnmodified(const Snapshot& view, Kind kind) {
    try {
        const auto& active = view.index.at(kind == Kind::Translation ? "activeTranslation" : "activeTexture");
        if (active.at("source") != "managed") return false;
        const auto root = view.ActiveBase(kind);
        const auto manifest = ReadJournal(root / "pack-manifest.json");
        if (manifest.at("owner") != "gakumas.localify" || manifest.at("schemaVersion") != 1 ||
            !manifest.at("files").is_array() || !manifest.at("directories").is_array()) return false;
        std::map<std::wstring, Json> expected;
        std::set<std::wstring> expectedDirectories;
        for (const auto& item : manifest.at("files")) {
            const auto relative = item.at("path").get<std::string>();
            if (!IsAllowedTarget(relative, kind) || !expected.emplace(Fold(NativePath(relative).generic_wstring()), item).second) return false;
        }
        for (const auto& name : manifest.at("directories")) {
            const auto relative = name.get<std::string>();
            if (NormalizeEntry(relative) != relative) return false;
            expectedDirectories.insert(Fold(NativePath(relative).generic_wstring()));
        }
        size_t found{}; std::set<std::wstring> directories;
        for (const auto& item : fs::recursive_directory_iterator(root)) {
            CheckNoReparse(item.path());
            const auto relative = Utf8(item.path().lexically_relative(root));
            const auto key = Fold(NativePath(relative).generic_wstring());
            if (item.is_directory()) { directories.insert(key); continue; }
            if (relative == "pack-manifest.json") continue;
            const auto entry = expected.find(key);
            if (!item.is_regular_file() || entry == expected.end() ||
                item.file_size() != entry->second.at("size").get<uintmax_t>() ||
                Sha256(item.path()) != entry->second.at("sha256").get<std::string>()) return false;
            ++found;
        }
        return found == expected.size() && directories == expectedDirectories;
    } catch (const std::exception&) { return false; } // Unproven ownership always asks before switching.
}
} // namespace LocalifyData
