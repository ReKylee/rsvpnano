#pragma once
#include <array>
#include <cstdint>

namespace screens {
    inline constexpr uint32_t kStandbyPowerOffMs = 5UL * 60UL * 1000UL;
    inline constexpr std::array<uint32_t, 5> kStandbyDurationsMs{
        0, 1UL * 60UL * 1000UL, 5UL * 60UL * 1000UL, 15UL * 60UL * 1000UL, 30UL * 60UL * 1000UL,
    };
} // namespace screens
