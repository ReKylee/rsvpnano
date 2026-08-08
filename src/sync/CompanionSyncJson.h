#pragma once

#include <glaze/json.hpp>

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "settings/SettingsModel.h"

namespace companion::api {

    // Lower-case enumerators intentionally define the public JSON spelling once.
    // Glaze's automatic enum reflection reads and writes these names directly.
    enum class NetworkMode : uint8_t {
        station,
        access_point,
    };

    struct ApiError {
        std::string code;
        std::string message;
        std::optional<std::string> field;
    };

    struct DeviceInfo {
        std::string name;
        NetworkMode mode = NetworkMode::access_point;
        std::string networkSsid;
        std::string firmwareVersion;
        std::string otaAsset;
        uint8_t apiVersion = 1;
    };

    struct Chapter {
        std::string title;
        uint32_t wordIndex = 0;
    };

    struct BookLanguage {
        std::string locale;
        uint32_t scriptMask = 0;
    };

    struct BookMetadata {
        std::string title;
        std::string author;
        uint32_t wordCount = 0;
        uint32_t chapterCount = 0;
        std::string locale;
        std::string direction = "auto";
        uint32_t scriptMask = 0;
        std::vector<std::string> scripts;
        std::vector<BookLanguage> languages;
        std::vector<std::string> requiredCapabilities;
        std::vector<Chapter> chapters;
    };

    struct BookSource {
        uint32_t size = 0;
        uint32_t fingerprint = 0;
    };

    struct CurrentChapter {
        uint32_t number = 0;
        std::string title;
    };

    struct BookReading {
        uint32_t wordIndex = 0;
        uint8_t percent = 0;
        uint32_t remainingWords = 0;
        uint32_t estimatedMinutes = 0;
        std::optional<CurrentChapter> currentChapter;
        std::vector<settings::LanguageFont> languageFonts;
    };

    struct LibraryItem {
        std::string id;
        std::string name;
        std::string category;
        uint32_t bytes = 0;
        BookMetadata metadata;
        std::optional<BookSource> source;
        std::optional<BookReading> reading;
    };

    struct LibraryResponse {
        std::vector<LibraryItem> books;
    };

    struct NetworkResponse {
        bool passwordSet = false;
    };

    struct NetworkUpdate {
        std::optional<std::string> ssid;
        std::optional<std::string> password;
    };

    struct BookPositionUpdate {
        std::optional<std::string> id;
        std::optional<uint32_t> wordIndex;
    };

    struct BookPositionResponse {
        std::string id;
        uint32_t wordIndex = 0;
        uint8_t percent = 0;
    };

    struct DeleteResponse {
        std::string id;
        bool deleted = true;
    };

    struct UploadResponse {
        std::string path;
    };

    struct ThemeUploadResponse {
        std::string path;
        std::string id;
    };

    struct ThemeSummary {
        std::string id;
        std::string name;
    };

    struct ThemesResponse {
        std::vector<ThemeSummary> themes;
    };

    struct FontSummary {
        std::string id;
        std::string name;
        std::vector<std::string> locales;
        std::vector<std::string> scripts;
        uint32_t scriptMask = 0;
        bool builtIn = false;
        bool shaping = false;
    };

    struct BookLanguageFontsUpdate {
        std::optional<std::string> id;
        std::vector<settings::LanguageFont> languageFonts;
    };

    struct FontsResponse {
        std::vector<FontSummary> fonts;
    };

    struct LocaleSummary {
        std::string id;
        std::string version;
        std::string locale;
        std::string nativeName;
        std::string englishName;
        std::string direction;
        std::string translationStatus;
        uint32_t scriptMask = 0;
        std::vector<std::string> requiredCapabilities;
        std::vector<std::string> scripts;
    };

    struct LocaleIssue {
        std::string id;
        std::string reason;
    };

    struct LocalesResponse {
        std::vector<LocaleSummary> locales;
        std::vector<LocaleIssue> rejected;
    };

    struct IdResponse {
        std::string id;
    };

    struct ErrorEnvelope {
        ApiError error;
    };

    template<typename T>
    std::expected<void, std::string> encodeData(const T& data, std::string& output) {
        output.clear();
        if (glz::write_json(glz::obj{"data", data}, output))
            return std::unexpected(std::string{"JSON serialization failed"});
        return {};
    }

    inline std::expected<void, std::string> encodeError(ApiError error, std::string& output) {
        output.clear();
        if (glz::write_json(ErrorEnvelope{std::move(error)}, output))
            return std::unexpected(std::string{"JSON error serialization failed"});
        return {};
    }

    template<typename T>
    std::expected<T, std::string> decode(std::string_view input) {
        T value{};
        if (const auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(value, input)) {
            return std::unexpected(glz::format_error(error, input));
        }
        return value;
    }

} // namespace companion::api

// Registering the enum values enables Glaze's enum-name serialization without
// maintaining separate string lookup tables.
template<>
struct glz::meta<companion::api::NetworkMode> {
    static constexpr auto value =
        glz::enumerate(companion::api::NetworkMode::station, companion::api::NetworkMode::access_point);
};
