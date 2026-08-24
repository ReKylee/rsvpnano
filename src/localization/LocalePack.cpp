#include "localization/LocalePack.h"

#include <glaze/toml.hpp>

#include <algorithm>
#include <array>
#include <functional>
#include <ranges>

#include "text/AsciiText.h"
#include "text/LocaleTag.h"
#include "text/UnicodeText.h"

template<>
struct glz::meta<TextDirection> {
    using enum TextDirection;
    static constexpr auto value = glz::enumerate("auto", automatic, "ltr", ltr, "rtl", rtl);
};

template<>
struct glz::meta<locales::TranslationStatus> {
    using enum locales::TranslationStatus;
    static constexpr auto value = glz::enumerate(preview, reviewed);
};

template<>
struct glz::meta<locales::Asset> {
    using T = locales::Asset;
    static constexpr auto value = glz::object("path", &T::path, "bytes", &T::bytes, "sha256", &T::sha256,
                                              "license", &T::license);
};

template<>
struct glz::meta<locales::UiComponent> {
    using T = locales::UiComponent;
    static constexpr auto value = glz::object("strings", &T::strings, "font", &T::font);
};

template<>
struct glz::meta<locales::Manifest> {
    using T = locales::Manifest;
    static constexpr auto value =
        glz::object("schema_version", &T::schemaVersion, "id", &T::id, "version", &T::version, "locale", &T::locale,
                    "native_name", &T::nativeName, "english_name", &T::englishName, "direction",
                    &T::direction, "scripts", &T::scripts, "unicode_version", &T::unicodeVersion, "translation_status",
                    &T::translationStatus, "minimum_firmware", &T::minimumFirmware, "engine_abi", &T::engineAbi,
                    "requires", &T::requiredCapabilities, "ui", &T::ui);
};

namespace locales {
    namespace {

        bool validIdentifier(std::string_view value) {
            if (value.empty() || value.size() > 64 || !AsciiText::isAlphaNumeric(value.front())
                || !AsciiText::isAlphaNumeric(value.back()))
                return false;
            return !value.contains("--") && std::ranges::all_of(value, [](char character) {
                return AsciiText::isAlphaNumeric(character) || character == '-';
            });
        }

        bool validCapability(std::string_view value) {
            return UnicodeText::capabilityMask(value) != UnicodeText::CapabilityNone;
        }

        bool hasDuplicates(std::span<const std::string> values) {
            for (size_t index = 1; index < values.size(); ++index) {
                if (std::ranges::find(values.first(index), values[index]) != values.first(index).end())
                    return true;
            }
            return false;
        }

        bool validVersion(std::string_view value) {
            const size_t suffix = value.find_first_of("-+");
            const std::string_view core = value.substr(0, suffix);
            size_t start = 0;
            for (uint8_t part = 0; part < 3; ++part) {
                const size_t end = part == 2 ? core.size() : core.find('.', start);
                if (end == std::string_view::npos || end == start)
                    return false;
                const std::string_view number = core.substr(start, end - start);
                if (!std::ranges::all_of(number, AsciiText::isDigit) || (number.size() > 1 && number.front() == '0'))
                    return false;
                start = end + 1;
            }
            if (start != core.size() + 1)
                return false;
            if (suffix == std::string_view::npos)
                return true;
            const std::string_view suffixValue = value.substr(suffix + 1);
            return !suffixValue.empty() && std::ranges::all_of(suffixValue, [](char character) {
                return AsciiText::isAlphaNumeric(character) || character == '-' || character == '.'
                    || character == '+';
            });
        }

        bool validRelativePath(std::string_view path) {
            if (path.empty() || path.size() > 160 || path.starts_with('/') || path.contains('\\'))
                return false;
            while (!path.empty()) {
                const size_t separator = path.find('/');
                const std::string_view segment = path.substr(0, separator);
                if (segment.empty() || segment == "." || segment == "..")
                    return false;
                if (separator == std::string_view::npos)
                    break;
                path.remove_prefix(separator + 1);
            }
            return true;
        }

        bool validSha256(std::string_view hash) {
            return hash.size() == 64 && std::ranges::all_of(hash, [](char character) {
                       return AsciiText::isDigit(character) || (character >= 'a' && character <= 'f');
                   });
        }

        uint16_t readLe16(std::span<const uint8_t> bytes, size_t offset) {
            return static_cast<uint16_t>(bytes[offset]) | static_cast<uint16_t>(bytes[offset + 1]) << 8U;
        }

