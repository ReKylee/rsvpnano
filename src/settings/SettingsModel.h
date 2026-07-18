#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "settings/SettingsRules.h"
#include "standby/ScreensaverTypes.h"
#include "ui/Localization.h"

namespace settings {

    // Persisted enum spellings are their TOML/JSON names.
    enum class PauseMode : uint8_t {
        sentenceEnd,
        instant,
        Count,
    };

    enum class FooterMetric : uint8_t {
        percentage,
        chapterTime,
        bookTime,
        Count,
    };

    enum class BatteryLabel : uint8_t {
        percentage,
        timeRemaining,
        voltage,
        Count,
    };

    struct TypographySettings {
        std::string fontId = "literata";
        BoundedValue<uint8_t, 0, 2> fontSizeIndex{0};
        bool focusHighlight = true;
        BoundedValue<int, -2, 3> tracking{0};
        BoundedValue<uint8_t, 30, 40> anchor{30};
        BoundedValue<uint8_t, 12, 30, 2> guideWidth{30};
        BoundedValue<uint8_t, 2, 8> guideGap{5};

        bool operator==(const TypographySettings&) const = default;
    };

    inline const TypographySettings& effectiveTypography(const std::optional<TypographySettings>& bookOverride,
                                                         const TypographySettings& theme) {
        return bookOverride ? *bookOverride : theme;
    }

    struct PacingSettings {
        BoundedValue<uint16_t, 0, 600, 50> longWordDelayMs{200};
        BoundedValue<uint16_t, 0, 600, 50> complexWordDelayMs{200};
        BoundedValue<uint16_t, 0, 600, 50> punctuationDelayMs{200};

        bool operator==(const PacingSettings&) const = default;
    };

    struct ReadingSettings {
        BoundedValue<uint16_t, 10, 1000, 10> wpm{300};
        PauseMode pauseMode = PauseMode::sentenceEnd;
        bool phantomWords = true;
        bool chapterScrollReversed = false;
        FooterMetric footerMetric = FooterMetric::percentage;
        BatteryLabel batteryLabel = BatteryLabel::percentage;
        bool batteryIconVisible = true;
        bool batteryVisibleWhileReading = true;
        bool chapterVisibleWhileReading = false;
        bool progressVisibleWhileReading = false;
        bool leftHanded = false;
        TypographySettings typography;
        PacingSettings pacing;

        bool operator==(const ReadingSettings&) const = default;
    };

    struct InterfaceSettings {
        BoundedValue<uint8_t, 5, 100, 5> brightnessPercent{70};
        UiLanguage language = UiLanguage::english;
        BoundedValue<uint8_t, 0, 4> standbyTimerIndex{1};
        standby::Kind screensaver = standby::Kind::life;
        std::string selectedThemeId = "default";

        bool operator==(const InterfaceSettings&) const = default;
    };

    struct NetworkSettings {
        std::string wifiSsid;

        bool operator==(const NetworkSettings&) const = default;
    };

    struct UpdateSettings {
        bool automatic = false;
        std::string repositoryOwner;
        std::string releaseTag;

        bool operator==(const UpdateSettings&) const = default;
    };

    struct DeviceSettings {
        ReadingSettings reading;
        InterfaceSettings interface;
        NetworkSettings network;
        UpdateSettings updates;

        bool operator==(const DeviceSettings&) const = default;
    };

    struct DeviceSecrets {
        std::string wifiPassword;

        bool operator==(const DeviceSecrets&) const = default;
    };

} // namespace settings
