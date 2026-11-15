#include "GUII18n.hpp"
#include <Windows.h>
#include <atomic>
#include "i18nData/strings_en.hpp"
#include "i18nData/strings_ja.hpp"
#include "i18nData/strings_zh-rCN.hpp"
#include "i18nData/strings_zh-rTW.hpp"


namespace GkmsGUII18n {
	namespace {
	uint32_t SystemLanguage() {
		switch (GetUserDefaultUILanguage()) {
		case 0x0004: case 0x0804: case 0x1004: return 1;
		case 0x0404: case 0x0c04: case 0x1404: case 0x048E: return 2;
		case 0x0011: case 0x0411: return 3;
		default: return 0;
		}
	}
	std::atomic_uint32_t g_language{SystemLanguage()};
	}

	bool SetLanguage(uint32_t language) {
		if (language > 3) return false;
		g_language.store(language, std::memory_order_relaxed);
		return true;
	}

	void UseSystemLanguage() { SetLanguage(SystemLanguage()); }
	uint32_t CurrentLanguage() { return g_language.load(std::memory_order_relaxed); }

    const char* ts(const std::string& key) {
        // Dictionaries stay immutable for the process lifetime: switching the
        // language cannot invalidate labels already borrowed by a draw call.
        const auto& i18nData = []() -> const std::unordered_map<std::string, std::string>& {
            switch (CurrentLanguage()) {
            case 1: return I18nData::i18nData_zh_rCN;
            case 2: return I18nData::i18nData_zh_rTW;
            case 3: return I18nData::i18nData_ja;
            default: return I18nData::i18nData_default;
            }
        }();

        if (auto it = i18nData.find(key); it != i18nData.end()) {
            return it->second.c_str();
        }
        if (auto it = I18nData::i18nData_default.find(key); it != I18nData::i18nData_default.end()) {
            return it->second.c_str();
        }

        static std::unordered_map<std::string, std::string> fallbackMap;
        auto [iter, inserted] = fallbackMap.emplace(key, key);
        return iter->second.c_str();
    }

}
