#pragma once

#include <cstddef>
#include <cstdint>

#include "ui/fonts/Font.h"

enum class PauseMode : uint8_t {
    SentenceEnd,
    Instant,
    Count,
};

enum class FooterMetric : uint8_t {
    Percentage,
    ChapterTime,
    BookTime,
    Count,
};

enum class BatteryLabel : uint8_t {
    Percentage,
    TimeRemaining,
    Voltage,
    Count,
};

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
    FooterMetric footerMetric = FooterMetric::Percentage;
    BatteryLabel batteryLabel = BatteryLabel::Percentage;
    bool phantomWords = true;
    bool chapterScrollReversed = false;
    bool leftHanded = false;
    bool batteryVisibleWhileReading = true;
    bool chapterVisibleWhileReading = false;
    bool progressVisibleWhileReading = false;
};

struct ReaderSession {
    ReaderSettings settings;
    uint32_t wpmFeedbackUntilMs = 0;
    bool playLocked = false;
    bool pauseAtSentenceEndRequested = false;
};
