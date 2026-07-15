#include "ui/screens/ScreenCommon.h"

namespace screens {

    Action ota(ui::Context& ui, std::string_view firmwareVersion, Screen& screen) {
        if (const Action action = detail::navigation(ui, Screen::Ota, screen); action != Action::None)
            return action;
        const ui::Rect content = detail::tabContent(ui);
        ui::Column column{content, 8};
        if (ui.button(column.next(28), ui.text(UiText::Back)))
            screen = Screen::Device;
        if (ui.button(column.next(40), ui.text(UiText::CheckOnly)))
            return Action::OtaCheck;
        if (ui.button(column.next(40), ui.text(UiText::InstallUpdate)))
            return Action::OtaInstall;
        const int16_t versionY = static_cast<int16_t>(content.y + content.h - 28);
        const size_t commit = firmwareVersion.find('+');
        if (commit == std::string_view::npos) {
            ui.label({content.x, versionY, content.w, 28}, firmwareVersion, 1, ui::themes::ColorRole::Muted,
                     ui::TextAlign::Center);
        } else {
            ui.label({content.x, versionY, content.w, 14}, firmwareVersion.substr(0, commit), 1,
                     ui::themes::ColorRole::Muted, ui::TextAlign::Center);
            ui.label({content.x, static_cast<int16_t>(versionY + 14), content.w, 14}, firmwareVersion.substr(commit), 1,
                     ui::themes::ColorRole::Muted, ui::TextAlign::Center);
        }
        return Action::None;
    }

} // namespace screens
