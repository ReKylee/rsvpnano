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
} // namespace screens::choices
