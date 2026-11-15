#include <string>
#ifdef __ANDROID__
#include "Config.hpp"
#endif
#include "nlohmann/json.hpp"
#include "../Log.h"
#include <thread>
#include <fstream>
#include "data/LocalifyData.hpp"

namespace GakumasLocal::Config {
    bool isConfigInit = false;

    bool dbgMode = false;
    bool enabled = true;
    bool lazyInit = true;
    bool replaceFont = true;
    bool replaceTexture = true;
    bool forceExportResource = true;
    bool textTest = false;
    bool useMasterTrans = true;
    int gameOrientation = 0;
    bool dumpText = false;
    bool dumpRuntimeTexture = false;
    bool enableFreeCamera = false;
    bool blockFreeCameraShortcuts = true;
    bool enableGyroEmu = false;
    float gyroSensitivity = 1.0f;
    bool gyroInvertX = false;
    bool gyroInvertY = false;
    int targetFrameRate = 0;
    bool unlockAllLive = false;
    bool unlockAllLiveCostume = false;

    bool enableLiveCustomeDress = false;
    std::string liveCustomeHeadId = "";
    std::string liveCustomeCostumeId = "";

#ifdef __ANDROID__
    std::atomic_bool loginAsIOS{false};
#else
    bool loginAsIOS = false;
#endif

    bool useCustomeGraphicSettings = false;
    float renderScale = 0.77f;
    int qualitySettingsLevel = 3;
    int volumeIndex = 3;
    int maxBufferPixel = 3384;
    int reflectionQualityLevel = 4;
    int lodQualityLevel = 4;

    bool enableBreastParam = false;
    float bDamping = 0.33f;
    float bStiffness = 0.08f;
    float bSpring = 1.0f;
    float bPendulum = 0.055f;
    float bPendulumRange = 0.15f;
    float bAverage = 0.20f;
    float bRootWeight = 0.5f;
    bool bUseArmCorrection = true;
    bool bUseScale = false;
    float bScale = 1.0f;
    bool bUseLimit = true;
    float bLimitXx = 1.0f;
    float bLimitXy = 1.0f;
    float bLimitYx = 1.0f;
    float bLimitYy = 1.0f;
    float bLimitZx = 1.0f;
    float bLimitZy = 1.0f;

    bool dmmUnlockSize = false;

    void LoadConfig(const std::string& configStr) {
        try {
            const auto config = nlohmann::json::parse(configStr);

            #define GetConfigItem(name) if (config.contains(#name)) name = config[#name]

            GetConfigItem(dbgMode);
            GetConfigItem(enabled);
            GetConfigItem(lazyInit);
            GetConfigItem(replaceFont);
            GetConfigItem(replaceTexture);
            GetConfigItem(forceExportResource);
            GetConfigItem(gameOrientation);
            GetConfigItem(textTest);
            GetConfigItem(useMasterTrans);
            GetConfigItem(dumpText);
            GetConfigItem(dumpRuntimeTexture);
            GetConfigItem(targetFrameRate);
            GetConfigItem(enableFreeCamera);
            GetConfigItem(blockFreeCameraShortcuts);
            GetConfigItem(enableGyroEmu);
            GetConfigItem(gyroSensitivity);
            GetConfigItem(gyroInvertX);
            GetConfigItem(gyroInvertY);
            GetConfigItem(unlockAllLive);
            GetConfigItem(unlockAllLiveCostume);
            GetConfigItem(enableLiveCustomeDress);
            GetConfigItem(liveCustomeHeadId);
            GetConfigItem(liveCustomeCostumeId);
#ifdef __ANDROID__
            if (config.contains("loginAsIOS")) loginAsIOS = config["loginAsIOS"].get<bool>();
#else
            GetConfigItem(loginAsIOS);
#endif
            GetConfigItem(useCustomeGraphicSettings);
            GetConfigItem(renderScale);
            GetConfigItem(qualitySettingsLevel);
            GetConfigItem(volumeIndex);
            GetConfigItem(maxBufferPixel);
            GetConfigItem(reflectionQualityLevel);
            GetConfigItem(lodQualityLevel);
            GetConfigItem(enableBreastParam);
            GetConfigItem(bDamping);
            GetConfigItem(bStiffness);
            GetConfigItem(bSpring);
            GetConfigItem(bPendulum);
            GetConfigItem(bPendulumRange);
            GetConfigItem(bAverage);
            GetConfigItem(bRootWeight);
            GetConfigItem(bUseArmCorrection);
            GetConfigItem(bUseScale);
            GetConfigItem(bScale);
            GetConfigItem(bUseLimit);
            GetConfigItem(bLimitXx);
            GetConfigItem(bLimitXy);
            GetConfigItem(bLimitYx);
            GetConfigItem(bLimitYy);
            GetConfigItem(bLimitZx);
            GetConfigItem(bLimitZy);
            GetConfigItem(dmmUnlockSize);
        }
        catch (std::exception& e) {
            Log::ErrorFmt("LoadConfig error: %s", e.what());
        }
        isConfigInit = true;
    }

