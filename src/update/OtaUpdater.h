#pragma once

#include <Arduino.h>
#include <expected>
#include <string>
#include <string_view>
#include "board/BoardConfig.h"
#include "settings/SettingsModel.h"

class OtaUpdater {
public:
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
        AssetMissing,
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

    Config config(const settings::DeviceSettings& settings, const settings::DeviceSecrets& secrets) const;
    bool isConfigured(const Config& config) const;
    std::string_view currentVersion() const;
    Result checkOnly(const Config& config, StatusCallback callback = nullptr, void* context = nullptr) const;
    Result checkAndInstall(const Config& config, StatusCallback callback = nullptr, void* context = nullptr) const;

private:
    struct LatestRelease {
        std::string version;
        std::string assetUrl;
    };

    bool connectWiFi(const Config& config, StatusCallback callback, void* context) const;
    void disconnectWiFi() const;
    std::expected<LatestRelease, std::string> fetchRelease(const Config& config, StatusCallback callback,
                                                           void* context) const;
    std::expected<std::string, std::string> resolveDownloadUrl(std::string_view assetUrl, std::string_view version,
                                                               StatusCallback callback, void* context) const;
    void reportStatus(StatusCallback callback, void* context, const char* title, const char* line1, const char* line2,
                      int progressPercent) const;
};
