#pragma once

#include <algorithm>
#include <cstdint>

namespace Board::Backlight {

    constexpr uint8_t kMinimumLogicalPercent = 1;
    constexpr uint8_t kMaximumLogicalPercent = 100;
    constexpr uint8_t kDefaultMinimumDuty = 1;
    constexpr uint8_t kMaximumDuty = 255;

    constexpr uint8_t sanitizePercent(uint8_t percent) noexcept {
        return std::clamp<uint8_t>(percent, kMinimumLogicalPercent, kMaximumLogicalPercent);
    }

    constexpr uint8_t dutyFromPercent(uint8_t percent, uint8_t minimumDuty = kDefaultMinimumDuty,
                                      uint8_t maximumDuty = kMaximumDuty) noexcept {
        const auto [lowDuty, highDuty] = std::minmax(minimumDuty, maximumDuty);
        if (lowDuty == highDuty) {
            return lowDuty;
        }

        constexpr uint16_t kPercentRange = kMaximumLogicalPercent - kMinimumLogicalPercent;

        const uint16_t dutyRange = static_cast<uint16_t>(highDuty - lowDuty);
        const uint16_t percentAboveMin = static_cast<uint16_t>(sanitizePercent(percent) - kMinimumLogicalPercent);

        const uint16_t scaled = percentAboveMin * dutyRange + kPercentRange / 2U;
        return static_cast<uint8_t>(lowDuty + scaled / kPercentRange);
    }

} // namespace Board::Backlight
