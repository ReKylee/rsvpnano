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

    struct ApiError {
        std::string code;
        std::string message;
        std::optional<std::string> field;
    };

    struct DeviceInfo {
        std::string firmwareVersion;
        std::string otaAsset;
    };

    struct Chapter {
        std::string title;
        uint32_t wordIndex = 0;
    };

    struct BookLanguage {
        std::string locale;
        std::vector<std::string> scripts;
    };

    struct BookMetadata {
        std::string title;
        std::string author;
        uint32_t wordCount = 0;
        std::string locale;
        std::vector<std::string> scripts;
        std::vector<BookLanguage> languages;
        std::vector<Chapter> chapters;
    };

    struct BookReading {
        uint32_t wordIndex = 0;
        std::vector<settings::LanguageFont> languageFonts;
    };

    struct LibraryItem {
        std::string id;
        std::string name;
        uint32_t bytes = 0;
        BookMetadata metadata;
        std::optional<BookReading> reading;
    };

    struct NetworkResponse {
        std::string ssid;
    };

    struct NetworkUpdate {
        std::optional<std::string> ssid;
        std::optional<std::string> password;
    };

    struct AppearanceSelection {
        std::string id;
    };

    struct BookPositionUpdate {
        std::optional<uint32_t> wordIndex;
    };

    struct ThemeSummary {
        std::string id;
        std::string name;
    };

    struct FontSummary {
        std::string id;
        std::string name;
        std::vector<std::string> locales;
        std::vector<std::string> scripts;
        bool builtIn = false;
    };

    struct BookLanguageFontsUpdate {
        std::vector<settings::LanguageFont> languageFonts;
    };

    struct LocaleSummary {
        std::string id;
        std::string name;
        std::string locale;
    };

    struct SettingsResponse {
        settings::ReadingSettings reading;
        settings::InterfaceSettings interface;
        settings::UpdateSettings updates;
    };

    template<typename T>
    [[nodiscard]] std::expected<void, std::string> encode(const T& data, std::string& output) {
        output.clear();
        if (glz::write_json(data, output))
            return std::unexpected(std::string{"JSON serialization failed"});
        return {};
    }

    template<typename T>
    [[nodiscard]] std::expected<T, std::string> decode(std::string_view input, T value = {}) {
        if (const auto error = glz::read<glz::opts{.error_on_unknown_keys = false}>(value, input)) {
            return std::unexpected(glz::format_error(error, input));
        }
        return value;
    }

} // namespace companion::api
