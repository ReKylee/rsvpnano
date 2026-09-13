#pragma once

#include <array>
#include "ui/screens/Screens.h"

namespace screens::settingsPage {
    // Application content only. Grid packing and input behavior belong to ui/.
    struct Entry {
        UiText label;
        Screen destination;
        UiText section;
        uint8_t span = 1;
    };

    inline constexpr std::array entries{
        Entry{UiText::Reading, Screen::ReadingSettings, UiText::ReadingSection},
        Entry{UiText::WordPacing, Screen::PacingSettings, UiText::ReadingSection},
        Entry{UiText::ReaderLayout, Screen::ReaderAppearance, UiText::ReadingSection, ui::Grid::FullRow},
        Entry{UiText::Display, Screen::InterfaceSettings, UiText::SystemSection},
        Entry{UiText::NetworkUpdates, Screen::NetworkSettings, UiText::SystemSection},
    };
} // namespace screens::settingsPage
