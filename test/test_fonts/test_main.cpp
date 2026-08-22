#include <unity.h>

#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "fonts/AlphaFont.h"
#include "fonts/RFont4Format.h"
#include "text/TextShaping.h"
#include "text/UnicodeText.h"

namespace {

    std::vector<uint8_t> readFont(const std::filesystem::path& path) {
        std::ifstream file(path, std::ios::binary);
        return {std::istreambuf_iterator<char>{file}, {}};
    }

    template<typename Record>
    Record recordAt(const std::vector<uint8_t>& bytes, uint32_t offset, uint32_t index = 0) {
        Record record;
        std::memcpy(&record, bytes.data() + offset + static_cast<size_t>(index) * sizeof(record), sizeof(record));
        return record;
    }

    std::array<RFont4::StrikeRecord, RFont4::kSizeCount> readStrikes(const std::vector<uint8_t>& bytes,
                                                                     const RFont4::Header& header) {
        std::array<RFont4::StrikeRecord, RFont4::kSizeCount> strikes;
        std::memcpy(strikes.data(), bytes.data() + header.strikesOffset, sizeof(strikes));
        return strikes;
    }

    std::array<RFont4::LayoutTableRecord, RFont4::kMaximumLayoutTableCount> readTables(const std::vector<uint8_t>&
                                                                                           bytes,
                                                                                       const RFont4::Header& header) {
        std::array<RFont4::LayoutTableRecord, RFont4::kMaximumLayoutTableCount> tables{};
        std::memcpy(tables.data(), bytes.data() + header.layoutTablesOffset,
                    static_cast<size_t>(header.layoutTableCount) * sizeof(tables.front()));
        return tables;
    }

    uint32_t validateFamilyLookup(const std::vector<uint8_t>& bytes, const RFont4::Header& header) {
        uint32_t scriptMask = 0;
        uint32_t previousCodepoint = 0;
        for (uint32_t glyphIndex = 0; glyphIndex < header.glyphCount; ++glyphIndex) {
            const auto identity = recordAt<RFont4::GlyphIdentityRecord>(bytes, header.identitiesOffset, glyphIndex);
            TEST_ASSERT_TRUE(glyphIndex == 0 || identity.codepoint > previousCodepoint
                             || (identity.codepoint == RFont4::kShapedGlyphCodepoint
                                 && previousCodepoint == RFont4::kShapedGlyphCodepoint));
            if (identity.codepoint <= UINT16_MAX) {
                const uint8_t page = bytes[header.pageMapOffset + (identity.codepoint >> 8U)];
                TEST_ASSERT_TRUE(page < header.pageTableCount);
                const uint32_t entry = page * RFont4::kPageTableEntries + (identity.codepoint & 0xFFU);
                TEST_ASSERT_EQUAL_UINT16(glyphIndex, recordAt<uint16_t>(bytes, header.pageTablesOffset, entry));
            }
            scriptMask |= UnicodeText::scriptMask(identity.codepoint);
            previousCodepoint = identity.codepoint;
        }

        for (uint32_t glyphId = 0; glyphId < header.sourceGlyphCount; ++glyphId) {
            const uint16_t glyphIndex = recordAt<uint16_t>(bytes, header.glyphMapOffset, glyphId);
            if (glyphIndex == UINT16_MAX)
                continue;
            TEST_ASSERT_TRUE(glyphIndex < header.glyphCount);
            const auto identity = recordAt<RFont4::GlyphIdentityRecord>(bytes, header.identitiesOffset, glyphIndex);
            TEST_ASSERT_EQUAL_UINT16(glyphId, identity.glyphId);
        }
        return scriptMask;
    }

