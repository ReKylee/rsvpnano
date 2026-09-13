#pragma once

#include "settings/SettingsModel.h"
#include "ui/Choices.h"

namespace screens::choices {
    inline constexpr std::array pause{
        ui::Choice{settings::PauseMode::sentenceEnd, UiText::SentenceEnd},
        ui::Choice{settings::PauseMode::instant, UiText::Instant},
    };
    inline constexpr std::array readingMode{
        ui::Choice{settings::ReadingMode::rsvp, UiText::RsvpMode},
        ui::Choice{settings::ReadingMode::page, UiText::ScrollMode},
    };
    inline constexpr std::array handedness{
        ui::Choice{false, UiText::Right}, ui::Choice{true, UiText::Left},
    };
    inline constexpr std::array chapterScroll{
        ui::Choice{false, UiText::Normal}, ui::Choice{true, UiText::Reversed},
    };
    inline constexpr std::array screensaver{
        ui::Choice{standby::Kind::life, UiText::Life},
        ui::Choice{standby::Kind::maze, UiText::Maze},
        ui::Choice{standby::Kind::voronoi, UiText::Voronoi},
        ui::Choice{standby::Kind::screenOff, UiText::ScreenOff},
        ui::Choice{standby::Kind::reaction, UiText::Reaction},
    };
    static_assert(screensaver.size() == static_cast<size_t>(standby::Kind::Count));
} // namespace screens::choices
