#pragma once

#include <FS.h>

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "locales/LocalePack.h"
#include "text/UnicodeText.h"

namespace locales {

    struct InstalledPack {
        std::string directory;
        Manifest manifest;
        uint32_t scriptMask = 0;
    };

    struct CatalogIssue {
        std::string id;
        std::string reason;
    };

    struct Catalog {
        std::vector<InstalledPack> packs;
        std::vector<CatalogIssue> rejected;
    };

    inline std::optional<std::reference_wrapper<const InstalledPack>> findPackForLocale(
        const Catalog& catalog, std::string_view locale) {
        std::string candidate{locale};
        while (!candidate.empty()) {
            const auto pack = std::ranges::find_if(catalog.packs, [&](const InstalledPack& installed) {
                return installed.manifest.locale == candidate;
            });
            if (pack != catalog.packs.end())
                return std::cref(*pack);
            const size_t separator = candidate.rfind('-');
            if (separator == std::string::npos)
                break;
            candidate.erase(separator);
        }
        return std::nullopt;
    }

    Catalog scanInstalled(fs::FS& filesystem);
    std::expected<UiAssets, std::string> loadUiAssets(fs::FS& filesystem, const Catalog& catalog,
                                                       std::string_view locale, size_t expectedStrings);
    std::expected<void, std::string> beginStaging(fs::FS& filesystem, std::string_view id);
    std::expected<std::string, std::string> prepareStagedFile(fs::FS& filesystem, std::string_view id,
                                                              std::string_view relativePath);
    std::expected<void, std::string> activateStaged(fs::FS& filesystem, Catalog& catalog, std::string_view id);
    std::expected<std::string, std::string> installArchive(fs::FS& filesystem, Catalog& catalog,
                                                           std::string_view archivePath);
    std::expected<void, std::string> removeInstalled(fs::FS& filesystem, Catalog& catalog, std::string_view id);
    void recoverInterrupted(fs::FS& filesystem);
    std::string_view localeName(const Catalog& catalog, std::string_view locale);
} // namespace locales
