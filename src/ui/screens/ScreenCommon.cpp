#include "ui/screens/ScreenCommon.h"

namespace screens::detail {

Action navigation(ui::Context& ui, Screen active, Screen& screen) {
    if (ui.width() < 620 || ui.height() < 150 || ui.height() > 240) {
        return Action::None;
    }

    constexpr int16_t railWidth = 82;
    if (ui.tab({0, 0, railWidth, 53}, "Read", active == Screen::Read || active == Screen::Library)) {
        screen = Screen::Read;
    }
    if (ui.tab({0, 53, railWidth, 53}, "Settings",
               active == Screen::Settings || active == Screen::ReadingSettings
                   || active == Screen::DisplaySettings || active == Screen::PacingSettings
                   || active == Screen::TypographySettings)) {
        screen = Screen::Settings;
    }
    if (ui.tab({0, 106, railWidth, 34}, "Device",
               active == Screen::Device || active == Screen::Sync || active == Screen::Ota)) {
        screen = Screen::Device;
    }
    if (ui.tab({0, 140, railWidth, static_cast<int16_t>(ui.height() - 140)}, "Focus",
               active == Screen::FocusGenres || active == Screen::FocusSession)) {
        screen = Screen::FocusGenres;
    }
    if (ui.button({static_cast<int16_t>(ui.width() - 38), 7, 28, 28}, "O")) {
        return Action::PowerOff;
    }
    return Action::None;
}

ui::Rect content(ui::Context& ui) {
    if (ui.width() >= 620 && ui.height() >= 150 && ui.height() <= 240) {
        return {94, 8, static_cast<int16_t>(ui.width() - 142), static_cast<int16_t>(ui.height() - 16)};
    }
    return {8, 8, static_cast<int16_t>(ui.width() - 16), static_cast<int16_t>(ui.height() - 16)};
}

String onOff(bool enabled) {
    return enabled ? "On" : "Off";
}

} // namespace screens::detail
