#include <unity.h>

#include <array>
#include <string>
#include <vector>

#include "fonts/UiFont6x9.h"
#include "fonts/FontCatalog.h"
#include "locales/LocaleCatalog.h"
#include "locales/LocalePack.h"
#include "text/LocaleTag.h"
#include "text/BidiText.h"
#include "text/UnicodeText.h"

namespace {

    constexpr std::string_view kHash = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";

    std::string manifest(std::string_view id = "ja", std::string_view locale = "ja") {
        return "schema_version = 2\n"
               "id = \""
             + std::string{id}
             + "\"\n"
               "version = \"1.0.0\"\n"
               "locale = \""
             + std::string{locale}
             + "\"\n"
               "native_name = \"Japanese\"\n"
               "english_name = \"Japanese\"\n"
               "direction = \"ltr\"\n"
               "scripts = [\"Jpan\"]\n"
               "unicode_version = \"17.0.0\"\n"
               "translation_status = \"preview\"\n"
               "minimum_firmware = \"1.0.0\"\n"
               "engine_abi = 1\n"
               "requires = [\"bidi\"]\n"
               "[ui.strings]\n"
               "path = \"ui/strings.bin\"\n"
               "bytes = 24\n"
               "sha256 = \""
             + std::string{kHash}
             + "\"\n"
               "license = \"MIT\"\n"
               "[ui.font]\n"
               "path = \"ui/font.u8g2\"\n"
               "bytes = 4096\n"
               "sha256 = \""
             + std::string{kHash}
             + "\"\n"
               "license = \"OFL-1.1\"\n";
    }

    std::vector<uint8_t> stringTable() {
        return {'R', 'S', 'L', '1', 1, 0, 2, 0, 5, 0, 0, 0, 0, 0, 0, 0, 5, 0, 0, 0, 5, 0, 0, 0,
                'H', 'e', 'l', 'l', 'o'};
    }

} // namespace

void setUp() {}
void tearDown() {}

void test_manifest_decodes_the_pack_contract() {
    auto decoded = locales::decodeManifest(manifest(), "ja");
    TEST_ASSERT_TRUE_MESSAGE(decoded.has_value(), decoded ? "" : decoded.error().c_str());
    TEST_ASSERT_EQUAL_STRING("ja", decoded->locale.c_str());
    TEST_ASSERT_TRUE(decoded->ui.has_value());
    TEST_ASSERT_EQUAL_UINT32(4096, decoded->ui->font->bytes);
}

void test_manifest_rejects_untrusted_paths_and_identity_mismatches() {
    auto wrongDirectory = locales::decodeManifest(manifest(), "zh-Hans");
    TEST_ASSERT_FALSE(wrongDirectory.has_value());

    std::string unsafe = manifest();
    unsafe.replace(unsafe.find("ui/font.u8g2"), std::string_view{"ui/font.u8g2"}.size(), "../font.u8g2");
    auto traversal = locales::decodeManifest(unsafe, "ja");
    TEST_ASSERT_FALSE(traversal.has_value());

    std::string unknownCapability = manifest();
    unknownCapability.replace(unknownCapability.find("requires = [\"bidi\"]"),
                              std::string_view{"requires = [\"bidi\"]"}.size(),
                              "requires = [\"unknown-engine\"]");
    TEST_ASSERT_FALSE(locales::decodeManifest(unknownCapability, "ja").has_value());
}

void test_manifest_rejects_reader_capabilities_in_locale_packs() {
    std::string reader = manifest() + "[reader]\nfonts = [\"noto-serif-cjk\"]\n";
    TEST_ASSERT_FALSE(locales::decodeManifest(reader, "ja").has_value());

    std::string oversized = manifest();
    oversized.replace(oversized.find("bytes = 4096"), std::string_view{"bytes = 4096"}.size(), "bytes = 65537");
    TEST_ASSERT_FALSE(locales::decodeManifest(oversized, "ja").has_value());

}

