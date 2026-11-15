#include "Local.h"
#include "Log.h"
#include "Plugin.h"
#include "config/Config.hpp"
#include <filesystem>
#include <iostream>
#include <fstream>
#include <unordered_map>
#include <unordered_set>
#include <nlohmann/json.hpp>
#include <atomic>
#include <chrono>
#include <thread>
#include <regex>
#include <ranges>
#include <string>
#include <cctype>
#include <algorithm>
#include "BaseDefine.h"
#include "TranslationSource.hpp"
#include "TranslationJson.hpp"
#include "DumpQueue.hpp"
#include <mutex>
#include "data/LocalifyData.hpp"
#include "string_parser/StringParser.hpp"
#include <windows.h>

extern std::filesystem::path gakumasLocalPath;

// #include "cpprest/details/http_helpers.h"


namespace GakumasLocal::Local {
    // Translation state is published as one immutable-by-convention snapshot.
    // Parsing may run on a plugin worker. The hosted initial snapshot may be
    // published before any detour exists; once readers are enabled, publication
    // happens on the Unity main thread only. That keeps reloads independent of
    // the host callback gate without locking every lookup.
    namespace {
        Bundle g_bundle{};
        Bundle& Data() { return g_bundle; }
    }

    std::mutex g_dumpMutex;

    int genericDumpFileIndex = 0;
    const std::string splitTextPrefix = "[__split__]";

    std::filesystem::path GetBasePath() {
        // The translation root is the same path every installer receives. Going
        // through Plugin::GetHookInstaller() crashed the hosted build: parsing
        // starts during activate, and the installer only exists once
        // onRuntimeReady runs.
        return LocalifyData::Capture()->ActiveBase(LocalifyData::Kind::Translation);
    }

    bool isAllSpace(const std::string& str) {
        return std::all_of(str.begin(), str.end(), [](unsigned char c) {
            return std::isspace(c);
        });
    }

    std::string trim(const std::string& str) {
        auto is_not_space = [](char ch) { return !std::isspace(ch); };
        auto start = std::ranges::find_if(str, is_not_space);
        auto end = std::find_if(str.rbegin(), str.rend(), is_not_space).base();

        if (start < end) {
            return {start, end};
        }
        return "";
    }

    std::string findInMapIgnoreSpace(const std::string& key, const std::unordered_map<std::string, std::string>& searchMap) {
        auto is_space = [](char ch) { return std::isspace(ch); };
        auto front = std::ranges::find_if_not(key, is_space);
        auto back = std::find_if_not(key.rbegin(), key.rend(), is_space).base();

        std::string prefix(key.begin(), front);
        std::string suffix(back, key.end());

        std::string trimmedKey = trim(key);
        if ( auto it = searchMap.find(trimmedKey); it != searchMap.end()) {
            return prefix + it->second + suffix;
        }
        else {
            return "";
        }
    }

    enum class DumpStrStat {
        DEFAULT = 0,
        SPLITTABLE_ORIG = 1,
        SPLITTED = 2,
        FMT = 3
    };

    enum class SplitTagsTranslationStat {
        NO_TRANS,
        PART_TRANS,
        FULL_TRANS,
        NO_SPLIT,
        NO_SPLIT_AND_EMPTY
    };

    void LoadJsonDataToMap(Bundle& data, const std::filesystem::path& filePath,
                           std::unordered_map<std::string, std::string>& dict,
                           const bool insertToTranslated = false, const bool needClearDict = true,
                           const bool needCheckSplitPrefix = false) {
        try {
            const auto source = ReadTranslationJson(filePath);
            if (!source) return;
            if (needClearDict) dict.clear();
            for (const auto& i : source->items()) {
                const auto& key = i.key();
                const std::string value = i.value();
                if (needCheckSplitPrefix && key.starts_with(splitTextPrefix) && value.starts_with(splitTextPrefix)) {
                    static const auto splitTextPrefixLength = splitTextPrefix.size();
                    const auto splitValue = value.substr(splitTextPrefixLength);
                    data.genericSplitText[key.substr(splitTextPrefixLength)] = splitValue;
                    if (insertToTranslated) data.translatedText.emplace(splitValue);
                } else {
                    dict[key] = value;
                    if (insertToTranslated) data.translatedText.emplace(value);
                }
            }
        } catch (const std::exception& error) {
            throw std::runtime_error("Cannot parse translation '" + LocalifyData::Utf8(filePath) + "': " + error.what());
        }
    }

