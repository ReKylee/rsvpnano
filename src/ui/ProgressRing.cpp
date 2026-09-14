#include "ui/Ui.h"

#include <cmath>

namespace ui {
    void Context::progressRing(Rect rect, int value, int maximum, themes::ColorRole role) {
        rect = paintBounds(rect);
        if (rect.w < 8 || rect.h < 8)
            return;
        const int percent = maximum > 0 ? static_cast<int>(std::clamp<int64_t>(100LL * value / maximum, 0, 100)) : 0;
        if (!claim(Kind::Ring, rect, combine(combine(signature("ring"), percent), role)).changed)
            return;
        const int16_t radius = std::min(rect.w, rect.h) / 2 - 2;
        const int16_t cx = rect.x + rect.w / 2, cy = rect.y + rect.h / 2;
        constexpr int segments = 60;
        std::array<std::array<int16_t, 4>, segments> ticks{};
        for (int i = 0; i < segments; ++i) {
            const float angle = (i * 6 - 90) * 0.01745329252f;
            ticks[i] = {static_cast<int16_t>(cx + std::cos(angle) * (radius - 4)),
                        static_cast<int16_t>(cy + std::sin(angle) * (radius - 4)),
                        static_cast<int16_t>(cx + std::cos(angle) * radius),
                        static_cast<int16_t>(cy + std::sin(angle) * radius)};
        }
        char label[6];
        std::snprintf(label, sizeof(label), "%d%%", percent);
        const auto labelText =
            prepareText({static_cast<int16_t>(rect.x + 7), rect.y, static_cast<int16_t>(rect.w - 14), rect.h}, label, 2,
                        TextAlign::Center);
        const Rect bounds = rect;
        paint(rect, [&](Arduino_GFX& output, Rect rect) {
            const int16_t dx = rect.x - bounds.x, dy = rect.y - bounds.y;
            for (int i = 0; i < segments; ++i) {
                const auto& tick = ticks[i];
                const auto ink = color(i * 100 < percent * segments ? role : themes::ProgressTrack);
                output.drawLine(tick[0] + dx, tick[1] + dy, tick[2] + dx, tick[3] + dy, ink);
            }
            drawText(output, labelText, color(role), dx, dy);
        });
    }

} // namespace ui
