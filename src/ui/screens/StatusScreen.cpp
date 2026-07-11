#include "ui/screens/Screens.h"

namespace screens {

    void status(ui::Context& ui, std::string_view title, std::string_view line1, std::string_view line2,
                int progressValue) {
        ui.beginFrame(static_cast<uint8_t>(Screen::Status));
        const int16_t center = ui.height() / 2;
        ui.label({12, static_cast<int16_t>(center - 38), static_cast<int16_t>(ui.width() - 24), 24}, title, 3,
                 ui::themes::ColorRole::Foreground, ui::TextAlign::Center);
        if (!line1.empty()) {
            ui.label({12, static_cast<int16_t>(center + 2), static_cast<int16_t>(ui.width() - 24), 16}, line1, 2,
                     ui::themes::ColorRole::Muted, ui::TextAlign::Center);
        }
        if (!line2.empty()) {
            ui.label({12, static_cast<int16_t>(center + 34), static_cast<int16_t>(ui.width() - 24), 8}, line2, 1,
                     ui::themes::ColorRole::Accent, ui::TextAlign::Center);
        }
        if (progressValue >= 0) {
            const int16_t width = std::min<int16_t>(ui.width() - 48, 320);
            ui.progress({static_cast<int16_t>((ui.width() - width) / 2), static_cast<int16_t>(center + 64), width, 8},
                        progressValue);
        }
        ui.endFrame();
    }

} // namespace screens
