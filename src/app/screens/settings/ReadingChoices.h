#pragma once

#include <tuple>
#include <type_traits>
#include <cstddef>
#include "settings/SettingsModel.h"
#include "settings/SettingsRules.h"
#include "ui/Localization.h"

namespace screens {
    template<typename Value>
    struct ReadingChoice {
        UiText label;
        Value settings::ReadingSettings::* member;
        Value first;
        UiText firstLabel;
        UiText otherLabel;

        UiText text(const settings::ReadingSettings& config) const {
            return config.*member == first ? firstLabel : otherLabel;
        }
        void advance(settings::ReadingSettings& config) const {
            if constexpr (std::is_same_v<Value, bool>)
                config.*member = !(config.*member);
            else
                config.*member = settings::cycleEnum(config.*member);
        }
    };

    inline constexpr auto kReadingChoices = std::tuple{
        ReadingChoice{UiText::Pause, &settings::ReadingSettings::pauseMode,
                      settings::PauseMode::sentenceEnd, UiText::SentenceEnd, UiText::Instant},
        ReadingChoice{UiText::ReadingMode, &settings::ReadingSettings::mode,
                      settings::ReadingMode::page, UiText::ScrollMode, UiText::RsvpMode},
        ReadingChoice{UiText::ReaderHand, &settings::ReadingSettings::leftHanded,
                      true, UiText::Left, UiText::Right},
        ReadingChoice{UiText::ChapterScroll, &settings::ReadingSettings::chapterScrollReversed,
                      true, UiText::Reversed, UiText::Normal},
    };

    // The presentation provides geometry and a control; the settings remain the only mutable model.
    template<typename Draw>
    bool editReadingChoices(settings::ReadingSettings& config, Draw&& draw) {
        bool changed = false;
        size_t index = 0;
        const auto edit = [&](const auto& choice) {
            if (draw(index++, choice.label, choice.text(config))) {
                choice.advance(config);
                changed = true;
            }
        };
        std::apply([&](const auto&... choices) { (edit(choices), ...); }, kReadingChoices);
        return changed;
    }
} // namespace screens
