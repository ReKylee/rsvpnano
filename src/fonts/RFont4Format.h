#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

#include "text/AsciiText.h"
#include "text/UnicodeText.h"

namespace RFont4 {

    constexpr char kExtension[] = ".rfont4";
    constexpr char kFilename[] = "font.rfont4";
    constexpr uint32_t kMagic = 0x34544652UL; // "RFT4", little endian on disk.
    constexpr uint16_t kVersion = 4;
    constexpr size_t kPageMapBytes = 256;
    constexpr size_t kPageTableEntries = 256;
    constexpr size_t kSizeCount = 3;
    constexpr size_t kMaximumLayoutTableCount = 3;
    constexpr uint32_t kShapedGlyphCodepoint = UINT32_MAX;
    constexpr std::array<const char*, kSizeCount> kSizeIds = {"large", "medium", "small"};
    constexpr std::array<const char*, kSizeCount> kSizeLabels = {"Large", "Medium", "Small"};
    constexpr uint32_t kKnownScriptMask = UnicodeText::ScriptLatin | UnicodeText::ScriptCyrillic
                                        | UnicodeText::ScriptGreek | UnicodeText::ScriptHebrew
                                        | UnicodeText::ScriptArabic | UnicodeText::ScriptHan
                                        | UnicodeText::ScriptHiragana | UnicodeText::ScriptKatakana
                                        | UnicodeText::ScriptHangul | UnicodeText::ScriptMath;

    struct __attribute__((packed)) Header {
        uint32_t magic = kMagic;
        uint16_t version = kVersion;
        uint16_t headerSize = 0;
        uint16_t strikeRecordSize = 0;
        uint16_t glyphRecordSize = 0;
        uint16_t kerningRecordSize = 0;
        uint16_t glyphIdRecordSize = 0;
        uint16_t layoutTableRecordSize = 0;
        uint8_t strikeCount = 0;
        uint8_t layoutTableCount = 0;
        uint16_t nameSize = 0;
        uint16_t unitsPerEm = 0;
        uint16_t localeSize = 0;
        uint32_t sourceGlyphCount = 0;
        uint32_t scriptMask = 0;
        uint32_t nameOffset = 0;
        uint32_t localeOffset = 0;
        uint32_t strikesOffset = 0;
        uint32_t layoutTablesOffset = 0;
        uint32_t totalSize = 0;
    };

    struct __attribute__((packed)) StrikeRecord {
        uint32_t glyphCount = 0;
        uint32_t kerningPairCount = 0;
        uint32_t glyphIdCount = 0;
        uint16_t pageTableCount = 0;
        uint8_t yAdvance = 0;
        uint8_t ascent = 0;
        uint8_t descent = 0;
        int8_t wordInkTop = 0;
        int8_t wordInkBottom = -1;
        uint8_t maxGlyphWidth = 0;
        uint8_t maxGlyphHeight = 0;
        uint8_t pixelsPerEm = 0;
        uint16_t reserved = 0;
        uint32_t bitmapSize = 0;
        uint32_t pageMapSize = kPageMapBytes;
        uint32_t pageTableSize = 0;
        uint32_t bitmapOffset = 0;
        uint32_t glyphsOffset = 0;
        uint32_t pageMapOffset = 0;
        uint32_t pageTablesOffset = 0;
        uint32_t kerningOffset = 0;
        uint32_t glyphIdsOffset = 0;
    };

    struct __attribute__((packed)) GlyphRecord {
        uint32_t codepoint = 0;
        uint32_t bitmapOffset = 0;
        uint32_t kernOffset = 0;
        uint8_t width = 0;
        uint8_t height = 0;
        uint8_t rowStride = 0;
        uint8_t xAdvance = 0;
        int8_t xOffset = 0;
        int8_t yOffset = 0;
        uint16_t kernCount = 0;
        uint32_t glyphId = 0;
    };

    struct __attribute__((packed)) KerningRecord {
        uint32_t rightCodepoint = 0;
        int8_t xAdjust = 0;
    };

    struct __attribute__((packed)) GlyphIdRecord {
        uint32_t glyphId = 0;
        uint32_t glyphIndex = 0;
    };

    struct __attribute__((packed)) LayoutTableRecord {
        uint32_t tag = 0;
        uint32_t offset = 0;
        uint32_t size = 0;
    };

    struct Directory {
        Header header;
        std::array<StrikeRecord, kSizeCount> strikes{};
        std::array<LayoutTableRecord, kMaximumLayoutTableCount> layoutTables{};
    };

    static_assert(sizeof(Header) == 54);
    static_assert(sizeof(StrikeRecord) == 60);
    static_assert(sizeof(GlyphRecord) == 24);
    static_assert(sizeof(KerningRecord) == 5);
    static_assert(sizeof(GlyphIdRecord) == 8);
    static_assert(sizeof(LayoutTableRecord) == 12);

