#pragma once

#include <glaze/json.hpp>

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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

    struct BookMetadata {
        std::string title;
        std::string author;
        uint32_t wordCount = 0;
        uint32_t chapterCount = 0;
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

    struct FeedsResponse {
        std::vector<std::string> feeds;
    };

    struct FeedsUpdate {
        std::optional<std::vector<std::string>> feeds;
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

    struct ErrorEnvelope {
        ApiError error;
    };

    template<typename T>
    bool encodeData(const T& data, std::string& output) {
        output.clear();
        return !glz::write_json(glz::obj{"data", data}, output);
    }

    inline bool encodeError(ApiError error, std::string& output) {
        output.clear();
        return !glz::write_json(ErrorEnvelope{std::move(error)}, output);
    }

    template<typename T>
    bool decode(std::string_view input, T& value, std::string& errorMessage) {
        if (const auto error = glz::read_json(value, input)) {
            errorMessage = glz::format_error(error, input);
            return false;
        }
        return true;
    }

} // namespace companion::api

// Registering the enum values enables Glaze's enum-name serialization without
// maintaining separate string lookup tables.
template<>
struct glz::meta<companion::api::NetworkMode> {
    static constexpr auto value = glz::enumerate(companion::api::NetworkMode::station,
                                                 companion::api::NetworkMode::access_point);
};
