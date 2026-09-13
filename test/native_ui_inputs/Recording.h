#pragma once

#include <optional>
#include <string>
#include <vector>
#include "ui/Ui.h"

namespace testui {
    enum class Kind { Setting, Card, Button, Rotary, Slider, Stepper, Label, Separator, Tab, Dock };
    struct Call {
        Kind kind;
        ui::Rect rect;
        std::string label;
        std::string value;
        ui::SettingLayout layout = ui::SettingLayout::Inline;
        uint8_t textSize = 0;
    };
    struct Recording {
        std::vector<Call> calls;
        std::string activate;
        std::optional<int> numericValue;
        std::optional<int> renderedNumericValue;
        int translations = 0;
        int minimum = 0, maximum = 0, step = 0;
        size_t pageFirst = 0;
        size_t pageCapacity = 5;
    };
    inline Recording recording;
    std::string_view caption(UiText key);
    bool record(Kind kind, ui::Rect rect, std::string_view label, std::string_view value = {},
                ui::SettingLayout layout = ui::SettingLayout::Inline, uint8_t size = 0, bool enabled = true);
}
