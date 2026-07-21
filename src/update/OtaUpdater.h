#pragma once

#include <Arduino.h>
#include <expected>
#include <string>
#include "board/BoardConfig.h"
#include "settings/SettingsModel.h"

class OtaUpdater {
public:
    using StatusCallback = void (*)(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);

    struct Config {
        String wifiSsid;
        String wifiPassword;
        String githubOwner = "ionutdecebal";
        String githubRepo = "rsvpnano";
        String githubTag;
        String assetName = Board::Config::OTA_ASSET_NAME;
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
        String currentVersion;
        String latestVersion;
        String summary;
        String detail;
        bool rebootRequired = false;
    };

    Config config(const settings::DeviceSettings& settings, const settings::DeviceSecrets& secrets) const;
    bool isConfigured(const Config& config) const;
    String currentVersion() const;
    Result checkOnly(const Config& config, StatusCallback callback = nullptr, void* context = nullptr) const;
    Result checkAndInstall(const Config& config, StatusCallback callback = nullptr, void* context = nullptr) const;

private:
    struct LatestRelease {
        String version;
        String assetUrl;
    };

    bool connectWiFi(const Config& config, StatusCallback callback, void* context) const;
    void disconnectWiFi() const;
    std::expected<LatestRelease, std::string> fetchRelease(const Config& config, StatusCallback callback,
                                                           void* context) const;
    std::expected<String, std::string> resolveDownloadUrl(const String& assetUrl, const String& version,
                                                          StatusCallback callback, void* context) const;
    void reportStatus(StatusCallback callback, void* context, const char* title, const String& line1,
                      const String& line2, int progressPercent) const;
};