void test_locale_normalization_is_stable() {
    auto normalized = LocaleTag::normalize("ZH-hans-cn");
    TEST_ASSERT_TRUE(normalized.has_value());
    TEST_ASSERT_EQUAL_STRING("zh-Hans-CN", normalized->c_str());
    TEST_ASSERT_FALSE(LocaleTag::normalize("zh_Hans").has_value());
    TEST_ASSERT_TRUE(locales::isValidPackId("zh-Hans"));
    TEST_ASSERT_TRUE(locales::isValidPackId("math-symbols"));
    TEST_ASSERT_FALSE(locales::isValidPackId("../ja"));
    TEST_ASSERT_TRUE(locales::isValidPackFilePath("manifest.toml"));
    TEST_ASSERT_FALSE(locales::isValidPackFilePath("reader/small.rfont4"));
    TEST_ASSERT_FALSE(locales::isValidPackFilePath("reader/segmentation.bin"));
    TEST_ASSERT_FALSE(locales::isValidPackFilePath("reader/../secret"));
    TEST_ASSERT_FALSE(locales::isValidPackFilePath("notes.txt"));
}

void test_binary_ui_assets_are_bounded_before_runtime_use() {
    auto strings = locales::decodeStringTable(stringTable(), 2);
    TEST_ASSERT_TRUE_MESSAGE(strings.has_value(), strings ? "" : strings.error().c_str());
    TEST_ASSERT_EQUAL_STRING("Hello", std::string{strings->at(0)}.c_str());
    TEST_ASSERT_TRUE(strings->at(1).empty());

    auto corrupt = stringTable();
    corrupt[20] = 6;
    TEST_ASSERT_FALSE(locales::decodeStringTable(std::move(corrupt), 2).has_value());

    const std::span font{u8g2_font_rsvpnano_ui_6x9_tf};
    auto valid = locales::validateU8g2Font(font);
    TEST_ASSERT_TRUE_MESSAGE(valid.has_value(), valid ? "" : valid.error().c_str());
    locales::UiFont loaded{font.begin(), font.end()};
    TEST_ASSERT_EQUAL_UINT8(6, locales::uiFontCellWidth(loaded));
    TEST_ASSERT_EQUAL_UINT8(9, locales::uiFontHeight(loaded));
}

void test_reader_font_selection_uses_requested_then_font_affinity_then_terminal_fallback() {
    const std::vector<FontCatalog::Family> families{
        {.id = "literata", .label = "Literata", .builtIn = true, .scriptMask = UnicodeText::ScriptLatin},
        {.id = "latin", .label = "Latin", .scriptMask = UnicodeText::ScriptLatin},
        {.id = "cjk", .label = "CJK", .locales = std::string{"ja\0", 3}, .scriptMask = UnicodeText::ScriptHan},
        {.id = "cjk-sc", .label = "CJK SC", .locales = std::string{"zh-Hans\0", 8},
         .scriptMask = UnicodeText::ScriptHan},
        {.id = "math", .label = "STIX Two Math", .scriptMask = UnicodeText::ScriptMath},
    };
    TEST_ASSERT_EQUAL_UINT32(1, FontCatalog::selectFamily(families, "latin", "en", UnicodeText::ScriptLatin));
    TEST_ASSERT_EQUAL_UINT32(2, FontCatalog::selectFamily(families, "latin", "ja", UnicodeText::ScriptHan));
    TEST_ASSERT_EQUAL_UINT32(2, FontCatalog::selectFamily(
                                    families, "latin", "ja", UnicodeText::ScriptHan | UnicodeText::ScriptLatin));
    TEST_ASSERT_TRUE(families[2].usableFor("ja", UnicodeText::ScriptHan | UnicodeText::ScriptLatin));
    TEST_ASSERT_FALSE(families[2].usableFor("zh-Hans", UnicodeText::ScriptHan | UnicodeText::ScriptLatin));
    TEST_ASSERT_EQUAL_UINT32(3, FontCatalog::selectFamily(families, "latin", "zh-Hans", UnicodeText::ScriptHan));
    TEST_ASSERT_EQUAL_UINT32(4, FontCatalog::selectFamily(families, "literata", "en", UnicodeText::ScriptMath));
    TEST_ASSERT_EQUAL_UINT32(4, FontCatalog::selectFamily(
                                    families, "literata", "en", UnicodeText::ScriptLatin | UnicodeText::ScriptMath));
    TEST_ASSERT_EQUAL_UINT32(0, FontCatalog::selectFamily(families, "missing", "ar", UnicodeText::ScriptArabic));
}

