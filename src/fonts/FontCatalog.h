#pragma once

#include <Arduino.h>
#include <FS.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <functional>
#include <list>
#include <optional>
#include <ranges>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fonts/AlphaFont.h"
#include "fonts/RFont4Format.h"
#include "text/TextShaping.h"

class FontCatalog {
public:
    struct Face {
        std::reference_wrapper<const ui::fonts::AlphaFont> raster;
        std::optional<std::reference_wrapper<TextShaping::Shaper>> shaper;
    };

    struct Family {
        std::string id;
        std::string label;
        std::string locales;
        std::string path;
        bool builtIn = false;
        bool shaping = false;
        uint32_t scriptMask = 0;

        bool supports(uint32_t requiredScripts) const {
            return (scriptMask & requiredScripts) == requiredScripts;
        }

        bool supportsAny(uint32_t requiredScripts) const {
            return (scriptMask & requiredScripts) != 0;
        }

        bool usableFor(std::string_view locale, uint32_t requiredScripts) const {
            return supports(requiredScripts) || (prefers(locale) && supportsAny(requiredScripts))
                || ((requiredScripts & UnicodeText::ScriptMath) != 0
                    && (scriptMask & UnicodeText::ScriptMath) != 0);
        }

        bool prefers(std::string_view locale) const {
            while (!locale.empty()) {
                size_t offset = 0;
                while (offset < locales.size()) {
                    const std::string_view entry{locales.data() + offset};
                    if (entry == locale)
                        return true;
                    offset += entry.size() + 1;
                }
                const size_t separator = locale.rfind('-');
                if (separator == std::string_view::npos)
                    break;
                locale = locale.substr(0, separator);
            }
            return false;
        }
    };

    FontCatalog();

    void loadFromSd();
    std::span<const Family> families() const {
        return families_;
    }
    std::optional<std::reference_wrapper<const Family>> find(std::string_view id) const;
    const ui::fonts::AlphaFont& load(size_t familyIndex, size_t sizeIndex);
    std::optional<std::reference_wrapper<TextShaping::Shaper>> loadShaper(size_t familyIndex);
    Face loadFace(size_t familyIndex, size_t sizeIndex) {
        return {.raster = std::cref(load(familyIndex, sizeIndex)),
                .shaper = loadShaper(familyIndex)};
    }
    void clearLoaded();
    static size_t selectFamily(std::span<const Family> families, std::string_view requested,
                               std::string_view locale, uint32_t requiredScripts) {
        if (families.empty())
            return 0;
        auto selected = std::ranges::find_if(families, [&](const Family& family) {
            return family.id == requested && family.supports(requiredScripts);
        });
        if (selected == families.end()) {
            selected = std::ranges::find_if(families, [&](const Family& family) {
                return family.prefers(locale) && family.supportsAny(requiredScripts);
            });
        }
        if (selected == families.end()) {
            selected = std::ranges::find_if(families, [&](const Family& family) {
                return family.supports(requiredScripts);
            });
        }
        if (selected == families.end()) {
            const auto requestedFamily = std::ranges::find(families, requested, &Family::id);
            const uint32_t missing = requiredScripts
                                   & ~(requestedFamily == families.end() ? 0U : requestedFamily->scriptMask);
            selected = std::ranges::find_if(families, [&](const Family& family) {
                return family.supportsAny(missing);
            });
        }
        return selected == families.end() ? 0 : static_cast<size_t>(selected - families.begin());
    }

    static std::expected<void, std::string> validateFontFile(std::string_view path);

private:
    struct LoadedStrike {
        size_t sizeIndex = 0;
        std::array<uint8_t, RFont4::kPageMapBytes> pageMap{};
        ui::fonts::AlphaFont font;
    };

    struct LoadedFamily {
        size_t familyIndex = 0;
        std::string name;
        File file;
        RFont4::Directory directory;
        std::optional<LoadedStrike> loadedStrike;
        TextShaping::Shaper shaper;
    };

    void reset();
    std::expected<std::reference_wrapper<LoadedFamily>, std::string> loadRuntimeFamily(size_t familyIndex);
    std::expected<std::reference_wrapper<const ui::fonts::AlphaFont>, std::string>
    loadRuntimeStrike(LoadedFamily& family, size_t sizeIndex);
    static std::string normalizeId(std::string_view value);

    std::vector<Family> families_;
    std::list<LoadedFamily> loadedFamilies_;
};
