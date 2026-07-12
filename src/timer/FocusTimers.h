#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace focus {

    constexpr size_t kMaxTimers = 6;
    constexpr size_t kMaxTimerNameBytes = 14;

    struct Timer {
        std::string name;
        uint16_t focusMinutes = 25;
        uint16_t breakMinutes = 5;
        uint8_t rounds = 4;
    };

    struct Timers {
        std::array<Timer, kMaxTimers> items;
        size_t count = 0;
    };

    inline Timer defaultTimer() {
        return {.name = "Pomodoro", .focusMinutes = 25, .breakMinutes = 5, .rounds = 4};
    }

    bool valid(const Timer& timer);
    bool parse(std::string_view content, Timers& timers);
    std::string serialize(const Timers& timers);

} // namespace focus
