#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "settings/SettingsRules.h"

namespace focus {

    constexpr size_t kMaxTimers = 6;
    constexpr size_t kMaxTimerNameBytes = 14;
    constexpr uint32_t kSchemaVersion = 1;

    struct Timer {
        std::string name;
        settings::BoundedValue<uint16_t, 1, 180> focusMinutes{25};
        settings::BoundedValue<uint16_t, 1, 60> breakMinutes{5};
        settings::BoundedValue<uint8_t, 1, 12> rounds{4};
    };

    struct Timers {
        uint32_t schemaVersion = kSchemaVersion;
        std::vector<Timer> timers;
    };

    Timer defaultTimer();
    Timers defaultTimers();

    bool valid(const Timer& timer);
    bool valid(const Timers& timers);
    bool decodeToml(std::string_view content, Timers& timers);
    std::string encodeToml(const Timers& timers);

} // namespace focus
