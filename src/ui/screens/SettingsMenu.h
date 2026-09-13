#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <span>
#include "ui/screens/Screens.h"

namespace screens {
    struct SettingsEntry {
        UiText label;
        Screen destination;
        bool fullRow = false;
    };

    inline constexpr std::array readingEntries{
        SettingsEntry{UiText::Reading, Screen::ReadingSettings},
        SettingsEntry{UiText::WordPacing, Screen::PacingSettings},
        SettingsEntry{UiText::ReaderLayout, Screen::ReaderAppearance, true},
    };
    inline constexpr std::array systemEntries{
        SettingsEntry{UiText::Display, Screen::InterfaceSettings},
        SettingsEntry{UiText::NetworkUpdates, Screen::NetworkSettings},
    };
    inline constexpr size_t settingsEntryCount = readingEntries.size() + systemEntries.size();

    constexpr int16_t settingsRows(std::span<const SettingsEntry> entries, uint8_t columns) {
        const size_t width = std::max<uint8_t>(1, columns);
        size_t cells = 0;
        for (const auto& entry : entries)
            cells = entry.fullRow ? ((cells + width - 1) / width + 1) * width : cells + 1;
        return static_cast<int16_t>((cells + width - 1) / width);
    }

    constexpr ui::Rect settingsItem(ui::Grid& grid, const SettingsEntry& entry) {
        if (!entry.fullRow)
            return grid.next();
        const uint16_t columns = std::max<uint8_t>(1, grid.columns);
        grid.index = static_cast<uint16_t>((grid.index + columns - 1) / columns * columns);
        auto rect = grid.next();
        rect.w = grid.bounds.w;
        grid.index = static_cast<uint16_t>(grid.index + columns - 1);
        return rect;
    }

    constexpr const SettingsEntry& settingsEntry(size_t index) {
        return index < readingEntries.size() ? readingEntries[index] : systemEntries[index - readingEntries.size()];
    }

    constexpr UiText settingsSection(size_t index) {
        return index < readingEntries.size() ? UiText::ReadingSection : UiText::SystemSection;
    }
} // namespace screens
