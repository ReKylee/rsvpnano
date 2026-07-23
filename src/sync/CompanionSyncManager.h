#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

#include <string>
#include <string_view>

#include "settings/SettingsStore.h"

class CompanionSyncManager {
public:
    explicit CompanionSyncManager(settings::SettingsStore& settingsStore) : settingsStore_(settingsStore) {}

    bool begin();
    bool update();
    void end();
    bool active() const;
    std::string_view statusLine1() const;
    std::string_view statusLine2() const;
    std::string baseUrl() const;

private:
    enum class NetworkMode : uint8_t {
        None,
        Station,
        AccessPoint,
    };

    struct RsvpMetadata {
        std::string title;
        std::string author;
    };

    bool startStation();
    bool startAccessPoint();
    bool startServer();
    void stopServer();
    void handleInfo();
    void handleRoot();
    void handleBooksList();
    void handleSettings();
    void handleWifi();
    void handleRssFeeds();
    void handleFocusTimers();
    void handleBookDelete();
    void handleBookPosition();
    void handleBooks();
    void handleBookUpload();
    void handleThemes();
    void handleThemeUpload();
    void handleFonts();
    void handleFontUpload();
    void handleNotFound();
    void sendError(int status, const char* code, std::string_view message, const char* field = nullptr);
    std::string deviceSuffix() const;
    std::string sanitizeFilename(std::string_view name) const;
    RsvpMetadata readRsvpMetadata(std::string_view path) const;
    bool progressForPath(std::string_view path, uint32_t sourceSize, uint32_t sourceFingerprint, uint32_t wordCount,
                         uint32_t& wordIndex, uint8_t& percent);
    std::string bookIdForPath(std::string_view path) const;
    bool resolveBookId(std::string_view id, std::string& path) const;
    void finishUpload(bool success);

    WebServer server_{80};
    File uploadFile_;
    std::string uploadFinalPath_;
    std::string uploadTmpPath_;
    std::string uploadError_;
    std::string networkSsid_;
    std::string jsonBuffer_;
    settings::SettingsStore& settingsStore_;
    std::string statusLine1_ = "Idle";
    std::string statusLine2_;
    NetworkMode networkMode_ = NetworkMode::None;
    bool active_ = false;
    bool serverStarted_ = false;
    bool settingsChanged_ = false;
    bool mdnsStarted_ = false;
};
