#include <unity.h>

#include <array>
#include <cstring>
#include <fstream>
#include <iterator>
#include <span>
#include <string>
#include <vector>

#include "fonts/AlphaFont.h"
#include "fonts/UiFont6x9.h"
#include "locales/LocalePack.h"
#include "reader/ReadingLoop.h"
#include "settings/SettingsRules.h"
#include "text/UnicodeText.h"
#include "text/Utf8Text.h"
#include "ui/Localization.h"
#include "ui/Ui.h"
#include "ui/screens/PageReaderScreen.h"

namespace {


    ui::TouchContact gContact;
    bool gTouchReadSucceeds;

    ui::TouchSampleResult pollTouch(ui::TouchContact& contact) {
        if (!gTouchReadSucceeds)
            return ui::TouchSampleResult::None;
        contact = gContact;
        return ui::TouchSampleResult::Contact;
    }
    void enableTouch(ui::Context& context) {
        context.setTouchSource({.surface = {320, 172}, .poll = &pollTouch});
    }

    ui::themes::Theme theme() {
        return ui::themes::defaultTheme();
    }

    class FontRecordingGfx final : public Arduino_GFX {
    public:
        using Arduino_GFX::Arduino_GFX;

        void setFont(const uint8_t* font) override {
            lastFont = font;
            ++fontSelections;
        }

        size_t write(uint8_t byte) override {
            text.push_back(byte);
            return Arduino_GFX::write(byte);
        }

        const uint8_t* externalFont = nullptr;
        const uint8_t* lastFont = nullptr;
        int fontSelections = 0;
        std::vector<uint8_t> text;
    };

    void appendLe16(std::vector<uint8_t>& out, uint16_t value) {
        out.push_back(static_cast<uint8_t>(value));
        out.push_back(static_cast<uint8_t>(value >> 8U));
    }

    void appendLe32(std::vector<uint8_t>& out, uint32_t value) {
        out.push_back(static_cast<uint8_t>(value));
        out.push_back(static_cast<uint8_t>(value >> 8U));
        out.push_back(static_cast<uint8_t>(value >> 16U));
        out.push_back(static_cast<uint8_t>(value >> 24U));
    }

    locales::StringTable translatedChapters() {
        constexpr std::string_view translation = "Capitulos externos";
        const size_t count = static_cast<size_t>(UiText::Count);
        std::vector<uint8_t> bytes{'R', 'S', 'L', '1'};
        appendLe16(bytes, 1);
        appendLe16(bytes, static_cast<uint16_t>(count));
        appendLe32(bytes, translation.size());
        uint32_t offset = 0;
        for (size_t index = 0; index <= count; ++index) {
            appendLe32(bytes, offset);
            if (index == static_cast<size_t>(UiText::Chapters))
                offset += translation.size();
        }
        bytes.insert(bytes.end(), translation.begin(), translation.end());
        return *locales::decodeStringTable(std::move(bytes), count);
    }

    constexpr uint8_t kReaderBitmap[]{0xF0};
    constexpr ui::fonts::AlphaGlyph kReaderGlyphs[]{
        {' ', 0, 0, 0, 0, 0, 3, 0, 0, 0},
        {'?', 0, 0, 1, 1, 1, 2, 0, -1, 0},
        {'a', 0, 0, 1, 1, 1, 4, 0, -1, 0, 17},
    };
    constexpr ui::fonts::AlphaGlyphId kReaderGlyphIds[]{{17, 2}};
    constexpr ui::fonts::AlphaFont kReaderFont{
        .name = "test-reader",
        .bitmap = kReaderBitmap,
        .glyphs = kReaderGlyphs,
        .glyphCount = std::size(kReaderGlyphs),
        .yAdvance = 9,
        .ascent = 7,
        .descent = 2,
        .wordInkTop = -1,
        .wordInkBottom = -1,
        .glyphIds = kReaderGlyphIds,
        .glyphIdCount = std::size(kReaderGlyphIds),
        .pixelsPerEm = 9,
    };
    constexpr ui::fonts::AlphaGlyph kWideReaderGlyphs[]{
        {' ', 0, 0, 0, 0, 0, 8, 0, 0, 0},
        {'?', 0, 0, 1, 1, 1, 20, 0, -1, 0},
        {'a', 0, 0, 1, 1, 1, 20, 0, -1, 0},
    };
    constexpr ui::fonts::AlphaFont kWideReaderFont{
        .name = "wide-reader",
        .bitmap = kReaderBitmap,
        .glyphs = kWideReaderGlyphs,
        .glyphCount = std::size(kWideReaderGlyphs),
        .yAdvance = 9,
        .ascent = 7,
        .descent = 2,
        .wordInkTop = -1,
        .wordInkBottom = -1,
    };

} // namespace

void setUp() {
    gContact = {};
    gTouchReadSucceeds = true;
}
void tearDown() {}

void test_unchanged_widget_does_not_draw_or_flush() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Open");
    context.endFrame();

    gfx.writes = 0;
    gfx.flushes = 0;
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Open");
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
    TEST_ASSERT_EQUAL(0, gfx.flushes);
}

void test_changed_and_removed_widgets_redraw() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "One");
    context.button({0, 28, 80, 24}, "Two");
    context.endFrame();

    gfx.writes = 0;
    gfx.flushes = 0;
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Changed");
    context.endFrame();
    TEST_ASSERT_GREATER_THAN(0, gfx.writes);
    TEST_ASSERT_EQUAL(1, gfx.flushes);
}

