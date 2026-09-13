#pragma once

#include <array>
#include "settings/SettingsModel.h"
#include "ui/Localization.h"

namespace screens::readingSettingsData {
    inline constexpr std::array pauseModes{settings::PauseMode::sentenceEnd, settings::PauseMode::instant};
    inline constexpr std::array readingModes{settings::ReadingMode::rsvp, settings::ReadingMode::page};
    inline constexpr std::array booleanValues{false, true};

    inline constexpr auto pauseLabel = [](settings::PauseMode value) {
        return value == settings::PauseMode::sentenceEnd ? UiText::SentenceEnd : UiText::Instant;
    };
    inline constexpr auto modeLabel = [](settings::ReadingMode value) {
        return value == settings::ReadingMode::rsvp ? UiText::RsvpMode : UiText::ScrollMode;
    };
    inline constexpr auto handLabel = [](bool left) { return left ? UiText::Left : UiText::Right; };
    inline constexpr auto scrollLabel = [](bool reversed) { return reversed ? UiText::Reversed : UiText::Normal; };
} // namespace screens::readingSettingsData
