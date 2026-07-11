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
        context.setTouchSource({{100, 100}, timing, &beginTouch, &touchReady, &readTouch, &touchOrientation}, 0);
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
    gContact = {true, 20, 10};
    TEST_ASSERT_TRUE(context.pollTouch(1));
    context.beginFrame(1);
    TEST_ASSERT_FALSE(context.button({0, 0, 80, 24}, "Tap"));
    context.endFrame();
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
    TEST_ASSERT_TRUE(value.changed);
    TEST_ASSERT_EQUAL(25, value.value);
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
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_unchanged_widget_does_not_draw_or_flush);
    RUN_TEST(test_changed_and_removed_widgets_redraw);
    RUN_TEST(test_button_and_slider_consume_touch);
    RUN_TEST(test_layout_cursors_are_deterministic);
    RUN_TEST(test_labels_truncate_to_their_rectangles);
    return UNITY_END();
}
