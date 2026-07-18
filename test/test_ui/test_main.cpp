#include <unity.h>

#include "ui/Ui.h"

namespace {

    ui::TouchContact gContact;

    bool beginTouch() {
        return true;
    }
    bool touchReady() {
        return true;
    }
    bool readTouch(ui::TouchContact& contact) {
        contact = gContact;
        return true;
    }
    void enableTouch(ui::Context& context) {
        ui::TouchTiming timing;
        timing.releaseConfirmSamples = 1;
        timing.pollIntervalMs = 0;
        context.setTouchSource({{320, 172}, timing, &beginTouch, &touchReady, &readTouch}, 0);
    }

    ui::themes::Theme theme() {
        return ui::themes::defaultTheme();
    }

} // namespace

void setUp() {
    gContact = {};
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
    const auto value = context.slider({0, 30, 101, 20}, 0, 0, 100, 5);
    context.endFrame();
    TEST_ASSERT_FALSE(value.changed);
    TEST_ASSERT_EQUAL(25, value.value);
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(4));
    context.beginFrame(2);
    const auto committed = context.slider({0, 30, 101, 20}, 0, 0, 100, 5);
    context.endFrame();
    TEST_ASSERT_TRUE(committed.changed);
    TEST_ASSERT_EQUAL(25, committed.value);
}

void test_stepper_taps_and_repeats() {
    Arduino_GFX gfx;
    ui::Context context(gfx);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);

    gContact = {true, 190, 20};
    TEST_ASSERT_TRUE(context.pollTouch(10));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.stepper({0, 0, 200, 40}, "Focus", 25, 1, 180, 1, " min").changed);
    context.endFrame();

    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(20));
    context.beginFrame(1);
    const auto tapped = context.stepper({0, 0, 200, 40}, "Focus", 25, 1, 180, 1, " min");
    context.endFrame();
    TEST_ASSERT_TRUE(tapped.changed);
    TEST_ASSERT_EQUAL(26, tapped.value);

    gContact = {true, 190, 20};
    TEST_ASSERT_TRUE(context.pollTouch(100));
    context.beginFrame(1);
    context.stepper({0, 0, 200, 40}, "Focus", 25, 1, 180, 1, " min");
    context.endFrame();

    TEST_ASSERT_TRUE(context.pollTouch(640));
    context.beginFrame(1);
    const auto held = context.stepper({0, 0, 200, 40}, "Focus", 25, 1, 180, 1, " min");
    context.endFrame();
    TEST_ASSERT_TRUE(held.changed);
    TEST_ASSERT_EQUAL(27, held.value);
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
    TEST_ASSERT_EQUAL(58, gfx.cursorX);
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
    TEST_ASSERT_EQUAL(215, gfx.cursorX);

    gfx.textWrites = 0;
    context.beginFrame(3);
    context.slider({0, 0, 200, 50}, "Long words", 150, 0, 600, 50, " ms");
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

    context.beginFrame(3);
    context.slider({0, 0, 200, 40}, "Focus", 25, 1, 180, 1, " min");
    context.endFrame();

    gfx.writes = 0;
    context.beginFrame(3);
    context.slider({0, 0, 200, 40}, "Focus", 25, 1, 180, 1, " min", ui::themes::ColorRole::BreakAccent);
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
    RUN_TEST(test_changed_and_removed_widgets_redraw);
    RUN_TEST(test_button_and_slider_consume_touch);
    RUN_TEST(test_stepper_taps_and_repeats);
    RUN_TEST(test_disabled_button_ignores_touch);
    RUN_TEST(test_tap_target_handles_touch_without_drawing);
    RUN_TEST(test_layout_cursors_are_deterministic);
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