void test_button_and_slider_consume_touch() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Tap");
    context.endFrame();
    gfx.writes = 0;
    gfx.flushes = 0;
    gContact = {true, 20, 10};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button({0, 0, 80, 24}, "Tap"));
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
    TEST_ASSERT_EQUAL(0, gfx.flushes);
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(2));
    context.beginFrame(1);
    TEST_ASSERT_TRUE(context.button({0, 0, 80, 24}, "Tap"));
    context.endFrame();

    gContact = {true, 25, 40};
    TEST_ASSERT_TRUE(context.pollTouch(3));
    context.beginFrame(2);
    settings::BoundedValue<int, 0, 100, 5> value{0};
    TEST_ASSERT_FALSE(context.slider({0, 30, 101, 20}, "", value));
    context.endFrame();
    TEST_ASSERT_EQUAL(0, value);
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(4));
    context.beginFrame(2);
    TEST_ASSERT_TRUE(context.slider({0, 30, 101, 20}, "", value));
    context.endFrame();
    TEST_ASSERT_EQUAL(25, value);
}

void test_tap_capture_tolerates_slow_release_just_outside() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    enableTouch(context);
    constexpr ui::Rect button{0, 0, 80, 24};

    gContact = {true, 75, 12};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 85, 12};
    TEST_ASSERT_TRUE(context.pollTouch(350));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(470));
    context.beginFrame(1);
    TEST_ASSERT_TRUE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 85, 12};
    TEST_ASSERT_TRUE(context.pollTouch(500));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 75, 12};
    TEST_ASSERT_TRUE(context.pollTouch(510));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(520));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();
}

void test_tap_tolerates_one_coordinate_outlier() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    enableTouch(context);
    constexpr ui::Rect button{0, 0, 80, 24};

    gContact = {true, 40, 12};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 70, 12};
    TEST_ASSERT_TRUE(context.pollTouch(2));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 40, 12};
    TEST_ASSERT_TRUE(context.pollTouch(3));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(4));
    context.beginFrame(1);
    TEST_ASSERT_TRUE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 40, 12};
    TEST_ASSERT_TRUE(context.pollTouch(10));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 70, 12};
    TEST_ASSERT_TRUE(context.pollTouch(11));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 75, 12};
    TEST_ASSERT_TRUE(context.pollTouch(12));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {true, 40, 12};
    TEST_ASSERT_TRUE(context.pollTouch(13));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(14));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();
}

void test_slow_press_remains_tap_until_hold_threshold() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    enableTouch(context);
    constexpr ui::Rect button{0, 0, 80, 24};

    gContact = {true, 40, 12};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    TEST_ASSERT_TRUE(context.pollTouch(500));
    TEST_ASSERT_FALSE(ui::hasTouch(*context.touch(), ui::TouchHold));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button(button, "Tap"));
    context.endFrame();

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(590));
    context.beginFrame(1);
    TEST_ASSERT_TRUE(context.button(button, "Tap"));
    context.endFrame();
}

void test_missing_sample_does_not_interrupt_active_touch() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    enableTouch(context);

    gContact = {true, 20, 10};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    TEST_ASSERT_TRUE(ui::hasTouch(*context.touch(), ui::TouchStart));

    gTouchReadSucceeds = false;
    TEST_ASSERT_FALSE(context.pollTouch(2));
    gTouchReadSucceeds = true;
    TEST_ASSERT_TRUE(context.pollTouch(3));

    TEST_ASSERT_TRUE(context.pollTouch(650));
    TEST_ASSERT_TRUE(ui::hasTouch(*context.touch(), ui::TouchHold));
    TEST_ASSERT_FALSE(ui::hasTouch(*context.touch(), ui::TouchRelease));

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(651));
    TEST_ASSERT_TRUE(ui::hasTouch(*context.touch(), ui::TouchRelease));
    TEST_ASSERT_FALSE(ui::hasTouch(*context.touch(), ui::TouchTap));
}

void test_queued_touch_uses_sample_time_instead_of_ui_time() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    ui::TouchTiming timing;
    timing.tapMaxDurationMs = 600;
    context.setTouchSource({{320, 172}, timing, &pollTouch});

    gContact = {.touched = true, .x = 40, .y = 50, .sampledAtMs = 100};
    TEST_ASSERT_TRUE(context.pollTouch(1000));
    TEST_ASSERT_TRUE(ui::hasTouch(*context.touch(), ui::TouchStart));

    gContact = {.touched = false, .sampledAtMs = 800};
    TEST_ASSERT_TRUE(context.pollTouch(1001));
    TEST_ASSERT_TRUE(ui::hasTouch(*context.touch(), ui::TouchRelease));
    TEST_ASSERT_FALSE(ui::hasTouch(*context.touch(), ui::TouchTap));
}

void test_stepper_taps_and_repeats() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);
    int value = 25;

    gContact = {true, 190, 20};
    TEST_ASSERT_TRUE(context.pollTouch(10));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.stepper({0, 0, 200, 40}, "Focus", value, 1, 180, 1, " min"));
    context.endFrame();

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(20));
    context.beginFrame(1);
    TEST_ASSERT_TRUE(context.stepper({0, 0, 200, 40}, "Focus", value, 1, 180, 1, " min"));
    context.endFrame();
    TEST_ASSERT_EQUAL(26, value);

    gContact = {true, 190, 20};
    TEST_ASSERT_TRUE(context.pollTouch(100));
    context.beginFrame(1);
    context.stepper({0, 0, 200, 40}, "Focus", value, 1, 180, 1, " min");
    context.endFrame();

    TEST_ASSERT_TRUE(context.pollTouch(880));
    context.beginFrame(1);
    TEST_ASSERT_TRUE(context.stepper({0, 0, 200, 40}, "Focus", value, 1, 180, 1, " min"));
    context.endFrame();
    TEST_ASSERT_EQUAL(28, value);
}