        uint32_t readLe32(std::span<const uint8_t> bytes, size_t offset) {
            return static_cast<uint32_t>(bytes[offset]) | static_cast<uint32_t>(bytes[offset + 1]) << 8U
                 | static_cast<uint32_t>(bytes[offset + 2]) << 16U | static_cast<uint32_t>(bytes[offset + 3]) << 24U;
        }

        uint16_t readBe16(std::span<const uint8_t> bytes, size_t offset) {
            return static_cast<uint16_t>(bytes[offset]) << 8U | bytes[offset + 1];
        }

        std::expected<void, std::string> validateAsset(const std::optional<Asset>& asset, std::string_view name) {
            if (!asset)
                return {};
            if (!validRelativePath(asset->path))
                return std::unexpected(std::string{name} + " has an invalid path");
            if (name.starts_with("ui.") && !asset->path.starts_with("ui/"))
                return std::unexpected(std::string{name} + " is outside its component directory");
            if (asset->bytes == 0)
                return std::unexpected(std::string{name} + " has an invalid byte length");
            if (asset->bytes > kMaximumAssetBytes)
                return std::unexpected(std::string{name} + " exceeds 16 MiB");
            if (!validSha256(asset->sha256))
                return std::unexpected(std::string{name} + " has an invalid SHA-256 hash");
            if (asset->license.empty())
                return std::unexpected(std::string{name} + " is missing license metadata");
            return {};
        }

        std::expected<void, std::string> validateAssets(const Manifest& manifest) {
            if (manifest.ui) {
                if (!manifest.ui->strings && !manifest.ui->font)
                    return std::unexpected("ui component is empty");
                for (const auto& [asset, name]: std::array{std::pair{&manifest.ui->strings, "ui.strings"},
                                                           std::pair{&manifest.ui->font, "ui.font"}}) {
                    if (auto valid = validateAsset(*asset, name); !valid)
                        return valid;
                }
                if (manifest.ui->font && manifest.ui->font->bytes > kMaximumUiFontBytes)
                    return std::unexpected("ui.font exceeds 64 KiB");
                const uint64_t residentBytes = (manifest.ui->strings ? manifest.ui->strings->bytes : 0)
                                             + (manifest.ui->font ? manifest.ui->font->bytes : 0);
                if (residentBytes > kMaximumResidentUiBytes)
                    return std::unexpected("resident UI assets exceed 96 KiB");
            }
            return {};
        }

    } // namespace

    bool isValidPackId(std::string_view id) {
        return validIdentifier(id);
    }

    bool isValidPackFilePath(std::string_view path) {
        return path == "manifest.toml"
            || (validRelativePath(path) && path.starts_with("ui/") && !path.ends_with(".rfont4"));
    }

    std::optional<std::string_view> packIdFromArchiveManifest(std::string_view path) {
        constexpr std::string_view prefix = "locales/";
        constexpr std::string_view suffix = "/manifest.toml";
        if (!path.starts_with(prefix) || !path.ends_with(suffix))
            return std::nullopt;
        const std::string_view id = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
        return isValidPackId(id) ? std::optional{id} : std::nullopt;
    }

    std::string_view toString(TranslationStatus value) {
        return value == TranslationStatus::preview ? "preview" : "reviewed";
    }

    std::string_view StringTable::at(size_t index) const {
        if (index >= entryCount || textOffset > bytes.size())
            return {};
        const std::span data{bytes};
        const size_t offsets = 12;
        const uint32_t begin = readLe32(data, offsets + index * 4);
        const uint32_t end = readLe32(data, offsets + (index + 1) * 4);
        if (begin > end || end > bytes.size() - textOffset)
            return {};
        return {reinterpret_cast<const char*>(bytes.data() + textOffset + begin), end - begin};
    }

