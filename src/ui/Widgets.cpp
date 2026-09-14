#include "ui/Ui.h"

namespace ui {
    void Context::label(Rect rect, std::string_view text, uint8_t textSize, ui::themes::ColorRole role, TextAlign align,
                        uint8_t textLines, std::string_view textLocale, uint8_t alpha) {
        rect = paintBounds(rect);
        uint32_t state = combine(signature(text), textSize);
        state = combine(state, role);
        state = combine(state, static_cast<uint8_t>(align));
        state = combine(state, textLines);
        state = signature(textLocale, state);
        state = combine(state, alpha);
        if (!claim(Kind::Label, rect, state).changed) {
            return;
        }
        drawText(rect, text, textSize, blend(role, alpha), align, textLines, textLocale);
    }

    void Context::separator(Rect rect, std::string_view text) {
        rect = paintBounds(rect);
        if (!claim(Kind::Separator, rect, signature(text)).changed)
            return;

        const int16_t labelWidth = std::min<int16_t>(rect.w, textWidthFor(text, 1));
        const auto label = prepareText({0, 0, labelWidth, rect.h}, text, 1);
        paint(rect, [&](Arduino_GFX& output, Rect rect) {
            drawText(output, label, color(ui::themes::ColorRole::Muted), rect.x, rect.y);
            const int16_t lineX = static_cast<int16_t>(rect.x + labelWidth + 6);
            if (lineX < rect.x + rect.w)
                output.drawFastHLine(lineX, static_cast<int16_t>(rect.y + rect.h / 2),
                                     static_cast<int16_t>(rect.x + rect.w - lineX),
                                     blend(ui::themes::ColorRole::Muted, 96));
            markDrawn();
        });
    }

