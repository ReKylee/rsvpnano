#include "ui/screens/ScreenCommon.h"

#include <cstdio>

namespace screens {

bool FocusScreen::genres(ui::Context& ui, uint32_t nowMs, Screen& screen) {
    detail::navigation(ui, Screen::FocusGenres, screen);
    ui::Grid grid{detail::content(ui), static_cast<uint8_t>(ui.width() >= 400 ? 2 : 1), 48, 8};
    struct Option { const char* label; FocusTimer::Genre genre; };
    constexpr Option genres[] = {
        {"Deep Work", FocusTimer::Genre::RsvpNano},
        {"Study", FocusTimer::Genre::StrengthLabs},
        {"Creative", FocusTimer::Genre::SelfCare},
        {"Reading", FocusTimer::Genre::Other},
    };
    for (const auto& [label, genre] : genres) {
        if (ui.button(grid.next(), label)) {
            timer.chooseGenre(genre, nowMs);
            return true;
        }
    }
    return false;
}

void FocusScreen::session(ui::Context& ui, uint32_t nowMs) {
    if (!timer.available()) {
        status(ui, "Focus Timer", "IMU unavailable");
        return;
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
    ui.label({static_cast<int16_t>(area.x + dialSize + 8), area.y,
              static_cast<int16_t>(area.w - dialSize - 8), 24}, FocusTimer::genreLabel(timer.genre()), 2);
    ui.button({static_cast<int16_t>(area.x + dialSize + 8), static_cast<int16_t>(area.y + 36),
               static_cast<int16_t>(area.w - dialSize - 8), 36}, "Exit");
    ui.endFrame();
}

} // namespace screens