    std::string SerializeConfig() {
        nlohmann::json config;

        #define SetConfigItem(name) config[#name] = name

        SetConfigItem(dbgMode);
        SetConfigItem(enabled);
        SetConfigItem(lazyInit);
        SetConfigItem(replaceFont);
        SetConfigItem(replaceTexture);
        SetConfigItem(forceExportResource);
        SetConfigItem(gameOrientation);
        SetConfigItem(textTest);
        SetConfigItem(useMasterTrans);
        SetConfigItem(dumpText);
        SetConfigItem(dumpRuntimeTexture);
        SetConfigItem(targetFrameRate);
        SetConfigItem(enableFreeCamera);
        SetConfigItem(blockFreeCameraShortcuts);
        SetConfigItem(enableGyroEmu);
        SetConfigItem(gyroSensitivity);
        SetConfigItem(gyroInvertX);
        SetConfigItem(gyroInvertY);
        SetConfigItem(unlockAllLive);
        SetConfigItem(unlockAllLiveCostume);
        SetConfigItem(enableLiveCustomeDress);
        SetConfigItem(liveCustomeHeadId);
        SetConfigItem(liveCustomeCostumeId);
#ifdef __ANDROID__
        config["loginAsIOS"] = loginAsIOS.load();
#else
        SetConfigItem(loginAsIOS);
#endif
        SetConfigItem(useCustomeGraphicSettings);
        SetConfigItem(renderScale);
        SetConfigItem(qualitySettingsLevel);
        SetConfigItem(volumeIndex);
        SetConfigItem(maxBufferPixel);
        SetConfigItem(reflectionQualityLevel);
        SetConfigItem(lodQualityLevel);
        SetConfigItem(enableBreastParam);
        SetConfigItem(bDamping);
        SetConfigItem(bStiffness);
        SetConfigItem(bSpring);
        SetConfigItem(bPendulum);
        SetConfigItem(bPendulumRange);
        SetConfigItem(bAverage);
        SetConfigItem(bRootWeight);
        SetConfigItem(bUseArmCorrection);
        SetConfigItem(bUseScale);
        SetConfigItem(bScale);
        SetConfigItem(bUseLimit);
        SetConfigItem(bLimitXx);
        SetConfigItem(bLimitXy);
        SetConfigItem(bLimitYx);
        SetConfigItem(bLimitYy);
        SetConfigItem(bLimitZx);
        SetConfigItem(bLimitZy);
        SetConfigItem(dmmUnlockSize);

        return config.dump(4);
    }

    bool WriteConfigFile(const std::filesystem::path& configPath, const std::string& content) {
        // The path is fixed at activation and `content` was serialized by the
        // caller on the thread that owns these values, so a plugin worker only
        // performs the write.
        try {
            LocalifyData::SaveConfig(configPath, content);
            Log::Info("SaveConfig success");
            return true;
        }
        catch (std::exception& e) {
            Log::ErrorFmt("SaveConfig error: %s", e.what());
            return false;
        }
    }

    void SaveConfig(const std::filesystem::path& configPath) {
        WriteConfigFile(configPath, SerializeConfig());
    }
    bool CreateDefaultFile(const std::filesystem::path& configPath) {
        return LocalifyData::SaveConfigNew(configPath, SerializeConfig());
    }
}