    void validateStrike(const std::vector<uint8_t>& bytes, const RFont4::StrikeRecord& strike, uint32_t glyphCount) {
        TEST_ASSERT_TRUE(strike.bitsPerPixel == 1 || strike.bitsPerPixel == 4);
        TEST_ASSERT_TRUE(strike.bitmapEncoding == RFont4::BitmapEncoding::raw
                         || strike.bitmapEncoding == RFont4::BitmapEncoding::lz4);
        for (uint32_t glyphIndex = 0; glyphIndex < glyphCount; ++glyphIndex) {
            const auto glyph = recordAt<RFont4::GlyphRecord>(bytes, strike.glyphsOffset, glyphIndex);
            const size_t decodedBytes = static_cast<size_t>(glyph.rowStride) * glyph.height;
            const size_t storedBytes = RFont4::bitmapBytes(glyph);
            TEST_ASSERT_TRUE(static_cast<uint64_t>(glyph.bitmapOffset) + storedBytes <= strike.bitmapSize);
            TEST_ASSERT_EQUAL_UINT8((glyph.width * strike.bitsPerPixel + 7U) / 8U, glyph.rowStride);
            if (RFont4::bitmapStoredRaw(strike, glyph)) {
                TEST_ASSERT_EQUAL_UINT32(decodedBytes, storedBytes);
            } else {
                std::vector<uint8_t> decoded(decodedBytes);
                const auto encoded =
                    std::span<const uint8_t>{bytes}.subspan(strike.bitmapOffset + glyph.bitmapOffset, storedBytes);
                TEST_ASSERT_TRUE(RFont4::decompressLz4Block(encoded, decoded));
            }
            TEST_ASSERT_TRUE(static_cast<uint64_t>(glyph.kernOffset) + glyph.kernCount <= strike.kerningPairCount);

            uint32_t previousRight = 0;
            for (uint32_t index = 0; index < glyph.kernCount; ++index) {
                const auto pair =
                    recordAt<RFont4::KerningRecord>(bytes, strike.kerningOffset, glyph.kernOffset + index);
                TEST_ASSERT_TRUE(index == 0 || pair.rightCodepoint > previousRight);
                previousRight = pair.rightCodepoint;
            }
        }
    }

    void validateFont(const std::filesystem::path& path) {
        const auto bytes = readFont(path);
        TEST_ASSERT_GREATER_OR_EQUAL(sizeof(RFont4::Header), bytes.size());
        const auto header = recordAt<RFont4::Header>(bytes, 0);
        const auto strikes = readStrikes(bytes, header);
        const auto tables = readTables(bytes, header);
        TEST_ASSERT_TRUE(RFont4::layoutValid(header, strikes, std::span{tables}.first(header.layoutTableCount),
                                             bytes.size()));
        TEST_ASSERT_EQUAL_UINT8('\0', bytes[header.nameOffset + header.nameSize - 1]);
        const uint32_t scriptMask = validateFamilyLookup(bytes, header);
        TEST_ASSERT_BITS_HIGH(header.scriptMask & ~UnicodeText::ScriptMath, scriptMask);
        if ((header.scriptMask & UnicodeText::ScriptMath) != 0)
            TEST_ASSERT_BITS_HIGH(UnicodeText::ScriptMath, scriptMask);
        for (size_t index = 0; index < strikes.size(); ++index) {
            TEST_ASSERT_EQUAL_UINT8(index == RFont4::kCompactStrikeIndex ? 1 : 4, strikes[index].bitsPerPixel);
            TEST_ASSERT_EQUAL_UINT8(static_cast<uint8_t>(index == RFont4::kCompactStrikeIndex
                                                             ? RFont4::BitmapEncoding::raw
                                                             : RFont4::BitmapEncoding::lz4),
                                    static_cast<uint8_t>(strikes[index].bitmapEncoding));
            validateStrike(bytes, strikes[index], header.glyphCount);
        }
    }