void test_disabled_button_ignores_touch() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);

    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button({0, 0, 80, 24}, "Enable", false));
    context.endFrame();
    gContact = {true, 20, 10};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button({0, 0, 80, 24}, "Enable", false));
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(2));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button({0, 0, 80, 24}, "Enable", false));
    context.endFrame();
}

void test_tap_target_handles_touch_without_drawing() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);

    context.beginFrame(10);
    context.tap({0, 0, 80, 24});
    context.endFrame();
    gfx.writes = 0;
    gfx.flushes = 0;

    gContact = {true, 20, 10};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(10);
    TEST_ASSERT_FALSE(context.tap({0, 0, 80, 24}));
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(2));
    context.beginFrame(10);
    TEST_ASSERT_TRUE(context.tap({0, 0, 80, 24}));
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
    TEST_ASSERT_EQUAL(0, gfx.flushes);

    context.beginFrame(10);
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
    TEST_ASSERT_EQUAL(0, gfx.flushes);
}

void test_layout_cursors_are_deterministic() {
    ui::Column column{{10, 20, 100, 80}, 4};
    TEST_ASSERT_EQUAL_INT16(20, column.next(10).y);
    TEST_ASSERT_EQUAL_INT16(34, column.next(10).y);
    ui::Grid grid{{0, 0, 100, 100}, 2, 20, 4};
    TEST_ASSERT_EQUAL_INT16(48, grid.next().w);
    TEST_ASSERT_EQUAL_INT16(52, grid.next().x);
    TEST_ASSERT_EQUAL_INT16(24, grid.next().y);
}

void test_page_reader_uses_reader_typeface_after_skipping_a_colliding_page_range() {
    Arduino_GFX gfx(136, 17);
    ui::Context context(gfx);
    ui::fonts::AlphaTextRenderer<640> text(gfx);
    TEST_ASSERT_TRUE(text.begin());
    auto colors = theme();
    context.setTheme(colors);
    settings::TypographySettings typography;
    std::array<std::string, 12> words;
    words.fill("a");
    ReadingSession session;
    ReadingLoop::setWords(session, words, 0);
    session.metadata.paragraphStarts = {0, 4, 8};
    screens::PageReader::State state;
    const auto typeface = [](size_t) -> FontCatalog::Face {
        return {std::cref(kReaderFont), std::nullopt};
    };
    constexpr ui::Rect area{0, 0, 136, 17};

    context.beginFrame(1);
    screens::PageReader::draw(state, context, text, typeface, typography, 1, session, area);
    context.endFrame();
    TEST_ASSERT_EQUAL(0, state.pageStart);
    TEST_ASSERT_EQUAL(4, state.pageEnd);

    gfx.bitmapWrites = 0;
    gfx.textWrites = 0;
    ReadingLoop::seekTo(session, 8);
    context.beginFrame(1);
    screens::PageReader::draw(state, context, text, typeface, typography, 1, session, area);
    context.endFrame();

    TEST_ASSERT_EQUAL(8, state.pageStart);
    TEST_ASSERT_EQUAL(12, state.pageEnd);
    TEST_ASSERT_EQUAL(4, gfx.bitmapWrites);
    TEST_ASSERT_EQUAL(0, gfx.textWrites);
}

void test_page_reader_uses_each_words_selected_typeface_for_layout() {
    Arduino_GFX gfx(60, 17);
    ui::Context context(gfx);
    ui::fonts::AlphaTextRenderer<640> text(gfx);
    TEST_ASSERT_TRUE(text.begin());
    auto colors = theme();
    context.setTheme(colors);
    settings::TypographySettings typography;
    const std::array<std::string, 3> words{"a", "a", "a"};
    ReadingSession session;
    ReadingLoop::setWords(session, words, 0);
    screens::PageReader::State state;
    std::array<bool, 3> selected{};
    const auto typeface = [&](size_t index) -> FontCatalog::Face {
        selected[index] = true;
        return {std::cref(index == 1 ? kWideReaderFont : kReaderFont), std::nullopt};
    };

    context.beginFrame(1);
    screens::PageReader::draw(state, context, text, typeface, typography, 1, session, {0, 0, 60, 17});
    context.endFrame();

    TEST_ASSERT_EQUAL(0, state.pageStart);
    TEST_ASSERT_EQUAL(1, state.pageEnd);
    TEST_ASSERT_TRUE(selected[0]);
    TEST_ASSERT_TRUE(selected[1]);
}

void test_page_reader_caches_visual_bidi_layout() {
    Arduino_GFX gfx(180, 40);
    ui::Context context(gfx);
    ui::fonts::AlphaTextRenderer<640> text(gfx);
    TEST_ASSERT_TRUE(text.begin());
    context.setTheme(theme());
    settings::TypographySettings typography;
    const std::array<std::string, 3> words{"abc", "\xD7\x90\xD7\x91\xD7\x92", "123"};
    ReadingSession session;
    ReadingLoop::setWords(session, words, 0);
    session.metadata.baseDirection = BookDirection::rtl;
    session.metadata.requiredCapabilities = UnicodeText::CapabilityBidi;
    session.metadata.paragraphStarts = {0};
    screens::PageReader::State state;
    const auto typeface = [](size_t) -> FontCatalog::Face {
        return {std::cref(kReaderFont), std::nullopt};
    };

    context.beginFrame(1);
    screens::PageReader::draw(state, context, text, typeface, typography, 1, session, {0, 0, 180, 40});
    context.endFrame();

    TEST_ASSERT_TRUE(state.bidi);
    TEST_ASSERT_TRUE(state.lines.front().rightToLeft);
    TEST_ASSERT_GREATER_THAN(0, state.characters.size());
    TEST_ASSERT_EQUAL_UINT32('1', state.characters.front().codepoint);
}

