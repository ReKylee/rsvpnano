#pragma once

#include <cstdint>
#include <cstddef>

#include "ui/fonts/Font.h"

enum class PauseMode : uint8_t { SentenceEnd, Instant };

struct ReaderTypography {
    bool focusHighlight = true;
    int8_t tracking = 0;
    uint8_t anchor = 30;
    uint8_t guideWidth = 30;
    uint8_t guideGap = 5;
};

struct ReaderSettings {
    ui::fonts::Font font;
    ReaderTypography typography;
    uint8_t fontSizeIndex = 0;
    uint8_t typefaceIndex = 0;
    PauseMode pauseMode = PauseMode::SentenceEnd;
    bool phantomWords = true;
};

struct ReaderSession {
    ReaderSettings settings;
    uint32_t wpmFeedbackUntilMs = 0;
    bool playLocked = false;
    bool pauseAtSentenceEndRequested = false;
};
