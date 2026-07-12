#include "ui/screens/ScreenCommon.h"

#include <cstdio>

namespace screens {

    bool FocusScreen::genres(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        detail::navigation(ui, Screen::FocusGenres, screen);
        ui::Grid grid{detail::tabContent(ui), static_cast<uint8_t>(ui.width() >= 400 ? 2 : 1), 48, 8};
        struct Option {
            std::string_view label;
            FocusTimer::Genre genre;
        };
        const Option genres[] = {
            {ui.text(UiText::DeepWork), FocusTimer::Genre::RsvpNano},
            {ui.text(UiText::Study), FocusTimer::Genre::StrengthLabs},
            {ui.text(UiText::Creative), FocusTimer::Genre::SelfCare},
            {ui.text(UiText::Reading), FocusTimer::Genre::Other},
        };
        for (const auto& [label, genre]: genres) {
            if (ui.button(grid.next(), label)) {
                timer.chooseGenre(genre, nowMs);
                return true;
            }
        }
        return false;
    }

    bool FocusScreen::session(ui::Context& ui, uint32_t nowMs) {
        if (!timer.available()) {
            status(ui, ui.text(UiText::FocusTimer), ui.text(UiText::ImuUnavailable));
            return false;
        }
        const uint32_t seconds = timer.remainingMs(nowMs) / 1000UL;
        char time[8];
        std::snprintf(time, sizeof(time), "%02lu:%02lu", static_cast<unsigned long>(seconds / 60UL),
                      static_cast<unsigned long>(seconds % 60UL));
        ui.beginFrame(static_cast<uint8_t>(Screen::FocusSession));
        const ui::Rect area = detail::content(ui);
        const int16_t dialSize = std::min<int16_t>(area.w, area.h);
        ui.dial({area.x, area.y, dialSize, dialSize}, timer.progressPercent(nowMs), 0, 100,
                timer.isActiveTimerRunning() ? time : "");
        const UiText genre = timer.genre() == FocusTimer::Genre::RsvpNano       ? UiText::DeepWork
                           : timer.genre() == FocusTimer::Genre::StrengthLabs ? UiText::Study
                           : timer.genre() == FocusTimer::Genre::SelfCare     ? UiText::Creative
                                                                              : UiText::Reading;
        ui.label({static_cast<int16_t>(area.x + dialSize + 8), area.y, static_cast<int16_t>(area.w - dialSize - 8), 24},
                 ui.text(genre), 2);
        const bool exit = ui.button({static_cast<int16_t>(area.x + dialSize + 8),
                                     static_cast<int16_t>(area.y + 36),
                                     static_cast<int16_t>(area.w - dialSize - 8), 36},
                                    ui.text(UiText::Exit));
        ui.endFrame();
        return exit;
    }

} // namespace screens