    void test_file_cache_coalesces_small_reads_into_blocks() {
        std::string bytes(2048, '\0');
        for (size_t index = 0; index < bytes.size(); ++index)
            bytes[index] = static_cast<char>(index);
        File file{bytes};
        ui::fonts::RFontFileCache cache;
        std::array<uint8_t, 600> output{};

        TEST_ASSERT_TRUE(cache.read(file, bytes.size(), 100, output.data(), 24));
        TEST_ASSERT_EQUAL_MEMORY(bytes.data() + 100, output.data(), 24);
        TEST_ASSERT_TRUE(cache.read(file, bytes.size(), 200, output.data(), 24));
        TEST_ASSERT_TRUE(cache.read(file, bytes.size(), 600, output.data(), 24));
        TEST_ASSERT_TRUE(cache.read(file, bytes.size(), 120, output.data(), output.size()));
        TEST_ASSERT_EQUAL_MEMORY(bytes.data() + 120, output.data(), output.size());
        TEST_ASSERT_EQUAL_UINT32(1, file.readCount());

        File other{std::string(2048, static_cast<char>(0xA5))};
        TEST_ASSERT_TRUE(cache.read(other, 2048, 100, output.data(), 24));
        TEST_ASSERT_EQUAL_MEMORY(std::string(24, static_cast<char>(0xA5)).data(), output.data(), 24);
        TEST_ASSERT_EQUAL_UINT32(1, other.readCount());
    }

    void test_file_cache_prefetch_respects_the_read_budget() {
        constexpr size_t blockSize = ui::fonts::RFontFileCache::kBlockSize;
        File file{std::string(blockSize * 3, static_cast<char>(0x5A))};
        ui::fonts::RFontFileCache cache;
        size_t remainingReads = 1;

        TEST_ASSERT_TRUE(cache.prefetch(file, file.size(), 100, blockSize * 2, remainingReads));
        TEST_ASSERT_EQUAL_UINT32(1, file.readCount());
        TEST_ASSERT_EQUAL_UINT32(0, remainingReads);

        uint8_t byte = 0;
        TEST_ASSERT_TRUE(cache.read(file, file.size(), 100, &byte, 1));
        TEST_ASSERT_EQUAL_HEX8(0x5A, byte);
        TEST_ASSERT_EQUAL_UINT32(1, file.readCount());
        TEST_ASSERT_TRUE(cache.read(file, file.size(), blockSize + 100, &byte, 1));
        TEST_ASSERT_EQUAL_UINT32(2, file.readCount());
    }

    void test_resident_metrics_avoid_file_reads_until_bitmap_rendering() {
        const std::array glyphs{
            ui::fonts::AlphaGlyph{.width = 3, .height = 1, .rowStride = 1, .xAdvance = 4, .bitmapBytes = 1},
        };
        const std::array identities{ui::fonts::AlphaGlyphIdentity{'x', 7}};
        std::array<uint16_t, 8> glyphMap;
        glyphMap.fill(UINT16_MAX);
        glyphMap[7] = 0;
        std::array<uint8_t, RFont4::kPageMapBytes> pageMap;
        pageMap.fill(UINT8_MAX);
        pageMap[0] = 0;
        std::array<uint8_t, RFont4::kPageTableEntries * sizeof(uint16_t)> pageTable;
        pageTable.fill(UINT8_MAX);
        const uint16_t glyphIndex = 0;
        std::memcpy(pageTable.data() + static_cast<size_t>('x') * sizeof(glyphIndex), &glyphIndex, sizeof(glyphIndex));
        File file{std::string(1, static_cast<char>(0xA0))};
        const ui::fonts::AlphaFont font{
            .glyphs = glyphs.data(),
            .identities = identities.data(),
            .glyphCount = glyphs.size(),
            .yAdvance = 2,
            .ascent = 1,
            .pageMap = pageMap.data(),
            .pageTableCount = 1,
            .glyphMap = reinterpret_cast<const uint8_t*>(glyphMap.data()),
            .glyphMapCount = glyphMap.size(),
            .pixelsPerEm = 2,
            .file = &file,
            .fileSize = 1,
            .fileStrike = {.bitmapSize = 1},
            .bitsPerPixel = 1,
            .pageTableData = pageTable.data(),
        };
        Arduino_GFX gfx{8, 4};
        ui::fonts::AlphaTextRenderer<8> renderer{gfx};
        TEST_ASSERT_TRUE(renderer.begin());
        renderer.setFont(font);

        uint32_t resolvedGlyphId = 0;
        uint16_t resolvedGlyphIndex = UINT16_MAX;
        TEST_ASSERT_TRUE(renderer.nominalGlyph('x', resolvedGlyphId));
        TEST_ASSERT_EQUAL_UINT32(7, resolvedGlyphId);
        TEST_ASSERT_TRUE(renderer.resolveGlyphId(7, resolvedGlyphIndex));
        TEST_ASSERT_EQUAL_UINT16(0, resolvedGlyphIndex);
        TEST_ASSERT_EQUAL_UINT32(0, file.readCount());
        TEST_ASSERT_EQUAL_INT16(4, renderer.drawCodepoint('x', 0, 1));
        TEST_ASSERT_EQUAL_UINT32(1, file.readCount());
        TEST_ASSERT_EQUAL_INT16(4, renderer.drawString("x", 0, 1));
        TEST_ASSERT_EQUAL_INT16(4, renderer.drawString("x", 0, 1));
        TEST_ASSERT_EQUAL_UINT32(2, file.readCount());
    }

