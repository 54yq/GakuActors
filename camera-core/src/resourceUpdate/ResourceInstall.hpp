#pragma once

// Phase-one compatibility installer. It keeps the existing lookup layout, but
// never claims an unknown old file as managed merely because it exists (or has
// the same bytes as a package). User/legacy conflicts survive every update.
#include "ArchivePath.hpp"
#include "ResourceCommit.hpp"
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <fstream>
#include <functional>
#include <memory>

namespace GkmsResourceUpdate {
namespace SafeResource {
using Json = nlohmann::json;

class Handle {
public:
    HANDLE value{INVALID_HANDLE_VALUE};
    explicit Handle(HANDLE handle = INVALID_HANDLE_VALUE) : value(handle) {}
    ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
    Handle(const Handle&) = delete;
    Handle& operator=(const Handle&) = delete;
};

inline void WinCheck(BOOL result, const char* operation) {
    if (!result) throw std::runtime_error(std::string(operation) + ": Windows error " + std::to_string(GetLastError()));
}

inline void EnsureDirectory(const std::filesystem::path& raw) {
    const auto path = ExtendedAbsolute(raw);
    CheckNoReparse(path);
    std::filesystem::create_directories(path);
    CheckNoReparse(path);
}

inline bool SameBytes(const std::filesystem::path& leftRaw, const std::filesystem::path& rightRaw) {
    const auto left = ExtendedAbsolute(leftRaw), right = ExtendedAbsolute(rightRaw);
    CheckNoReparse(left);
    CheckNoReparse(right);
    if (!std::filesystem::is_regular_file(left) || !std::filesystem::is_regular_file(right)) return false;
    if (std::filesystem::file_size(left) != std::filesystem::file_size(right)) return false;
    std::ifstream a(left, std::ios::binary), b(right, std::ios::binary);
    if (!a || !b) throw std::runtime_error("Cannot compare resource files");
    char x[65536], y[65536];
    do {
        a.read(x, sizeof(x)); b.read(y, sizeof(y));
        if (a.bad() || b.bad()) throw std::runtime_error("Cannot read complete resource files");
        if (a.gcount() != b.gcount() || !std::equal(x, x + a.gcount(), y)) return false;
    } while (a.gcount());
    return true;
}

inline bool HandleMatches(HANDLE handle, const std::filesystem::path& expectedRaw) {
    const auto expected = ExtendedAbsolute(expectedRaw);
    CheckNoReparse(expected);
    std::ifstream input(expected, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read resource snapshot");
    LARGE_INTEGER zero{};
    WinCheck(SetFilePointerEx(handle, zero, nullptr, FILE_BEGIN), "Rewind resource");
    char actual[65536], wanted[65536];
    for (;;) {
        DWORD got = 0;
        WinCheck(ReadFile(handle, actual, sizeof(actual), &got, nullptr), "Read locked resource");
        input.read(wanted, sizeof(wanted));
        if (input.bad()) throw std::runtime_error("Cannot read complete resource snapshot");
        if (got != static_cast<DWORD>(input.gcount()) || !std::equal(actual, actual + got, wanted)) return false;
        if (!got) return true;
    }
}

inline void DurableCopyNew(const std::filesystem::path& sourceRaw, const std::filesystem::path& targetRaw) {
    const auto source = ExtendedAbsolute(sourceRaw), target = ExtendedAbsolute(targetRaw);
    CheckNoReparse(source);
    EnsureDirectory(target.parent_path());
    CheckNoReparse(target);
    WinCheck(CopyFileW(source.c_str(), target.c_str(), TRUE), "Copy resource snapshot");
    Handle file(CreateFileW(target.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
        OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr));
    if (file.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Open copied resource");
    WinCheck(FlushFileBuffers(file.value), "Flush copied resource");
}

inline std::string UniqueId() {
    static std::atomic<unsigned long> counter{};
    return std::to_string(GetCurrentProcessId()) + "-" + std::to_string(GetTickCount64()) + "-" +
        std::to_string(counter.fetch_add(1));
}

inline void SaveJournal(const std::filesystem::path& fileRaw, const Json& json) {
    const auto file = ExtendedAbsolute(fileRaw);
    EnsureDirectory(file.parent_path());
    const auto temporary = file.parent_path() / NativePath("journal-" + UniqueId() + ".tmp");
    const auto contents = json.dump(2);
    Handle output(CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL, nullptr));
    if (output.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Create resource journal");
    DWORD written = 0;
    WinCheck(WriteFile(output.value, contents.data(), static_cast<DWORD>(contents.size()), &written, nullptr),
        "Write resource journal");
    if (written != contents.size()) throw std::runtime_error("Incomplete resource journal write");
    WinCheck(FlushFileBuffers(output.value), "Flush resource journal");
    CloseHandle(output.value); output.value = INVALID_HANDLE_VALUE;
    CheckNoReparse(file);
    WinCheck(MoveFileExW(temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
        "Commit resource journal");
}

inline Json ReadJournal(const std::filesystem::path& fileRaw) {
    const auto file = ExtendedAbsolute(fileRaw);
    CheckNoReparse(file);
    if (std::filesystem::file_size(file) > 64 * 1024 * 1024)
        throw std::runtime_error("Oversized resource journal");
    std::ifstream input(file, std::ios::binary);
    if (!input) throw std::runtime_error("Cannot read resource journal");
    try { return Json::parse(input); }
    catch (const nlohmann::json::parse_error&) { throw std::runtime_error("Invalid JSON document; original retained"); }
}

inline void RenameHandleNew(HANDLE handle, const std::filesystem::path& destinationRaw) {
    const auto destination = ExtendedAbsolute(destinationRaw);
    CheckNoReparse(destination);
    const auto name = destination.native();
    const size_t bytes = name.size() * sizeof(wchar_t);
    std::vector<unsigned char> storage(sizeof(FILE_RENAME_INFO) + bytes);
    auto* rename = reinterpret_cast<FILE_RENAME_INFO*>(storage.data());
    rename->ReplaceIfExists = FALSE;
    rename->RootDirectory = nullptr;
    rename->FileNameLength = static_cast<DWORD>(bytes);
    std::copy(name.begin(), name.end(), rename->FileName);
    WinCheck(SetFileInformationByHandle(handle, FileRenameInfo, rename,
        static_cast<DWORD>(storage.size())), "Move locked resource into recovery");
}

enum class ConflictChoice { PreserveCurrent, UsePackageWithBackup };
struct InstallResult {
    size_t installed{}, unchanged{}, conflicts{};
    std::filesystem::path transaction;
};
using InstallHook = std::function<void(const char*, const std::filesystem::path&)>;

class ResourceInstaller {
    std::filesystem::path live_, state_;
    std::unique_ptr<Handle> lock_;

    static bool VersionFile(const std::string& path) {
        return path == "version.txt" || path == "texture2d/texture_version.txt";
    }
    static PackageKind Kind(const Json& journal) {
        const auto kind = journal.at("kind").get<std::string>();
        if (kind == "translation") return PackageKind::Translation;
        if (kind == "texture") return PackageKind::Texture;
        throw std::runtime_error("Unknown resource transaction kind");
    }
    std::filesystem::path Baseline(PackageKind kind, const std::filesystem::path& relative) const {
        return state_ / "managed" / (kind == PackageKind::Translation ? "translation" : "texture") / relative;
    }
    void RecordManaged(PackageKind kind, const std::filesystem::path& relative,
        const std::filesystem::path& candidate) {
        const auto baseline = Baseline(kind, relative);
        EnsureDirectory(baseline.parent_path());
        const auto temporary = baseline.parent_path() / NativePath("baseline-" + UniqueId() + ".tmp");
        DurableCopyNew(candidate, temporary);
        CheckNoReparse(baseline);
        WinCheck(MoveFileExW(temporary.c_str(), baseline.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
            "Save managed resource baseline");
    }
    void ValidateJournal(const std::filesystem::path& transaction, const Json& journal) {
        CheckNoReparse(transaction);
        if (journal.at("owner") != "gakumas.localify" || journal.at("schemaVersion") != 1 ||
            !journal.at("entries").is_array() || journal.at("entries").size() > 250000)
            throw std::runtime_error("Unsupported resource transaction");
        const auto kind = Kind(journal);
        std::set<std::wstring> names;
        for (const auto& entry : journal.at("entries")) {
            const auto path = entry.at("path").get<std::string>();
            if (!IsAllowedTarget(path, kind) || !names.insert(Fold(NativePath(path).generic_wstring())).second)
                throw std::runtime_error("Invalid resource transaction target");
            CheckNoReparse(live_ / NativePath(path));
            CheckNoReparse(transaction / "incoming" / NativePath(path));
            CheckNoReparse(transaction / "before" / NativePath(path));
            CheckNoReparse(transaction / "retired" / NativePath(path));
            CheckNoReparse(transaction / "apply" / NativePath(path));
        }
    }
public:
    explicit ResourceInstaller(const std::filesystem::path& live)
        : live_(ExtendedAbsolute(live)), state_(live_ / ".localify-updates") {
        EnsureDirectory(live_);
        CheckNoReparse(state_);
        const bool existed = std::filesystem::exists(state_);
        if (existed && (!std::filesystem::is_directory(state_) ||
            !std::filesystem::is_regular_file(state_ / "owner.json")))
            throw std::runtime_error("Existing update-state directory is not owned by Localify");
        if (!existed) {
            const auto initializing = live_ / NativePath(".localify-initializing-" + UniqueId());
            if (!std::filesystem::create_directory(initializing))
                throw std::runtime_error("Cannot initialize unique Localify update state");
            SaveJournal(initializing / "owner.json", {{"owner", "gakumas.localify"}, {"schemaVersion", 1}});
            // Publish a fully initialized directory. A crash before this rename
            // leaves a harmless private initializer, not an unowned final root.
            if (!MoveFileExW(initializing.c_str(), state_.c_str(), MOVEFILE_WRITE_THROUGH) &&
                !std::filesystem::is_regular_file(state_ / "owner.json"))
                WinCheck(FALSE, "Publish Localify update state");
        }
        CheckNoReparse(state_ / "update.lock");
        lock_ = std::make_unique<Handle>(CreateFileW((state_ / "update.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
            0, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr));
        if (lock_->value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Lock resource updates for this installation");
        const auto owner = ReadJournal(state_ / "owner.json");
        if (owner.value("owner", "") != "gakumas.localify" || owner.value("schemaVersion", 0) != 1)
            throw std::runtime_error("Unsupported Localify update-state directory");
        EnsureDirectory(state_ / "transactions");
    }

    std::filesystem::path NewTransaction() {
        const auto transaction = state_ / "transactions" / NativePath(UniqueId());
        CheckNoReparse(transaction);
        if (!std::filesystem::create_directory(transaction)) throw std::runtime_error("Cannot create unique update transaction");
        return transaction;
    }

    size_t Prepare(const std::filesystem::path& transaction, PackageKind kind) {
        CheckNoReparse(transaction);
        Json journal{{"owner", "gakumas.localify"}, {"schemaVersion", 1},
            {"kind", kind == PackageKind::Translation ? "translation" : "texture"},
            {"phase", "preparing"}, {"entries", Json::array()}};
        std::vector<std::string> paths;
        for (const auto& file : std::filesystem::recursive_directory_iterator(transaction / "incoming")) {
            CheckNoReparse(file.path());
            if (!file.is_regular_file()) continue;
            const auto path = Utf8(file.path().lexically_relative(transaction / "incoming"));
            if (!IsAllowedTarget(path, kind)) throw std::runtime_error("Unexpected staged resource target");
            paths.push_back(path);
        }
        if (paths.empty()) throw std::runtime_error("Empty resource update");
        std::sort(paths.begin(), paths.end(), [](const std::string& a, const std::string& b) {
            if (VersionFile(a) != VersionFile(b)) return !VersionFile(a);
            return a < b;
        });
        size_t conflicts = 0;
        for (const auto& path : paths) {
            const auto relative = NativePath(path);
            const auto target = live_ / relative;
            const auto incoming = transaction / "incoming" / relative;
            CheckNoReparse(target);
            const bool existed = std::filesystem::exists(target);
            std::string action = "new";
            if (existed) {
                if (!std::filesystem::is_regular_file(target)) throw std::runtime_error("Resource target is not a regular file");
                Handle source(CreateFileW(target.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
                    FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
                if (source.value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Snapshot current resource");
                DurableCopyNew(target, transaction / "before" / relative);
                if (!HandleMatches(source.value, transaction / "before" / relative))
                    throw std::runtime_error("Current resource changed while preparing update");
                if (HandleMatches(source.value, incoming)) action = "same";
                else if (std::filesystem::is_regular_file(Baseline(kind, relative)) &&
                    HandleMatches(source.value, Baseline(kind, relative))) action = "managed";
                else { action = "conflict"; if (!VersionFile(path)) ++conflicts; }
            }
            journal["entries"].push_back({{"path", path}, {"action", action}, {"hadOriginal", existed}, {"status", "planned"}});
        }
        journal["phase"] = "prepared";
        SaveJournal(transaction / "journal.json", journal);
        return conflicts;
    }

    InstallResult Apply(const std::filesystem::path& transaction, ConflictChoice choice,
        const std::atomic_bool* cancel = nullptr, const InstallHook& hook = {}, CommitGate* gate = nullptr) {
        auto journal = ReadJournal(transaction / "journal.json");
        ValidateJournal(transaction, journal);
        if (journal.at("phase") != "prepared") throw std::runtime_error("Resource transaction is not prepared");
        journal["phase"] = "committing";
        journal["choice"] = choice == ConflictChoice::PreserveCurrent ? "preserve" : "package-with-backup";
        SaveJournal(transaction / "journal.json", journal);
        InstallResult result{}; result.transaction = transaction;
        const auto kind = Kind(journal);
        for (auto& entry : journal["entries"]) {
            if (cancel && cancel->load()) throw std::runtime_error("Resource commit interrupted; recovery retained");
            const auto path = entry.at("path").get<std::string>();
            const auto relative = NativePath(path);
            const auto target = live_ / relative;
            const auto candidate = transaction / "incoming" / relative;
            const auto before = transaction / "before" / relative;
            const auto retired = transaction / "retired" / relative;
            const auto action = entry.at("action").get<std::string>();
            CheckNoReparse(target);
            if ((action == "conflict" && choice == ConflictChoice::PreserveCurrent && !VersionFile(path)) ||
                (VersionFile(path) && result.conflicts)) {
                entry["status"] = "preserved";
                ++result.conflicts;
            } else if (action == "same") {
                // Same bytes are not evidence of ownership. Do not create a
                // baseline for an untracked legacy file.
                if (!SameBytes(target, candidate)) { entry["status"] = "preserved"; ++result.conflicts; }
                else { entry["status"] = "unchanged"; ++result.unchanged; }
            } else {
                const auto replacement = transaction / "apply" / relative;
                DurableCopyNew(candidate, replacement);
                // Large private copies happen before entering the shutdown gate.
                // The gate covers only one file's journal/rename bookkeeping.
                const auto baselineReady = transaction / "baseline-ready" / relative;
                DurableCopyNew(candidate, baselineReady);
                const auto baseline = Baseline(kind, relative);
                EnsureDirectory(baseline.parent_path());
                auto guard = gate ? gate->Acquire(cancel) : std::unique_lock<std::mutex>{};
                if ((gate && !guard.owns_lock()) || (cancel && cancel->load()))
                    throw std::runtime_error("Resource commit cancelled; recovery retained");
                EnsureDirectory(target.parent_path());
                if (hook) hook("before-lock", target);
                const bool hadOriginal = entry.at("hadOriginal").get<bool>();
                std::unique_ptr<Handle> current;
                if (hadOriginal) {
                    current = std::make_unique<Handle>(CreateFileW(target.c_str(), GENERIC_READ | DELETE,
                        FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr));
                    if (current->value == INVALID_HANDLE_VALUE) WinCheck(FALSE, "Lock current resource before replacing");
                    CheckNoReparse(target);
                    if (!HandleMatches(current->value, before)) {
                        entry["status"] = "preserved"; ++result.conflicts;
                        SaveJournal(transaction / "journal.json", journal);
                        continue;
                    }
                } else if (std::filesystem::exists(target)) {
                    entry["status"] = "preserved"; ++result.conflicts;
                    SaveJournal(transaction / "journal.json", journal);
                    continue;
                }
                entry["status"] = "installing";
                SaveJournal(transaction / "journal.json", journal);
                if (hook) hook("before-install", target);
                if (current) {
                    EnsureDirectory(retired.parent_path());
                    RenameHandleNew(current->value, retired);
                    if (hook) hook("after-retire", target);
                }
                CheckNoReparse(target);
                if (!MoveFileExW(replacement.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH)) {
                    const DWORD installError = GetLastError();
                    // Restore only into a still-empty path. A competing writer
                    // always wins; both its file and our retired copy survive.
                    if (current && !std::filesystem::exists(target)) {
                        try { RenameHandleNew(current->value, target); } catch (...) {}
                    }
                    throw std::runtime_error("Cannot install resource without overwriting a competitor: " +
                        std::to_string(installError));
                }
                if (hook) hook("after-install", target);
                current.reset();
                CheckNoReparse(baseline);
                WinCheck(MoveFileExW(baselineReady.c_str(), baseline.c_str(),
                    MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH), "Commit managed resource baseline");
                entry["status"] = "applied";
                ++result.installed;
            }
            SaveJournal(transaction / "journal.json", journal);
        }
        journal["phase"] = result.conflicts ? "completed-with-conflicts" : "completed";
        SaveJournal(transaction / "journal.json", journal);
        return result;
    }

    // Recovery is intentionally conservative: finish bookkeeping for a fully
    // installed candidate, restore a retired original into an empty path, and
    // preserve any third-party content. Uncommitted entries wait for a normal
    // update retry rather than silently applying an old package at startup.
    size_t RecoverInterrupted(CommitGate* gate = nullptr) {
        size_t recovered = 0;
        for (const auto& directory : std::filesystem::directory_iterator(state_ / "transactions")) {
            CheckNoReparse(directory.path());
            if (!directory.is_directory() || !std::filesystem::exists(directory.path() / "journal.json")) continue;
            const auto transaction = directory.path();
            auto journal = ReadJournal(transaction / "journal.json");
            ValidateJournal(transaction, journal);
            if (journal.at("phase") != "committing") continue;
            const auto kind = Kind(journal);
            for (auto& entry : journal["entries"]) {
                if (entry.at("status") != "installing") continue;
                const auto relative = NativePath(entry.at("path").get<std::string>());
                const auto target = live_ / relative;
                const auto candidate = transaction / "incoming" / relative;
                const auto before = transaction / "before" / relative;
                const auto retired = transaction / "retired" / relative;
                const bool consumedReplacement = !std::filesystem::exists(transaction / "apply" / relative);
                const bool retiredOriginal = !entry.at("hadOriginal").get<bool>() ||
                    (std::filesystem::is_regular_file(retired) && SameBytes(retired, before));
                if (consumedReplacement && retiredOriginal &&
                    std::filesystem::is_regular_file(target) && SameBytes(target, candidate)) {
                    RecordManaged(kind, relative, candidate);
                    entry["status"] = "applied";
                } else if (!std::filesystem::exists(target) && entry.at("hadOriginal").get<bool>() &&
                    std::filesystem::is_regular_file(retired) && SameBytes(retired, before)) {
                    auto guard = gate ? gate->Acquire(nullptr) : std::unique_lock<std::mutex>{};
                    if (gate && !guard.owns_lock()) throw std::runtime_error("Resource recovery cancelled by shutdown");
                    EnsureDirectory(target.parent_path());
                    WinCheck(MoveFileExW(retired.c_str(), target.c_str(), MOVEFILE_WRITE_THROUGH), "Restore interrupted resource");
                    entry["status"] = "restored";
                } else if (entry.at("hadOriginal").get<bool>() && SameBytes(target, before)) {
                    entry["status"] = "restored";
                } else {
                    entry["status"] = "preserved";
                }
            }
            journal["phase"] = "recovered-partial";
            SaveJournal(transaction / "journal.json", journal);
            ++recovered;
        }
        return recovered;
    }
};
} // namespace SafeResource
} // namespace GkmsResourceUpdate