    void DumpMapDataToJson(const std::filesystem::path& dumpBasePath, const std::filesystem::path& fileName,
                           const std::unordered_map<std::string, std::string>& dict) {
        const auto dumpFilePath = dumpBasePath / fileName;
        try {
            if (!is_directory(dumpBasePath)) {
                std::filesystem::create_directories(dumpBasePath);
            }
            if (!std::filesystem::exists(dumpFilePath)) {
                std::ofstream dumpWriteLrcFile(dumpFilePath, std::ofstream::out);
                dumpWriteLrcFile << "{}";
                dumpWriteLrcFile.close();
            }

            std::ifstream dumpLrcFile(dumpFilePath);
            std::string fileContent((std::istreambuf_iterator<char>(dumpLrcFile)), std::istreambuf_iterator<char>());
            dumpLrcFile.close();
            auto fileData = nlohmann::ordered_json::parse(fileContent);
            for (const auto& i : dict) {
                fileData[i.first] = i.second;
            }
            const auto newStr = fileData.dump(4, 32, false);
            std::ofstream dumpWriteLrcFile(dumpFilePath, std::ofstream::out);
            dumpWriteLrcFile << newStr.c_str();
            dumpWriteLrcFile.close();
        }
        catch (std::exception& e) {
            Log::ErrorFmt("DumpMapDataToJson %s failed: %s", dumpFilePath.c_str(), e.what());
        }
    }

    DumpQueue g_dumpQueue([](const std::filesystem::path& file, const DumpQueue::Dictionary& values) {
        DumpMapDataToJson(file.parent_path(), file.filename(), values);
    });

    bool FlushPendingDumps(unsigned timeoutMs) {
        return g_dumpQueue.FlushFor(std::chrono::milliseconds(timeoutMs));
    }

    std::string to_lower(const std::string& str) {
        std::string lower_str = str;
        std::transform(lower_str.begin(), lower_str.end(), lower_str.begin(), ::tolower);
        return lower_str;
    }

    bool IsPureStringValue(const std::string& str) {
        static std::unordered_set<char> notDeeds = {'0', '1', '2', '3', '4', '5', '6', '7', '8', '9', ':',
                                                    '/', ' ', '.', '%', ',', '+', '-', 'x', '\n'};
        for (const auto& i : str) {
            if (!notDeeds.contains(i)) {
                return false;
            }
        }
        return true;
    }

    std::vector<std::string> SplitByTags(const std::string& origText) {
        static const std::regex tagsRe("<.*?>(.*?)</.*?>");
        std::string text = origText;
        std::smatch match;

        std::vector<std::string> ret{};

        std::string lastSuffix;
        while (std::regex_search(text, match, tagsRe)) {
            const auto tagValue = match[1].str();
            if (IsPureStringValue(tagValue)) {
                ret.push_back(match.prefix().str());
                lastSuffix = match.suffix().str();
            }
            text = match.suffix().str();
        }
        if (!lastSuffix.empty()) {
            ret.push_back(lastSuffix);
        }

        return ret;
    }

    void ProcessGenericTextLabels(Bundle& data) {
        std::unordered_map<std::string, std::string> appendsText{};

        for (const auto& i : data.genericText) {
            const auto origContents = SplitByTags(i.first);
            if (origContents.empty()) {
                continue;
            }
            const auto translatedContents = SplitByTags(i.second);
            if (origContents.size() == translatedContents.size()) {
                for (const auto& [orig, trans] : std::ranges::views::zip(origContents, translatedContents)) {
                    appendsText.emplace(orig, trans);
                }
            }
        }
        data.genericText.insert(appendsText.begin(), appendsText.end());
    }