    constexpr bool headerValid(const Header& header, size_t fileSize) {
        const bool hasShaping = header.layoutTableCount != 0;
        return header.magic == kMagic && header.version == kVersion && header.headerSize == sizeof(Header)
            && header.strikeRecordSize == sizeof(StrikeRecord) && header.glyphRecordSize == sizeof(GlyphRecord)
            && header.kerningRecordSize == sizeof(KerningRecord)
            && header.glyphIdRecordSize == sizeof(GlyphIdRecord)
            && header.layoutTableRecordSize == sizeof(LayoutTableRecord) && header.strikeCount == kSizeCount
            && header.layoutTableCount <= kMaximumLayoutTableCount && header.nameSize > 1
            && (header.scriptMask & ~kKnownScriptMask) == 0 && header.nameOffset == sizeof(Header)
            && header.localeOffset == header.nameOffset + header.nameSize
            && header.strikesOffset == header.localeOffset + header.localeSize
            && header.layoutTablesOffset >= header.strikesOffset + kSizeCount * sizeof(StrikeRecord)
            && header.totalSize == fileSize
            && (hasShaping ? header.unitsPerEm > 0 && header.sourceGlyphCount > 0
                           : header.unitsPerEm == 0 && header.sourceGlyphCount == 0);
    }

    inline bool layoutValid(const Header& header, std::span<const StrikeRecord, kSizeCount> strikes,
                            std::span<const LayoutTableRecord> tables, size_t fileSize) {
        if (!headerValid(header, fileSize) || tables.size() != header.layoutTableCount)
            return false;

        uint64_t cursor = header.strikesOffset + strikes.size_bytes();
        const auto section = [&cursor](uint32_t offset, uint64_t bytes) {
            if (offset != cursor || bytes > UINT32_MAX - cursor)
                return false;
            cursor += bytes;
            return true;
        };
        for (const StrikeRecord& strike: strikes) {
            const bool validPageTables = strike.pageMapSize == kPageMapBytes
                                      && strike.pageTableCount <= UINT8_MAX
                                      && strike.pageTableSize
                                             == static_cast<uint32_t>(strike.pageTableCount * kPageTableEntries
                                                                      * sizeof(uint16_t));
            if (strike.glyphCount == 0 || strike.yAdvance == 0 || strike.pixelsPerEm == 0
                || strike.glyphIdCount > strike.glyphCount || !validPageTables
                || !section(strike.bitmapOffset, strike.bitmapSize)
                || !section(strike.glyphsOffset,
                            static_cast<uint64_t>(strike.glyphCount) * sizeof(GlyphRecord))
                || !section(strike.pageMapOffset, strike.pageMapSize)
                || !section(strike.pageTablesOffset, strike.pageTableSize)
                || !section(strike.kerningOffset,
                            static_cast<uint64_t>(strike.kerningPairCount) * sizeof(KerningRecord))
                || !section(strike.glyphIdsOffset,
                            static_cast<uint64_t>(strike.glyphIdCount) * sizeof(GlyphIdRecord)))
                return false;
        }
        if (header.layoutTablesOffset != cursor)
            return false;
        cursor += tables.size_bytes();
        for (const LayoutTableRecord& table: tables) {
            cursor = (cursor + 3U) & ~uint64_t{3U};
            if (table.size == 0 || !section(table.offset, table.size))
                return false;
        }
        return cursor == header.totalSize;
    }

    inline bool equalIgnoreCase(std::string_view left, std::string_view right) {
        return std::ranges::equal(left, right, [](unsigned char a, unsigned char b) {
            return AsciiText::toLower(static_cast<char>(a)) == AsciiText::toLower(static_cast<char>(b));
        });
    }

    inline bool hasFontExtension(std::string_view path) {
        const std::string_view extension{kExtension};
        return path.size() >= extension.size()
            && equalIgnoreCase(path.substr(path.size() - extension.size()), extension);
    }

    inline size_t sizeIndexForId(std::string_view id) {
        while (!id.empty() && AsciiText::isWhitespace(id.front()))
            id.remove_prefix(1);
        while (!id.empty() && AsciiText::isWhitespace(id.back()))
            id.remove_suffix(1);
        const auto size = std::ranges::find_if(kSizeIds, [id](const char* candidate) {
            return equalIgnoreCase(id, candidate);
        });
        return static_cast<size_t>(std::distance(kSizeIds.begin(), size));
    }

    inline const char* sizeId(size_t index) {
        return index < kSizeIds.size() ? kSizeIds[index] : kSizeIds[0];
    }

    inline const char* sizeLabel(size_t index) {
        return index < kSizeLabels.size() ? kSizeLabels[index] : kSizeLabels[0];
    }

} // namespace RFont4
