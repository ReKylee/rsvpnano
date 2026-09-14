#pragma once

#include <array>
#include "settings/SettingsModel.h"
#include "ui/Localization.h"

namespace screens {
    struct PacingChoice {
        UiText label;
        decltype(settings::PacingSettings::longWordDelayMs) settings::PacingSettings::* member;
    };
    inline constexpr std::array kPacingChoices{
        PacingChoice{UiText::LongWords, &settings::PacingSettings::longWordDelayMs},
        PacingChoice{UiText::Complexity, &settings::PacingSettings::complexWordDelayMs},
        PacingChoice{UiText::Punctuation, &settings::PacingSettings::punctuationDelayMs},
    };
} // namespace screens
