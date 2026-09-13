#include "Recording.h"
#include "ui/screens/ScreenCommon.h"
#include <array>
#include <cassert>
#include <iostream>

int main() {
    using namespace testui;
    constexpr std::array sizes{ui::TouchSurface{640, 172}, ui::TouchSurface{368, 448},
                               ui::TouchSurface{448, 368}, ui::TouchSurface{320, 172},
                               ui::TouchSurface{172, 320}, ui::TouchSurface{480, 480}};
    for (const auto size : sizes) {
        Arduino_GFX gfx{static_cast<int16_t>(size.width), static_cast<int16_t>(size.height)};
        ui::Context ui{gfx};
        recording = {};
        recording.pageCapacity = 2;
        auto screen = screens::Screen::Settings;
        recording.activate = std::string{caption(UiText::WordPacing)};
        assert(screens::settings(ui, screen) == screens::Action::None);
        assert(screen == screens::Screen::PacingSettings);
        recording = {};
        recording.pageCapacity = 2;
        recording.pageFirst = 3;
        recording.activate = std::string{caption(UiText::Display)};
        screen = screens::Screen::Settings;
        screens::settings(ui, screen);
        assert(screen == screens::Screen::InterfaceSettings);

        settings::ReadingSettings config;
        recording = {};
        recording.pageFirst = 1;
        recording.pageCapacity = 4;
        recording.activate = std::string{caption(UiText::Pause)};
        screen = screens::Screen::ReadingSettings;
        assert(screens::readingSettings(ui, config, screen));
        assert(config.pauseMode == settings::PauseMode::instant);
        assert(screens::readingSettings(ui, config, screen));
        assert(config.pauseMode == settings::PauseMode::sentenceEnd);
        recording.activate = std::string{caption(UiText::ReaderHand)};
        assert(screens::readingSettings(ui, config, screen) && config.leftHanded);
        recording.activate = std::string{caption(UiText::ChapterScroll)};
        assert(screens::readingSettings(ui, config, screen) && config.chapterScrollReversed);
        recording.activate = std::string{caption(UiText::ReadingMode)};
        assert(screens::readingSettings(ui, config, screen) && config.mode == settings::ReadingMode::page);
        recording.activate.clear();
        assert(!screens::readingSettings(ui, config, screen));
    }
    std::cout << "Screens: separate profile sources, navigation and setting semantics passed\n";
}