    void test_lz4_block_decoder_checks_bounds_and_overlap() {
        constexpr std::array<uint8_t, 6> encoded{0x32, 'a', 'b', 'c', 0x03, 0x00};
        std::array<uint8_t, 9> decoded{};
        TEST_ASSERT_TRUE(RFont4::decompressLz4Block(encoded, decoded));
        TEST_ASSERT_EQUAL_STRING_LEN("abcabcabc", reinterpret_cast<const char*>(decoded.data()), decoded.size());

        constexpr std::array<uint8_t, 3> invalidOffset{0x00, 0x00, 0x00};
        TEST_ASSERT_FALSE(RFont4::decompressLz4Block(invalidOffset, decoded));
        TEST_ASSERT_FALSE(RFont4::decompressLz4Block(encoded, std::span<uint8_t>{decoded}.first(8)));
    }

    void test_oversized_compressed_run_reads_each_glyph_once() {
        constexpr size_t glyphCount = 9;
        constexpr size_t glyphBytes = 1024;
        constexpr std::array<uint8_t, 8> encoded{0x1F, 0xFF, 0x01, 0x00, 0xFF, 0xFF, 0xFF, 0xEF};
        std::string fileBytes;
        fileBytes.reserve(glyphCount * encoded.size());
        std::array<ui::fonts::AlphaGlyph, glyphCount> glyphs{};
        std::array<ui::fonts::AlphaGlyphIdentity, glyphCount> identities{};
        std::array<uint8_t, RFont4::kPageMapBytes> pageMap;
        std::array<uint8_t, RFont4::kPageTableEntries * sizeof(uint16_t)> pageTable;
        pageMap.fill(UINT8_MAX);
        pageMap[0] = 0;
        pageTable.fill(UINT8_MAX);
        std::string text;
        for (size_t index = 0; index < glyphCount; ++index) {
            fileBytes.append(reinterpret_cast<const char*>(encoded.data()), encoded.size());
            glyphs[index] = {.bitmapOffset = static_cast<uint32_t>(index * encoded.size()),
                             .width = 32,
                             .height = 64,
                             .rowStride = 16,
                             .xAdvance = 32,
                             .bitmapBytes = encoded.size()};
            const uint32_t codepoint = static_cast<uint32_t>('A' + index);
            identities[index] = {codepoint, static_cast<uint16_t>(index)};
            const uint16_t glyphIndex = static_cast<uint16_t>(index);
            std::memcpy(pageTable.data() + codepoint * sizeof(glyphIndex), &glyphIndex, sizeof(glyphIndex));
            text.push_back(static_cast<char>(codepoint));
        }

        File file{std::move(fileBytes)};
        const ui::fonts::AlphaFont font{
            .glyphs = glyphs.data(),
            .identities = identities.data(),
            .glyphCount = glyphs.size(),
            .yAdvance = 64,
            .ascent = 64,
            .pageMap = pageMap.data(),
            .pageTableCount = 1,
            .pixelsPerEm = 64,
            .file = &file,
            .fileSize = static_cast<uint32_t>(file.size()),
            .fileStrike = {.bitmapEncoding = RFont4::BitmapEncoding::lz4,
                           .bitmapSize = static_cast<uint32_t>(file.size())},
            .bitsPerPixel = 4,
            .bitmapEncoding = RFont4::BitmapEncoding::lz4,
            .pageTableData = pageTable.data(),
        };
        Arduino_GFX gfx{320, 80};
        ui::fonts::AlphaTextRenderer<320> renderer{gfx};
        TEST_ASSERT_TRUE(renderer.begin());
        renderer.setFont(font);
        renderer.setTextColor(0xFFFF, 0);

        TEST_ASSERT_EQUAL_INT16(glyphCount * 32, renderer.drawString(text, 0, 64));
        TEST_ASSERT_EQUAL_UINT32(glyphCount, file.readCount());

        Arduino_GFX clippedGfx{64, 80};
        ui::fonts::AlphaTextRenderer<64> clippedRenderer{clippedGfx};
        TEST_ASSERT_TRUE(clippedRenderer.begin());
        clippedRenderer.setFont(font);
        clippedRenderer.setTextColor(0xFFFF, 0);
        TEST_ASSERT_EQUAL_INT16(glyphCount * 32, clippedRenderer.drawString(text, -224, 64));
        TEST_ASSERT_EQUAL_UINT32(glyphCount + 2, file.readCount());
    }

} // namespace

