#include "ui/Ui.h"

namespace ui {
    bool Context::card(Rect rect, std::string_view title, std::string_view detail, uint8_t textSize,
                       themes::ColorRole role, Icon icon, bool enabled, uint8_t alpha) {
        rect = paintBounds(rect);
        if (rect.w <= 0 || rect.h <= 0)
            return false;
        auto state = signature(title, signature(detail));
        state = combine(state, textSize);
        state = combine(state, role);
        state = combine(state, static_cast<uint8_t>(icon));
        state = combine(state, enabled);
        state = combine(state, alpha);
        const Claim widget = claim(Kind::Card, rect, state);
        if (widget.changed) {
            const int16_t iconWidth = icon == Icon::None ? 0 : std::min<int16_t>(32, rect.w / 4);
            const int16_t x = rect.x + 6 + iconWidth;
            const int16_t width = std::max<int16_t>(0, rect.w - 12 - iconWidth);
            const int16_t detailHeight = detail.empty() ? 0 : std::min<int16_t>(36, rect.h / 3);
            const auto titleText = prepareFixedText({x, static_cast<int16_t>(rect.y + 3), width,
                                                     static_cast<int16_t>(rect.h - detailHeight - 6)},
                                                    title, textSize, TextAlign::Center, 8);
            const auto detailText =
                prepareFixedText({x, static_cast<int16_t>(rect.y + rect.h - detailHeight - 3), width, detailHeight},
                                 detail, 2, TextAlign::Center, 1);
            const Rect bounds = rect;
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const int16_t dx = rect.x - bounds.x, dy = rect.y - bounds.y;
                const auto surface = blend(themes::SurfaceActive, alpha / 2);
                const auto ink = blend(enabled ? themes::Foreground : themes::Muted, alpha);
                output.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 8, surface);
                output.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 8, blend(role, alpha));
                if (iconWidth)
                    drawIcon(output, {static_cast<int16_t>(rect.x + 6), rect.y, iconWidth, rect.h}, icon,
                             blend(role, alpha), surface);
                drawText(output, titleText, ink, dx, dy);
                drawText(output, detailText, blend(role, alpha), dx, dy);
            });
        }
        return tapped(widget.index, rect, enabled);
    }

    bool Context::dockItem(Rect rect, std::string_view label, Icon icon, uint16_t accent) {
        rect = paintBounds(rect);
        const uint32_t state = combine(combine(signature(label), accent), static_cast<uint8_t>(icon));
        const Claim widget = claim(Kind::Dock, rect, state);
        if (widget.changed) {
            const int16_t iconWidth = std::min<int16_t>(30, rect.w - 8);
            const int16_t x = label.empty() ? rect.x + (rect.w - iconWidth) / 2 : rect.x + 6;
            const auto labelText = prepareFixedText({static_cast<int16_t>(x + iconWidth + 3), rect.y,
                                                     static_cast<int16_t>(rect.w - iconWidth - 15), rect.h},
                                                    label, 2);
            const Rect bounds = rect;
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const int16_t dx = rect.x - bounds.x, dy = rect.y - bounds.y;
                const auto surface = color(label.empty() ? themes::Surface : themes::SurfaceActive);
                output.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 7, surface);
                output.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 7, accent);
                drawIcon(output,
                         {static_cast<int16_t>(x + dx), static_cast<int16_t>(rect.y + 4), iconWidth,
                          static_cast<int16_t>(rect.h - 8)},
                         icon, accent, surface);
                drawText(output, labelText, color(themes::Foreground), dx, dy);
            });
        }
        return tapped(widget.index, rect);
    }

} // namespace ui
