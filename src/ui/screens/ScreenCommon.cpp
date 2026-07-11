#include "ui/screens/ScreenCommon.h"

namespace screens::detail {

    namespace {
        constexpr int16_t kRailWidth = 136;
        constexpr int16_t kContentGap = 12;
        constexpr int16_t kRightInset = 48;
    } // namespace

    Action navigation(ui::Context& ui, Screen active, Screen& screen) {
        if (ui.width() < 620 || ui.height() < 150 || ui.height() > 240) {
            return Action::None;
        }

        if (ui.tab({0, 0, kRailWidth, 53}, "Read", active == Screen::Read || active == Screen::Library,
                   ui::Icon::Books)) {
            screen = Screen::Read;
        }
        if (ui.tab({0, 53, kRailWidth, 53}, "Settings",
                   active == Screen::Settings || active == Screen::ReadingSettings || active == Screen::DisplaySettings
                       || active == Screen::PacingSettings || active == Screen::TypographySettings,
                   ui::Icon::Edit)) {
            screen = Screen::Settings;
        }
        if (ui.tab({0, 106, kRailWidth, 34}, "Device",
                   active == Screen::Device || active == Screen::Sync || active == Screen::Ota, ui::Icon::Device)) {
            screen = Screen::Device;
        }
        if (ui.tab({0, 140, kRailWidth, static_cast<int16_t>(ui.height() - 140)}, "Focus",
                   active == Screen::FocusGenres || active == Screen::FocusSession, ui::Icon::Hourglass)) {
            screen = Screen::FocusGenres;
        }
        if (ui.iconButton({static_cast<int16_t>(ui.width() - 38), 7, 28, 28}, ui::Icon::Power)) {
            return Action::PowerOff;
        }
        return Action::None;
    }

    ui::Rect content(ui::Context& ui) {
        if (ui.width() >= 620 && ui.height() >= 150 && ui.height() <= 240) {
            const int16_t x = static_cast<int16_t>(kRailWidth + kContentGap);
            return {x, 8, static_cast<int16_t>(ui.width() - x - kRightInset), static_cast<int16_t>(ui.height() - 16)};
        }
        return {8, 8, static_cast<int16_t>(ui.width() - 16), static_cast<int16_t>(ui.height() - 16)};
    }

    String onOff(bool enabled) {
        return enabled ? "On" : "Off";
    }

} // namespace screens::detail
