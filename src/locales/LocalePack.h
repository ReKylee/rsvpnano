#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "locales/LocaleUiFont.h"

namespace locales {

    inline constexpr uint32_t kManifestSchemaVersion = 2;
    inline constexpr uint32_t kEngineAbiVersion = 1;
    inline constexpr size_t kMaximumManifestBytes = 16 * 1024;
    inline constexpr uint32_t kMaximumAssetBytes = 16 * 1024 * 1024;
    inline constexpr uint32_t kMaximumUiFontBytes = 64 * 1024;
    inline constexpr uint32_t kMaximumResidentUiBytes = 96 * 1024;

    enum class Direction : uint8_t {
        ltr,
        rtl,
    };

    enum class TranslationStatus : uint8_t {
        preview,
        reviewed,
    };

    struct Asset {
        std::string path;
        uint32_t bytes = 0;
        std::string sha256;
        std::string license;
    };

    struct UiComponent {
        std::optional<Asset> strings;
        std::optional<Asset> font;
    };

    struct Manifest {
        uint32_t schemaVersion = 0;
        std::string id;
        std::string version;
        std::string locale;
        std::string nativeName;
        std::string englishName;
        Direction direction = Direction::ltr;
        std::vector<std::string> scripts;
        std::string unicodeVersion;
        TranslationStatus translationStatus = TranslationStatus::preview;
        std::string minimumFirmware;
        uint32_t engineAbi = 0;
        std::vector<std::string> requiredCapabilities;
        std::optional<UiComponent> ui;
    };

    struct StringTable {
        std::vector<uint8_t> bytes;
        uint16_t entryCount = 0;
        uint32_t textOffset = 0;

        std::string_view at(size_t index) const;
    };

    struct UiAssets {
        std::string packId;
        std::string locale;
        Direction direction = Direction::ltr;
        StringTable strings;
        UiFont font;

        std::string_view text(size_t key) const {
            return strings.at(key);
        }
        bool owns(std::string_view text) const {
            if (text.empty() || strings.bytes.empty())
                return false;
            const uintptr_t begin = reinterpret_cast<uintptr_t>(strings.bytes.data()) + strings.textOffset;
            const uintptr_t end = reinterpret_cast<uintptr_t>(strings.bytes.data()) + strings.bytes.size();
            const uintptr_t value = reinterpret_cast<uintptr_t>(text.data());
            return value >= begin && value <= end && text.size() <= end - value;
        }
    };

    bool isValidPackId(std::string_view id);
    bool isValidPackFilePath(std::string_view path);
    std::string_view toString(Direction value);
    std::string_view toString(TranslationStatus value);
    std::expected<StringTable, std::string> decodeStringTable(std::vector<uint8_t> bytes, size_t expectedEntries);
    std::expected<void, std::string> validateU8g2Font(std::span<const uint8_t> bytes);
    std::expected<Manifest, std::string> decodeManifest(std::string_view toml, std::string_view directoryId);

} // namespace locales
