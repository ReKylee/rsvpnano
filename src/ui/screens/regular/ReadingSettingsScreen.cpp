#include "ui/screens/ScreenCommon.h"
#include "ui/Inputs.h"
#include "ui/screens/ReadingSettingsScreen.h"

namespace screens {
    bool readingSettings(ui::Context& ui, settings::ReadingSettings& config, Screen& screen) {
        using namespace readingSettingsData;
        bool changed = false;
        const ui::Rect content = detail::content(ui);
        constexpr int16_t gap = 6;
        constexpr int16_t backWidth = 56;
        constexpr int16_t topHeight = 44;
        if (ui.button({content.x, content.y, backWidth, topHeight}, "<<"))
            screen = Screen::Settings;
        changed |= ui.slider({static_cast<int16_t>(content.x + backWidth + gap), content.y,
                              static_cast<int16_t>(content.w - backWidth - gap), topHeight},
                             ui.text(UiText::WordsPerMinute), config.wpm, " WPM");

        const int16_t sectionY = static_cast<int16_t>(content.y + topHeight + gap);
        ui.separator({content.x, sectionY, content.w, 10}, ui.text(UiText::BehaviorSection));
        const int16_t halfWidth = static_cast<int16_t>((content.w - gap) / 2);
        const int16_t rowY = static_cast<int16_t>(sectionY + 14);
        constexpr int16_t rowHeight = 40;
        changed |= ui::select(ui, {content.x, rowY, halfWidth, rowHeight}, UiText::Pause,
                              config.pauseMode, pauseModes, pauseLabel);
        changed |= ui::select(ui, {static_cast<int16_t>(content.x + halfWidth + gap), rowY, halfWidth, rowHeight},
                              UiText::ReadingMode, config.mode, readingModes, modeLabel);

        const int16_t toggleY = static_cast<int16_t>(rowY + rowHeight + gap);
        const int16_t height = content.y + content.h - toggleY;
        changed |= ui::select(ui, {content.x, toggleY, halfWidth, height}, UiText::ReaderHand,
                              config.leftHanded, booleanValues, handLabel);
        changed |= ui::select(ui, {static_cast<int16_t>(content.x + halfWidth + gap), toggleY, halfWidth, height},
                              UiText::ChapterScroll, config.chapterScrollReversed, booleanValues, scrollLabel);
        return changed;
    }
} // namespace screens
