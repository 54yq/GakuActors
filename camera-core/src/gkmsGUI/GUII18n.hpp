#pragma once

#include <string>
#include <cstdint>

namespace GkmsGUII18n {
	// Values match the optional Loader UI language service. This selects panel
	// labels only; game translation resources remain a separate user choice.
	bool SetLanguage(uint32_t language);
	void UseSystemLanguage();
	uint32_t CurrentLanguage();
	const char* ts(const std::string& key);
}