    std::expected<StringTable, std::string> decodeStringTable(std::vector<uint8_t> bytes, size_t expectedEntries) {
        constexpr std::array<uint8_t, 4> magic{'R', 'S', 'L', '1'};
        if (bytes.size() < 16 || !std::ranges::equal(magic, std::span{bytes}.first<4>()))
            return std::unexpected("invalid UI string table header");
        const std::span data{bytes};
        if (readLe16(data, 4) != 1)
            return std::unexpected("unsupported UI string table version");
        const uint16_t count = readLe16(data, 6);
        if (count != expectedEntries)
            return std::unexpected("UI string table has the wrong key count");
        const uint32_t textBytes = readLe32(data, 8);
        const size_t textOffset = 12 + (static_cast<size_t>(count) + 1) * 4;
        if (textOffset > bytes.size() || textBytes != bytes.size() - textOffset)
            return std::unexpected("UI string table has an invalid length");
        uint32_t previous = 0;
        for (size_t index = 0; index <= count; ++index) {
            const uint32_t offset = readLe32(data, 12 + index * 4);
            if (offset < previous || offset > textBytes)
                return std::unexpected("UI string table has an invalid offset");
            previous = offset;
        }
        if (previous != textBytes)
            return std::unexpected("UI string table does not cover its text data");
        return StringTable{std::move(bytes), count, static_cast<uint32_t>(textOffset)};
    }

    std::expected<void, std::string> validateU8g2Font(std::span<const uint8_t> bytes) {
        if (bytes.size() < 24 || bytes.size() > kMaximumUiFontBytes)
            return std::unexpected("UI font has an invalid length");
        if (std::ranges::any_of(bytes.subspan(2, 7), [](uint8_t bits) { return bits > 8; }))
            return std::unexpected("UI font has invalid glyph encoding widths");
        const uint8_t width = bytes[9];
        const uint8_t height = bytes[10];
        if (width == 0 || width > 32 || height == 0 || height > 32)
            return std::unexpected("UI font has invalid cell metrics");
        const size_t recordBytes = bytes.size() - 23;
        for (const size_t offset: {size_t{17}, size_t{19}, size_t{21}}) {
            if (readBe16(bytes, offset) >= recordBytes)
                return std::unexpected("UI font has an invalid lookup offset");
        }
        return {};
    }

    std::expected<Manifest, std::string> decodeManifest(std::string_view toml, std::string_view directoryId) {
        if (toml.empty() || toml.size() > kMaximumManifestBytes)
            return std::unexpected("manifest is empty or too large");
        Manifest manifest;
        constexpr glz::opts options{.format = glz::TOML, .error_on_unknown_keys = true};
        if (const glz::error_ctx error = glz::read<options>(manifest, toml))
            return std::unexpected(glz::format_error(error, toml));
        if (manifest.schemaVersion != kManifestSchemaVersion)
            return std::unexpected("unsupported manifest schema");
        if (!isValidPackId(manifest.id) || manifest.id != directoryId)
            return std::unexpected("manifest ID does not match its directory");
        if (!validVersion(manifest.version) || !validVersion(manifest.minimumFirmware))
            return std::unexpected("invalid semantic version");
        if (manifest.engineAbi == 0 || manifest.engineAbi > kEngineAbiVersion)
            return std::unexpected("unsupported engine ABI");
        if (manifest.nativeName.empty() || manifest.englishName.empty() || manifest.unicodeVersion.empty())
            return std::unexpected("manifest is missing required metadata");
        if (manifest.direction == TextDirection::automatic)
            return std::unexpected("locale direction must be ltr or rtl");
        if (std::ranges::any_of(manifest.requiredCapabilities, std::not_fn(validCapability)))
            return std::unexpected("manifest has invalid engine capabilities");
        if (hasDuplicates(manifest.requiredCapabilities))
            return std::unexpected("manifest has duplicate engine capabilities");
        auto locale = LocaleTag::normalize(manifest.locale);
        if (!locale || *locale != manifest.locale)
            return std::unexpected("locale is not a normalized BCP 47 tag");
        if (manifest.scripts.empty() || std::ranges::any_of(manifest.scripts, [](std::string_view script) {
                return script.size() != 4 || !std::ranges::all_of(script, AsciiText::isAlpha)
                    || !std::isupper(static_cast<unsigned char>(script.front()))
                    || !std::ranges::all_of(script.substr(1), [](char character) {
                           return std::islower(static_cast<unsigned char>(character));
                       })
                    || UnicodeText::scriptMask(script) == UnicodeText::ScriptNone;
            }) || hasDuplicates(manifest.scripts))
            return std::unexpected("manifest has invalid script metadata");
        if (!manifest.ui || !manifest.ui->strings)
            return std::unexpected("locale pack has no UI strings");
        return validateAssets(manifest).transform([manifest = std::move(manifest)]() mutable {
            return std::move(manifest);
        });
    }

} // namespace locales
