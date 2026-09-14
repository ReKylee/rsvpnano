#pragma once

#include <array>
#include <span>
#include "app/screens/Navigation.h"
#include "ui/Localization.h"

namespace screens {
    struct SettingsMenuItem {
        UiText label;
        Screen target;
        bool fullRow = false;
    };

    inline constexpr std::array kSettingsMenu{
        SettingsMenuItem{UiText::Reading, Screen::ReadingSettings},
        SettingsMenuItem{UiText::WordPacing, Screen::PacingSettings},
        SettingsMenuItem{UiText::ReaderLayout, Screen::ReaderAppearance, true},
        SettingsMenuItem{UiText::Display, Screen::InterfaceSettings},
        SettingsMenuItem{UiText::NetworkUpdates, Screen::NetworkSettings},
    };
    inline constexpr auto kReadingMenu = std::span{kSettingsMenu}.first<3>();
    inline constexpr auto kSystemMenu = std::span{kSettingsMenu}.subspan<3>();
} // namespace screens
