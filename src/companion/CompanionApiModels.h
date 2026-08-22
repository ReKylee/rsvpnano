#pragma once

#include <glaze/json.hpp>

#include <algorithm>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "book/BookMetadata.h"
#include "fonts/FontCatalog.h"
#include "locales/LocaleCatalog.h"
#include "reader/ReadingState.h"
#include "settings/SettingsModel.h"
#include "ui/Theme.h"

namespace companion::api {

    struct ApiError {
        std::string code;
        std::string message;
        std::optional<std::string> field;
    };

    struct DeviceInfo {
        std::string ssid;
        std::string firmwareVersion;
        std::string otaAsset;
    };

    struct BookLanguage {
        std::string locale;
        std::vector<std::string> scripts;
    };

    inline std::vector<BookLanguage> bookLanguages(const BookMetadata& metadata) {
        std::vector<BookLanguage> languages;
        languages.reserve(metadata.textRuns.size() + 1);
        metadata.forEachLanguage([&languages](std::string_view locale, uint32_t scriptMask) {
            languages.push_back({std::string{locale}, UnicodeText::scriptTags(scriptMask)});
        });
        return languages;
    }

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

    struct BookLanguageFontsUpdate {
        std::vector<settings::LanguageFont> languageFonts;
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

template<>
struct glz::meta<ChapterMarker> {
    using T = ChapterMarker;
    static constexpr auto value = glz::object("title", &T::title, "wordIndex", &T::wordIndex);
};

template<>
struct glz::meta<ui::themes::ThemeEntry> {
    using T = ui::themes::ThemeEntry;
    static constexpr auto readName = [](T& theme, std::string name) {
        theme.definition.name = std::move(name);
    };
    static constexpr auto writeName = [](const T& theme) -> const std::string& {
        return theme.definition.name;
    };
    static constexpr auto value = glz::object("id", &T::id, "name", glz::custom<readName, writeName>);
};

template<>
struct glz::meta<FontCatalog::Family> {
    using T = FontCatalog::Family;
    static constexpr auto readName = [](T& family, std::string name) {
        family.label = std::move(name);
    };
    static constexpr auto writeName = [](const T& family) -> const std::string& {
        return family.label;
    };
    static constexpr auto readLocales = [](T&, const std::vector<std::string>&) {};
    static constexpr auto writeLocales = [](const T& family) {
        std::vector<std::string> locales;
        for (size_t offset = 0; offset < family.locales.size();) {
            const std::string_view locale{family.locales.data() + offset};
            locales.emplace_back(locale);
            offset += locale.size() + 1;
        }
        return locales;
    };
    static constexpr auto readScripts = [](T&, const std::vector<std::string>&) {};
    static constexpr auto writeScripts = [](const T& family) {
        return UnicodeText::scriptTags(family.scriptMask);
    };
    static constexpr auto value = glz::object("id", &T::id, "name", glz::custom<readName, writeName>, "locales",
                                              glz::custom<readLocales, writeLocales>, "scripts",
                                              glz::custom<readScripts, writeScripts>, "builtIn", &T::builtIn);
};

template<>
struct glz::meta<locales::InstalledPack> {
    using T = locales::InstalledPack;
    static constexpr auto readId = [](T& pack, std::string id) {
        pack.manifest.id = std::move(id);
    };
    static constexpr auto writeId = [](const T& pack) -> const std::string& {
        return pack.manifest.id;
    };
    static constexpr auto readName = [](T& pack, std::string name) {
        pack.manifest.nativeName = std::move(name);
    };
    static constexpr auto writeName = [](const T& pack) -> const std::string& {
        return pack.manifest.nativeName;
    };
    static constexpr auto readLocale = [](T& pack, std::string locale) {
        pack.manifest.locale = std::move(locale);
    };
    static constexpr auto writeLocale = [](const T& pack) -> const std::string& {
        return pack.manifest.locale;
    };
    static constexpr auto value =
        glz::object("id", glz::custom<readId, writeId>, "name", glz::custom<readName, writeName>, "locale",
                    glz::custom<readLocale, writeLocale>);
};
