#include "ui/screens/ScreenCommon.h"
#include "ui/SettingsControls.h"
#include "ui/screens/SettingsChoices.h"

namespace screens {
    bool readingSettings(ui::Context& ui, settings::ReadingSettings& config, Screen& screen) {
        ui::SettingsControls controls{ui};
        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 6;
        constexpr int16_t backWidth = 56;
        constexpr int16_t topHeight = 44;
        if (ui.button({content.x, content.y, backWidth, topHeight}, "<<"))
            screen = Screen::Settings;
        controls.slider({static_cast<int16_t>(content.x + backWidth + gap), content.y,
                         static_cast<int16_t>(content.w - backWidth - gap), topHeight},
                        UiText::WordsPerMinute, config.wpm, " WPM");

        const int16_t sectionY = static_cast<int16_t>(content.y + topHeight + gap);
        ui.separator({content.x, sectionY, content.w, 10}, ui.text(UiText::BehaviorSection));
        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t rowY = static_cast<int16_t>(sectionY + 14);
        constexpr int16_t rowHeight = 40;
        controls.choice({content.x, rowY, halfWidth, rowHeight}, UiText::Pause, config.pauseMode, choices::pause);
        controls.choice({static_cast<int16_t>(content.x + halfWidth + gap), rowY, halfWidth, rowHeight},
                        UiText::ReadingMode, config.mode, choices::readingMode);

        const int16_t toggleY = static_cast<int16_t>(rowY + rowHeight + gap);
        const int16_t height = content.y + content.h - toggleY;
        controls.choice({content.x, toggleY, halfWidth, height}, UiText::ReaderHand,
                        config.leftHanded, choices::handedness);
        controls.choice({static_cast<int16_t>(content.x + halfWidth + gap), toggleY, halfWidth, height},
                        UiText::ChapterScroll, config.chapterScrollReversed, choices::chapterScroll);
        return controls.changed();
    }
} // namespace screens
