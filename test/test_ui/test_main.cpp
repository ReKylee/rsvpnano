#include <unity.h>

#include "ui/Ui.h"

namespace {

    int gFlushes = 0;
    int gRegionFlushes = 0;
    ui::TouchContact gContact;

    void flush() {
        ++gFlushes;
    }
    bool flushRegion(uint16_t, uint16_t, uint16_t, uint16_t) {
        ++gRegionFlushes;
        return true;
    }
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
    ui::Orientation touchOrientation() {
        return ui::Orientation::Portrait;
    }

    void enableTouch(ui::Context& context) {
        ui::TouchTiming timing;
        timing.releaseConfirmSamples = 1;
        timing.pollIntervalMs = 0;
        context.setTouchSource({{320, 172}, timing, &beginTouch, &touchReady, &readTouch, &touchOrientation}, 0);
    }

    ui::themes::Theme theme() {
        ui::themes::Theme value;
        value.colors.fill(0xFFFF);
        value.colors[static_cast<size_t>(ui::themes::ColorRole::Background)] = 0;
        return value;
    }

} // namespace

void setUp() {
    gFlushes = gRegionFlushes = 0;
    gContact = {};
}
void tearDown() {}

void test_unchanged_widget_does_not_draw_or_flush() {
    Arduino_GFX gfx;
    ui::Context context(gfx, &flush, &flushRegion);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Open");
    context.endFrame();

    gfx.writes = 0;
    gRegionFlushes = 0;
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Open");
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
    TEST_ASSERT_EQUAL(0, gRegionFlushes);
}

void test_changed_and_removed_widgets_redraw() {
    Arduino_GFX gfx;
    ui::Context context(gfx, &flush, &flushRegion);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "One");
    context.button({0, 28, 80, 24}, "Two");
    context.endFrame();

    gfx.writes = 0;
    gRegionFlushes = 0;
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Changed");
    context.endFrame();
    TEST_ASSERT_GREATER_THAN(0, gfx.writes);
    TEST_ASSERT_EQUAL(1, gRegionFlushes);
}

void test_button_and_slider_consume_touch() {
    Arduino_GFX gfx;
    ui::Context context(gfx, &flush, &flushRegion);
    auto colors = theme();
    context.setTheme(colors);
    enableTouch(context);
    context.beginFrame(1);
    context.button({0, 0, 80, 24}, "Tap");
    context.endFrame();
    gfx.writes = 0;
    gRegionFlushes = 0;
    gContact = {true, 20, 10};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button({0, 0, 80, 24}, "Tap"));
    context.endFrame();
    TEST_ASSERT_EQUAL(0, gfx.writes);
    TEST_ASSERT_EQUAL(0, gRegionFlushes);
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

void test_disabled_button_ignores_touch() {
    Arduino_GFX gfx;
    ui::Context context(gfx, &flush, &flushRegion);
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
    ui::Context context(gfx, &flush, &flushRegion);
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
}

void test_labels_align_and_battery_owns_its_drawing() {
    Arduino_GFX gfx;
    ui::Context context(gfx, &flush, &flushRegion);
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
    ui::Context context(gfx, &flush, &flushRegion);
    auto colors = theme();
    context.setTheme(colors);
    context.beginFrame(1);
    context.setting({0, 0, 120, 34}, "Typeface", "Atkinson Hyperlegible");
    context.endFrame();
    TEST_ASSERT_EQUAL(28, gfx.textWrites);

    context.beginFrame(2);
    context.setting({0, 0, 200, 30}, "Home WiFi", "-42 dBm");
    context.endFrame();
    TEST_ASSERT_EQUAL(109, gfx.cursorX);
}

void test_keyboard_edits_and_submits() {
    Arduino_GFX gfx;
    ui::Context context(gfx, &flush, &flushRegion);
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

    gContact = {true, 35, 126};
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

    gContact = {true, 185, 126};
    TEST_ASSERT_TRUE(context.pollTouch(7));
    context.beginFrame(3);
    context.keyboard({0, 0, 200, 140}, value, 8, keyboard);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(8));
    context.beginFrame(3);
    TEST_ASSERT_EQUAL(ui::KeyboardAction::Submit, context.keyboard({0, 0, 200, 140}, value, 8, keyboard));
    context.endFrame();

    std::string password = "secret";
    ui::KeyboardState passwordKeyboard;
    context.beginFrame(4);
    context.keyboard({0, 0, 200, 140}, password, 8, passwordKeyboard, true);
    context.endFrame();
    gContact = {true, 170, 10};
    TEST_ASSERT_TRUE(context.pollTouch(9));
    context.beginFrame(4);
    context.keyboard({0, 0, 200, 140}, password, 8, passwordKeyboard, true);
    context.endFrame();
    gContact = {};
    TEST_ASSERT_TRUE(context.pollTouch(10));
    context.beginFrame(4);
    context.keyboard({0, 0, 200, 140}, password, 8, passwordKeyboard, true);
    context.endFrame();
    TEST_ASSERT_TRUE(passwordKeyboard.passwordVisible);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unchanged_widget_does_not_draw_or_flush);
    RUN_TEST(test_changed_and_removed_widgets_redraw);
    RUN_TEST(test_button_and_slider_consume_touch);
    RUN_TEST(test_disabled_button_ignores_touch);
    RUN_TEST(test_layout_cursors_are_deterministic);
    RUN_TEST(test_labels_truncate_to_their_rectangles);
    RUN_TEST(test_labels_align_and_battery_owns_its_drawing);
    RUN_TEST(test_setting_gives_long_values_the_full_card_width);
    RUN_TEST(test_keyboard_edits_and_submits);
    return UNITY_END();
}
