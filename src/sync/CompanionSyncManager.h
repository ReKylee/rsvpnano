#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

#include <string>
#include <string_view>

#include "fonts/FontCatalog.h"
#include "locales/LocaleCatalog.h"
#include "settings/SettingsStore.h"

class CompanionSyncManager {
public:
    enum Change : uint8_t {
        Settings = 1U << 0U,
        Network = 1U << 1U,
        Fonts = 1U << 2U,
        Locales = 1U << 3U,
    };

    CompanionSyncManager(settings::SettingsStore& settingsStore, locales::Catalog& localeCatalog,
                         FontCatalog& fontCatalog)
        : settingsStore_(settingsStore), localeCatalog_(localeCatalog), fontCatalog_(fontCatalog) {}

    bool begin();
    uint8_t update();
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
    void handleBookLanguageFonts();
    void handleBooks();
    void handleBookUpload();
    void handleThemes();
    void handleThemeUpload();
    void handleFonts();
    void handleFontUpload();
    void handleLocales();
    void handleLocaleStage();
    void handleLocaleFile();
    void handleLocaleFileUpload();
    void handleLocaleActivate();
    void handleLocaleDelete();
    void handleNotFound();
    void sendError(int status, const char* code, std::string_view message, const char* field = nullptr);
    std::string deviceSuffix() const;
    std::string sanitizeFilename(std::string_view name) const;
    RsvpMetadata readRsvpMetadata(std::string_view path) const;
    bool progressForPath(std::string_view path, uint32_t sourceSize, uint32_t sourceFingerprint, uint32_t wordCount,
                         uint32_t& wordIndex, uint8_t& percent, settings::ReadingOverrides& overrides);
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
    locales::Catalog& localeCatalog_;
    FontCatalog& fontCatalog_;
    std::string statusLine1_ = "Idle";
    std::string statusLine2_;
    NetworkMode networkMode_ = NetworkMode::None;
    bool active_ = false;
    bool serverStarted_ = false;
    uint8_t changes_ = 0;
    bool mdnsStarted_ = false;
};
