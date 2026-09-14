#include "ui/Ui.h"

#include <cmath>
#include <cstdio>

namespace ui {
    void Context::hourglass(Rect rect, uint16_t progress, bool paused, bool complete, ui::themes::ColorRole sandRole,
                            bool reversed, std::string_view time) {
        rect = paintBounds(rect);
        progress = std::min<uint16_t>(progress, 1000);
        uint32_t state = combine(progress, paused);
        state = combine(state, complete);
        state = combine(state, sandRole);
        state = combine(state, reversed);
        if constexpr (displayWriteAlignment() == 2)
            state = signature(time, state);
        const bool visualChanged = claim(Kind::Hourglass, rect, state).changed;
        const auto drawTime = [&] {
            if (time.empty())
                return;
            const Rect timeRect{static_cast<int16_t>(rect.x + (rect.w - 120) / 2), rect.y, 120, 28};
            if (claim(Kind::Label, timeRect, signature(time, state)).changed)
                drawText(timeRect, time, 3, color(sandRole), TextAlign::Center);
        };
        if (!visualChanged) {
            if constexpr (displayWriteAlignment() == 1)
                drawTime();
            return;
        }

        TextLayout timeText;
        if constexpr (displayWriteAlignment() == 2)
            timeText = prepareText({static_cast<int16_t>((rect.w - 120) / 2), 0, 120, 28}, time, 3, TextAlign::Center);
        constexpr int16_t segments = 14;
        const int16_t inset = std::max<int16_t>(3, std::min(rect.w, rect.h) / 12);
        const int16_t chamberWidth = std::max<int16_t>(1, (rect.w - 2 * inset - 1) / 2);
        const int16_t chamberHeight = std::max<int16_t>(3, (rect.h - 2 * inset - 1) / 2);
        const int16_t waist = std::max<int16_t>(2, chamberHeight / 14);
        const int16_t capWidth = std::max<int16_t>(8, std::min<int16_t>(16, rect.h / 8));
        std::array<int16_t, segments + 1> profile{};
        std::array<std::array<int16_t, 4>, segments * 4> outlineSegments{};
        if (rect.w > rect.h) {
            for (int16_t step = 0; step <= segments; ++step) {
                const int16_t offset = static_cast<int16_t>(chamberWidth * step / segments);
                int16_t curve = static_cast<int16_t>(offset * 100 / chamberWidth);
                curve = static_cast<int16_t>(curve * curve / 100);
                curve = static_cast<int16_t>(curve * curve / 100);
                profile[step] = static_cast<int16_t>(chamberHeight - (chamberHeight - waist) * curve / 100);
            }
            const int16_t right = rect.w - inset - 1;
            const int16_t centerY = inset + (rect.h - 2 * inset - 1) / 2;
            for (int16_t step = 1; step <= segments; ++step) {
                const int16_t previousOffset = chamberWidth * (step - 1) / segments;
                const int16_t offset = chamberWidth * step / segments;
                const int16_t previousHalf = profile[step - 1], half = profile[step];
                outlineSegments[(step - 1) * 4] = {static_cast<int16_t>(inset + previousOffset),
                                                   static_cast<int16_t>(centerY - previousHalf),
                                                   static_cast<int16_t>(inset + offset),
                                                   static_cast<int16_t>(centerY - half)};
                outlineSegments[(step - 1) * 4 + 1] = {static_cast<int16_t>(inset + previousOffset),
                                                       static_cast<int16_t>(centerY + previousHalf),
                                                       static_cast<int16_t>(inset + offset),
                                                       static_cast<int16_t>(centerY + half)};
                outlineSegments[(step - 1) * 4 + 2] = {static_cast<int16_t>(right - previousOffset),
                                                       static_cast<int16_t>(centerY - previousHalf),
                                                       static_cast<int16_t>(right - offset),
                                                       static_cast<int16_t>(centerY - half)};
                outlineSegments[(step - 1) * 4 + 3] = {static_cast<int16_t>(right - previousOffset),
                                                       static_cast<int16_t>(centerY + previousHalf),
                                                       static_cast<int16_t>(right - offset),
                                                       static_cast<int16_t>(centerY + half)};
            }
        }
        const int16_t pileWidth = static_cast<int16_t>(chamberWidth * 65 / 100);
        const uint32_t sourceTarget = static_cast<uint32_t>(1000 - progress) * pileWidth * pileWidth;
        int16_t sourceColumns = 0;
        if (rect.w > rect.h)
            while (sourceColumns < pileWidth
                   && static_cast<uint32_t>(sourceColumns + 1) * (sourceColumns + 1) * 1000U <= sourceTarget)
                ++sourceColumns;
        const int16_t receivedColumns =
            static_cast<int16_t>((static_cast<uint32_t>(pileWidth) * progress + 999U) / 1000U);
        const int16_t plateauColumns = receivedColumns == 0 ? 0 : std::max<int16_t>(1, receivedColumns / 6);
        const int16_t slopeColumns = std::max<int16_t>(1, receivedColumns - plateauColumns);
        const auto halfAt = [&](int16_t offsetFromBase) {
            const int32_t scaled =
                static_cast<int32_t>(std::clamp<int16_t>(offsetFromBase, 0, chamberWidth)) * segments;
            const int16_t step = static_cast<int16_t>(scaled / chamberWidth);
            if (step >= segments)
                return profile[segments];
            const int32_t remainder = scaled - static_cast<int32_t>(step) * chamberWidth;
            return static_cast<int16_t>(profile[step]
                                        + static_cast<int32_t>(profile[step + 1] - profile[step]) * remainder
                                              / chamberWidth);
        };
        paint(rect, [&](Arduino_GFX& output, Rect rect) {
            const auto drawStripTime = [&] {
                if constexpr (displayWriteAlignment() == 2)
                    drawText(output, timeText, color(sandRole), rect.x, rect.y);
            };
            const uint16_t ink = color(paused ? ui::themes::ColorRole::Muted : sandRole);
            const uint16_t outline = color(ui::themes::ColorRole::Foreground);
            const int16_t left = static_cast<int16_t>(rect.x + inset);
            const int16_t right = static_cast<int16_t>(rect.x + rect.w - inset - 1);
            const int16_t top = static_cast<int16_t>(rect.y + inset);
            const int16_t bottom = static_cast<int16_t>(rect.y + rect.h - inset - 1);
            const int16_t centerX = static_cast<int16_t>(left + (right - left) / 2);
            const int16_t centerY = static_cast<int16_t>(top + (bottom - top) / 2);

            if (rect.w > rect.h) {
                const int16_t baseTop = static_cast<int16_t>(rect.y + 2);
                const int16_t baseHeight = static_cast<int16_t>(rect.h - 4);
                const uint16_t base = color(ui::themes::ColorRole::SurfaceActive);
                output.fillRoundRect(static_cast<int16_t>(left - capWidth / 2), baseTop, capWidth, baseHeight,
                                     capWidth / 2, base);
                output.drawRoundRect(static_cast<int16_t>(left - capWidth / 2), baseTop, capWidth, baseHeight,
                                     capWidth / 2, outline);
                output.fillRoundRect(static_cast<int16_t>(right - capWidth / 2), baseTop, capWidth, baseHeight,
                                     capWidth / 2, base);
                output.drawRoundRect(static_cast<int16_t>(right - capWidth / 2), baseTop, capWidth, baseHeight,
                                     capWidth / 2, outline);

                for (const auto& segment: outlineSegments) {
                    const int16_t x0 = rect.x + segment[0], y0 = rect.y + segment[1];
                    const int16_t x1 = rect.x + segment[2], y1 = rect.y + segment[3];
                    // Reject whole segments without changing Bresenham endpoints or thick-edge pixels.
                    if (std::max(x0, x1) < 0 || std::min(x0, x1) >= output.width() || std::max(y0, y1) + 1 < 0
                        || std::min(y0, y1) - 1 >= output.height())
                        continue;
                    for (int16_t thickness = -1; thickness <= 1; ++thickness) {
                        output.drawLine(x0, static_cast<int16_t>(y0 + thickness), x1,
                                        static_cast<int16_t>(y1 + thickness), outline);
                    }
                }
                const int16_t leftGlassEdge = static_cast<int16_t>(left + capWidth / 2 + 2);
                const int16_t rightGlassEdge = static_cast<int16_t>(right - capWidth / 2 - 2);
                for (int16_t column = 1; column <= sourceColumns; ++column) {
                    const int16_t x =
                        reversed ? static_cast<int16_t>(centerX + column) : static_cast<int16_t>(centerX - column);
                    if (x < 0 || x >= output.width())
                        continue;
                    const int16_t half = halfAt(static_cast<int16_t>(chamberWidth - column));
                    output.drawFastVLine(x, static_cast<int16_t>(centerY - half + 3),
                                         std::max<int16_t>(1, static_cast<int16_t>(half * 2 - 5)), ink);
                }
                const int16_t receivedBaseHalf = receivedColumns == 0 ? 0 : chamberHeight;
                for (int16_t column = 1; column <= receivedColumns; ++column) {
                    const int16_t x = reversed ? static_cast<int16_t>(leftGlassEdge + column)
                                               : static_cast<int16_t>(rightGlassEdge - column);
                    if (x < 0 || x >= output.width())
                        continue;
                    const int16_t pileHalf =
                        column <= plateauColumns
                            ? receivedBaseHalf
                            : static_cast<int16_t>(receivedBaseHalf * (receivedColumns - column) / slopeColumns);
                    const int16_t half = std::min(halfAt(column), pileHalf);
                    output.drawFastVLine(x, static_cast<int16_t>(centerY - half + 3),
                                         std::max<int16_t>(1, static_cast<int16_t>(half * 2 - 5)), ink);
                }
                if (!paused && progress > 0 && progress < 1000) {
                    if (reversed) {
                        const int16_t streamX = static_cast<int16_t>(left + receivedColumns + 1);
                        output.drawFastHLine(streamX, centerY,
                                             std::max<int16_t>(1, static_cast<int16_t>(centerX - streamX)), ink);
                    } else {
                        output.drawFastHLine(static_cast<int16_t>(centerX + 1), centerY,
                                             std::max<int16_t>(1, static_cast<int16_t>(right - receivedColumns - centerX
                                                                                       - 2)),
                                             ink);
                    }
                }
                if (paused) {
                    output.fillRect(static_cast<int16_t>(centerX - 6), static_cast<int16_t>(bottom - 17), 4, 13, ink);
                    output.fillRect(static_cast<int16_t>(centerX + 2), static_cast<int16_t>(bottom - 17), 4, 13, ink);
                } else if (complete) {
                    output.drawLine(static_cast<int16_t>(centerX - 7), static_cast<int16_t>(bottom - 11),
                                    static_cast<int16_t>(centerX - 2), static_cast<int16_t>(bottom - 6), ink);
                    output.drawLine(static_cast<int16_t>(centerX - 2), static_cast<int16_t>(bottom - 6),
                                    static_cast<int16_t>(centerX + 8), static_cast<int16_t>(bottom - 17), ink);
                }
                markDrawn();
                drawStripTime();
                return;
            }

            const int16_t chamberHeight = std::max<int16_t>(1, static_cast<int16_t>(centerY - top - 3));

            output.drawFastHLine(left, top, static_cast<int16_t>(right - left + 1), outline);
            output.drawFastHLine(left, bottom, static_cast<int16_t>(right - left + 1), outline);
            output.drawLine(left, static_cast<int16_t>(top + 1), centerX, centerY, outline);
            output.drawLine(right, static_cast<int16_t>(top + 1), centerX, centerY, outline);
            output.drawLine(centerX, centerY, left, static_cast<int16_t>(bottom - 1), outline);
            output.drawLine(centerX, centerY, right, static_cast<int16_t>(bottom - 1), outline);

            const int16_t topRows = static_cast<int16_t>(chamberHeight * (1000 - progress) / 1000);
            for (int16_t row = 0; row < topRows; ++row) {
                const int16_t y = static_cast<int16_t>(centerY - 2 - row);
                const int16_t half =
                    std::max<int16_t>(1, static_cast<int16_t>((right - left) * (row + 1) / (2 * chamberHeight)));
                output.drawFastHLine(static_cast<int16_t>(centerX - half), y, static_cast<int16_t>(half * 2 + 1), ink);
            }
            const int16_t bottomRows = static_cast<int16_t>(chamberHeight * progress / 1000);
            for (int16_t row = 0; row < bottomRows; ++row) {
                const int16_t y = static_cast<int16_t>(bottom - 2 - row);
                const int16_t half = std::max<int16_t>(1, static_cast<int16_t>((right - left) * (bottomRows - row)
                                                                               / (2 * chamberHeight)));
                output.drawFastHLine(static_cast<int16_t>(centerX - half), y, static_cast<int16_t>(half * 2 + 1), ink);
            }
            if (!paused && progress > 0 && progress < 1000)
                output.drawFastVLine(centerX, static_cast<int16_t>(centerY + 1),
                                     std::max<int16_t>(1, static_cast<int16_t>(bottom - bottomRows - centerY - 2)),
                                     ink);
            markDrawn();
            drawStripTime();
        });
        if constexpr (displayWriteAlignment() == 1)
            drawTime();
    }

} // namespace ui