void test_page_reader_shapes_each_visible_word_once_and_caches_glyphs() {
    Arduino_GFX gfx(80, 20);
    ui::Context context(gfx);
    context.setTheme(theme());
    ui::fonts::AlphaTextRenderer<640> text(gfx);
    TEST_ASSERT_TRUE(text.begin());

    File file{std::string(4, '\0')};
    RFont4::Header header{.unitsPerEm = 1000, .sourceGlyphCount = 18};
    const RFont4::LayoutTableRecord table{.tag = HB_TAG('G', 'D', 'E', 'F'), .size = 4};
    TextShaping::Shaper shaper;
    auto opened = shaper.open(file, header, std::span{&table, 1});
    TEST_ASSERT_TRUE_MESSAGE(opened.has_value(), opened ? "" : opened.error().c_str());

    const std::array<std::string, 2> words{"a", "a"};
    ReadingSession session;
    ReadingLoop::setWords(session, words, 0);
    session.metadata.requiredCapabilities = UnicodeText::CapabilityShaping;
    settings::TypographySettings typography;
    screens::PageReader::State state;
    size_t selections = 0;
    const auto typeface = [&](size_t) -> FontCatalog::Face {
        ++selections;
        return {std::cref(kReaderFont), std::ref(shaper)};
    };

    context.beginFrame(1);
    screens::PageReader::draw(state, context, text, typeface, typography, 1, session, {0, 0, 80, 20});
    context.endFrame();
    TEST_ASSERT_TRUE(state.words.front().shaped);
    TEST_ASSERT_EQUAL_UINT16(1, state.words.front().glyphCount);
    TEST_ASSERT_EQUAL_UINT32(2, state.glyphs.front().glyphIndex);

    const size_t afterLayout = selections;
    ReadingLoop::seekTo(session, 1);
    context.beginFrame(1);
    screens::PageReader::draw(state, context, text, typeface, typography, 1, session, {0, 0, 80, 20});
    context.endFrame();
    TEST_ASSERT_EQUAL(afterLayout, selections);
}

void test_ui_font_measures_utf8_codepoints() {
    TEST_ASSERT_EQUAL(18, ui::Context::textWidth("A\xC4\x80\xD0\x91", 1));
    TEST_ASSERT_EQUAL(36, ui::Context::textWidth("A\xC4\x80\xD0\x91", 2));
    TEST_ASSERT_EQUAL(9, ui::Context::textHeight(1));
    TEST_ASSERT_EQUAL(18, ui::Context::textHeight(2));
    TEST_ASSERT_EQUAL(27, ui::Context::textHeight(3));
}

void test_compiled_localization_is_the_english_rescue_table() {
    TEST_ASSERT_EQUAL_STRING("Chapters", std::string{Localization::text(UiText::Chapters)}.c_str());
    TEST_ASSERT_EQUAL_STRING("Library", std::string{Localization::text(UiText::Library)}.c_str());
}

void test_reader_font_resolves_opentype_glyph_ids() {
    Arduino_GFX gfx;
    ui::fonts::AlphaTextRenderer<16> renderer(gfx);
    renderer.begin();
    renderer.setFont(kReaderFont);
    renderer.setTextColor(0xFFFF, 0);

    TEST_ASSERT_EQUAL_INT16(4, renderer.glyphIdAdvance(17));
    TEST_ASSERT_EQUAL_INT16(0, renderer.glyphIdAdvance(18));
    uint32_t glyphIndex = 0;
    TEST_ASSERT_TRUE(renderer.resolveGlyphId(17, glyphIndex));
    TEST_ASSERT_EQUAL_UINT32(2, glyphIndex);
    TEST_ASSERT_EQUAL_INT16(4, renderer.drawGlyphIndex(glyphIndex, 0, 1));
    TEST_ASSERT_GREATER_THAN(0, gfx.writes);
}

