#include "fonts/FontCatalog.h"

#include <esp_log.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <span>

#include "board/BoardStorage.h"
#include "fonts/LiterataFallbackAlpha4.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"
#include "text/AsciiText.h"

namespace {

    constexpr const char* kFallbackId = "literata";
    constexpr const char* kFallbackLabel = "Literata";
    const std::array<const ui::fonts::AlphaFont*, RFont4::kSizeCount> kFallbackFonts = {
        &ui::fonts::LiterataFallbackAlpha4_52,
        &ui::fonts::LiterataFallbackAlpha4_43,
        &ui::fonts::LiterataFallbackAlpha4_33,
    };

    template<typename T, size_t Extent>
    std::expected<void, std::string> readSection(File& file, uint32_t offset, std::span<T, Extent> out) {
        if (out.empty())
            return {};
        if (!file.seek(offset))
            return std::unexpected("Font section seek failed");
        if (file.read(reinterpret_cast<uint8_t*>(out.data()), out.size_bytes()) != out.size_bytes())
            return std::unexpected("Font section read failed");
        return {};
    }

    std::expected<RFont4::Header, std::string> readHeader(File& file) {
        RFont4::Header header;
        if (!file.seek(0) || file.read(reinterpret_cast<uint8_t*>(&header), sizeof(header)) != sizeof(header))
            return std::unexpected("Font header read failed");
        return header;
    }

    std::expected<RFont4::Directory, std::string> readDirectory(File& file) {
        RFont4::Directory directory;
        auto header = readHeader(file);
        if (!header || !RFont4::headerValid(*header, file.size()))
            return std::unexpected(header ? "Unsupported or corrupt font format" : header.error());
        directory.header = *header;
        if (auto read = readSection(file, header->strikesOffset, std::span{directory.strikes}); !read)
            return std::unexpected(read.error());
        auto tables = std::span{directory.layoutTables}.first(header->layoutTableCount);
        if (auto read = readSection(file, header->layoutTablesOffset, tables); !read)
            return std::unexpected(read.error());
        if (!RFont4::layoutValid(*header, directory.strikes, tables, file.size()))
            return std::unexpected("Unsupported or corrupt font format");
        return directory;
    }

    std::expected<std::string, std::string> readName(File& file, const RFont4::Header& header) {
        std::vector<uint8_t> bytes(header.nameSize);
        if (auto read = readSection(file, header.nameOffset, std::span{bytes}); !read)
            return std::unexpected(read.error());
        if (bytes.back() != '\0')
            return std::unexpected("Font name is invalid");
        return std::string{reinterpret_cast<const char*>(bytes.data()), bytes.size() - 1};
    }

    std::expected<std::string, std::string> readLocales(File& file, const RFont4::Header& header) {
        if (header.localeSize == 0)
            return std::string{};
        std::string locales(header.localeSize, '\0');
        if (auto read = readSection(file, header.localeOffset, std::span{locales}); !read || locales.back() != '\0')
            return std::unexpected("Font locales are invalid");
        return locales;
    }

} // namespace

FontCatalog::FontCatalog() {
    reset();
}

void FontCatalog::reset() {
    clearLoaded();
    families_ = {{.id = kFallbackId,
                  .label = kFallbackLabel,
                  .builtIn = true,
                  .scriptMask = kFallbackFonts.front()->scriptMask}};
}

void FontCatalog::loadFromSd() {
    reset();
    StorageFiles::ensureDirectory(StoragePaths::kFontsPath);

    File root = Board::Storage::filesystem().open(StoragePaths::kFontsPath);
    if (!root || !root.isDirectory()) {
        if (root)
            root.close();
        return;
    }

    while (File entry = root.openNextFile()) {
        if (!entry.isDirectory()) {
            entry.close();
            continue;
        }

        const std::string assetPath = std::string{entry.path()} + "/" + RFont4::kFilename;
        File asset = Board::Storage::filesystem().open(assetPath.c_str(), FILE_READ);
        if (!asset) {
            entry.close();
            continue;
        }

        auto header = readHeader(asset);
        auto label = header && RFont4::headerValid(*header, asset.size()) ? readName(asset, *header)
                                                                          : std::expected<std::string, std::string>{
                                                                                std::unexpected("Invalid font")};
        auto locales = header && label ? readLocales(asset, *header)
                                       : std::expected<std::string, std::string>{std::unexpected("Invalid font")};
        if (header && label && locales) {
            const std::string id = normalizeId(*label);
            if (!id.empty() && id != kFallbackId && !find(id)) {
                families_.push_back({.id = id,
                                     .label = std::move(*label),
                                     .locales = std::move(*locales),
                                     .path = asset.path(),
                                     .shaping = header->layoutTableCount != 0,
                                     .scriptMask = header->scriptMask});
            }
        }
        asset.close();
        entry.close();
    }
    root.close();

    std::ranges::sort(families_.begin() + 1, families_.end(), {}, &Family::label);
}

std::optional<std::reference_wrapper<const FontCatalog::Family>> FontCatalog::find(std::string_view id) const {
    const auto findId = [this](std::string_view value) {
        return std::ranges::find_if(families_, [value](const Family& family) {
            return family.id == value;
        });
    };
    auto found = findId(id);
    if (found == families_.end())
        found = findId(normalizeId(id));
    return found == families_.end() ? std::nullopt
                                    : std::optional<std::reference_wrapper<const Family>>{std::cref(*found)};
}

