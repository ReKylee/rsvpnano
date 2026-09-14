#pragma once

#include <cstdio>
#include <span>
#include "settings/SettingsModel.h"
#include "ui/Ui.h"

namespace screens {
    inline std::string_view standbyLabel(ui::Context& ui, size_t index, std::span<const uint32_t> durations,
                                         char (&buffer)[16]) {
        if (index >= durations.size() || durations[index] == 0)
            return ui.text(UiText::Off);
        std::snprintf(buffer, sizeof(buffer), "%lum", static_cast<unsigned long>(durations[index] / 60000));
        return buffer;
    }

    constexpr UiText screensaverLabel(standby::Kind kind) {
        switch (kind) {
        case standby::Kind::maze: return UiText::Maze;
        case standby::Kind::voronoi: return UiText::Voronoi;
        case standby::Kind::reaction: return UiText::Reaction;
        case standby::Kind::screenOff: return UiText::ScreenOff;
        default: return UiText::Life;
        }
    }
} // namespace screens