void setUp() {}
void tearDown() {}

void test_all_rfont4_assets_are_fully_validated_off_device() {
    size_t count = 0;
    for (const auto& entry: std::filesystem::recursive_directory_iterator{"fonts"}) {
        if (entry.path().extension() != RFont4::kExtension)
            continue;
        validateFont(entry.path());
        ++count;
    }
    TEST_ASSERT_TRUE(count > 0);

    const auto bytes = readFont("fonts/Andika/font.rfont4");
    const auto header = recordAt<RFont4::Header>(bytes, 0);
    auto strikes = readStrikes(bytes, header);
    const auto tables = readTables(bytes, header);
    const auto tableSpan = std::span{tables}.first(header.layoutTableCount);
    TEST_ASSERT_FALSE(RFont4::layoutValid(header, strikes, tableSpan, bytes.size() - 1));
    ++strikes.front().bitmapOffset;
    TEST_ASSERT_FALSE(RFont4::layoutValid(header, strikes, tableSpan, bytes.size()));
    auto unknownScript = header;
    unknownScript.scriptMask |= 1UL << 31U;
    TEST_ASSERT_FALSE(RFont4::layoutValid(unknownScript, strikes, tableSpan, bytes.size()));

    const auto shapedBytes = readFont("fonts/Noto Serif Hebrew/font.rfont4");
    const auto shapedHeader = recordAt<RFont4::Header>(shapedBytes, 0);
    const auto shapedStrikes = readStrikes(shapedBytes, shapedHeader);
    auto shapedTables = readTables(shapedBytes, shapedHeader);
    const auto shapedTableSpan = std::span{shapedTables}.first(shapedHeader.layoutTableCount);
    TEST_ASSERT_GREATER_THAN(1, shapedTableSpan.size());
    shapedTables.front().tag = 0;
    TEST_ASSERT_FALSE(RFont4::layoutValid(shapedHeader, shapedStrikes, shapedTableSpan, shapedBytes.size()));
    shapedTables = readTables(shapedBytes, shapedHeader);
    shapedTables[1].tag = shapedTables[0].tag;
    TEST_ASSERT_FALSE(RFont4::layoutValid(shapedHeader, shapedStrikes, shapedTableSpan, shapedBytes.size()));
}