FontCatalog::Face FontCatalog::loadFace(size_t familyIndex, size_t sizeIndex) {
    const size_t safeFamily = std::min(familyIndex, families_.size() - 1);
    const size_t safeSize = std::min(sizeIndex, RFont4::kSizeCount - 1);
    if (!families_[safeFamily].builtIn) {
        auto family = loadRuntimeFamily(safeFamily);
        if (family) {
            LoadedFamily& loaded = family->get();
            auto strike = loadRuntimeStrike(loaded, safeSize);
            if (strike) {
                std::optional<std::reference_wrapper<TextShaping::Shaper>> shaper;
                if (families_[safeFamily].shaping && !loaded.shapingFailed) {
                    if (!loaded.shaper.ready()) {
                        const auto tables = std::span{loaded.directory.layoutTables}
                                                .first(loaded.directory.header.layoutTableCount);
                        if (auto opened = loaded.shaper.open(loaded.file, loaded.directory.header, tables); !opened) {
                            loaded.shapingFailed = true;
                            ESP_LOGE("font", "shaping failed %s: %s", families_[safeFamily].path.c_str(),
                                     opened.error().c_str());
                        }
                    }
                    if (loaded.shaper.ready())
                        shaper = std::ref(loaded.shaper);
                }
                return {.raster = *strike, .shaper = shaper};
            }
            ESP_LOGE("font", "load failed %s: %s", families_[safeFamily].path.c_str(), strike.error().c_str());
        } else {
            ESP_LOGE("font", "load failed %s: %s", families_[safeFamily].path.c_str(), family.error().c_str());
        }
    }
    return {.raster = std::cref(*kFallbackFonts[safeSize])};
}

void FontCatalog::clearLoaded() {
    loadedFamilies_.clear();
}

std::expected<std::reference_wrapper<FontCatalog::LoadedFamily>, std::string>
FontCatalog::loadRuntimeFamily(size_t familyIndex) {
    const auto existing = std::ranges::find_if(loadedFamilies_, [familyIndex](const LoadedFamily& loaded) {
        return loaded.familyIndex == familyIndex;
    });
    if (existing != loadedFamilies_.end())
        return std::ref(*existing);

    LoadedFamily& loaded = loadedFamilies_.emplace_back();
    loaded.familyIndex = familyIndex;
    loaded.file = Board::Storage::filesystem().open(families_[familyIndex].path.c_str(), FILE_READ);
    const auto fail = [this](std::string error) {
        loadedFamilies_.pop_back();
        return std::expected<std::reference_wrapper<LoadedFamily>, std::string>{std::unexpected(std::move(error))};
    };
    if (!loaded.file || loaded.file.isDirectory())
        return fail("Font file unavailable");
    auto directory = readDirectory(loaded.file);
    if (!directory)
        return fail(directory.error());
    loaded.directory = std::move(*directory);
    return std::ref(loaded);
}

std::expected<std::reference_wrapper<const ui::fonts::AlphaFont>, std::string>
FontCatalog::loadRuntimeStrike(LoadedFamily& family, size_t sizeIndex) {
    if (family.loadedStrike && family.loadedStrike->sizeIndex == sizeIndex)
        return std::cref(family.loadedStrike->font);

    LoadedStrike& loaded = family.loadedStrike.emplace();
    loaded.sizeIndex = sizeIndex;
    const RFont4::StrikeRecord& strike = family.directory.strikes[sizeIndex];
    if (auto read = readSection(family.file, strike.pageMapOffset, std::span{loaded.pageMap}); !read) {
        family.loadedStrike.reset();
        return std::unexpected(read.error());
    }
    loaded.font = {
        .name = families_[family.familyIndex].label,
        .glyphCount = strike.glyphCount,
        .yAdvance = strike.yAdvance,
        .ascent = strike.ascent,
        .descent = strike.descent,
        .pageMap = loaded.pageMap.data(),
        .pageTableCount = strike.pageTableCount,
        .kerningPairCount = strike.kerningPairCount,
        .wordInkTop = strike.wordInkTop,
        .wordInkBottom = strike.wordInkBottom,
        .glyphIdCount = strike.glyphIdCount,
        .scriptMask = family.directory.header.scriptMask,
        .pixelsPerEm = strike.pixelsPerEm,
        .file = std::ref(family.file),
        .fileStrike = strike,
    };
    return std::cref(loaded.font);
}

std::string FontCatalog::normalizeId(std::string_view value) {
    while (!value.empty() && AsciiText::isWhitespace(value.front()))
        value.remove_prefix(1);
    while (!value.empty() && AsciiText::isWhitespace(value.back()))
        value.remove_suffix(1);
    std::string out;
    out.reserve(value.size());
    bool previousDash = false;
    for (const char character: value) {
        if (AsciiText::isAlphaNumeric(character)) {
            out += AsciiText::toLower(character);
            previousDash = false;
        } else if (!previousDash && !out.empty()) {
            out.push_back('-');
            previousDash = true;
        }
    }
    if (!out.empty() && out.back() == '-')
        out.pop_back();
    return out;
}

std::expected<void, std::string> FontCatalog::validateFontFile(std::string_view path) {
    const std::string ownedPath{path};
    File file = Board::Storage::filesystem().open(ownedPath.c_str(), FILE_READ);
    if (!file || file.isDirectory()) {
        if (file)
            file.close();
        return std::unexpected("Font file unavailable");
    }
    auto directory = readDirectory(file);
    auto result = directory ? std::expected<void, std::string>{}
                            : std::expected<void, std::string>{std::unexpected(directory.error())};
    file.close();
    return result;
}
