#pragma once

#include <Arduino.h>
#include <FS.h>

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "ui/fonts/Font.h"
#include "fonts/RFont4Format.h"

class FontCatalog {
public:
    struct Family {
        String id;
        String label;
        std::array<String, RFont4::kSizeCount> paths;
        bool builtIn = false;
    };

    FontCatalog();
    FontCatalog(const FontCatalog&) = delete;
    FontCatalog& operator=(const FontCatalog&) = delete;

    void reset();
    void loadFromSd();

    uint8_t typefaceCount() const;
    const char* typefaceLabel(uint8_t index) const;
    String typefaceId(uint8_t index) const;
    bool hasSize(uint8_t typefaceIndex, uint8_t sizeIndex) const;
    bool indexForId(const String& id, uint8_t& index) const;

    ui::fonts::Font loadFont(uint8_t typefaceIndex, uint8_t sizeIndex);
    ui::fonts::Font currentFont() const;

    static constexpr uint8_t sizeCount() { return static_cast<uint8_t>(RFont4::kSizeCount); }
    static const char* sizeLabel(uint8_t index) { return RFont4::sizeLabel(index); }
    static ui::fonts::Font fallbackFont(uint8_t sizeIndex);
    static String normalizeId(const String& value);
    static bool validateFontFile(const String& path, String& error);

    const std::vector<Family>& families() const { return families_; }

private:
    class RuntimeFont {
    public:
        bool load(const String& path, String& error);
        void clear();
        const ui::fonts::AlphaFont* font() const;

    private:
        bool readHeader(File& file, RFont4::Header& header, String& error) const;
        bool readBytes(File& file, uint32_t offset, uint8_t* target, size_t bytes, String& error) const;
        bool loadRecords(File& file, const RFont4::Header& header, String& error);
        void rebuildFont(const RFont4::Header& header);

        String name_;
        std::vector<uint8_t> bitmap_;
        std::vector<ui::fonts::AlphaGlyph> glyphs_;
        std::vector<ui::fonts::AlphaRow> rows_;
        std::vector<ui::fonts::AlphaSpan> spans_;
        std::array<uint8_t, RFont4::kPageMapBytes> pageMap_ = {};
        std::vector<uint16_t> pageTableData_;
        std::vector<const uint16_t*> pageTablePointers_;
        std::vector<ui::fonts::AlphaKerningPair> kerningPairs_;
        ui::fonts::AlphaFont font_;
        bool valid_ = false;
    };

    size_t safeTypefaceIndex(uint8_t index) const;
    uint8_t safeSizeIndex(uint8_t index) const;
    bool loadRuntimeFont(const String& path);

    std::vector<Family> families_;
    RuntimeFont loaded_;
    uint8_t loadedTypefaceIndex_ = 0xFF;
    uint8_t loadedSizeIndex_ = 0xFF;
    String loadError_;
};
