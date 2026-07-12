#pragma once

#include <Arduino.h>
#include <FS.h>
#include <WebServer.h>

#include <string>
#include <string_view>

class Preferences;

class CompanionSyncManager {
public:
    bool begin(Preferences& preferences);
    void update();
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
        String title;
        String author;
    };

    static void handleInfoStatic();
    static void handleRootStatic();
    static void handleBooksListStatic();
    static void handleSettingsStatic();
    static void handleWifiStatic();
    static void handleRssFeedsStatic();
    static void handleBookDeleteStatic();
    static void handleBookPositionStatic();
    static void handleBooksStatic();
    static void handleBookUploadStatic();
    static void handleThemesStatic();
    static void handleThemeUploadStatic();
    static void handleFontsStatic();
    static void handleFontUploadStatic();
    static void handleNotFoundStatic();

    bool startAccessPoint();
    bool startServer();
    void stopServer();
    void handleInfo();
    void handleRoot();
    void handleBooksList();
    void handleSettings();
    void handleWifi();
    void handleRssFeeds();
    void handleBookDelete();
    void handleBookPosition();
    void handleBooks();
    void handleBookUpload();
    void handleThemes();
    void handleThemeUpload();
    void handleFonts();
    void handleFontUpload();
    void handleNotFound();
    String settingsJson();
    bool applySettingsJson(const String& body, String& error);
    String wifiJson();
    bool applyWifiJson(const String& body, String& error);
    String rssFeedsJson();
    bool writeRssFeedsJson(const String& body, String& error);
    String deviceSuffix() const;
    String jsonEscape(const String& value) const;
    String sanitizeFilename(const String& name) const;
    RsvpMetadata readRsvpMetadata(const String& path) const;
    bool progressForPath(const String& path, uint32_t sourceSize, uint32_t sourceFingerprint, uint32_t wordCount,
                         uint32_t& wordIndex, uint8_t& percent);
    String bookIdForPath(const String& path) const;
    bool resolveBookId(const String& id, String& path) const;
    bool resolveBookName(const String& requested, String& path) const;
    void finishUpload(bool success);

    static CompanionSyncManager* instance_;

    WebServer server_{80};
    File uploadFile_;
    String uploadFinalPath_;
    String uploadTmpPath_;
    String uploadError_;
    std::string pairingCode_;
    std::string networkSsid_;
    Preferences* preferences_ = nullptr;
    std::string statusLine1_ = "Idle";
    std::string statusLine2_;
    NetworkMode networkMode_ = NetworkMode::None;
    bool active_ = false;
    bool serverStarted_ = false;
};
