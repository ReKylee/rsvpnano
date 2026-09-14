#pragma once

#include <unity.h>
#include "ui/Ui.h"
#include "app/screens/settings/ReadingChoices.h"
#include "app/screens/settings/PacingChoices.h"
#include "app/screens/settings/SettingsMenu.h"

namespace uiCacheChecks {
    inline void distinctTextFields() {
        const auto a = ui::Context::signature("c", ui::Context::signature("ab"));
        const auto b = ui::Context::signature("bc", ui::Context::signature("a"));
        TEST_ASSERT_NOT_EQUAL(a, b);
    }

    inline void dockIconChanges() {
        Arduino_GFX gfx{320, 172};
        ui::Context context{gfx};
        const auto draw = [&](ui::Icon icon) {
            context.beginFrame(1);
            context.dockItem({8, 8, 120, 40}, "Read", icon, 0xffff);
            context.endFrame();
        };
        draw(ui::Icon::Books);
        const auto writes = gfx.writes;
        const auto flushes = gfx.flushes;
        draw(ui::Icon::Books);
        TEST_ASSERT_EQUAL(writes, gfx.writes);
        TEST_ASSERT_EQUAL(flushes, gfx.flushes);
        draw(ui::Icon::Device);
        TEST_ASSERT_GREATER_THAN(writes, gfx.writes);
        TEST_ASSERT_EQUAL(flushes + 1, gfx.flushes);
    }

    inline void opaquePaintOwnsBackground() {
        Arduino_GFX gfx{320, 172};
        ui::Context context{gfx};
        context.beginFrame(1);
        const auto writes = gfx.writes;
        TEST_ASSERT_TRUE(context.redraw({8, 8, 100, 40}, 1, true));
        TEST_ASSERT_EQUAL(writes, gfx.writes);
        context.paint({8, 8, 100, 40}, [](Arduino_GFX& output, ui::Rect rect) {
            output.drawPixel(rect.x + 1, rect.y + 1, 0xffff);
        });
        TEST_ASSERT_EQUAL(writes + 2, gfx.writes); // One background and the owned pixel.
        context.endFrame();
        context.beginFrame(1);
        const auto flushes = gfx.flushes;
        TEST_ASSERT_FALSE(context.redraw({8, 8, 100, 40}, 1, true));
        context.endFrame();
        TEST_ASSERT_EQUAL(flushes, gfx.flushes);
    }

    inline void invisiblePaintDoesNoWork() {
        Arduino_GFX gfx{320, 172};
        ui::Context context{gfx};
        context.beginFrame(1);
        context.endFrame();
        const auto writes = gfx.writes;
        const auto flushes = gfx.flushes;
        context.beginFrame(1);
        int calls = 0;
        const auto draw = [&](Arduino_GFX&, ui::Rect) { ++calls; };
        context.paint({5, 5, 0, 30}, draw);
        context.paint({320, 5, 20, 30}, draw);
        context.redraw({320, 5, 20, 30}, 1);
        context.endFrame();
        TEST_ASSERT_EQUAL(0, calls);
        TEST_ASSERT_EQUAL(writes, gfx.writes);
        TEST_ASSERT_EQUAL(flushes, gfx.flushes);
    }

    inline void typedReadingChoicesEditOnlyTheirMembers() {
        settings::ReadingSettings config;
        const auto original = config;
        TEST_ASSERT_FALSE(screens::editReadingChoices(config, [](size_t, UiText, UiText) { return false; }));
        TEST_ASSERT_TRUE(config == original);
        size_t count = 0;
        TEST_ASSERT_TRUE(screens::editReadingChoices(config, [&](size_t index, UiText, UiText) {
            TEST_ASSERT_EQUAL(count++, index);
            return true;
        }));
        TEST_ASSERT_EQUAL(4, count);
        TEST_ASSERT_EQUAL(int(original.wpm), int(config.wpm));
        TEST_ASSERT_NOT_EQUAL(original.pauseMode, config.pauseMode);
        TEST_ASSERT_NOT_EQUAL(original.mode, config.mode);
        TEST_ASSERT_NOT_EQUAL(original.leftHanded, config.leftHanded);
        TEST_ASSERT_NOT_EQUAL(original.chapterScrollReversed, config.chapterScrollReversed);
        settings::PacingSettings pacing;
        for (const auto& choice: screens::kPacingChoices)
            pacing.*choice.member = 350;
        TEST_ASSERT_EQUAL(350, int(pacing.longWordDelayMs));
        TEST_ASSERT_EQUAL(350, int(pacing.complexWordDelayMs));
        TEST_ASSERT_EQUAL(350, int(pacing.punctuationDelayMs));
        TEST_ASSERT_EQUAL(5, screens::kSettingsMenu.size());
    }
}
