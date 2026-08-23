#pragma once

#include <FS.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "locales/LocalePack.h"
#include "text/UnicodeText.h"

namespace locales {

    struct InstalledAsset {
        std::string path;
        uint32_t bytes = 0;
    };

    struct InstalledUiComponent {
        std::optional<InstalledAsset> strings;
        std::optional<InstalledAsset> font;
    };

    struct InstalledPack {
        std::string directory;
        std::string id;
        std::string locale;
        std::string nativeName;
        std::string englishName;
        TextDirection direction = TextDirection::ltr;
        uint32_t scriptMask = 0;
        std::optional<InstalledUiComponent> ui;
    };

    using Catalog = std::vector<InstalledPack>;

    inline const InstalledPack* findPackForLocale(const Catalog& catalog, std::string_view locale) {
        std::string_view candidate = locale;
        while (!candidate.empty()) {
            const auto pack = std::ranges::find_if(catalog, [&](const InstalledPack& installed) {
                return installed.locale == candidate;
            });
            if (pack != catalog.end())
                return &*pack;
            const size_t separator = candidate.rfind('-');
            if (separator == std::string::npos)
                break;
            candidate = candidate.substr(0, separator);
        }
        return nullptr;
    }

    inline const InstalledPack* findPackForScripts(const Catalog& catalog, std::string_view preferredLocale,
                                                    uint32_t requiredScripts) {
        const auto supports = [requiredScripts](const InstalledPack& pack) {
            return (pack.scriptMask & requiredScripts) == requiredScripts;
        };
        if (const InstalledPack* preferred = findPackForLocale(catalog, preferredLocale);
            preferred != nullptr && supports(*preferred))
            return preferred;
        const auto pack = std::ranges::find_if(catalog, supports);
        return pack == catalog.end() ? nullptr : &*pack;
    }

    Catalog scanInstalled(fs::FS& filesystem, size_t expectedStrings);
    std::expected<std::vector<uint8_t>, std::string> loadUiFont(fs::FS& filesystem, const InstalledPack& pack);
    std::expected<UiAssets, std::string> loadUiAssets(fs::FS& filesystem, const Catalog& catalog,
                                                      std::string_view locale, size_t expectedStrings);
    std::expected<void, std::string> beginStaging(fs::FS& filesystem, std::string_view id);
    std::expected<std::string, std::string> prepareStagedFile(fs::FS& filesystem, std::string_view id,
                                                              std::string_view relativePath);
    std::expected<std::reference_wrapper<const InstalledPack>, std::string> activateStaged(fs::FS& filesystem,
                                                                                           Catalog& catalog,
                                                                                           std::string_view id,
                                                                                           size_t expectedStrings);
    std::expected<std::reference_wrapper<const InstalledPack>, std::string> installArchive(fs::FS& filesystem,
                                                                                           Catalog& catalog,
                                                                                           std::string_view
                                                                                               archivePath,
                                                                                           size_t expectedStrings);
    std::expected<void, std::string> removeInstalled(fs::FS& filesystem, Catalog& catalog, std::string_view id);
    void recoverInterrupted(fs::FS& filesystem);
    std::string_view localeName(const Catalog& catalog, std::string_view locale);
} // namespace locales
