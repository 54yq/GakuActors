#pragma once
#include <filesystem>
#include <string>

#ifdef __ANDROID__
#include <atomic>
#endif

namespace GakumasLocal::Config {
    extern bool isConfigInit;

    extern bool dbgMode;
    extern bool enabled;
    extern bool lazyInit;
    extern bool replaceFont;
    extern bool replaceTexture;
    extern bool forceExportResource;
    extern int gameOrientation;
    extern bool textTest;
    extern bool useMasterTrans;
    extern bool dumpText;
    extern bool dumpRuntimeTexture;
    extern bool enableFreeCamera;
    extern bool blockFreeCameraShortcuts;
    extern bool enableGyroEmu;
    extern float gyroSensitivity;
    extern bool gyroInvertX;
    extern bool gyroInvertY;
    extern int targetFrameRate;
    extern bool unlockAllLive;
    extern bool unlockAllLiveCostume;

    extern bool enableLiveCustomeDress;
    extern std::string liveCustomeHeadId;
    extern std::string liveCustomeCostumeId;

#ifdef __ANDROID__
    extern std::atomic_bool loginAsIOS;
#else
    extern bool loginAsIOS;
#endif

    extern bool useCustomeGraphicSettings;
    extern float renderScale;
    extern int qualitySettingsLevel;
    extern int volumeIndex;
    extern int maxBufferPixel;

    extern int reflectionQualityLevel;
    extern int lodQualityLevel;

    extern bool enableBreastParam;
    extern float bDamping;
    extern float bStiffness;
    extern float bSpring;
    extern float bPendulum;
    extern float bPendulumRange;
    extern float bAverage;
    extern float bRootWeight;
    extern bool bUseArmCorrection;
    extern bool bUseScale;
    extern float bScale;
    extern bool bUseLimit;
    extern float bLimitXx;
    extern float bLimitXy;
    extern float bLimitYx;
    extern float bLimitYy;
    extern float bLimitZx;
    extern float bLimitZy;

    extern bool dmmUnlockSize;

    void LoadConfig(const std::string& configStr);
    // Split so the Unity main thread can serialize the values it owns and a
    // plugin worker can perform the file write without reading them.
    std::string SerializeConfig();
    bool WriteConfigFile(const std::filesystem::path& configPath, const std::string& content);
    void SaveConfig(const std::filesystem::path& configPath);
    bool CreateDefaultFile(const std::filesystem::path& configPath);
}
