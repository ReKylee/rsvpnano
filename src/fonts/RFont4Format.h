#pragma once

#include <Arduino.h>

#include <array>
#include <cstddef>
#include <cstdint>

namespace RFont4 {

    constexpr char kExtension[] = ".rfont4";
    constexpr uint32_t kMagic = 0x34544652UL; // "RFT4", little endian on disk.
    constexpr uint16_t kVersion = 1;
    constexpr size_t kPageMapBytes = 256;
    constexpr size_t kPageTableEntries = 256;
    constexpr size_t kSizeCount = 3;
    constexpr std::array<const char*, kSizeCount> kSizeIds = {"large", "medium", "small"};
    constexpr std::array<const char*, kSizeCount> kSizeLabels = {"Large", "Medium", "Small"};

    struct __attribute__((packed)) Header {
        uint32_t magic = kMagic;
        uint16_t version = kVersion;
        uint16_t headerSize = 0;
        uint16_t glyphRecordSize = 0;
        uint16_t rowRecordSize = 0;
        uint16_t spanRecordSize = 0;
        uint16_t kerningRecordSize = 0;
        uint16_t glyphCount = 0;
        uint16_t rowCount = 0;
        uint16_t spanCount = 0;
        uint16_t pageTableCount = 0;
        uint16_t kerningPairCount = 0;
        uint32_t bitmapSize = 0;
        uint32_t pageMapSize = kPageMapBytes;
        uint32_t pageTableSize = 0;
        uint16_t nameSize = 0;
        uint8_t yAdvance = 0;
        uint8_t ascent = 0;
        uint8_t descent = 0;
        int8_t wordInkTop = 0;
        int8_t wordInkBottom = -1;
        uint8_t maxGlyphWidth = 0;
        uint8_t maxGlyphHeight = 0;
        uint8_t maxRowSpanCount = 0;
        uint8_t reserved0 = 0;
        uint32_t nameOffset = 0;
        uint32_t bitmapOffset = 0;
        uint32_t glyphsOffset = 0;
        uint32_t rowsOffset = 0;
        uint32_t spansOffset = 0;
        uint32_t pageMapOffset = 0;
        uint32_t pageTablesOffset = 0;
        uint32_t kerningOffset = 0;
        uint32_t totalSize = 0;
        uint32_t checksum = 0;
    };

    struct __attribute__((packed)) GlyphRecord {
        uint16_t codepoint = 0;
        uint32_t bitmapOffset = 0;
        uint32_t rowOffset = 0;
        uint16_t kernOffset = 0;
        uint8_t width = 0;
        uint8_t height = 0;
        uint8_t rowStride = 0;
        uint8_t xAdvance = 0;
        int8_t xOffset = 0;
        int8_t yOffset = 0;
        uint8_t kernCount = 0;
    };

    struct __attribute__((packed)) RowRecord {
        uint32_t spanOffset = 0;
        uint8_t spanCount = 0;
    };

    struct __attribute__((packed)) SpanRecord {
        uint8_t x = 0;
        uint8_t width = 0;
    };

    struct __attribute__((packed)) KerningRecord {
        uint16_t rightCodepoint = 0;
        int8_t xAdjust = 0;
    };

    inline bool hasFontExtension(const String& path) {
        String lowered = path;
        lowered.toLowerCase();
        return lowered.endsWith(kExtension);
    }

    inline int sizeIndexForId(String id) {
        id.trim();
        id.toLowerCase();
        for (size_t i = 0; i < kSizeIds.size(); ++i) {
            if (id == kSizeIds[i]) {
                return static_cast<int>(i);
            }
        }
        return -1;
    }

    inline const char* sizeId(uint8_t index) {
        return index < kSizeIds.size() ? kSizeIds[index] : kSizeIds[0];
    }

    inline const char* sizeLabel(uint8_t index) {
        return index < kSizeLabels.size() ? kSizeLabels[index] : kSizeLabels[0];
    }

    inline String sizeFilename(uint8_t index) {
        return String(sizeId(index)) + kExtension;
    }

} // namespace RFont4
