#pragma once

// Explicit, copy-only migration. Runtime never calls Apply automatically.
#include "LocalifyData.hpp"
#include <functional>
#include <sstream>

namespace LocalifyData::Migration {
using Hook = std::function<void(const char*, const fs::path&)>;

inline fs::path Absolute(const fs::path& path) {
    if (path.empty()) throw std::runtime_error("A Localify local-root path is required");
    const auto result = ExtendedAbsolute(path);
    if (IsFilesystemRoot(result)) throw std::runtime_error("Localify local-root cannot be a filesystem root");
    CheckNoReparse(result);
    return result;
}
inline void Child(const fs::path& root, const fs::path& child) {
    const auto relative = ExtendedAbsolute(child).lexically_relative(root);
    if (relative.empty() || relative.is_absolute()) throw std::runtime_error("Migration path is not a child of local-root");
    for (const auto& part : relative) if (part == ".." || part == ".") throw std::runtime_error("Migration path escapes local-root");
    CheckNoReparse(child);
}
inline fs::path RelativePath(const fs::path& root, const std::string& relative) {
    if (NormalizeEntry(relative) != relative) throw std::runtime_error("Non-canonical migration path");
    const auto path = ExtendedAbsolute(root / NativePath(relative)); Child(root, path); return path;
}
inline std::string Relative(const fs::path& root, const fs::path& path) {
    const auto native = ExtendedAbsolute(path);
    Child(root, native); return Utf8(native.lexically_relative(root));
}
inline Json Paths(const fs::path& input) {
    const auto root = Absolute(input);
    return {{"configs", Utf8(ExtendedAbsolute(root / NativePath("Configs/gakumas.localify")))},
        {"userData", Utf8(ExtendedAbsolute(root / NativePath("UserData/gakumas.localify")))},
        {"pluginData", Utf8(ExtendedAbsolute(root / NativePath("PluginData/gakumas.localify")))}};
}
inline Json ExpectedIndex(const std::string& id) {
    return {{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"revision", uint64_t{1}},
        {"activeTranslation", {{"source", "legacy"}, {"id", id}}},
        {"activeTexture", {{"source", "legacy"}, {"id", id}}},
        {"sets", Json::array({{{"source", "legacy"}, {"id", id},
            {"label", "Imported upstream resources"}, {"origin", "legacy-unclassified"}}})}};
}
inline Json ExpectedMarker(const std::string& id) {
    return {{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"layoutVersion", 2}, {"migrationId", id}};
}
inline Json Fingerprint(const Json& plan) {
    return {{"owner", plan.at("owner")}, {"schemaVersion", plan.at("schemaVersion")},
        {"layoutVersion", plan.at("layoutVersion")}, {"localRoot", plan.at("localRoot")},
        {"migrationId", plan.at("migrationId")}, {"paths", plan.at("paths")},
        {"sources", plan.at("sources")}, {"sourceDirectories", plan.at("sourceDirectories")},
        {"files", plan.at("files")}, {"directories", plan.at("directories")},
        {"index", plan.at("index")}, {"marker", plan.at("marker")}};
}
inline bool SamePath(const fs::path& left, const fs::path& right) {
    return Fold(Absolute(left).generic_wstring()) == Fold(Absolute(right).generic_wstring());
}
inline void AddDirectory(Json& plan, const fs::path& root, const fs::path& target) {
    Child(root, target);
    if (fs::exists(target) && !fs::is_directory(target)) plan["conflicts"].push_back(Utf8(target));
    plan["directories"].push_back(Relative(root, target));
}
inline void AddFile(Json& plan, const fs::path& root, const fs::path& source, const fs::path& target,
    const std::string& reason) {
    Child(root, source); Child(root, target);
    if (!fs::exists(source)) return;
    if (!fs::is_regular_file(source)) throw std::runtime_error("Expected a regular migration source: " + Utf8(source));
    const auto digest = Sha256(source);
    const auto size = fs::file_size(source);
    plan["sources"].push_back({{"path", Relative(root, source)}, {"sha256", digest}, {"size", size}});
    plan["files"].push_back({{"source", Relative(root, source)}, {"target", Relative(root, target)},
        {"sha256", digest}, {"size", size}, {"reason", reason}});
    if (fs::exists(target)) {
        if (!fs::is_regular_file(target) || Sha256(target) != digest) plan["conflicts"].push_back(Utf8(target));
    }
}
inline void AddTree(Json& plan, const fs::path& root, const fs::path& source, const fs::path& target,
    const std::string& reason) {
    Child(root, source); Child(root, target);
    if (!fs::exists(source)) return;
    if (fs::is_regular_file(source)) { AddFile(plan, root, source, target, reason); return; }
    if (!fs::is_directory(source)) throw std::runtime_error("Unexpected migration source type");
    std::vector<std::pair<fs::path, fs::path>> pending{{source, target}};
    while (!pending.empty()) {
        auto pair = pending.back(); pending.pop_back();
        CheckNoReparse(pair.first); CheckNoReparse(pair.second);
        plan["sourceDirectories"].push_back(Relative(root, pair.first));
        AddDirectory(plan, root, pair.second);
        for (const auto& entry : fs::directory_iterator(pair.first)) {
            CheckNoReparse(entry.path());
            const auto destination = pair.second / entry.path().filename();
            if (entry.is_directory()) pending.emplace_back(entry.path(), destination);
            else AddFile(plan, root, entry.path(), destination, reason);
        }
    }
}
inline void SortUnique(Json& values) {
    std::sort(values.begin(), values.end());
    values.erase(std::unique(values.begin(), values.end()), values.end());
}
inline void CheckTargetExtras(Json& plan, const fs::path& root, const fs::path& target) {
    Child(root, target);
    if (!fs::exists(target) || !fs::is_directory(target)) return;
    std::set<std::wstring> files, directories;
    for (const auto& item : plan.at("files")) files.insert(Fold(NativePath(item.at("target").get<std::string>()).generic_wstring()));
    for (const auto& item : plan.at("directories")) directories.insert(Fold(NativePath(item.get<std::string>()).generic_wstring()));
    for (const auto& entry : fs::recursive_directory_iterator(target)) {
        CheckNoReparse(entry.path());
        const auto name = Fold(NativePath(Relative(root, entry.path())).generic_wstring());
        if ((entry.is_directory() && !directories.count(name)) || (!entry.is_directory() && !files.count(name)))
            plan["conflicts"].push_back(Utf8(entry.path()));
    }
}
inline void CheckPhaseOne(Json& plan, const fs::path& root) {
    const auto state = root / ".localify-updates";
    CheckNoReparse(state);
    if (!fs::exists(state)) return;
    const auto transactions = state / "transactions"; CheckNoReparse(transactions);
    if (!fs::exists(transactions)) return;
    if (!fs::is_directory(transactions)) throw std::runtime_error("Phase-one transactions path is not a directory");
    for (const auto& entry : fs::directory_iterator(transactions)) {
        CheckNoReparse(entry.path());
        if (!entry.is_directory()) continue;
        const auto journal = entry.path() / "journal.json"; CheckNoReparse(journal);
        if (!fs::exists(journal)) continue;
        try {
            const auto value = ReadJournal(journal);
            if (value.at("owner") != "gakumas.localify" || value.at("schemaVersion") != 1)
                throw std::runtime_error("Unknown phase-one journal owner/schema");
            if (value.at("phase") == "committing") plan["blockers"].push_back(
                "Unfinished phase-one update: " + Utf8(entry.path()) +
                ". Stop the game, back up the installation, run recover-updates explicitly, then build a new plan.");
        } catch (const std::exception& error) {
            plan["blockers"].push_back("Cannot prove phase-one recovery state: " + Utf8(journal) + ": " + error.what());
        }
    }
}
inline Json BuildPlan(const fs::path& localRoot, const std::string& id) {
    if (!SafeId(id)) throw std::runtime_error("Migration id must contain 1-64 lowercase ASCII letters, digits or hyphens");
    const auto root = Absolute(localRoot);
    if (!fs::is_directory(root)) throw std::runtime_error("Localify local-root is missing or not a directory");
    const auto paths = Paths(root);
    const auto configs = ExtendedAbsolute(NativePath(paths.at("configs").get<std::string>()));
    const auto users = ExtendedAbsolute(NativePath(paths.at("userData").get<std::string>()));
    const auto plugin = ExtendedAbsolute(NativePath(paths.at("pluginData").get<std::string>()));
    const auto legacy = users / "legacy-import" / NativePath(id);
    const auto exports = users / "Exports" / NativePath("legacy-" + id);
    const auto migration = plugin / "migrations" / NativePath(id);
    for (const auto& path : {configs, users, plugin, legacy, exports, migration}) Child(root, path);
    Json plan{{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"layoutVersion", 2}, {"state", "planned"},
        {"localRoot", Utf8(root)}, {"migrationId", id}, {"paths", paths},
        {"sources", Json::array()}, {"sourceDirectories", Json::array()},
        {"files", Json::array()}, {"directories", Json::array()}, {"conflicts", Json::array()},
        {"blockers", Json::array()}, {"excludedTopLevel", Json::array()},
        {"index", ExpectedIndex(id)}, {"marker", ExpectedMarker(id)}};
    // Load validates an existing marker and resource index. It never silently
    // treats a corrupt/unknown new layout as a reason to retry old paths.
    const auto view = Load(root);
    if (view->migrated) {
        plan["state"] = "already-active";
        plan["activeMigrationId"] = view->migrationId;
        return plan;
    }
    for (const auto& target : {configs, users, plugin, legacy, exports, migration, users / "overrides"}) AddDirectory(plan, root, target);
    CheckPhaseOne(plan, root);
    for (const auto& pair : {std::pair{"config.json", "program.json"}, std::pair{"localizationConfig.json", "localization.json"}}) {
        const auto source = root / pair.first; CheckNoReparse(source);
        if (!fs::exists(source)) continue;
        if (!ReadJournal(source).is_object()) throw std::runtime_error("Legacy configuration must be a JSON object; original retained: " + Utf8(source));
        AddFile(plan, root, source, configs / pair.second, "config-raw-preserved");
        AddFile(plan, root, source, migration / "source-configs" / pair.first, "config-raw-backup");
    }
    for (const auto& name : {"local-files", "texture2d"}) AddTree(plan, root, root / name, legacy / name, "legacy-unclassified");
    AddFile(plan, root, root / "version.txt", legacy / "version.txt", "legacy-unclassified");
    AddTree(plan, root, root / "dump-files", exports, "legacy-exports");
    AddTree(plan, root, root / ".localify-updates", migration / "phase1-recovery", "phase1-recovery-copy-only");
    const std::set<std::string> unconsumed{"genericTrans", "masterTrans", "resource", "localization.json", "generic.json", "generic.split.json", "gkamsZHFontMIX.otf", "gakumasassets"};
    const std::set<std::string> recovery{"temp_download.zip", "temp_texture_download.zip", "resource.staging", "texture2d.extract", "texture2d.tmp", "texture2d.old"};
    for (const auto& name : unconsumed) AddTree(plan, root, root / NativePath(name), legacy / "unconsumed-top-level" / NativePath(name), "unconsumed-legacy-candidate");
    for (const auto& name : recovery) AddTree(plan, root, root / NativePath(name), migration / "legacy-recovery" / NativePath(name), "legacy-recovery-copy-only");
    const std::set<std::string> consumed{"config.json", "localizationConfig.json", "local-files", "texture2d", "version.txt", "dump-files", ".localify-updates"};
    for (const auto& entry : fs::directory_iterator(root)) {
        const auto name = Utf8(entry.path().filename());
        if (!consumed.count(name) && !unconsumed.count(name) && !recovery.count(name)) plan["excludedTopLevel"].push_back(name);
    }
    // Record all implied parents too, so an interrupted prefix is recognised
    // but independently added destination files are never silently merged.
    const auto listedDirectories = plan.at("directories");
    for (const auto& item : plan.at("files")) {
        auto target = RelativePath(root, item.at("target").get<std::string>()).parent_path();
        while (target != root) {
            AddDirectory(plan, root, target);
            const auto parent = target.parent_path();
            if (parent.empty() || parent == target) throw std::runtime_error("Cannot reach Localify root from migration target");
            target = parent;
        }
    }
    for (const auto& item : listedDirectories) {
        auto target = RelativePath(root, item.get<std::string>()).parent_path();
        while (target != root) {
            AddDirectory(plan, root, target);
            const auto parent = target.parent_path();
            if (parent.empty() || parent == target) throw std::runtime_error("Cannot reach Localify root from migration directory");
            target = parent;
        }
    }
    for (const auto* key : {"sources", "sourceDirectories", "files", "directories", "excludedTopLevel"}) SortUnique(plan[key]);
    for (const auto& target : {legacy, exports, migration / "phase1-recovery", migration / "legacy-recovery", migration / "source-configs"}) CheckTargetExtras(plan, root, target);
    const auto indexPath = plugin / "resource-index.json"; CheckNoReparse(indexPath);
    if (fs::exists(indexPath) && (!fs::is_regular_file(indexPath) || ReadJournal(indexPath) != plan.at("index")))
        plan["conflicts"].push_back(Utf8(indexPath));
    SortUnique(plan["conflicts"]);
    if (!plan["blockers"].empty() || !plan["conflicts"].empty()) plan["state"] = "blocked";
    return plan;
}

inline void WriteJsonNew(const fs::path& input, const Json& value) {
    const auto file = ExtendedAbsolute(input);
    CheckNoReparse(file);
    EnsureDirectory(file.parent_path());
    const auto temporary = file.parent_path() / NativePath("migration-" + UniqueId() + ".tmp");
    bool owned = false;
    try {
        Handle out(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (out.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Create new migration JSON");
        owned = true;
        const auto bytes = value.dump(2);
        size_t offset = 0;
        while (offset < bytes.size()) {
            const auto chunk = static_cast<DWORD>((std::min)(bytes.size() - offset, size_t{1024 * 1024}));
            DWORD written{}; WinCheck(WriteFile(out.value, bytes.data() + offset, chunk, &written, nullptr), "Write migration JSON");
            if (written != chunk) throw std::runtime_error("Incomplete migration JSON write");
            offset += written;
        }
        WinCheck(FlushFileBuffers(out.value), "Flush migration JSON");
        CloseHandle(out.value); out.value = INVALID_HANDLE_VALUE;
        CheckNoReparse(file);
        WinCheck(MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_WRITE_THROUGH), "Publish new migration JSON without replacement");
    } catch (...) {
        if (owned) { CheckNoReparse(temporary); std::error_code ignored; fs::remove(temporary, ignored); }
        throw;
    }
}
inline void WritePlan(const fs::path& file, const Json& plan) {
    const auto root = Absolute(NativePath(plan.at("localRoot").get<std::string>()));
    const auto report = ExtendedAbsolute(file); CheckNoReparse(report);
    const auto relative = report.lexically_relative(root);
    if (!relative.empty() && !relative.is_absolute() && *relative.begin() != "..")
        throw std::runtime_error("Plan output must be outside local-root; Plan does not write into the installation");
    WriteJsonNew(report, plan);
}
inline void VerifyPersistedPlan(const fs::path& root, const Json& original, const Json& current) {
    if (original.at("owner") != "gakumas.localify" || original.at("schemaVersion") != 1 || original.at("layoutVersion") != 2 ||
        !SamePath(root, NativePath(original.at("localRoot").get<std::string>()))) throw std::runtime_error("Foreign or unsupported migration plan");
    if (original.at("state") != "planned") throw std::runtime_error("Only a reviewed, unblocked migration plan can be applied");
    if (Fingerprint(original) != Fingerprint(current)) throw std::runtime_error("Migration source or mapping changed since the reviewed plan; write and review a new plan");
    if (current.at("state") == "blocked") throw std::runtime_error("Migration plan is blocked by a conflict or unfinished update; original files retained");
}
inline void CopyEntry(const fs::path& local, const fs::path& scratchInput, const Json& entry) {
    const auto root = Absolute(local);
    const auto scratch = ExtendedAbsolute(scratchInput); Child(root, scratch);
    const auto source = RelativePath(root, entry.at("source").get<std::string>());
    const auto target = RelativePath(root, entry.at("target").get<std::string>());
    const auto digest = entry.at("sha256").get<std::string>();
    if (Sha256(source) != digest || fs::file_size(source) != entry.at("size").get<uintmax_t>()) throw std::runtime_error("Migration source changed before copy");
    if (fs::exists(target)) {
        if (!fs::is_regular_file(target) || Sha256(target) != digest) throw std::runtime_error("Migration target conflict; no overwrite");
        return;
    }
    EnsureDirectory(target.parent_path()); Child(root, target);
    const auto temporary = scratch / NativePath("copy-" + UniqueId() + ".tmp");
    bool owned = false;
    try {
        CheckNoReparse(temporary);
        Handle input(CreateFileW(source.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
            OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
        if (input.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Open migration source");
        Handle output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
            CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (output.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Create migration copy without replacement");
        owned = true;
        unsigned char buffer[65536]; DWORD count{};
        do {
            WinCheck(ReadFile(input.value, buffer, sizeof(buffer), &count, nullptr), "Read migration source");
            DWORD offset{};
            while (offset < count) {
                DWORD written{};
                WinCheck(WriteFile(output.value, buffer + offset, count - offset, &written, nullptr), "Write migration copy");
                if (!written) throw std::runtime_error("Incomplete migration copy");
                offset += written;
            }
        } while (count);
        WinCheck(FlushFileBuffers(output.value), "Flush migration copy");
        CloseHandle(output.value); output.value = INVALID_HANDLE_VALUE;
        CloseHandle(input.value); input.value = INVALID_HANDLE_VALUE;
        if (Sha256(temporary) != digest) throw std::runtime_error("Migration copy SHA256 mismatch");
        Child(root, target);
        WinCheck(MoveFileExW(temporary.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH), "Publish migration copy without replacement");
    } catch (...) {
        if (owned) { CheckNoReparse(temporary); std::error_code ignored; fs::remove(temporary, ignored); }
        throw;
    }
}
inline Json Apply(const fs::path& localRoot, const Json& plan, const Hook& hook = {}) {
    const auto root = Absolute(localRoot);
    if (plan.at("owner") != "gakumas.localify" || plan.at("schemaVersion") != 1 || plan.at("layoutVersion") != 2)
        throw std::runtime_error("Foreign or unsupported migration plan");
    const auto id = plan.at("migrationId").get<std::string>();
    if (!SafeId(id) || !SamePath(root, NativePath(plan.at("localRoot").get<std::string>()))) throw std::runtime_error("Plan belongs to a different Localify installation");
    auto current = BuildPlan(root, id);
    if (current.at("state") == "already-active") {
        if (current.at("activeMigrationId") != id) throw std::runtime_error("Another migration already owns the active layout");
        return {{"state", "already-active"}, {"migrationId", id}};
    }
    VerifyPersistedPlan(root, plan, current);
    const auto plugin = ExtendedAbsolute(root / "PluginData/gakumas.localify");
    IndexLock lock(plugin);
    current = BuildPlan(root, id);
    VerifyPersistedPlan(root, plan, current);
    const auto migration = plugin / "migrations" / NativePath(id);
    const auto scratch = migration / "scratch";
    const auto journalFile = migration / "journal.json";
    EnsureDirectory(scratch); Child(root, journalFile);
    const auto fingerprint = Fingerprint(plan);
    if (fs::exists(journalFile)) {
        const auto previous = ReadJournal(journalFile);
        if (previous.at("owner") != "gakumas.localify" || previous.at("schemaVersion") != 1 ||
            previous.at("migrationId") != id || previous.at("plan") != fingerprint)
            throw std::runtime_error("Existing migration journal belongs to a different plan; preserved");
    }
    SaveJournal(journalFile, {{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"migrationId", id}, {"phase", "copying"}, {"plan", fingerprint}});
    for (const auto& name : plan.at("directories")) EnsureDirectory(RelativePath(root, name.get<std::string>()));
    for (const auto& entry : plan.at("files")) {
        const auto target = RelativePath(root, entry.at("target").get<std::string>());
        if (hook) hook("before-copy", target);
        CopyEntry(root, scratch, entry);
        if (hook) hook("after-copy", target);
    }
    if (hook) hook("before-verify", root);
    current = BuildPlan(root, id);
    VerifyPersistedPlan(root, plan, current);
    for (const auto& entry : plan.at("files")) {
        const auto target = RelativePath(root, entry.at("target").get<std::string>());
        if (!fs::is_regular_file(target) || Sha256(target) != entry.at("sha256").get<std::string>())
            throw std::runtime_error("Migration target verification failed; layout stays legacy");
    }
    const auto indexFile = plugin / "resource-index.json";
    if (!fs::exists(indexFile)) WriteJsonNew(indexFile, plan.at("index"));
    else if (ReadJournal(indexFile) != plan.at("index")) throw std::runtime_error("Resource index conflict before activation");
    // Validate selected set directories using the same contract as runtime.
    Snapshot candidate; candidate.localRoot = root; candidate.configRoot = ExtendedAbsolute(root / "Configs/gakumas.localify");
    candidate.userRoot = ExtendedAbsolute(root / "UserData/gakumas.localify"); candidate.pluginRoot = plugin;
    candidate.migrated = true; candidate.migrationId = id; candidate.index = plan.at("index"); ValidateIndex(candidate);
    SaveJournal(journalFile, {{"owner", "gakumas.localify"}, {"schemaVersion", 1}, {"migrationId", id}, {"phase", "verified"}, {"plan", fingerprint}});
    if (hook) hook("before-marker", plugin / "layout.json");
    // A late source/target change or new recovery journal must not slip past
    // the hook/crash boundary used by both fixtures and operational tooling.
    VerifyPersistedPlan(root, plan, BuildPlan(root, id));
    WriteJsonNew(plugin / "layout.json", plan.at("marker"));
    return {{"state", "active"}, {"migrationId", id}, {"filesVerified", plan.at("files").size()},
        {"sourcePolicy", "retained"}, {"layout", Utf8(plugin / "layout.json")}};
}
} // namespace LocalifyData::Migration
