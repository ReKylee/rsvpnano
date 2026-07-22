#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>

namespace BoardDrivers::BatteryCurve {

    inline uint8_t percentForVoltage(float voltage) {
        struct Point {
            float voltage;
            uint8_t percent;
        };

        constexpr Point kCurve[] = {
            {3.30f, 0},  {3.50f, 5},  {3.60f, 10}, {3.65f, 20}, {3.70f, 30}, {3.75f, 40},
            {3.79f, 50}, {3.85f, 60}, {3.92f, 70}, {4.00f, 80}, {4.10f, 90}, {4.15f, 100},
        };

        if (voltage <= kCurve[0].voltage) {
            return kCurve[0].percent;
        }
        constexpr size_t curveSize = std::size(kCurve);
        if (voltage >= kCurve[curveSize - 1].voltage) {
            return kCurve[curveSize - 1].percent;
        }

        const auto upper = std::ranges::lower_bound(kCurve, voltage, {}, &Point::voltage);
        const Point& lower = *std::prev(upper);
        const float span = upper->voltage - lower.voltage;
        const float ratio = span <= 0.0f ? 0.0f : (voltage - lower.voltage) / span;
        const int percent = static_cast<int>(lower.percent + (upper->percent - lower.percent) * ratio + 0.5f);
        return static_cast<uint8_t>(std::clamp(percent, 0, 100));
    }

} // namespace BoardDrivers::BatteryCurve
