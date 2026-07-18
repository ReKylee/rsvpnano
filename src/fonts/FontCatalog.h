#pragma once

#include <Arduino.h>
#include <FS.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "fonts/AlphaFont.h"
#include "fonts/RFont4Format.h"

class FontCatalog {
public:
    struct Family {
        std::string id;
        std::string label;
        std::array<std::string, RFont4::kSizeCount> paths;
        bool builtIn = false;
    };

    FontCatalog();

    void loadFromSd();
    std::span<const Family> families() const {
        return families_;
    }
    const Family* find(std::string_view id) const;
    const ui::fonts::AlphaFont* load(size_t familyIndex, size_t sizeIndex);

    static std::expected<void, std::string> validateFontFile(const String& path);

private:
    void reset();
    std::expected<void, std::string> loadRuntimeFont(const std::string& path);
    void clearRuntimeFont();
    std::expected<void, std::string> loadRecords(File& file, const RFont4::Header& header);
    static std::string normalizeId(std::string_view value);

    std::vector<Family> families_;
    std::string runtimeName_;
    std::vector<uint8_t> bitmap_;
    std::vector<ui::fonts::AlphaGlyph> glyphs_;
    std::vector<ui::fonts::AlphaRow> rows_;
    std::vector<ui::fonts::AlphaSpan> spans_;
    std::array<uint8_t, RFont4::kPageMapBytes> pageMap_ = {};
    std::vector<uint16_t> pageTableData_;
    std::vector<const uint16_t*> pageTablePointers_;
    std::vector<ui::fonts::AlphaKerningPair> kerningPairs_;
    ui::fonts::AlphaFont runtimeFont_;
    size_t loadedFamilyIndex_ = RFont4::kSizeCount;
    size_t loadedSizeIndex_ = RFont4::kSizeCount;
};
