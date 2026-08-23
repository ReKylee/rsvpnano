#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include "board/BoardConfig.h"
#include "settings/SettingsModel.h"

namespace OtaUpdater {

    using StatusCallback = void (*)(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    struct Config {
        std::string wifiSsid;
        std::string wifiPassword;
        std::string githubOwner = "ionutdecebal";
        std::string githubRepo = "rsvpnano";
        std::string githubTag;
        std::string assetName = Board::Config::OTA_ASSET_NAME;
    };

    enum class ResultCode : uint8_t {
        Success,
        NoUpdate,
        UpdateAvailable,
        NotConfigured,
        ConnectFailed,
        MetadataFailed,
        AssetMismatch,
        InstallFailed,
    };

    struct Result {
        ResultCode code = ResultCode::MetadataFailed;
        std::string currentVersion;
        std::string latestVersion;
        std::string summary;
        std::string detail;
        bool rebootRequired = false;
    };

    Config config(const settings::DeviceSettings& settings, const settings::DeviceSecrets& secrets);
    std::string_view currentVersion();
    Result checkOnly(const Config& config, StatusCallback callback = nullptr, void* context = nullptr);
    Result checkAndInstall(const Config& config, StatusCallback callback = nullptr, void* context = nullptr);

} // namespace OtaUpdater
