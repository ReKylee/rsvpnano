#include "ui/screens/InterfaceSettingsLayout.h"
#include "ui/screens/ScreenCommon.h"

namespace screens::interfaceLayout {
    Layout make(ui::Context& ui, Screen& screen) {
        Layout layout;
        const auto content = detail::content(ui);
        constexpr int16_t gap = 6;
        constexpr int16_t backWidth = 56;
        constexpr int16_t topHeight = 42;
        if (ui.button({content.x, content.y, backWidth, topHeight}, "<<"))
            screen = Screen::Settings;
        layout[Field::Brightness] = {static_cast<int16_t>(content.x + backWidth + gap), content.y,
                                    static_cast<int16_t>(content.w - backWidth - gap), topHeight};

        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t rightX = static_cast<int16_t>(content.x + halfWidth + gap);
        const int16_t sectionsY = static_cast<int16_t>(content.y + topHeight + 4);
        ui.separator({content.x, sectionsY, halfWidth, 10}, ui.text(UiText::AppearanceSection));
        ui.separator({rightX, sectionsY, halfWidth, 10}, ui.text(UiText::StandbySection));

        constexpr int16_t rowHeight = 40;
        const int16_t firstRowY = static_cast<int16_t>(sectionsY + 14);
        const int16_t secondRowY = static_cast<int16_t>(firstRowY + rowHeight + 4);
        layout[Field::Theme] = {content.x, firstRowY, halfWidth, rowHeight};
        layout[Field::Language] = {content.x, secondRowY, halfWidth, rowHeight};
        layout[Field::Standby] = {rightX, firstRowY, halfWidth, rowHeight};
        layout[Field::Screensaver] = {rightX, secondRowY, halfWidth, rowHeight};
        return layout;
    }
} // namespace screens::interfaceLayout