    bool ReplaceString(std::string* str, const std::string& oldSubstr, const std::string& newSubstr) {
        size_t pos = str->find(oldSubstr);
        if (pos != std::string::npos) {
            str->replace(pos, oldSubstr.length(), newSubstr);
            return true;
        }
        return false;
    }

    bool GetSplitTagsTranslation(const Bundle& data, const std::string& origText, std::string* newText, std::vector<std::string>& unTransResultRet) {
        if (!origText.contains('<')) return false;
        const auto splitResult = SplitByTags(origText);
        if (splitResult.empty()) return false;

        *newText = origText;
        bool ret = true;
        for (const auto& i : splitResult) {
            if (const auto iter = data.genericText.find(i); iter != data.genericText.end()) {
                ReplaceString(newText, i, iter->second);
            }
            else {
                unTransResultRet.emplace_back(i);
                ret = false;
            }
        }
        return ret;
    }

    void ReplaceNumberComma(std::string* orig) {
        if (!orig->contains("，")) return;
        std::string newStr = *orig;
        ReplaceString(&newStr, "，", ",");
        if (IsPureStringValue(newStr)) {
            *orig = newStr;
        }
    }

    SplitTagsTranslationStat GetSplitTagsTranslationFull(const Bundle& data, const std::string& origTextIn, std::string* newText, std::vector<std::string>& unTransResultRet) {
        // static const std::u16string splitFlags = u"0123456789+＋-－%％【】.";
        static const std::unordered_set<char16_t> splitFlags = {u'0', u'1', u'2', u'3', u'4', u'5',
                                                                u'6', u'7', u'8', u'9', u'+', u'＋',
                                                                u'-', u'－', u'%', u'％', u'【', u'】',
                                                                u'.', u':', u'：', u'×'};

        const auto origText = Misc::ToUTF16(origTextIn);
        bool isInTag = false;
        std::vector<std::string> waitingReplaceTexts{};

        std::u16string currentWaitingReplaceText;

#ifdef GKMS_WINDOWS
#define checkCurrentWaitingReplaceTextAndClear() \
    if (!currentWaitingReplaceText.empty()) { \
        auto trimmed = trim(Misc::ToUTF8(currentWaitingReplaceText)); \
        waitingReplaceTexts.push_back(trimmed); \
        currentWaitingReplaceText.clear(); }
#else
#define checkCurrentWaitingReplaceTextAndClear() \
    if (!currentWaitingReplaceText.empty()) { \
        waitingReplaceTexts.push_back(Misc::ToUTF8(currentWaitingReplaceText)); \
        currentWaitingReplaceText.clear(); }
#endif

        for (char16_t currChar : origText) {
            if (currChar == u'<') {
                isInTag = true;
            }
            if (currChar == u'>') {
                isInTag = false;
                checkCurrentWaitingReplaceTextAndClear()
                continue;
            }
            if (isInTag) {
                checkCurrentWaitingReplaceTextAndClear()
                continue;
            }

            if (!splitFlags.contains(currChar)) {
                currentWaitingReplaceText.push_back(currChar);
            }
            else {
                checkCurrentWaitingReplaceTextAndClear()
            }
        }
        if (waitingReplaceTexts.empty()) {
            if (currentWaitingReplaceText.empty()) {
                return SplitTagsTranslationStat::NO_SPLIT_AND_EMPTY;
            }
            else {
                if (!(!origText.empty() && splitFlags.contains(origText[0]))) {  // 开头为特殊符号或数字
                    return SplitTagsTranslationStat::NO_SPLIT;
                }
            }
        }
        checkCurrentWaitingReplaceTextAndClear()

        *newText = origTextIn;
        SplitTagsTranslationStat ret;
        bool hasTrans = false;
        bool hasNotTrans = false;
        if (!waitingReplaceTexts.empty()) {
            for (const auto& i : waitingReplaceTexts) {
                if (isAllSpace(i)) continue;
                std::string searchResult = findInMapIgnoreSpace(i, data.genericSplitText);
                if (!searchResult.empty()) {
                    ReplaceNumberComma(&searchResult);
                    ReplaceString(newText, i, searchResult);
                    hasTrans = true;
                }
                else {
                    unTransResultRet.emplace_back(trim(i));
                    hasNotTrans = true;
                }
            }
            if (hasTrans && hasNotTrans) {
                ret = SplitTagsTranslationStat::PART_TRANS;
            }
            else if (hasTrans && !hasNotTrans) {
                ret = SplitTagsTranslationStat::FULL_TRANS;
            }
            else {
                ret = SplitTagsTranslationStat::NO_TRANS;
            }
        }
        else {
            ret = SplitTagsTranslationStat::NO_TRANS;
        }
        return ret;
    }

