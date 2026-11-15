#pragma once

#define NOMINMAX

#include <Windows.h>
#include <shlobj.h>

#include <cinttypes>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <locale>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <map>
#include <thread>
#include <variant>

#include <exception>
#include <vector>
#include <regex>

#ifndef GKMS_HOSTED
// MinHook belongs to the standalone build only: the hosted target routes every
// detour through the loader's hook service. Leaving it out of the hosted build
// makes an accidental private-hook call a compile error instead of a silent
// second hook engine at runtime.
#include <MinHook.h>
#endif

#include <rapidjson/document.h>
#include <rapidjson/encodings.h>
#include <rapidjson/istreamwrapper.h>
#include <rapidjson/stringbuffer.h>
#include <rapidjson/ostreamwrapper.h>
#include <rapidjson/writer.h>

#include "il2cpp/il2cpp_symbols.hpp"
#include <nlohmann/json.hpp>


extern bool g_has_config_file;
// config 区
extern bool g_enable_console;
extern bool g_useRemoteAssets;
extern bool g_useAPIAssets;
extern bool g_useAPITextureAssets;
extern bool g_delTextureRemoteAfterUpdate;
extern std::string g_remoteResourceUrl;
extern std::string g_useAPIAssetsURL;
extern std::string g_useAPITextureAssetsURL;
// config 区结束