void test_shaper_reuses_rfont4_nominal_glyphs_and_advances() {
    RFont4::Header header{.unitsPerEm = 1000, .sourceGlyphCount = 4};
    const std::array tableBytes{uint8_t{0}, uint8_t{0}, uint8_t{0}, uint8_t{0}};
    RFont4::LayoutTableRecord table{.tag = HB_TAG('G', 'D', 'E', 'F'), .size = tableBytes.size()};
    std::string bytes(tableBytes.size(), '\0');
    File file{std::move(bytes)};

    const std::array glyphs{
        ui::fonts::AlphaGlyph{.xAdvance = 7},
        ui::fonts::AlphaGlyph{.xAdvance = 8},
    };
    const std::array identities{
        ui::fonts::AlphaGlyphIdentity{'A', 2},
        ui::fonts::AlphaGlyphIdentity{'B', 3},
    };
    const std::array<uint16_t, 4> glyphMap{UINT16_MAX, UINT16_MAX, 0, 1};
    const ui::fonts::AlphaFont font{
        .glyphs = glyphs.data(),
        .identities = identities.data(),
        .glyphCount = glyphs.size(),
        .yAdvance = 16,
        .glyphMap = reinterpret_cast<const uint8_t*>(glyphMap.data()),
        .glyphMapCount = glyphMap.size(),
        .pixelsPerEm = 16,
    };
    Arduino_GFX gfx;
    ui::fonts::AlphaTextRenderer<640> renderer{gfx};
    TEST_ASSERT_TRUE(renderer.begin());
    renderer.setFont(font);

    TextShaping::Shaper shaping;
    auto opened = shaping.open(file, header, std::span{&table, 1});
    TEST_ASSERT_TRUE_MESSAGE(opened.has_value(), opened ? "" : opened.error().c_str());
    std::vector<ui::fonts::PositionedGlyph> output;
    auto shaped = shaping.shape("xABy", 1, 2, false, "en", renderer, output);
    TEST_ASSERT_TRUE_MESSAGE(shaped.has_value(), shaped ? "" : shaped.error().c_str());
    TEST_ASSERT_EQUAL_INT16(15, *shaped);
    TEST_ASSERT_EQUAL_UINT32(2, output.size());
    TEST_ASSERT_EQUAL_UINT32(0, output[0].glyphIndex);
    TEST_ASSERT_EQUAL_UINT32(1, output[0].cluster);
    TEST_ASSERT_EQUAL_INT32(7, output[0].xAdvance);
    TEST_ASSERT_EQUAL_UINT32(1, output[1].glyphIndex);
    TEST_ASSERT_EQUAL_UINT32(2, output[1].cluster);
    TEST_ASSERT_EQUAL_INT32(8, output[1].xAdvance);

    shaped = shaping.shape("xABy", 1, 2, false, "en", renderer, output);
    TEST_ASSERT_TRUE_MESSAGE(shaped.has_value(), shaped ? "" : shaped.error().c_str());
    TEST_ASSERT_EQUAL_INT16(15, *shaped);
    TEST_ASSERT_EQUAL_UINT32(4, output.size());
}

void test_compact_strike_renders_one_bit_rows() {
    constexpr uint8_t bitmap[]{0xA0};
    constexpr ui::fonts::AlphaGlyph glyphs[]{
        {.width = 3, .height = 1, .rowStride = 1, .xAdvance = 4},
    };
    constexpr ui::fonts::AlphaGlyphIdentity identities[]{{'x', 0}};
    const ui::fonts::AlphaFont font{
        .bitmap = bitmap,
        .glyphs = glyphs,
        .identities = identities,
        .glyphCount = 1,
        .yAdvance = 2,
        .ascent = 1,
        .pixelsPerEm = 2,
        .bitsPerPixel = 1,
    };
    Arduino_GFX gfx{8, 4};
    ui::fonts::AlphaTextRenderer<8> renderer{gfx};
    TEST_ASSERT_TRUE(renderer.begin());
    renderer.setFont(font);
    renderer.setTextColor(0xFFFF, 0);

    TEST_ASSERT_EQUAL_INT16(4, renderer.drawCodepoint('x', 0, 1));
    TEST_ASSERT_EQUAL(2, gfx.writes);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_file_cache_coalesces_small_reads_into_blocks);
    RUN_TEST(test_file_cache_prefetch_respects_the_read_budget);
    RUN_TEST(test_resident_metrics_avoid_file_reads_until_bitmap_rendering);
    RUN_TEST(test_lz4_block_decoder_checks_bounds_and_overlap);
    RUN_TEST(test_oversized_compressed_run_reads_each_glyph_once);
    RUN_TEST(test_all_rfont4_assets_are_fully_validated_off_device);
    RUN_TEST(test_shaper_reuses_rfont4_nominal_glyphs_and_advances);
    RUN_TEST(test_compact_strike_renders_one_bit_rows);
    return UNITY_END();
}