    bool Context::setting(Rect rect, std::string_view label, std::string_view value, SettingLayout layout) {
        rect = paintBounds(rect);
        const size_t slot = nextSlot_;
        uint32_t state = signature(value, signature(label));
        state = combine(state, static_cast<uint8_t>(layout));
        if (claim(Kind::Setting, rect, state).changed) {
            TextLayout labelText, valueText;
            const int16_t textWidth = std::max<int16_t>(0, static_cast<int16_t>(rect.w - 14));
            if (layout == SettingLayout::Inline) {
                const int16_t labelRequired = textWidthFor(label, 2);
                uint8_t valueSize = 2;
                int16_t valueRequired = textWidthFor(value, 2);
                if (labelRequired + valueRequired + 8 > textWidth) {
                    valueSize = 1;
                    valueRequired = textWidthFor(value, 1);
                }
                const int16_t labelWidth = labelRequired + valueRequired + 8 <= textWidth
                                             ? labelRequired
                                             : std::min<int16_t>(labelRequired, textWidth / 2);
                const int16_t valueWidth = std::max<int16_t>(0, static_cast<int16_t>(textWidth - labelWidth - 8));
                labelText = prepareText({7, 0, labelWidth, rect.h}, label, 2);
                valueText = prepareText({static_cast<int16_t>(rect.w - valueWidth - 7), 0, valueWidth, rect.h}, value,
                                        valueSize, TextAlign::Right);
            } else {
                const bool largeValue = textWidthFor(value, 2) <= textWidth;
                labelText = prepareText({7, 3, textWidth, 8}, label, 1);
                valueText =
                    prepareText({7, 11, textWidth, static_cast<int16_t>(std::max<int16_t>(0, rect.h - 13))}, value,
                                largeValue ? 2 : 1, TextAlign::Start, !largeValue && rect.h >= 32 ? 2 : 1);
            }
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
                output.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
                output.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, color(ui::themes::ColorRole::Outline));
                drawText(output, labelText, color(layout == SettingLayout::Inline ? themes::Foreground : themes::Muted),
                         rect.x, rect.y);
                drawText(output, valueText, color(themes::Accent), rect.x, rect.y);
            });
        }
        return tapped(slot, rect);
    }

    bool Context::toggle(Rect rect, std::string_view label, bool& enabled) {
        rect = paintBounds(rect);
        const size_t slot = nextSlot_;
        uint32_t state = combine(signature(label), enabled);
        if (claim(Kind::Toggle, rect, state).changed) {
            constexpr int16_t switchWidth = 34;
            const auto labelText =
                prepareText({7, 0, static_cast<int16_t>(std::max<int16_t>(0, rect.w - switchWidth - 21)), rect.h},
                            label, 2);
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
                output.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
                output.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, color(ui::themes::ColorRole::Outline));
                const int16_t switchX = static_cast<int16_t>(rect.x + rect.w - switchWidth - 7);
                const int16_t switchY = static_cast<int16_t>(rect.y + (rect.h - 16) / 2);
                output.fillRoundRect(switchX, switchY, switchWidth, 16, 8,
                                     color(enabled ? ui::themes::ColorRole::Accent
                                                   : ui::themes::ColorRole::ProgressTrack));
                output.fillCircle(static_cast<int16_t>(switchX + (enabled ? switchWidth - 8 : 8)),
                                  static_cast<int16_t>(switchY + 8), 6, color(ui::themes::ColorRole::Foreground));
                drawText(output, labelText, color(themes::Foreground), rect.x, rect.y);
            });
        }
        if (!tapped(slot, rect))
            return false;
        enabled = !enabled;
        return true;
    }

    bool Context::button(Rect rect, std::string_view text, bool enabled, Icon icon, uint8_t textLines,
                         std::string_view detailLeft, std::string_view detailRight) {
        rect = paintBounds(rect);
        const size_t slot = nextSlot_;
        const bool activated = tapped(slot, rect, enabled);
        uint32_t state = combine(signature(text), enabled);
        state = combine(state, static_cast<uint8_t>(icon));
        state = combine(state, textLines);
        state = signature(detailLeft, state);
        state = signature(detailRight, state);
        const Claim widget = claim(Kind::Button, rect, state);
        if (widget.changed) {
            const int16_t iconWidth = icon == Icon::None ? 0 : std::min<int16_t>(34, rect.w / 3);
            const bool hasDetail = !detailLeft.empty() || !detailRight.empty();
            const int16_t textHeight = hasDetail ? static_cast<int16_t>(rect.h - 18) : rect.h;
            const Rect textRect{6, 0, static_cast<int16_t>(std::max<int16_t>(0, rect.w - iconWidth - 12)), textHeight};
            const auto title = prepareText(textRect, text, 2, TextAlign::Center, textLines);
            TextLayout detail;
            if (hasDetail) {
                const int16_t detailY = static_cast<int16_t>(rect.h - 20);
                if (detailLeft.empty() || detailRight.empty()) {
                    detail = prepareText({textRect.x, detailY, textRect.w, 16},
                                         detailLeft.empty() ? detailRight : detailLeft, 2,
                                         detailLeft.empty() ? TextAlign::Right : TextAlign::Left);
                } else {
                    const int16_t detailWidth = static_cast<int16_t>((textRect.w - 8) / 2);
                    detail.lines.reserve(2);
                    appendText(detail, {textRect.x, detailY, detailWidth, 16}, detailLeft, 2, TextAlign::Start, 1, {});
                    appendText(detail,
                               {static_cast<int16_t>(textRect.x + textRect.w - detailWidth), detailY, detailWidth, 16},
                               detailRight, 2, TextAlign::Right, 1, {});
                }
            }
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
                output.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
                output.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5,
                                     color(enabled ? ui::themes::ColorRole::Outline
                                                   : ui::themes::ColorRole::ProgressTrack));
                if (enabled && rect.w > 16 && rect.h >= 28)
                    output.fillRect(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h - 3),
                                    static_cast<int16_t>(rect.w - 16), 2, color(ui::themes::ColorRole::Accent));
                drawText(output, title, color(enabled ? themes::Foreground : themes::Muted), rect.x, rect.y);
                drawText(output, detail, color(themes::Muted), rect.x, rect.y);
                if (icon != Icon::None)
                    drawIcon(output, {static_cast<int16_t>(rect.x + rect.w - iconWidth), rect.y, iconWidth, rect.h},
                             icon, color(enabled ? ui::themes::ColorRole::Accent : ui::themes::ColorRole::Muted),
                             surface);
            });
        }
        return activated;
    }

    bool Context::iconButton(Rect rect, Icon icon) {
        rect = paintBounds(rect);
        const size_t slot = nextSlot_;
        const bool activated = tapped(slot, rect);
        const uint32_t state = static_cast<uint8_t>(icon);
        const Claim widget = claim(Kind::Button, rect, state);
        if (widget.changed) {
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
                output.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 7, surface);
                output.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 7, color(ui::themes::ColorRole::Outline));
                drawIcon(output, rect, icon, color(ui::themes::ColorRole::Muted), surface);
            });
        }
        return activated;
    }

    bool Context::tab(Rect rect, std::string_view text, bool active, Icon icon) {
        rect = paintBounds(rect);
        uint32_t state = combine(signature(text), active);
        state = combine(state, static_cast<uint8_t>(icon));
        const Claim widget = claim(Kind::Tab, rect, state);
        if (widget.changed) {
            const int16_t iconWidth = icon == Icon::None ? 0 : std::min<int16_t>(26, rect.w / 3);
            const auto label = prepareText({static_cast<int16_t>(iconWidth + 8), 0,
                                            static_cast<int16_t>(rect.w - iconWidth - 12), rect.h},
                                           text, 2, TextAlign::Center);
            const uint16_t surface = color(active ? themes::Surface : themes::SurfaceMuted);
            paint(rect, surface, [&](Arduino_GFX& output, Rect rect) {
                output.drawRect(rect.x, rect.y, rect.w, rect.h, color(ui::themes::ColorRole::Outline));
                if (active) {
                    output.fillRect(rect.x, static_cast<int16_t>(rect.y + 5), 3, static_cast<int16_t>(rect.h - 10),
                                    color(ui::themes::ColorRole::Accent));
                }
                const uint16_t ink = color(active ? ui::themes::ColorRole::Foreground : ui::themes::ColorRole::Muted);
                if (icon != Icon::None)
                    drawIcon(output, {static_cast<int16_t>(rect.x + 7), rect.y, iconWidth, rect.h}, icon, ink, surface);
                drawText(output, label, ink, rect.x, rect.y);
            });
        }
        return tapped(widget.index, rect);
    }

    Context::BatteryLayout Context::batteryLayout(Rect rect, std::string_view labelText, bool showIcon) const {
        constexpr int16_t iconWidth = 29;
        constexpr int16_t iconHeight = 13;
        constexpr int16_t labelGap = 7;

        const int16_t iconAreaWidth = showIcon ? iconWidth + labelGap : 0;
        const int16_t labelWidth =
            std::min<int16_t>(textWidth(labelText, 2), std::max<int16_t>(0, rect.w - iconAreaWidth));
        const int16_t totalWidth = static_cast<int16_t>(iconAreaWidth + labelWidth);
        const int16_t x = std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + rect.w - totalWidth));

        return {{x, static_cast<int16_t>(rect.y + std::max<int16_t>(0, (rect.h - iconHeight) / 2)), iconWidth,
                 iconHeight},
                {static_cast<int16_t>(x + iconAreaWidth), rect.y, labelWidth, rect.h}};
    }

    void Context::battery(Rect rect, uint8_t percent, bool charging, std::string_view labelText, bool showIcon,
                          uint8_t iconAlpha, uint8_t labelAlpha) {
        rect = paintBounds(rect);
        percent = std::min<uint8_t>(percent, 100);
        uint32_t state = combine(signature(labelText), percent);
        state = combine(state, charging);
        state = combine(state, showIcon);
        state = combine(state, iconAlpha);
        state = combine(state, labelAlpha);
        if (!claim(Kind::Battery, rect, state).changed)
            return;
        const auto layout = batteryLayout({0, 0, rect.w, rect.h}, labelText, showIcon);
        const auto label = prepareText(layout.label, labelText, 2);
        paint(rect, [&](Arduino_GFX& output, Rect rect) {
            if (!showIcon && labelText.empty())
                return;
            if (showIcon)
                drawBatteryIcon(output,
                                {static_cast<int16_t>(layout.icon.x + rect.x),
                                 static_cast<int16_t>(layout.icon.y + rect.y), layout.icon.w, layout.icon.h},
                                percent, charging, blend(themes::Muted, iconAlpha), color(themes::Background));
            drawText(output, label, blend(themes::Muted, labelAlpha), rect.x, rect.y);
        });
    }

    void Context::progress(Rect rect, int value, int minimum, int maximum) {
        rect = paintBounds(rect);
        value = std::clamp(value, minimum, maximum);
        uint32_t state = combine(static_cast<uint32_t>(value), static_cast<uint32_t>(minimum));
        state = combine(state, static_cast<uint32_t>(maximum));
        if (!claim(Kind::Progress, rect, state).changed) {
            return;
        }
        paint(rect, color(themes::ProgressTrack), [&](Arduino_GFX& output, Rect rect) {
            if (maximum > minimum && rect.w > 2 && rect.h > 2) {
                const int16_t fill =
                    static_cast<int16_t>((static_cast<int32_t>(rect.w - 2) * (value - minimum)) / (maximum - minimum));
                output.fillRect(static_cast<int16_t>(rect.x + 1), static_cast<int16_t>(rect.y + 1), fill,
                                static_cast<int16_t>(rect.h - 2), color(ui::themes::ColorRole::Accent));
            }
        });
    }

    void Context::steps(Rect rect, uint8_t current, uint8_t total, ui::themes::ColorRole activeRole) {
        rect = paintBounds(rect);
        current = std::min(current, total);
        uint32_t state = combine(current, total);
        state = combine(state, activeRole);
        if (!claim(Kind::Steps, rect, state).changed)
            return;

        paint(rect, [&](Arduino_GFX& output, Rect rect) {
            if (total == 0)
                return;
            const bool vertical = rect.h > rect.w;
            const int16_t crossSize = vertical ? rect.w : rect.h;
            const int16_t radius =
                std::max<int16_t>(2, std::min<int16_t>(4, static_cast<int16_t>((crossSize - 2) / 2)));
            const int16_t spacing = static_cast<int16_t>(radius * 2 + 5);
            const int16_t length = static_cast<int16_t>((total - 1) * spacing + radius * 2);
            const int16_t first = static_cast<int16_t>((vertical ? rect.y : rect.x)
                                                       + ((vertical ? rect.h : rect.w) - length) / 2 + radius);
            const int16_t center = static_cast<int16_t>((vertical ? rect.x : rect.y) + crossSize / 2);
            for (uint8_t index = 0; index < total; ++index) {
                const int16_t position = static_cast<int16_t>(first + index * spacing);
                const int16_t x = vertical ? center : position;
                const int16_t y = vertical ? position : center;
                if (index < current)
                    output.fillCircle(x, y, radius, color(activeRole));
                else
                    output.drawCircle(x, y, radius, color(ui::themes::ColorRole::Outline));
            }
        });
    }

} // namespace ui