void test_bidi_resolves_metadata_direction_visual_runs_and_mirroring() {
    BookMetadata metadata;
    metadata.baseDirection = BookDirection::ltr;
    metadata.textRuns = {{0, "en", BookDirection::automatic}, {3, "he", BookDirection::rtl},
                         {5, "en", BookDirection::automatic}};
    metadata.textRuns[0].scriptMask = UnicodeText::ScriptLatin;
    metadata.textRuns[1].scriptMask = UnicodeText::ScriptHebrew;
    metadata.textRuns[2].scriptMask = UnicodeText::ScriptLatin;
    TEST_ASSERT_EQUAL(BookDirection::ltr, metadata.directionAt(2));
    TEST_ASSERT_EQUAL(BookDirection::rtl, metadata.directionAt(3));
    TEST_ASSERT_EQUAL(BookDirection::ltr, metadata.directionAt(5));
    TEST_ASSERT_EQUAL_HEX32(UnicodeText::ScriptLatin, metadata.scriptMaskAt(2));
    TEST_ASSERT_EQUAL_HEX32(UnicodeText::ScriptHebrew, metadata.scriptMaskAt(3));

    const std::string_view text = "(\xD7\x90\xD7\x91\xD7\x92 123)";
    const std::array lines{BidiText::LineRange{0, text.size()}};
    BidiText::Analysis analysis;
    auto analyzed = analysis.reset(text, BookDirection::rtl);
    TEST_ASSERT_TRUE_MESSAGE(analyzed.has_value(), analyzed ? "" : analyzed.error().c_str());
    std::vector<BidiText::Line> resolved;
    auto result = analysis.resolve(lines, resolved);
    TEST_ASSERT_TRUE_MESSAGE(result.has_value(), result ? "" : result.error().c_str());
    TEST_ASSERT_TRUE(analysis.rightToLeft());
    TEST_ASSERT_EQUAL_UINT32(1, resolved.size());
    const auto& line = resolved.front();
    TEST_ASSERT_EQUAL_UINT32(3, line.size());
    TEST_ASSERT_TRUE(line.front().rightToLeft);
    TEST_ASSERT_FALSE(line[1].rightToLeft);
    TEST_ASSERT_TRUE(line.back().rightToLeft);
    TEST_ASSERT_GREATER_THAN(line.back().offset, line.front().offset);
    TEST_ASSERT_FALSE(analysis.uniformRightToLeft(0, text.size()).has_value());
    const auto hebrew = analysis.uniformRightToLeft(1, 6);
    const auto numbers = analysis.uniformRightToLeft(8, 3);
    TEST_ASSERT_TRUE(hebrew.has_value());
    TEST_ASSERT_TRUE(*hebrew);
    TEST_ASSERT_TRUE(numbers.has_value());
    TEST_ASSERT_FALSE(*numbers);

    std::vector<BidiText::Codepoint> visual;
    BidiText::visualCodepoints(text, line, visual);
    TEST_ASSERT_EQUAL_UINT32('(', visual.front().value);
    TEST_ASSERT_EQUAL_UINT32(')', visual.back().value);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_manifest_decodes_the_pack_contract);
    RUN_TEST(test_manifest_rejects_untrusted_paths_and_identity_mismatches);
    RUN_TEST(test_manifest_rejects_reader_capabilities_in_locale_packs);
    RUN_TEST(test_locale_normalization_is_stable);
    RUN_TEST(test_binary_ui_assets_are_bounded_before_runtime_use);
    RUN_TEST(test_reader_font_selection_uses_requested_then_font_affinity_then_terminal_fallback);
    RUN_TEST(test_bidi_resolves_metadata_direction_visual_runs_and_mirroring);
    return UNITY_END();
}