void test_reader_streams_rfont4_glyphs_from_file() {
    std::ifstream input("fonts/Andika/font.rfont4", std::ios::binary);
    std::string bytes{std::istreambuf_iterator<char>{input}, {}};
    TEST_ASSERT_GREATER_THAN(sizeof(RFont4::Header), bytes.size());

    RFont4::Header header;
    std::memcpy(&header, bytes.data(), sizeof(header));
    std::array<RFont4::StrikeRecord, RFont4::kSizeCount> strikes;
    std::memcpy(strikes.data(), bytes.data() + header.strikesOffset, sizeof(strikes));
    std::array<RFont4::LayoutTableRecord, RFont4::kMaximumLayoutTableCount> tables{};
    std::memcpy(tables.data(), bytes.data() + header.layoutTablesOffset,
                static_cast<size_t>(header.layoutTableCount) * sizeof(tables.front()));
    TEST_ASSERT_TRUE(RFont4::layoutValid(header, strikes,
                                         std::span{tables}.first(header.layoutTableCount), bytes.size()));
    const RFont4::StrikeRecord& strike = strikes.back();

    std::array<uint8_t, RFont4::kPageMapBytes> pageMap;
    std::memcpy(pageMap.data(), bytes.data() + strike.pageMapOffset, pageMap.size());
    File file{std::move(bytes)};
    const ui::fonts::AlphaFont font{
        .name = "file-reader",
        .glyphCount = strike.glyphCount,
        .yAdvance = strike.yAdvance,
        .ascent = strike.ascent,
        .descent = strike.descent,
        .pageMap = pageMap.data(),
        .pageTableCount = strike.pageTableCount,
        .kerningPairCount = strike.kerningPairCount,
        .wordInkTop = strike.wordInkTop,
        .wordInkBottom = strike.wordInkBottom,
        .glyphIdCount = strike.glyphIdCount,
        .scriptMask = header.scriptMask,
        .file = std::ref(file),
        .fileStrike = strike,
    };
    Arduino_GFX gfx(640, 172);
    ui::fonts::AlphaTextRenderer<640> renderer(gfx);
    TEST_ASSERT_TRUE(renderer.begin());
    renderer.setFont(font);

    TEST_ASSERT_TRUE(renderer.hasGlyph('a'));
    TEST_ASSERT_FALSE(renderer.hasGlyph(0x10FFFF));
    TEST_ASSERT_GREATER_THAN(0, renderer.glyphAdvance('a'));
    TEST_ASSERT_GREATER_THAN(0, renderer.drawCodepoint('a', 10, 50));
    TEST_ASSERT_GREATER_THAN(0, gfx.writes);

    TEST_ASSERT_GREATER_THAN(0, renderer.glyphAdvance('A'));
    TEST_ASSERT_GREATER_THAN(0, renderer.glyphAdvance('V'));
    const size_t readsBeforeKerning = file.readCount();
    const int16_t adjustment = renderer.kerningAdjust('A', 'V');
    TEST_ASSERT_TRUE(adjustment != 0);
    const size_t readsAfterKerning = file.readCount();
    TEST_ASSERT_GREATER_THAN(readsBeforeKerning, readsAfterKerning);
    TEST_ASSERT_EQUAL(adjustment, renderer.kerningAdjust('A', 'V'));
    TEST_ASSERT_EQUAL_UINT32(readsAfterKerning, file.readCount());
}

void test_reader_uses_u8g2_only_for_glyphs_missing_from_rfont4() {
    FontRecordingGfx gfx;
    ui::fonts::AlphaTextRenderer<640> renderer(gfx);
    TEST_ASSERT_TRUE(renderer.begin());
    renderer.setFont(kReaderFont);
    renderer.setTextColor(0xFFFF, 0);
    const uint8_t* fallbackBytes = u8g2_font_rsvpnano_ui_6x9_tf;

    TEST_ASSERT_EQUAL_INT16(16, renderer.textAdvance("a\xD0\x91\xD0\x92"));
    TEST_ASSERT_EQUAL_INT16(16, renderer.drawString("a\xD0\x91\xD0\x92", 0, 8));
    TEST_ASSERT_EQUAL_PTR(fallbackBytes, gfx.lastFont);
    TEST_ASSERT_EQUAL(4, gfx.textWrites);
    TEST_ASSERT_EQUAL(1, gfx.fontSelections);

    gfx.textWrites = 0;
    gfx.fontSelections = 0;
    TEST_ASSERT_EQUAL_INT16(6, renderer.drawCodepoint(0x1F600, 0, 8));
    TEST_ASSERT_EQUAL_PTR(fallbackBytes, gfx.lastFont);
    TEST_ASSERT_EQUAL(4, gfx.textWrites);
    TEST_ASSERT_EQUAL(1, gfx.fontSelections);
}

void test_external_ui_strings_fallback_by_key_and_keep_their_font_separate() {
    FontRecordingGfx gfx(320, 172);
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    locales::UiAssets assets;
    assets.strings = translatedChapters();
    assets.font.assign(std::begin(u8g2_font_rsvpnano_ui_6x9_tf), std::end(u8g2_font_rsvpnano_ui_6x9_tf));
    TEST_ASSERT_TRUE(locales::validateU8g2Font(assets.font).has_value());
    const uint8_t* externalFont = assets.font.data();
    gfx.externalFont = externalFont;
    context.setLanguageAssets(std::move(assets));
    context.setLocale("es");

    TEST_ASSERT_EQUAL_STRING("Capitulos externos", std::string{context.text(UiText::Chapters)}.c_str());
    TEST_ASSERT_EQUAL_STRING("Library", std::string{context.text(UiText::Library)}.c_str());

    context.beginFrame(1);
    context.label({0, 0, 180, 18}, context.text(UiText::Chapters));
    context.endFrame();
    TEST_ASSERT_EQUAL_PTR(externalFont, gfx.lastFont);

    context.beginFrame(2);
    context.label({0, 0, 180, 18}, "Book title");
    context.endFrame();
    TEST_ASSERT_TRUE(gfx.externalFont != gfx.lastFont);
}

void test_rtl_ui_text_uses_pack_direction_for_alignment_and_bidi() {
    FontRecordingGfx gfx(320, 172);
    ui::Context context(gfx);
    locales::UiAssets assets;
    assets.direction = locales::Direction::rtl;
    context.setLanguageAssets(std::move(assets));

    context.drawText({10, 0, 60, 18}, "\xD7\x90\xD7\x91", 1, 0xFFFF);

    const std::vector<uint8_t> expected{0xD7, 0x91, 0xD7, 0x90};
    TEST_ASSERT_EQUAL_INT16(58, gfx.cursorX);
    TEST_ASSERT_EQUAL_UINT8_ARRAY(expected.data(), gfx.text.data(), expected.size());
}

