#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "settings/SettingsRules.h"

namespace focus {

    constexpr size_t kMaxConfigBytes = 4096;
    constexpr size_t kMaxTimers = 6;
    constexpr size_t kMaxTimerNameBytes = 14;
    struct Timer {
        std::string name;
        settings::BoundedValue<uint16_t, 1, 180> focusMinutes{25};
        settings::BoundedValue<uint16_t, 1, 60> breakMinutes{5};
        settings::BoundedValue<uint8_t, 1, 12> rounds{4};
    };

    struct Timers {
        std::vector<Timer> timers;
    };

    Timer defaultTimer();
    Timers defaultTimers();

    bool valid(const Timer& timer);
    bool valid(const Timers& timers);
    std::expected<Timers, std::error_code> decodeToml(std::string_view content);
    std::expected<std::string, std::error_code> encodeToml(const Timers& timers);

} // namespace focus
