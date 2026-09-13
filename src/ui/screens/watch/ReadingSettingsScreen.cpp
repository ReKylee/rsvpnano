#include "ui/screens/watch/Layout.h"
#include "ui/SettingsControls.h"
#include "ui/screens/SettingsChoices.h"

namespace screens {
    bool readingSettings(ui::Context& ui, settings::ReadingSettings& config, Screen& screen) {
        const auto area = watch::header(ui, ui.text(UiText::Reading), Screen::Settings, screen);
        const auto grid = ui.pagedGrid(area, 5, 1, 72);
        ui::SettingsControls controls{ui, {.presentation = ui::SettingPresentation::Card,
                                          .textSize = watch::textSize(ui)}};
        controls.rotaryStepper(grid.item(0), "WPM", config.wpm);
        controls.choice(grid.item(1), UiText::Pause, config.pauseMode, choices::pause);
        controls.choice(grid.item(2), UiText::ReadingMode, config.mode, choices::readingMode);
        controls.choice(grid.item(3), UiText::ReaderHand, config.leftHanded, choices::handedness);
        controls.choice(grid.item(4), UiText::ChapterScroll, config.chapterScrollReversed, choices::chapterScroll);
        return controls.changed();
    }
} // namespace screens