void test_utf8_text_decodes_and_keeps_codepoint_boundaries() {
    constexpr std::string_view text = "A\xC4\x80\xD0\x91";
    TEST_ASSERT_EQUAL(3, Utf8Text::count(text));
    TEST_ASSERT_EQUAL(3, Utf8Text::prefixBytes(text, 2));
    TEST_ASSERT_EQUAL_STRING("\xC4\x80\xD0\x91", std::string{Utf8Text::suffix(text, 4)}.c_str());
    TEST_ASSERT_EQUAL(3, Utf8Text::lastCodepointStart(text));

    std::string_view malformed = "\xC0\xAF";
    uint32_t codepoint = 0;
    TEST_ASSERT_FALSE(Utf8Text::decode(malformed, codepoint));
    TEST_ASSERT_EQUAL(1, malformed.size());

    std::string_view truncated = "\xE2\x82";
    TEST_ASSERT_FALSE(Utf8Text::decode(truncated, codepoint));
    TEST_ASSERT_EQUAL(1, truncated.size());

    std::string_view supplementary = "\xF0\x9F\x99\x82";
    TEST_ASSERT_TRUE(Utf8Text::decode(supplementary, codepoint));
    TEST_ASSERT_EQUAL_UINT32(0x1F642, codepoint);
}

void test_ui_font_preserves_widget_background() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    context.beginFrame(1);
    context.label({0, 0, 100, 24}, "Label", 2);
    context.endFrame();

    TEST_ASSERT_EQUAL(1, gfx.transparentTextColors);
    TEST_ASSERT_EQUAL(0, gfx.opaqueTextColors);
    TEST_ASSERT_EQUAL(2, gfx.lastTextSize);
}

void test_centered_drag_rate_has_deadzone_and_signed_edges() {
    TEST_ASSERT_EQUAL(0, ui::centeredDragRate(86, 0, 172, 15, 400'000));
    TEST_ASSERT_EQUAL(-400'000, ui::centeredDragRate(0, 0, 172, 15, 400'000));
    TEST_ASSERT_EQUAL(400'000, ui::centeredDragRate(172, 0, 172, 15, 400'000));
}

void test_labels_truncate_to_their_rectangles() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.label({0, 0, 30, 8}, "123456789", 1);
    context.endFrame();
    TEST_ASSERT_EQUAL(5, gfx.textWrites);

    gfx.textWrites = 0;
    context.beginFrame(2);
    context.button({0, 0, 72, 40}, "Alpha Beta", true, ui::Icon::None, 2, "By", "42%");
    context.endFrame();
    TEST_ASSERT_EQUAL(13, gfx.textWrites);

    gfx.textWrites = 0;
    context.beginFrame(3);
    context.button({0, 0, 120, 50}, "A", true, ui::Icon::None, 1, "12345678");
    context.endFrame();
    TEST_ASSERT_EQUAL(9, gfx.textWrites);
}

void test_labels_align_and_battery_owns_its_drawing() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.label({10, 0, 60, 8}, "AB", 1, ui::themes::ColorRole::Foreground, ui::TextAlign::Right);
    TEST_ASSERT_EQUAL(70 - ui::Context::textWidth("AB", 1), gfx.cursorX);
    context.battery({100, 0, 120, 36}, 100, false, "100%");
    context.endFrame();
    TEST_ASSERT_EQUAL(ui::themes::rgb565(126, 176, 92), gfx.lastFillColor);

    gfx.writes = 0;
    context.beginFrame(1);
    context.label({10, 0, 60, 8}, "AB", 1, ui::themes::ColorRole::Foreground, ui::TextAlign::Right);
    context.battery({100, 0, 120, 36}, 100, false, "100%");
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
}

void test_setting_gives_long_values_the_full_card_width() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.setting({0, 0, 120, 34}, "Typeface", "Atkinson Hyperlegible");
    context.endFrame();
    TEST_ASSERT_EQUAL(28, gfx.textWrites);

    context.beginFrame(2);
    context.setting({0, 0, 306, 30}, "Home WiFi", "-42 dBm", ui::SettingLayout::Inline);
    context.endFrame();
    TEST_ASSERT_EQUAL(299 - ui::Context::textWidth("-42 dBm", 2), gfx.cursorX);

    gfx.textWrites = 0;
    context.beginFrame(3);
    int longWordDelay = 150;
    context.slider({0, 0, 200, 50}, "Long words", longWordDelay, 0, 600, 50, " ms");
    context.endFrame();
    TEST_ASSERT_EQUAL(16, gfx.textWrites);
}

void test_slider_redraws_with_its_active_color() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    colors.definition.colors.accent = 0x1111;
    colors.definition.colors.breakAccent = 0x2222;
    context.setTheme(colors);
    int focusMinutes = 25;

    context.beginFrame(3);
    context.slider({0, 0, 200, 40}, "Focus", focusMinutes, 1, 180, 1, " min");
    context.endFrame();

    gfx.writes = 0;
    context.beginFrame(3);
    context.slider({0, 0, 200, 40}, "Focus", focusMinutes, 1, 180, 1, " min", ui::themes::ColorRole::BreakAccent);
    context.endFrame();
    TEST_ASSERT_GREATER_THAN(0, gfx.writes);
    TEST_ASSERT_EQUAL_HEX16(0x2222, gfx.lastFillColor);
}