    Bundle ParseSnapshot(bool allowMissingLocalization) {
        const auto started = std::chrono::steady_clock::now();
        Bundle data;
        const bool hasLocalization = !LocalifyData::Find("local-files/localization.json").empty();
        // An active set is the base; explicit user overrides are applied last.
        // All paths come from one frozen reload view, shared with MasterLocal.
        for (const auto& root : LocalifyData::Roots(LocalifyData::Kind::Translation, true)) {
            GkmsResourceUpdate::SafeResource::CheckNoReparse(root);
            LoadJsonDataToMap(data, root / LocalifyData::NativePath("local-files/localization.json"), data.i18nData, true, false);
            LoadJsonDataToMap(data, root / LocalifyData::NativePath("local-files/generic.json"), data.genericText, true, false, true);
            LoadJsonDataToMap(data, root / LocalifyData::NativePath("local-files/generic.split.json"), data.genericSplitText, true, false, true);
            const auto genericDir = root / LocalifyData::NativePath("local-files/genericTrans");
            GkmsResourceUpdate::SafeResource::CheckNoReparse(genericDir);
            std::error_code genericDirError;
            const auto genericDirState = ClassifyGenericTransDirectory(genericDir, genericDirError);
            if (genericDirState == GenericTransState::Unusable)
                throw std::filesystem::filesystem_error("genericTrans is not a readable directory", genericDir, genericDirError);
            if (genericDirState == GenericTransState::Ready) {
                ForEachGenericTransFile(genericDir, [&](const std::filesystem::path& currFile) {
                    GkmsResourceUpdate::SafeResource::CheckNoReparse(currFile);
                    if (currFile.filename() == "home_voice_subtitles.json" ||
                        to_lower(LocalifyData::Utf8(currFile.extension())) != ".json") return;
                    if (LocalifyData::Utf8(currFile.filename()).ends_with(".split.json"))
                        LoadJsonDataToMap(data, currFile, data.genericSplitText, true, false, true);
                    if (LocalifyData::Utf8(currFile.filename()).ends_with(".fmt.json"))
                        LoadJsonDataToMap(data, currFile, data.genericFmtText, true, false, false);
                    else
                        LoadJsonDataToMap(data, currFile, data.genericText, true, false, true);
                });
            }
        }
        if (!hasLocalization && !allowMissingLocalization)
            throw std::runtime_error("localization.json is missing from the selected resource set");
        ProcessGenericTextLabels(data);
        Log::InfoFmt("%ld localization items and %ld generic text items loaded.", data.i18nData.size(), data.genericText.size());
        for (const auto& exports : LocalifyData::PreviousExports()) {
            try {
                LoadJsonDataToMap(data, exports / "localization.json", data.i18nDumpData, false, false);
            } catch (const std::exception& error) {
                // Export history only avoids duplicate dumps; it is not an
                // active translation source and may be in the middle of a write.
                Log::ErrorFmt("Ignoring unavailable dump history: %s", error.what());
            }
        }
        const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - started).count();
        Log::InfoFmt("Translation snapshot parsed in %lld ms on thread %lu.",
            static_cast<long long>(elapsed), static_cast<unsigned long>(GetCurrentThreadId()));
        return data;
    }

    void PublishSnapshot(Bundle&& snapshot) {
        // Called before any detour exists for hosted startup, or on the Unity
        // main thread for every later publication. The counts record "first
        // translation": a zero here means the panel is up but nothing loaded.
        Log::InfoFmt("Publishing localization snapshot: %zu i18n, %zu generic, %zu split, %zu fmt, "
            "%zu translated (thread %lu).",
            snapshot.i18nData.size(), snapshot.genericText.size(), snapshot.genericSplitText.size(),
            snapshot.genericFmtText.size(), snapshot.translatedText.size(),
            static_cast<unsigned long>(GetCurrentThreadId()));
        std::lock_guard lock(g_dumpMutex);
        // New snapshots may predate a pending dump. Keep those already observed
        // keys known without letting the writer borrow the published maps.
        snapshot.i18nDumpData.insert(Data().i18nDumpData.begin(), Data().i18nDumpData.end());
        Data() = std::move(snapshot);
    }

    void LoadData() {
        PublishSnapshot(ParseSnapshot());
    }

    void AddTranslatedTexts(const std::vector<std::string>& texts) {
        auto& translated = Data().translatedText;
        translated.insert(texts.begin(), texts.end());
    }

    bool GetI18n(const std::string& key, std::string* ret) {
        const auto& i18nData = Data().i18nData;
        if (const auto iter = i18nData.find(key); iter != i18nData.end()) {
            *ret = iter->second;
            return true;
        }
        return false;
    }

    void DumpI18nItem(const std::string& key, const std::string& value) {
        if (!Config::dumpText) return;
        const auto file = LocalifyData::Exports() / "localization.json";
        std::lock_guard lock(g_dumpMutex);
        if (!Data().i18nDumpData.emplace(key, value).second) return;
        Log::DebugFmt("DumpI18nItem: %s - %s", key.c_str(), value.c_str());
        g_dumpQueue.Append(file, key, value);
    }

    std::string readFileToString(const std::string& filename) {
        std::ifstream file(filename);
        if (!file.is_open()) {
            throw std::exception();
        }
        std::string content((std::istreambuf_iterator<char>(file)),
                            (std::istreambuf_iterator<char>()));
        file.close();
        return content;
    }

    bool GetResourceText(const std::string& name, std::string* ret) {

        try {
            const auto targetFilePath = LocalifyData::Find("local-files/resource/" + name);
            // Log::DebugFmt("GetResourceText: %s", targetFilePath.c_str());
            if (!targetFilePath.empty()) {
                std::ifstream resource(targetFilePath, std::ios::binary);
                auto readStr = std::string(std::istreambuf_iterator<char>(resource), {});
                *ret = readStr;
                return true;
            }
        }
        catch (std::exception& e) {
            Log::ErrorFmt("read file: %s failed.", name.c_str());
        }
        return false;
    }

    std::string GetDumpGenericFileName(DumpStrStat stat = DumpStrStat::DEFAULT) {
        if (stat == DumpStrStat::SPLITTABLE_ORIG) {
            if (genericDumpFileIndex == 0) return "generic_orig.json";
            return Log::StringFormat("generic_orig_%d.json", genericDumpFileIndex);
        }
        else if (stat == DumpStrStat::FMT) {
            if (genericDumpFileIndex == 0) return "generic.fmt.json";
            return Log::StringFormat("generic_%d.fmt.json", genericDumpFileIndex);
        }
        else {
            if (genericDumpFileIndex == 0) return "generic.json";
            return Log::StringFormat("generic_%d.json", genericDumpFileIndex);
        }
    }

    void DumpGenericText(const std::string& origText, DumpStrStat stat = DumpStrStat::DEFAULT) {
        if (IsPureStringValue(origText)) return;
        const auto base = LocalifyData::Exports();
        std::lock_guard lock(g_dumpMutex);
        if (Data().translatedText.contains(origText)) return;
        const auto key = stat == DumpStrStat::SPLITTED ? splitTextPrefix + origText : origText;
        g_dumpQueue.Append(base / GetDumpGenericFileName(stat), key, key);
    }

    bool GetGenericText(const std::string& origText, std::string* newStr) {
        const auto& data = Data();
        // 完全匹配
        if (const auto iter = data.genericText.find(origText); iter != data.genericText.end()) {
            *newStr = iter->second;
            return true;
        }
        // 不翻译翻译过的文本
        if (data.translatedText.contains(origText)) {
            return false;
        }

        // 匹配升级卡名
        if (auto plusPos = origText.find_last_not_of('+'); plusPos != std::string::npos) {
            const auto noPlusText = origText.substr(0, plusPos + 1);

            if (const auto iter = data.genericText.find(noPlusText); iter != data.genericText.end()) {
                size_t plusCount = origText.length() - (plusPos + 1);
                *newStr = iter->second + std::string(plusCount, '+');
                return true;
            }
        }

        // fmt 文本
        auto fmtText = StringParser::ParseItems::parse(origText, false);
        if (fmtText.isValid) {
            const auto fmtStr = fmtText.ToFmtString();
            if (auto it = data.genericFmtText.find(fmtStr); it != data.genericFmtText.end()) {
                auto newRet = fmtText.MergeText(it->second);
                if (!newRet.empty()) {
                    *newStr = newRet;
                    return true;
                }
            }
            if (Config::dumpText) {
                DumpGenericText(fmtStr, DumpStrStat::FMT);
            }
        }

        auto ret = false;

        // 分割匹配
        std::vector<std::string> unTransResultRet;
        const auto splitTransStat = GetSplitTagsTranslationFull(data, origText, newStr, unTransResultRet);
        switch (splitTransStat) {
            case SplitTagsTranslationStat::FULL_TRANS: {
                DumpGenericText(origText, DumpStrStat::SPLITTABLE_ORIG);
                return true;
            } break;

            case SplitTagsTranslationStat::NO_SPLIT_AND_EMPTY: {
                return false;
            } break;

            case SplitTagsTranslationStat::NO_SPLIT: {
                ret = false;
            } break;

            case SplitTagsTranslationStat::NO_TRANS: {
                ret = false;
            } break;

            case SplitTagsTranslationStat::PART_TRANS: {
                ret = true;
            } break;
        }

        if (!Config::dumpText) {
            return ret;
        }

        if (unTransResultRet.empty() || (splitTransStat == SplitTagsTranslationStat::NO_SPLIT)) {
            DumpGenericText(origText, DumpStrStat::SPLITTABLE_ORIG);
        }
        else {
            for (const auto& i : unTransResultRet) {
                DumpGenericText(i, DumpStrStat::SPLITTED);
            }
            // 若未翻译部分长度为1，且未翻译文本等于原文本，则不 dump 到原文本文件
            //if (unTransResultRet.size() != 1 || unTransResultRet[0] != origText) {
                DumpGenericText(origText, DumpStrStat::SPLITTABLE_ORIG);
            //}
        }

        return ret;
    }

    std::string ChangeDumpTextIndex(int changeValue) {
        if (!Config::dumpText) return "";
        std::lock_guard lock(g_dumpMutex);
        genericDumpFileIndex += changeValue;
        return Log::StringFormat("GenericDumpFile: %s", GetDumpGenericFileName().c_str());
    }

    std::string OnKeyDown(int message, int key) {
        if (message == WM_KEYDOWN) {
            switch (key) {
                case KEY_ADD: {
                    return ChangeDumpTextIndex(1);
                } break;
                case KEY_SUB: {
                    return ChangeDumpTextIndex(-1);
                } break;
            }
        }
        return "";
    }

}
