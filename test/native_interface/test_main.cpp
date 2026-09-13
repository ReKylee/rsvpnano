#include <cassert>
#include <iostream>
#include <string_view>
#include "ui/screens/ScreenCommon.h"
#include "ui/screens/InterfaceSettingsLayout.h"
#include "ui/screens/SettingsChoices.h"
#include "ui/screens/watch/Layout.h"

namespace screens::detail {
    ui::Rect content(ui::Context& context) {
#if TEST_WATCH
        return watch::contentBounds(context.width(), context.height());
#else
        return {8, 8, static_cast<int16_t>(context.width() - 16), static_cast<int16_t>(context.height() - 16)};
#endif
    }
}

// Paging input is supplied by the fixture; retain production PagedGrid item geometry.
ui::PagedGrid ui::Context::pagedGrid(Rect rect, size_t count, uint8_t columns, int16_t minimumHeight) {
    constexpr int16_t gap = 4;
    int rows = std::max(1, (rect.h + gap) / (minimumHeight + gap));
    if (count > static_cast<size_t>(rows * columns)) {
        rect.h = std::max<int16_t>(1, rect.h - 36);
        rows = std::max(1, (rect.h + gap) / (minimumHeight + gap));
    } else {
        rows = std::min<size_t>(rows, std::max<size_t>(1, (count + columns - 1) / columns));
    }
    const size_t capacity = rows * columns;
    const size_t pages = std::max<size_t>(1, (count + capacity - 1) / capacity);
    const size_t first = std::min(page, pages - 1) * capacity;
    return {rect, first, std::min(capacity, count - first), columns,
            static_cast<int16_t>((rect.h - (rows - 1) * gap) / rows), gap};
}

namespace {
    constexpr std::array<uint32_t, 5> durations{0, 60000, 120000, 300000, 600000};
    int brightnessCalls = 0;
    uint8_t brightness = 0;
    void setBrightness(uint8_t value) { ++brightnessCalls; brightness = value; }

    struct Fixture {
        ui::Context ui;
        screens::InterfaceScreen page;
        settings::InterfaceSettings config;
        locales::Catalog languages{{"he"}, {"ja"}};
        screens::Screen screen = screens::Screen::InterfaceSettings;
        Fixture() {
            // At this watch size, each page contains one field.
            if constexpr (TEST_WATCH) { ui.surfaceWidth = 320; ui.surfaceHeight = 172; }
            page.begin(ui, config, languages, nullptr);
            ui.themeApplications = ui.localeApplications = 0;
            page.themes.resolutions = 0;
            locales::nameLookups = 0;
        }
        bool draw(UiText field = UiText::Count, size_t index = 0,
                  std::span<const uint32_t> times = durations) {
            ui.controls.clear();
            ui.activateField = field;
            ui.page = index;
            return page.draw(ui, config, times, &setBrightness, screen);
        }
    };

    void testBrightness() {
        Fixture f;
        brightnessCalls = 0;
        f.ui.numericDelta = 5;
        assert(f.draw(UiText::Brightness));
        assert(f.config.brightnessPercent == 75 && brightness == 75 && brightnessCalls == 1);
        f.config.brightnessPercent = 100;
        assert(!f.draw(UiText::Brightness));
        assert(brightnessCalls == 1);
        assert(f.ui.sliders == (TEST_WATCH ? 0 : 2));
        assert(f.ui.steppers == (TEST_WATCH ? 2 : 0));
    }

    void testSelectionAndNoOps() {
        Fixture f;
        assert(!f.draw(UiText::Theme, 1));
        assert(f.ui.themeApplications == 0);
        f.page.themes.entries.push_back({"night", {"Night"}});
        assert(f.draw(UiText::Theme, 1));
        assert(f.config.selectedThemeId == "night" && f.ui.selectedTheme == "night");
        for (const auto locale : {"he", "ja", "en"}) {
            assert(f.draw(UiText::Language, 2));
            assert(f.config.locale == locale && f.ui.selectedLocale == locale);
        }
        f.languages.clear();
        f.config.locale = "en";
        const int changes = f.ui.localeApplications;
        assert(!f.draw(UiText::Language, 2));
        assert(f.ui.localeApplications == changes);
        f.config.locale = "missing";
        assert(f.draw(UiText::Language, 2));
        assert(f.config.locale == "en");
    }

    void testChoicesAndDurations() {
        Fixture f;
        for (const auto kind : {standby::Kind::maze, standby::Kind::voronoi, standby::Kind::screenOff,
                                standby::Kind::reaction, standby::Kind::life}) {
            assert(f.draw(UiText::Screensaver, 4));
            assert(f.config.screensaver == kind);
        }
        f.config.standbyTimerIndex = 4;
        assert(f.draw(UiText::Standby, 3));
        assert(f.config.standbyTimerIndex == 0);
        assert(!f.draw(UiText::Count, 3, {}));
        const auto control = std::ranges::find(f.ui.controls, UiText::Standby, &ui::Context::Control::label);
        assert(control != f.ui.controls.end() && control->value == "off");
    }

    void testHiddenWorkAndBack() {
        Fixture f;
        assert(!f.draw());
        if constexpr (TEST_WATCH) {
            assert(f.page.themes.resolutions == 0 && locales::nameLookups == 0);
            assert(f.ui.translated[static_cast<size_t>(UiText::Off)] == 0);
            assert(f.ui.controls.size() == 1 && f.ui.controls.front().label == UiText::Brightness);
        } else {
            assert(f.page.themes.resolutions == 1 && locales::nameLookups == 1);
            assert(f.ui.controls.size() == 5);
        }
        f.ui.buttonToActivate = TEST_WATCH ? "<" : "<<";
        f.draw();
        assert(f.screen == screens::Screen::Settings);
    }

    void testGeometry() {
        constexpr std::array<ui::TouchSurface, 9> watches{{
            {450,600}, {480,480}, {410,502}, {368,448}, {172,320},
            {600,450}, {502,410}, {448,368}, {320,172}}};
        Fixture f;
        const auto check = [&] {
            const auto bounds = screens::detail::content(f.ui);
            for (size_t page = 0; page < 5; ++page) {
                assert(!f.draw(UiText::Count, page));
                for (const auto& control : f.ui.controls) {
                    assert(control.rect.w > 0 && control.rect.h > 0);
                    assert(control.rect.x >= bounds.x && control.rect.y >= bounds.y);
                    assert(control.rect.x + control.rect.w <= bounds.x + bounds.w);
                    assert(control.rect.y + control.rect.h <= bounds.y + bounds.h);
                }
            }
        };
        if constexpr (TEST_WATCH) {
            for (const auto size : watches) {
                f.ui.surfaceWidth = size.width; f.ui.surfaceHeight = size.height;
                check();
            }
        } else {
            check();
        }
    }
}

int main() {
    testBrightness(); testSelectionAndNoOps(); testChoicesAndDurations();
    testHiddenWorkAndBack(); testGeometry();
    std::cout << (TEST_WATCH ? "Watch" : "Regular")
              << " interface settings: behavior, hidden work and layout checks passed\n";
}
