#ifndef GAKUMAS_LOCALIFY_LOCAL_H
#define GAKUMAS_LOCALIFY_LOCAL_H

#include <string>
#include <vector>
#include <filesystem>
#include <unordered_map>
#include <unordered_set>

namespace GakumasLocal::Local {
    // One published translation snapshot. Parsing fills a fresh Bundle off
    // thread. Initial hosted publication precedes every detour; after readers
    // are enabled, publication and reads stay on the Unity main thread.
    struct Bundle {
        std::unordered_map<std::string, std::string> i18nData{};
        std::unordered_map<std::string, std::string> i18nDumpData{};
        std::unordered_map<std::string, std::string> genericText{};
        std::unordered_map<std::string, std::string> genericSplitText{};
        std::unordered_map<std::string, std::string> genericFmtText{};
        std::unordered_set<std::string> translatedText{};
    };

    std::filesystem::path GetBasePath();
    // Parse every translation file into a new snapshot. Pure I/O plus parsing:
    // touches no published state and may run on a worker thread.
    Bundle ParseSnapshot(bool allowMissingLocalization = false);
    // Publish a parsed snapshot before detours, or on the Unity main thread.
    void PublishSnapshot(Bundle&& snapshot);
    // Parse and publish immediately (standalone callers).
    void LoadData();
    // Merge translated-source strings produced by a parsed master table set.
    // Called together with PublishSnapshot under the same publication rule.
    void AddTranslatedTexts(const std::vector<std::string>& texts);
    bool GetI18n(const std::string& key, std::string* ret);
    bool FlushPendingDumps(unsigned timeoutMs);
    void DumpI18nItem(const std::string& key, const std::string& value);

    bool GetResourceText(const std::string& name, std::string* ret);
    bool GetGenericText(const std::string& origText, std::string* newStr);

    std::string OnKeyDown(int message, int key);
}

#endif //GAKUMAS_LOCALIFY_LOCAL_H
