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

    std::array<RFont4::StrikeRecord, RFont4::kSizeCount>
    readStrikes(const std::vector<uint8_t>& bytes, const RFont4::Header& header) {
        std::array<RFont4::StrikeRecord, RFont4::kSizeCount> strikes;
        std::memcpy(strikes.data(), bytes.data() + header.strikesOffset, sizeof(strikes));
        return strikes;
    }

    std::array<RFont4::LayoutTableRecord, RFont4::kMaximumLayoutTableCount>
    readTables(const std::vector<uint8_t>& bytes, const RFont4::Header& header) {
        std::array<RFont4::LayoutTableRecord, RFont4::kMaximumLayoutTableCount> tables{};
        std::memcpy(tables.data(), bytes.data() + header.layoutTablesOffset,
                    static_cast<size_t>(header.layoutTableCount) * sizeof(tables.front()));
        return tables;
    }

    void validateStrike(const std::vector<uint8_t>& bytes, const RFont4::StrikeRecord& strike,
                        uint32_t expectedScriptMask) {
        TEST_ASSERT_EQUAL_UINT32(RFont4::kPageMapBytes, strike.pageMapSize);
        TEST_ASSERT_TRUE(strike.pageTableCount > 0);
        uint32_t scriptMask = 0;
        uint32_t previousCodepoint = 0;
        for (uint32_t glyphIndex = 0; glyphIndex < strike.glyphCount; ++glyphIndex) {
            const auto glyph = recordAt<RFont4::GlyphRecord>(bytes, strike.glyphsOffset, glyphIndex);
            TEST_ASSERT_TRUE(glyphIndex == 0 || glyph.codepoint > previousCodepoint
                             || (glyph.codepoint == RFont4::kShapedGlyphCodepoint
                                 && previousCodepoint == RFont4::kShapedGlyphCodepoint));
            TEST_ASSERT_TRUE(static_cast<uint64_t>(glyph.bitmapOffset)
                                 + static_cast<uint64_t>(glyph.rowStride) * glyph.height
                             <= strike.bitmapSize);
            TEST_ASSERT_TRUE(static_cast<uint64_t>(glyph.kernOffset) + glyph.kernCount
                             <= strike.kerningPairCount);

            uint32_t previousRight = 0;
            for (uint32_t index = 0; index < glyph.kernCount; ++index) {
                const auto pair = recordAt<RFont4::KerningRecord>(bytes, strike.kerningOffset,
                                                                  glyph.kernOffset + index);
                TEST_ASSERT_TRUE(index == 0 || pair.rightCodepoint > previousRight);
                previousRight = pair.rightCodepoint;
            }
            if (glyph.codepoint <= UINT16_MAX && glyphIndex < UINT16_MAX) {
                const uint8_t page = bytes[strike.pageMapOffset + (glyph.codepoint >> 8U)];
                TEST_ASSERT_TRUE(page < strike.pageTableCount);
                const uint32_t entry = page * RFont4::kPageTableEntries + (glyph.codepoint & 0xFFU);
                TEST_ASSERT_EQUAL_UINT16(glyphIndex,
                                         recordAt<uint16_t>(bytes, strike.pageTablesOffset, entry));
            }
            scriptMask |= UnicodeText::scriptMask(glyph.codepoint);
            previousCodepoint = glyph.codepoint;
        }
        TEST_ASSERT_BITS_HIGH(expectedScriptMask & ~UnicodeText::ScriptMath, scriptMask);
        if ((expectedScriptMask & UnicodeText::ScriptMath) != 0)
            TEST_ASSERT_BITS_HIGH(UnicodeText::ScriptMath, scriptMask);

        uint32_t previousGlyphId = 0;
        for (uint32_t index = 0; index < strike.glyphIdCount; ++index) {
            const auto record = recordAt<RFont4::GlyphIdRecord>(bytes, strike.glyphIdsOffset, index);
            TEST_ASSERT_TRUE(record.glyphId > previousGlyphId);
            TEST_ASSERT_TRUE(record.glyphIndex < strike.glyphCount);
            previousGlyphId = record.glyphId;
        }
    }

    void validateFont(const std::filesystem::path& path) {
        const auto bytes = readFont(path);
        TEST_ASSERT_GREATER_OR_EQUAL(sizeof(RFont4::Header), bytes.size());
        const auto header = recordAt<RFont4::Header>(bytes, 0);
        const auto strikes = readStrikes(bytes, header);
        const auto tables = readTables(bytes, header);
        TEST_ASSERT_TRUE(RFont4::layoutValid(header, strikes,
                                             std::span{tables}.first(header.layoutTableCount), bytes.size()));
        TEST_ASSERT_EQUAL_UINT8('\0', bytes[header.nameOffset + header.nameSize - 1]);
        for (const auto& strike: strikes)
            validateStrike(bytes, strike, header.scriptMask);
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

    const auto bytes = readFont("fonts/Literata/font.rfont4");
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
}

void test_shaper_reuses_rfont4_nominal_glyphs_and_advances() {
    RFont4::Header header{.unitsPerEm = 1000, .sourceGlyphCount = 4};
    const std::array tableBytes{uint8_t{0}, uint8_t{0}, uint8_t{0}, uint8_t{0}};
    RFont4::LayoutTableRecord table{.tag = HB_TAG('G', 'D', 'E', 'F'), .size = tableBytes.size()};
    std::string bytes(tableBytes.size(), '\0');
    File file{std::move(bytes)};

    const std::array glyphs{
        ui::fonts::AlphaGlyph{.codepoint = 'A', .xAdvance = 7, .glyphId = 2},
        ui::fonts::AlphaGlyph{.codepoint = 'B', .xAdvance = 8, .glyphId = 3},
    };
    const std::array glyphIds{
        ui::fonts::AlphaGlyphId{2, 0},
        ui::fonts::AlphaGlyphId{3, 1},
    };
    const ui::fonts::AlphaFont font{
        .glyphs = glyphs.data(),
        .glyphCount = glyphs.size(),
        .yAdvance = 16,
        .glyphIds = glyphIds.data(),
        .glyphIdCount = glyphIds.size(),
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
    auto shaped = shaping.shape("xABy", 1, 2, false, "en", font.pixelsPerEm, renderer, output);
    TEST_ASSERT_TRUE_MESSAGE(shaped.has_value(), shaped ? "" : shaped.error().c_str());
    TEST_ASSERT_EQUAL_UINT32(2, output.size());
    TEST_ASSERT_EQUAL_UINT32(0, output[0].glyphIndex);
    TEST_ASSERT_EQUAL_UINT32(1, output[0].cluster);
    TEST_ASSERT_EQUAL_INT32(7, output[0].xAdvance);
    TEST_ASSERT_EQUAL_UINT32(1, output[1].glyphIndex);
    TEST_ASSERT_EQUAL_UINT32(2, output[1].cluster);
    TEST_ASSERT_EQUAL_INT32(8, output[1].xAdvance);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_all_rfont4_assets_are_fully_validated_off_device);
    RUN_TEST(test_shaper_reuses_rfont4_nominal_glyphs_and_advances);
    return UNITY_END();
}