void test_keyboard_edits_and_submits() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);
    std::string value;
    ui::KeyboardState keyboard;

    context.beginFrame(3);
    TEST_ASSERT_EQUAL(ui::KeyboardAction::None, context.keyboard({0, 0, 200, 140}, value, 8, keyboard));
    context.endFrame();

    gContact = {true, 8, 38};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(2));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    TEST_ASSERT_EQUAL_STRING("q", value.c_str());

    gContact = {true, 15, 126};
    TEST_ASSERT_TRUE(context.pollTouch(3));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(4));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    TEST_ASSERT_EQUAL(ui::KeyboardMode::Numbers, keyboard.mode);

    gContact = {true, 8, 38};
    TEST_ASSERT_TRUE(context.pollTouch(5));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(6));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    TEST_ASSERT_EQUAL_STRING("q1", value.c_str());

    gContact = {true, 180, 10};
    TEST_ASSERT_TRUE(context.pollTouch(7));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(8));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    TEST_ASSERT_TRUE(value.empty());

    gContact = {true, 185, 126};
    TEST_ASSERT_TRUE(context.pollTouch(9));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(10));
    context.beginFrame(3);
    TEST_ASSERT_EQUAL(ui::KeyboardAction::Submit, context.keyboard({0, 0, 200, 140}, value, 8, keyboard));
    context.endFrame();

    std::string password = "secret";
    ui::KeyboardState passwordKeyboard;
    context.beginFrame(4);
    context.keyboard({0, 0, 200, 140}, password, 8, passwordKeyboard, "Password", true);
    context.endFrame();
    gContact = {true, 170, 10};
    TEST_ASSERT_TRUE(context.pollTouch(11));
    context.beginFrame(4);
    context.keyboard({0, 0, 200, 140}, password, 8, passwordKeyboard, "Password", true);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(12));
    context.beginFrame(4);
    context.keyboard({0, 0, 200, 140}, password, 8, passwordKeyboard, "Password", true);
    context.endFrame();
    TEST_ASSERT_TRUE(passwordKeyboard.passwordVisible);
}

void test_orientation_owns_graphics_touch_and_hourglass_cache() {
    Arduino_GFX gfx(320, 172);
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);

    context.setOrientation(ui::Orientation::Landscape);
    TEST_ASSERT_EQUAL(ui::Orientation::Landscape, context.orientation());
    TEST_ASSERT_EQUAL(3, gfx.rotation_);
    TEST_ASSERT_EQUAL(172, context.width());
    TEST_ASSERT_EQUAL(320, context.height());

    context.beginFrame(5);
    context.hourglass({10, 10, 80, 120}, 250);
    context.endFrame();
    gfx.writes = 0;
    gfx.flushes = 0;
    context.beginFrame(5);
    context.hourglass({10, 10, 80, 120}, 250);
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
    TEST_ASSERT_EQUAL(0, gfx.flushes);

    context.beginFrame(5);
    context.hourglass({10, 10, 80, 120}, 260);
    context.endFrame();
    TEST_ASSERT_GREATER_THAN(0, gfx.writes);

    gfx.horizontalLines = gfx.verticalLines = 0;
    context.beginFrame(5);
    context.hourglass({10, 10, 120, 80}, 260);
    context.endFrame();
    TEST_ASSERT_TRUE(gfx.verticalLines > gfx.horizontalLines);

    struct OrientationCase {
        ui::Orientation orientation;
        uint16_t expected[4][2];
    };
    constexpr OrientationCase cases[] = {
        {ui::Orientation::Portrait, {{0, 0}, {319, 0}, {0, 171}, {319, 171}}},
        {ui::Orientation::LandscapeFlipped, {{0, 319}, {0, 0}, {171, 319}, {171, 0}}},
        {ui::Orientation::PortraitFlipped, {{319, 171}, {0, 171}, {319, 0}, {0, 0}}},
        {ui::Orientation::Landscape, {{171, 0}, {171, 319}, {0, 0}, {0, 319}}},
    };
    constexpr uint16_t corners[][2] = {{0, 0}, {319, 0}, {0, 171}, {319, 171}};
    uint32_t nowMs = 1;
    for (const auto& testCase: cases) {
        context.setOrientation(testCase.orientation);
        for (size_t corner = 0; corner < 4; ++corner) {
            gContact = {true, corners[corner][0], corners[corner][1]};
            TEST_ASSERT_TRUE(context.pollTouch(nowMs++));
            TEST_ASSERT_EQUAL(testCase.expected[corner][0], context.touch()->x);
            TEST_ASSERT_EQUAL(testCase.expected[corner][1], context.touch()->y);
        }
    }

    context.setOrientation(ui::Orientation::Portrait);
    context.setOrientation(ui::Orientation::Landscape);
    gContact = {true, 10, 20};
    TEST_ASSERT_TRUE(context.pollTouch(nowMs++));
    context.beginFrame(6);
    context.button({140, 0, 30, 30}, "Mapped");
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(nowMs));
    context.beginFrame(6);
    TEST_ASSERT_TRUE(context.button({140, 0, 30, 30}, "Mapped"));
    context.endFrame();
}

void test_focus_timer_text_does_not_redraw_hourglass() {
    Arduino_GFX gfx(640, 172);
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);

    context.beginFrame(7);
    context.hourglass({8, 0, 506, 142}, 200, false, false, ui::themes::ColorRole::Accent, false, "25:00");
    context.steps({8, 144, 624, 14}, 1, 4);
    context.endFrame();

    gfx.writes = gfx.textWrites = gfx.horizontalLines = gfx.verticalLines = 0;
    context.beginFrame(7);
    context.hourglass({8, 0, 506, 142}, 200, false, false, ui::themes::ColorRole::Accent, false, "24:59");
    context.steps({8, 144, 624, 14}, 1, 4);
    context.endFrame();
    TEST_ASSERT_GREATER_THAN(0, gfx.textWrites);
    TEST_ASSERT_EQUAL(0, gfx.horizontalLines);
    TEST_ASSERT_EQUAL(0, gfx.verticalLines);

    context.beginFrame(7);
    context.hourglass({8, 0, 506, 142}, 200, true, false, ui::themes::ColorRole::Accent, false, "24:59");
    context.steps({8, 144, 624, 14}, 1, 4);
    context.endFrame();
    TEST_ASSERT_GREATER_THAN(0, gfx.verticalLines);

    gfx.writes = gfx.horizontalLines = gfx.verticalLines = 0;
    context.beginFrame(7);
    context.hourglass({8, 0, 506, 142}, 200, false, false, ui::themes::ColorRole::BreakAccent, true, "24:59");
    context.steps({8, 144, 624, 14}, 1, 4, ui::themes::ColorRole::BreakAccent);
    context.endFrame();
    TEST_ASSERT_GREATER_THAN(0, gfx.horizontalLines);
    TEST_ASSERT_GREATER_THAN(0, gfx.verticalLines);
}

