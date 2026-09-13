#include "ui/screens/watch/Layout.h"
#include "ui/Inputs.h"
#include "ui/screens/ReadingSettingsScreen.h"

namespace screens {
    bool readingSettings(ui::Context& ui, settings::ReadingSettings& config, Screen& screen) {
        using namespace readingSettingsData;
        const auto area = watch::header(ui, ui.text(UiText::Reading), Screen::Settings, screen);
        const auto grid = ui.pagedGrid(area, 5, 1, 72);
        const ui::ValueStyle style{.layout = ui::ValueLayout::Card, .textSize = watch::textSize(ui)};
        bool changed = ui::rotaryStepper(ui, grid.item(0), "WPM", config.wpm);
        changed |= ui::select(ui, grid.item(1), UiText::Pause, config.pauseMode, pauseModes, pauseLabel, style);
        changed |= ui::select(ui, grid.item(2), UiText::ReadingMode, config.mode, readingModes, modeLabel, style);
        changed |= ui::select(ui, grid.item(3), UiText::ReaderHand, config.leftHanded, booleanValues, handLabel, style);
        changed |= ui::select(ui, grid.item(4), UiText::ChapterScroll, config.chapterScrollReversed,
                              booleanValues, scrollLabel, style);
        return changed;
    }
} // namespace screens