void test_steps_follow_the_long_axis() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);

    context.beginFrame(8);
    context.steps({8, 8, 40, 156}, 1, 4);
    context.endFrame();

    TEST_ASSERT_EQUAL(4, gfx.circleWrites);
    TEST_ASSERT_EQUAL(gfx.firstCircleX, gfx.lastCircleX);
    TEST_ASSERT_LESS_THAN(gfx.lastCircleY, gfx.firstCircleY);
}

void test_hourglass_source_follows_glass_and_fallen_sand_settles_at_base() {
    Arduino_GFX gfx(640, 172);
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);

    context.beginFrame(9);
    context.hourglass({52, 8, 536, 156}, 0);
    context.endFrame();

    TEST_ASSERT_GREATER_THAN(0, gfx.verticalLines);
    TEST_ASSERT_GREATER_THAN(gfx.firstVerticalHeight, gfx.lastVerticalHeight);

    gfx.verticalLines = 0;
    gfx.firstVerticalHeight = gfx.secondVerticalHeight = gfx.lastVerticalHeight = 0;
    context.beginFrame(9);
    context.hourglass({52, 8, 536, 156}, 1);
    context.endFrame();

    TEST_ASSERT_GREATER_THAN(100, gfx.lastVerticalHeight);

    gfx.verticalLines = 0;
    gfx.firstVerticalHeight = gfx.lastVerticalHeight = 0;
    context.beginFrame(9);
    context.hourglass({52, 8, 536, 156}, 1000);
    context.endFrame();

    TEST_ASSERT_GREATER_THAN(0, gfx.verticalLines);
    TEST_ASSERT_LESS_THAN(566, gfx.maxVerticalX);
    TEST_ASSERT_EQUAL(gfx.firstVerticalHeight, gfx.secondVerticalHeight);
    TEST_ASSERT_GREATER_THAN(gfx.lastVerticalHeight, gfx.firstVerticalHeight);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unchanged_widget_does_not_draw_or_flush);
    RUN_TEST(test_reader_font_resolves_opentype_glyph_ids);
    RUN_TEST(test_reader_streams_rfont4_glyphs_from_file);
    RUN_TEST(test_reader_uses_u8g2_only_for_glyphs_missing_from_rfont4);
    RUN_TEST(test_changed_and_removed_widgets_redraw);
    RUN_TEST(test_button_and_slider_consume_touch);
    RUN_TEST(test_tap_capture_tolerates_slow_release_just_outside);
    RUN_TEST(test_tap_tolerates_one_coordinate_outlier);
    RUN_TEST(test_slow_press_remains_tap_until_hold_threshold);
    RUN_TEST(test_missing_sample_does_not_interrupt_active_touch);
    RUN_TEST(test_queued_touch_uses_sample_time_instead_of_ui_time);
    RUN_TEST(test_stepper_taps_and_repeats);
    RUN_TEST(test_disabled_button_ignores_touch);
    RUN_TEST(test_tap_target_handles_touch_without_drawing);
    RUN_TEST(test_layout_cursors_are_deterministic);
    RUN_TEST(test_page_reader_uses_reader_typeface_after_skipping_a_colliding_page_range);
    RUN_TEST(test_page_reader_uses_each_words_selected_typeface_for_layout);
    RUN_TEST(test_page_reader_caches_visual_bidi_layout);
    RUN_TEST(test_page_reader_shapes_each_visible_word_once_and_caches_glyphs);
    RUN_TEST(test_ui_font_measures_utf8_codepoints);
    RUN_TEST(test_compiled_localization_is_the_english_rescue_table);
    RUN_TEST(test_external_ui_strings_fallback_by_key_and_keep_their_font_separate);
    RUN_TEST(test_rtl_ui_text_uses_pack_direction_for_alignment_and_bidi);
    RUN_TEST(test_utf8_text_decodes_and_keeps_codepoint_boundaries);
    RUN_TEST(test_ui_font_preserves_widget_background);
    RUN_TEST(test_centered_drag_rate_has_deadzone_and_signed_edges);
    RUN_TEST(test_labels_truncate_to_their_rectangles);
    RUN_TEST(test_labels_align_and_battery_owns_its_drawing);
    RUN_TEST(test_setting_gives_long_values_the_full_card_width);
    RUN_TEST(test_slider_redraws_with_its_active_color);
    RUN_TEST(test_keyboard_edits_and_submits);
    RUN_TEST(test_orientation_owns_graphics_touch_and_hourglass_cache);
    RUN_TEST(test_focus_timer_text_does_not_redraw_hourglass);
    RUN_TEST(test_steps_follow_the_long_axis);
    RUN_TEST(test_hourglass_source_follows_glass_and_fallen_sand_settles_at_base);
    return UNITY_END();
}
