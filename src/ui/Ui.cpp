#include "ui/Ui.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "fonts/UiFont6x9.h"
#include "text/Utf8Text.h"

namespace ui {
    namespace {

        constexpr uint16_t kFallbackBlack = 0x0000;
        constexpr uint16_t kFallbackWhite = 0xFFFF;

        constexpr uint8_t kUiFontCellWidth = 6;
        constexpr uint8_t kUiFontHeight = 9;

        size_t fittedLength(std::string_view text, size_t capacity) {
            if (Utf8Text::count(text) <= capacity)
                return text.size();
            if (capacity <= 3)
                return 0;
            return Utf8Text::prefixBytes(text, capacity - 3);
        }

    } // namespace

    Context::Context(Arduino_GFX& gfx) : gfx_(gfx) {}

    int16_t Context::textWidth(std::string_view text, uint8_t size) {
        const int32_t width =
            static_cast<int32_t>(Utf8Text::count(text)) * kUiFontCellWidth * std::max<uint8_t>(1, size);
        return static_cast<int16_t>(std::min<int32_t>(width, INT16_MAX));
    }

    int16_t Context::textHeight(uint8_t size) {
        return static_cast<int16_t>(kUiFontHeight * std::max<uint8_t>(1, size));
    }

    void Context::setTheme(const ui::themes::Theme& theme) {
        if (theme_ != &theme) {
            theme_ = &theme;
            invalidate();
        }
    }

    void Context::setLanguage(UiLanguage language) {
        language = Localization::sanitizeLanguage(static_cast<uint8_t>(language));
        if (language_ != language) {
            language_ = language;
            invalidate();
        }
    }

    void Context::setOrientation(Orientation orientation) {
        if (touchOrientation_ == orientation)
            return;
        touchOrientation_ = orientation;
        gfx_.setRotation(static_cast<uint8_t>(orientation));
        resetTouchGesture();
        invalidate();
    }

    std::string_view Context::text(UiText key) const {
        return Localization::text(language_, key);
    }

    void Context::setTouchSource(TouchSource source, uint32_t nowMs) {
        touchSource_ = source;
        beginTouch(nowMs);
    }

    bool Context::pollTouch(uint32_t nowMs) {
        touchPending_ = false;
        if (touchSource_.read == nullptr)
            return false;

        if (!touchInitialized_) {
            if (nowMs >= touchBackoffUntilMs_ && !beginTouch(nowMs)) {
                touchBackoffUntilMs_ = nowMs + touchSource_.timing.recoveryRetryMs;
            }
            return false;
        }
        if (nowMs < touchBackoffUntilMs_ || nowMs - touchLastPollMs_ < touchSource_.timing.pollIntervalMs)
            return false;
        touchLastPollMs_ = nowMs;

        TouchContact contact;
        // IRQ starts a contact; the controller packet, not the IRQ level, ends it.
        if (!touchActive_ && touchSource_.ready != nullptr && !touchSource_.ready()) {
            contact = {};
        } else if (!touchSource_.read(contact)) {
            touchBackoffUntilMs_ = nowMs + touchSource_.timing.failureBackoffMs;
            if (++touchReadFailures_ >= touchSource_.timing.maxConsecutiveReadFailures) {
                touchInitialized_ = false;
                touchBackoffUntilMs_ = nowMs + touchSource_.timing.recoveryRetryMs;
                resetTouchGesture();
            }
            return false;
        } else {
            touchReadFailures_ = 0;
        }

        if (nowMs < touchIgnoreUntilMs_) {
            resetTouchGesture();
            return false;
        }
        return updateTouch(contact, nowMs);
    }

    void Context::beginFrame(uint8_t screen) {
        nextSlot_ = 0;
        drew_ = false;
        if (screen_ != screen) {
            screen_ = screen;
            invalid_ = true;
            capturedSlot_ = kSlotCapacity;
        }
        if (invalid_) {
            gfx_.fillScreen(color(ui::themes::ColorRole::Background));
            markDrawn();
            for (Slot& slot: slots_) {
                slot.valid = false;
            }
            slotCount_ = 0;
            invalid_ = false;
            drew_ = true;
        }
    }

    void Context::endFrame() {
        for (size_t index = nextSlot_; index < slotCount_; ++index) {
            if (slots_[index].valid) {
                if (slots_[index].kind != Kind::Touch)
                    clear(slots_[index].rect);
                slots_[index].valid = false;
            }
        }
        if (capturedSlot_ >= nextSlot_) {
            capturedSlot_ = kSlotCapacity;
        }
        slotCount_ = std::min(nextSlot_, kSlotCapacity);
        if (drew_)
            gfx_.flush();
        touchPending_ = false;
    }

    void Context::invalidate() {
        invalid_ = true;
    }

    void Context::label(Rect rect, std::string_view text, uint8_t textSize, ui::themes::ColorRole role, TextAlign align,
                        uint8_t textLines) {
        uint32_t state = combine(signature(text), textSize);
        state = combine(state, role);
        state = combine(state, static_cast<uint8_t>(align));
        state = combine(state, textLines);
        if (!claim(Kind::Label, rect, state).changed) {
            return;
        }
        drawText(rect, text, textSize, color(role), align, textLines);
    }

    void Context::separator(Rect rect, std::string_view text) {
        if (!claim(Kind::Separator, rect, signature(text)).changed)
            return;

        gfx_.fillRect(rect.x, rect.y, rect.w, rect.h, color(ui::themes::ColorRole::Background));
        const int16_t labelWidth = std::min<int16_t>(rect.w, Context::textWidth(text, 1));
        drawText({rect.x, rect.y, labelWidth, rect.h}, text, 1, color(ui::themes::ColorRole::Muted));
        const int16_t lineX = static_cast<int16_t>(rect.x + labelWidth + 6);
        if (lineX < rect.x + rect.w)
            gfx_.drawFastHLine(lineX, static_cast<int16_t>(rect.y + rect.h / 2),
                               static_cast<int16_t>(rect.x + rect.w - lineX), blend(ui::themes::ColorRole::Muted, 96));
        markDrawn();
    }

    bool Context::setting(Rect rect, std::string_view label, std::string_view value, SettingLayout layout) {
        const size_t slot = nextSlot_;
        uint32_t state = signature(value, signature(label));
        state = combine(state, static_cast<uint8_t>(layout));
        if (claim(Kind::Setting, rect, state).changed) {
            const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
            gfx_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
            gfx_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, color(ui::themes::ColorRole::Outline));
            const int16_t textWidth = std::max<int16_t>(0, static_cast<int16_t>(rect.w - 14));
            if (layout == SettingLayout::Inline) {
                const int16_t labelRequired = Context::textWidth(label, 2);
                uint8_t valueSize = 2;
                int16_t valueRequired = Context::textWidth(value, 2);
                if (labelRequired + valueRequired + 8 > textWidth) {
                    valueSize = 1;
                    valueRequired = Context::textWidth(value, 1);
                }
                const int16_t labelWidth = labelRequired + valueRequired + 8 <= textWidth
                                             ? labelRequired
                                             : std::min<int16_t>(labelRequired, textWidth / 2);
                const int16_t valueWidth = std::max<int16_t>(0, static_cast<int16_t>(textWidth - labelWidth - 8));
                drawText({static_cast<int16_t>(rect.x + 7), rect.y, labelWidth, rect.h}, label, 2,
                         color(ui::themes::ColorRole::Foreground));
                drawText({static_cast<int16_t>(rect.x + rect.w - valueWidth - 7), rect.y, valueWidth, rect.h}, value,
                         valueSize, color(ui::themes::ColorRole::Accent), TextAlign::Right);
            } else {
                const bool largeValue = Context::textWidth(value, 2) <= textWidth;
                drawText({static_cast<int16_t>(rect.x + 7), static_cast<int16_t>(rect.y + 3), textWidth, 8}, label, 1,
                         color(ui::themes::ColorRole::Muted));
                drawText({static_cast<int16_t>(rect.x + 7), static_cast<int16_t>(rect.y + 11), textWidth,
                          static_cast<int16_t>(std::max<int16_t>(0, rect.h - 13))},
                         value, largeValue ? 2 : 1, color(ui::themes::ColorRole::Accent), TextAlign::Left,
                         !largeValue && rect.h >= 32 ? 2 : 1);
            }
        }
        return tapped(slot, rect);
    }

    bool Context::toggle(Rect rect, std::string_view label, bool& enabled) {
        const size_t slot = nextSlot_;
        uint32_t state = combine(signature(label), enabled);
        if (claim(Kind::Toggle, rect, state).changed) {
            const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
            gfx_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
            gfx_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, color(ui::themes::ColorRole::Outline));
            constexpr int16_t switchWidth = 34;
            const int16_t switchX = static_cast<int16_t>(rect.x + rect.w - switchWidth - 7);
            const int16_t switchY = static_cast<int16_t>(rect.y + (rect.h - 16) / 2);
            gfx_.fillRoundRect(switchX, switchY, switchWidth, 16, 8,
                               color(enabled ? ui::themes::ColorRole::Accent : ui::themes::ColorRole::ProgressTrack));
            gfx_.fillCircle(static_cast<int16_t>(switchX + (enabled ? switchWidth - 8 : 8)),
                            static_cast<int16_t>(switchY + 8), 6, color(ui::themes::ColorRole::Foreground));
            drawText({static_cast<int16_t>(rect.x + 7), rect.y,
                      static_cast<int16_t>(std::max<int16_t>(0, switchX - rect.x - 14)), rect.h},
                     label, 2, color(ui::themes::ColorRole::Foreground));
        }
        if (!tapped(slot, rect))
            return false;
        enabled = !enabled;
        return true;
    }

    bool Context::tap(Rect rect, bool enabled) {
        const size_t slot = nextSlot_;
        claim(Kind::Touch, rect, enabled);
        return enabled && tapped(slot, rect);
    }

    bool Context::button(Rect rect, std::string_view text, bool enabled, Icon icon, uint8_t textLines,
                         std::string_view detailLeft, std::string_view detailRight) {
        const size_t slot = nextSlot_;
        const bool activated = enabled && tapped(slot, rect);
        uint32_t state = combine(signature(text), enabled);
        state = combine(state, static_cast<uint8_t>(icon));
        state = combine(state, textLines);
        state = signature(detailLeft, state);
        state = signature(detailRight, state);
        const Claim widget = claim(Kind::Button, rect, state);
        if (widget.changed) {
            const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
            gfx_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
            gfx_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5,
                               color(enabled ? ui::themes::ColorRole::Outline : ui::themes::ColorRole::ProgressTrack));
            if (enabled && rect.w > 16 && rect.h >= 28)
                gfx_.fillRect(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h - 3),
                              static_cast<int16_t>(rect.w - 16), 2, color(ui::themes::ColorRole::Accent));
            const int16_t iconWidth = icon == Icon::None ? 0 : std::min<int16_t>(34, rect.w / 3);
            const bool hasDetail = !detailLeft.empty() || !detailRight.empty();
            const int16_t textHeight = hasDetail ? static_cast<int16_t>(rect.h - 18) : rect.h;
            const ui::Rect textRect{static_cast<int16_t>(rect.x + 6), rect.y,
                                    static_cast<int16_t>(std::max<int16_t>(0, rect.w - iconWidth - 12)), textHeight};
            drawText(textRect, text, 2,
                     color(enabled ? ui::themes::ColorRole::Foreground : ui::themes::ColorRole::Muted),
                     TextAlign::Center, textLines);
            if (hasDetail) {
                const int16_t detailY = static_cast<int16_t>(rect.y + rect.h - 20);
                if (detailLeft.empty() || detailRight.empty()) {
                    drawText({textRect.x, detailY, textRect.w, 16}, detailLeft.empty() ? detailRight : detailLeft, 2,
                             color(ui::themes::ColorRole::Muted),
                             detailLeft.empty() ? TextAlign::Right : TextAlign::Left);
                } else {
                    const int16_t detailWidth = static_cast<int16_t>((textRect.w - 8) / 2);
                    drawText({textRect.x, detailY, detailWidth, 16}, detailLeft, 2,
                             color(ui::themes::ColorRole::Muted));
                    drawText({static_cast<int16_t>(textRect.x + textRect.w - detailWidth), detailY, detailWidth, 16},
                             detailRight, 2, color(ui::themes::ColorRole::Muted), TextAlign::Right);
                }
            }
            if (icon != Icon::None)
                drawIcon({static_cast<int16_t>(rect.x + rect.w - iconWidth), rect.y, iconWidth, rect.h}, icon,
                         color(enabled ? ui::themes::ColorRole::Accent : ui::themes::ColorRole::Muted), surface);
        }
        return activated;
    }

    bool Context::iconButton(Rect rect, Icon icon) {
        const size_t slot = nextSlot_;
        const bool activated = tapped(slot, rect);
        const uint32_t state = static_cast<uint8_t>(icon);
        const Claim widget = claim(Kind::Button, rect, state);
        if (widget.changed) {
            const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
            gfx_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 7, surface);
            gfx_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 7, color(ui::themes::ColorRole::Outline));
            drawIcon(rect, icon, color(ui::themes::ColorRole::Muted), surface);
        }
        return activated;
    }

    bool Context::tab(Rect rect, std::string_view text, bool active, Icon icon) {
        uint32_t state = combine(signature(text), active);
        state = combine(state, static_cast<uint8_t>(icon));
        const Claim widget = claim(Kind::Tab, rect, state);
        if (widget.changed) {
            const uint16_t surface =
                color(active ? ui::themes::ColorRole::Surface : ui::themes::ColorRole::SurfaceMuted);
            gfx_.fillRect(rect.x, rect.y, rect.w, rect.h, surface);
            gfx_.drawRect(rect.x, rect.y, rect.w, rect.h, color(ui::themes::ColorRole::Outline));
            if (active) {
                gfx_.fillRect(rect.x, static_cast<int16_t>(rect.y + 5), 3, static_cast<int16_t>(rect.h - 10),
                              color(ui::themes::ColorRole::Accent));
            }
            const uint16_t ink = color(active ? ui::themes::ColorRole::Foreground : ui::themes::ColorRole::Muted);
            const int16_t iconWidth = icon == Icon::None ? 0 : std::min<int16_t>(26, rect.w / 3);
            if (icon != Icon::None)
                drawIcon({static_cast<int16_t>(rect.x + 7), rect.y, iconWidth, rect.h}, icon, ink, surface);
            drawText({static_cast<int16_t>(rect.x + iconWidth + 8), rect.y,
                      static_cast<int16_t>(rect.w - iconWidth - 12), rect.h},
                     text, 2, ink, TextAlign::Center);
        }
        return tapped(widget.index, rect);
    }

    void Context::battery(Rect rect, uint8_t percent, bool charging, std::string_view labelText, bool showIcon) {
        percent = std::min<uint8_t>(percent, 100);

        uint32_t state = combine(signature(labelText), percent);
        state = combine(state, charging);
        state = combine(state, showIcon);

        if (!claim(Kind::Battery, rect, state).changed || (!showIcon && labelText.empty()))
            return;

        constexpr int16_t iconWidth = 29;
        constexpr int16_t iconHeight = 13;
        constexpr int16_t labelGap = 7;

        const int16_t iconAreaWidth = showIcon ? iconWidth + labelGap : 0;
        const int16_t labelWidth = textWidth(labelText, 2);
        const int16_t totalWidth = static_cast<int16_t>(iconAreaWidth + labelWidth);
        const int16_t x = std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + rect.w - totalWidth));

        const uint16_t ink = color(ui::themes::ColorRole::Muted);
        const uint16_t surface = color(ui::themes::ColorRole::Background);

        if (showIcon) {
            const int16_t iconY = static_cast<int16_t>(rect.y + std::max<int16_t>(0, (rect.h - iconHeight) / 2));

            drawBatteryIcon({x, iconY, iconWidth, iconHeight}, percent, charging, ink, surface);
        }

        drawText(
            {
                static_cast<int16_t>(x + iconAreaWidth),
                rect.y,
                labelWidth,
                rect.h,
            },
            labelText, 2, ink);
    }

    void Context::progress(Rect rect, int value, int minimum, int maximum) {
        value = std::clamp(value, minimum, maximum);
        uint32_t state = combine(static_cast<uint32_t>(value), static_cast<uint32_t>(minimum));
        state = combine(state, static_cast<uint32_t>(maximum));
        if (!claim(Kind::Progress, rect, state).changed) {
            return;
        }
        gfx_.fillRect(rect.x, rect.y, rect.w, rect.h, color(ui::themes::ColorRole::ProgressTrack));
        if (maximum > minimum && rect.w > 2 && rect.h > 2) {
            const int16_t fill =
                static_cast<int16_t>((static_cast<int32_t>(rect.w - 2) * (value - minimum)) / (maximum - minimum));
            gfx_.fillRect(static_cast<int16_t>(rect.x + 1), static_cast<int16_t>(rect.y + 1), fill,
                          static_cast<int16_t>(rect.h - 2), color(ui::themes::ColorRole::Accent));
        }
    }

    void Context::steps(Rect rect, uint8_t current, uint8_t total, ui::themes::ColorRole activeRole) {
        current = std::min(current, total);
        uint32_t state = combine(current, total);
        state = combine(state, activeRole);
        if (!claim(Kind::Steps, rect, state).changed)
            return;

        if (total == 0) {
            return;
        }
        const bool vertical = rect.h > rect.w;
        const int16_t crossSize = vertical ? rect.w : rect.h;
        const int16_t radius = std::max<int16_t>(2, std::min<int16_t>(4, static_cast<int16_t>((crossSize - 2) / 2)));
        const int16_t spacing = static_cast<int16_t>(radius * 2 + 5);
        const int16_t length = static_cast<int16_t>((total - 1) * spacing + radius * 2);
        const int16_t first =
            static_cast<int16_t>((vertical ? rect.y : rect.x) + ((vertical ? rect.h : rect.w) - length) / 2 + radius);
        const int16_t center = static_cast<int16_t>((vertical ? rect.x : rect.y) + crossSize / 2);
        for (uint8_t index = 0; index < total; ++index) {
            const int16_t position = static_cast<int16_t>(first + index * spacing);
            const int16_t x = vertical ? center : position;
            const int16_t y = vertical ? position : center;
            if (index < current)
                gfx_.fillCircle(x, y, radius, color(activeRole));
            else
                gfx_.drawCircle(x, y, radius, color(ui::themes::ColorRole::Outline));
        }
    }

    bool Context::sliderValue(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step,
                              std::string_view suffix, ui::themes::ColorRole activeRole) {
        const size_t slot = nextSlot_;
        const Touch* event = touch();
        const bool labeled = !label.empty();
        const int16_t visualHeight = labeled ? std::min<int16_t>(50, rect.h) : rect.h;
        const Rect visual{rect.x, static_cast<int16_t>(rect.y + (rect.h - visualHeight) / 2), rect.w, visualHeight};
        const Rect track{static_cast<int16_t>(visual.x + (labeled ? 8 : 0)),
                         static_cast<int16_t>(visual.y + (labeled ? visual.h - 8 : visual.h / 2 - 1)),
                         static_cast<int16_t>(visual.w - (labeled ? 16 : 0)), 3};
        const bool started = event != nullptr && hasTouch(*event, TouchStart) && contains(rect, event->x, event->y);
        if (started && slot < kSlotCapacity) {
            capturedSlot_ = slot;
            capturedScalarInitialValue_ = std::clamp(value, minimum, maximum);
            capturedScalarValue_ = valueAt(track, event->x, minimum, maximum, step);
        }

        int displayedValue = std::clamp(value, minimum, maximum);
        bool changed = false;
        const bool moving =
            event != nullptr
            && (hasTouch(*event, TouchStart) || hasTouch(*event, TouchMove) || hasTouch(*event, TouchRelease));
        if (capturedSlot_ == slot && moving) {
            capturedScalarValue_ = valueAt(track, event->x, minimum, maximum, step);
        }
        if (capturedSlot_ == slot)
            displayedValue = capturedScalarValue_;
        if (capturedSlot_ == slot && event != nullptr && hasTouch(*event, TouchRelease))
            changed = displayedValue != capturedScalarInitialValue_;

        uint32_t state = signature(suffix, signature(label));
        state = combine(state, static_cast<uint32_t>(displayedValue));
        state = combine(state, static_cast<uint32_t>(minimum));
        state = combine(state, static_cast<uint32_t>(maximum));
        state = combine(state, static_cast<uint32_t>(step));
        state = combine(state, activeRole);
        if (claim(Kind::Slider, rect, state).changed) {
            if (labeled) {
                const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
                gfx_.fillRoundRect(visual.x, visual.y, visual.w, visual.h, 5, surface);
                gfx_.drawRoundRect(visual.x, visual.y, visual.w, visual.h, 5, color(ui::themes::ColorRole::Outline));
                char valueText[24];
                std::snprintf(valueText, sizeof(valueText), "%d%.*s", displayedValue, static_cast<int>(suffix.size()),
                              suffix.data());
                const std::string_view valueView{valueText};
                const int16_t headerWidth = static_cast<int16_t>(visual.w - 14);
                if (visual.h >= 44) {
                    drawText({static_cast<int16_t>(visual.x + 7), static_cast<int16_t>(visual.y + 2), headerWidth, 16},
                             label, 2, color(ui::themes::ColorRole::Foreground));
                    drawText({static_cast<int16_t>(visual.x + 7), static_cast<int16_t>(visual.y + 18), headerWidth, 16},
                             valueView, 2, color(activeRole), TextAlign::Right);
                } else {
                    uint8_t labelSize = visual.h >= 30 ? 2 : 1;
                    uint8_t valueSize = labelSize;
                    int16_t valueWidth = Context::textWidth(valueView, valueSize);
                    if (headerWidth < Context::textWidth(label, labelSize) + valueWidth + 8) {
                        valueSize = 1;
                        valueWidth = Context::textWidth(valueView, 1);
                    }
                    if (headerWidth < Context::textWidth(label, labelSize) + valueWidth + 8)
                        labelSize = 1;
                    const int16_t labelWidth = std::max<int16_t>(0, static_cast<int16_t>(headerWidth - valueWidth - 8));
                    const int16_t textY = static_cast<int16_t>(visual.y + 2);
                    drawText({static_cast<int16_t>(visual.x + 7), textY, labelWidth, 16}, label, labelSize,
                             color(ui::themes::ColorRole::Foreground));
                    drawText({static_cast<int16_t>(visual.x + visual.w - valueWidth - 7), textY, valueWidth, 16},
                             valueView, valueSize, color(activeRole), TextAlign::Right);
                }
            }
            gfx_.fillRect(track.x, track.y, track.w, track.h, color(ui::themes::ColorRole::ProgressTrack));
            const int16_t knobX =
                maximum == minimum
                    ? track.x
                    : static_cast<int16_t>(track.x
                                           + (static_cast<int32_t>(track.w - 1) * (displayedValue - minimum))
                                                 / (maximum - minimum));
            const int16_t trackCenterY = static_cast<int16_t>(track.y + track.h / 2);
            if (step > 0 && maximum > minimum) {
                const int intervalCount = (maximum - minimum + step - 1) / step;
                const int tickStride = std::max(1, (intervalCount + 9) / 10);
                for (int interval = 0;; interval = std::min(interval + tickStride, intervalCount)) {
                    const int tickValue = std::min(minimum + interval * step, maximum);
                    const int16_t tickX =
                        static_cast<int16_t>(track.x
                                             + (static_cast<int32_t>(track.w - 1) * (tickValue - minimum))
                                                   / (maximum - minimum));
                    gfx_.drawFastVLine(tickX, static_cast<int16_t>(trackCenterY - 3), 7,
                                       color(ui::themes::ColorRole::Outline));
                    if (interval == intervalCount)
                        break;
                }
            }
            gfx_.fillRect(track.x, track.y, static_cast<int16_t>(knobX - track.x + 1), track.h, color(activeRole));
            const int16_t knobRadius = labeled ? 5 : 7;
            gfx_.fillCircle(knobX, trackCenterY, knobRadius, color(activeRole));
            gfx_.drawCircle(knobX, trackCenterY, knobRadius, color(ui::themes::ColorRole::OnAccent));
        }

        if (capturedSlot_ == slot && event != nullptr && hasTouch(*event, TouchRelease)) {
            capturedSlot_ = kSlotCapacity;
        }
        if (changed)
            value = displayedValue;
        return changed;
    }

    bool Context::stepperValue(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step,
                               std::string_view suffix, ui::themes::ColorRole activeRole) {
        const size_t slot = nextSlot_;
        const int safeStep = std::max(1, step);
        const int16_t buttonWidth = std::min<int16_t>(42, std::max<int16_t>(16, rect.w / 5));
        const Rect decrement{rect.x, rect.y, buttonWidth, rect.h};
        const Rect increment{static_cast<int16_t>(rect.x + rect.w - buttonWidth), rect.y, buttonWidth, rect.h};
        const Touch* event = touch();

        if (event != nullptr && hasTouch(*event, TouchStart) && slot < kSlotCapacity) {
            const int8_t direction = contains(decrement, event->x, event->y) ? -1
                                   : contains(increment, event->x, event->y) ? 1
                                                                             : 0;
            if (direction != 0) {
                capturedSlot_ = slot;
                capturedScalarInitialValue_ = std::clamp(value, minimum, maximum);
                capturedScalarValue_ = capturedScalarInitialValue_;
                capturedStepperDirection_ = direction;
            }
        }

        int displayedValue = std::clamp(value, minimum, maximum);
        bool changed = false;
        if (capturedSlot_ == slot) {
            const Rect target = capturedStepperDirection_ < 0 ? decrement : increment;
            const bool overTarget = event != nullptr && contains(target, event->x, event->y);
            if (overTarget && event != nullptr && hasTouch(*event, TouchRelease) && hasTouch(*event, TouchTap)) {
                capturedScalarValue_ =
                    std::clamp(capturedScalarInitialValue_ + capturedStepperDirection_ * safeStep, minimum, maximum);
            } else if (overTarget && touchActive_
                       && touchLastPollMs_ - touchStartedAtMs_ >= touchSource_.timing.holdMs) {
                constexpr uint32_t repeatMs = 120;
                const uint32_t repeats = (touchLastPollMs_ - touchStartedAtMs_ - touchSource_.timing.holdMs) / repeatMs;
                const int delta = safeStep * (1 + static_cast<int>(repeats));
                capturedScalarValue_ =
                    std::clamp(capturedScalarInitialValue_ + capturedStepperDirection_ * delta, minimum, maximum);
            }
            displayedValue = capturedScalarValue_;
            changed = displayedValue != value;
            if (event != nullptr && hasTouch(*event, TouchRelease)) {
                capturedSlot_ = kSlotCapacity;
                capturedStepperDirection_ = 0;
            }
        }

        uint32_t state = signature(suffix, signature(label));
        state = combine(state, static_cast<uint32_t>(displayedValue));
        state = combine(state, static_cast<uint32_t>(minimum));
        state = combine(state, static_cast<uint32_t>(maximum));
        state = combine(state, activeRole);
        if (claim(Kind::Stepper, rect, state).changed) {
            const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
            const uint16_t outline = color(ui::themes::ColorRole::Outline);
            gfx_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
            gfx_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, outline);
            gfx_.drawFastVLine(static_cast<int16_t>(rect.x + buttonWidth), static_cast<int16_t>(rect.y + 4),
                               static_cast<int16_t>(rect.h - 8), outline);
            gfx_.drawFastVLine(static_cast<int16_t>(rect.x + rect.w - buttonWidth), static_cast<int16_t>(rect.y + 4),
                               static_cast<int16_t>(rect.h - 8), outline);

            const uint16_t muted = color(ui::themes::ColorRole::Muted);
            drawText(decrement, "-", 2, displayedValue > minimum ? color(activeRole) : muted, TextAlign::Center);
            drawText(increment, "+", 2, displayedValue < maximum ? color(activeRole) : muted, TextAlign::Center);

            char valueText[24];
            std::snprintf(valueText, sizeof(valueText), "%d%.*s", displayedValue, static_cast<int>(suffix.size()),
                          suffix.data());
            const Rect middle{static_cast<int16_t>(decrement.x + decrement.w + 6), rect.y,
                              static_cast<int16_t>(rect.w - buttonWidth * 2 - 12), rect.h};
            if (rect.h >= 44) {
                drawText({middle.x, static_cast<int16_t>(middle.y + 2), middle.w, 16}, label, 2,
                         color(ui::themes::ColorRole::Foreground), TextAlign::Center);
                drawText({middle.x, static_cast<int16_t>(middle.y + 20), middle.w, static_cast<int16_t>(middle.h - 20)},
                         valueText, 2, color(activeRole), TextAlign::Center);
            } else {
                const int16_t valueWidth = std::min<int16_t>(middle.w / 2, textWidth(valueText, 2));
                drawText({middle.x, middle.y, static_cast<int16_t>(middle.w - valueWidth - 6), middle.h}, label, 2,
                         color(ui::themes::ColorRole::Foreground));
                drawText({static_cast<int16_t>(middle.x + middle.w - valueWidth), middle.y, valueWidth, middle.h},
                         valueText, 2, color(activeRole), TextAlign::Right);
            }
        }
        if (changed)
            value = displayedValue;
        return changed;
    }

    void Context::dial(Rect rect, int value, int minimum, int maximum, std::string_view labelText) {
        value = std::clamp(value, minimum, maximum);
        uint32_t state = combine(signature(labelText), static_cast<uint32_t>(value));
        state = combine(state, static_cast<uint32_t>(minimum));
        state = combine(state, static_cast<uint32_t>(maximum));
        if (!claim(Kind::Dial, rect, state).changed) {
            return;
        }
        const int16_t radius = std::max<int16_t>(2, std::min(rect.w, rect.h) / 2 - 2);
        const int16_t cx = static_cast<int16_t>(rect.x + rect.w / 2);
        const int16_t cy = static_cast<int16_t>(rect.y + rect.h / 2);
        gfx_.drawCircle(cx, cy, radius, color(ui::themes::ColorRole::ProgressTrack));
        if (maximum > minimum) {
            constexpr float kPi = 3.14159265358979323846f;
            const float angle = (-135.0f + 270.0f * (value - minimum) / (maximum - minimum)) * kPi / 180.0f;
            gfx_.drawLine(cx, cy, static_cast<int16_t>(cx + std::cos(angle) * (radius - 4)),
                          static_cast<int16_t>(cy + std::sin(angle) * (radius - 4)),
                          color(ui::themes::ColorRole::Accent));
        }
        if (!labelText.empty()) {
            drawText({rect.x, static_cast<int16_t>(cy + radius / 2), rect.w, textHeight(1)}, labelText, 1,
                     color(ui::themes::ColorRole::Muted), TextAlign::Center);
        }
    }

    void Context::hourglass(Rect rect, uint16_t progress, bool paused, bool complete, ui::themes::ColorRole sandRole,
                            bool reversed, std::string_view time) {
        progress = std::min<uint16_t>(progress, 1000);
        uint32_t state = combine(progress, paused);
        state = combine(state, complete);
        state = combine(state, sandRole);
        state = combine(state, reversed);
        const bool visualChanged = claim(Kind::Custom, rect, state).changed;
        const auto drawTime = [&] {
            if (time.empty())
                return;
            const Rect timeRect{static_cast<int16_t>(rect.x + (rect.w - 120) / 2), rect.y, 120, 28};
            if (claim(Kind::Label, timeRect, signature(time, state)).changed)
                drawText(timeRect, time, 3, color(sandRole), TextAlign::Center);
        };
        if (!visualChanged) {
            drawTime();
            return;
        }

        const uint16_t ink = color(paused ? ui::themes::ColorRole::Muted : sandRole);
        const uint16_t outline = color(ui::themes::ColorRole::Foreground);
        const int16_t inset = std::max<int16_t>(3, std::min(rect.w, rect.h) / 12);
        const int16_t left = static_cast<int16_t>(rect.x + inset);
        const int16_t right = static_cast<int16_t>(rect.x + rect.w - inset - 1);
        const int16_t top = static_cast<int16_t>(rect.y + inset);
        const int16_t bottom = static_cast<int16_t>(rect.y + rect.h - inset - 1);
        const int16_t centerX = static_cast<int16_t>((left + right) / 2);
        const int16_t centerY = static_cast<int16_t>((top + bottom) / 2);

        if (rect.w > rect.h) {
            constexpr int16_t segments = 14;
            const int16_t chamberWidth = std::max<int16_t>(1, static_cast<int16_t>(centerX - left));
            const int16_t chamberHeight = std::max<int16_t>(3, static_cast<int16_t>((bottom - top) / 2));
            const int16_t waist = std::max<int16_t>(2, chamberHeight / 14);
            const int16_t capWidth = std::max<int16_t>(8, std::min<int16_t>(16, rect.h / 8));
            const int16_t baseTop = static_cast<int16_t>(rect.y + 2);
            const int16_t baseHeight = static_cast<int16_t>(rect.h - 4);
            const uint16_t base = color(ui::themes::ColorRole::SurfaceActive);
            gfx_.fillRoundRect(static_cast<int16_t>(left - capWidth / 2), baseTop, capWidth, baseHeight, capWidth / 2,
                               base);
            gfx_.drawRoundRect(static_cast<int16_t>(left - capWidth / 2), baseTop, capWidth, baseHeight, capWidth / 2,
                               outline);
            gfx_.fillRoundRect(static_cast<int16_t>(right - capWidth / 2), baseTop, capWidth, baseHeight, capWidth / 2,
                               base);
            gfx_.drawRoundRect(static_cast<int16_t>(right - capWidth / 2), baseTop, capWidth, baseHeight, capWidth / 2,
                               outline);

            std::array<int16_t, segments + 1> profile{};
            for (int16_t step = 0; step <= segments; ++step) {
                const int16_t offsetFromBase = static_cast<int16_t>(chamberWidth * step / segments);
                int16_t curve = static_cast<int16_t>(offsetFromBase * 100 / chamberWidth);
                curve = static_cast<int16_t>(curve * curve / 100);
                curve = static_cast<int16_t>(curve * curve / 100);
                profile[step] = static_cast<int16_t>(chamberHeight - (chamberHeight - waist) * curve / 100);
            }
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

            int16_t previousLeftX = left;
            int16_t previousLeftHalf = chamberHeight;
            int16_t previousRightX = right;
            int16_t previousRightHalf = chamberHeight;
            for (int16_t step = 1; step <= segments; ++step) {
                const int16_t offset = static_cast<int16_t>(chamberWidth * step / segments);
                const int16_t leftX = static_cast<int16_t>(left + offset);
                const int16_t rightX = static_cast<int16_t>(right - offset);
                const int16_t leftHalf = profile[step];
                const int16_t rightHalf = leftHalf;
                for (int16_t thickness = -1; thickness <= 1; ++thickness) {
                    gfx_.drawLine(previousLeftX, static_cast<int16_t>(centerY - previousLeftHalf + thickness), leftX,
                                  static_cast<int16_t>(centerY - leftHalf + thickness), outline);
                    gfx_.drawLine(previousLeftX, static_cast<int16_t>(centerY + previousLeftHalf + thickness), leftX,
                                  static_cast<int16_t>(centerY + leftHalf + thickness), outline);
                    gfx_.drawLine(previousRightX, static_cast<int16_t>(centerY - previousRightHalf + thickness), rightX,
                                  static_cast<int16_t>(centerY - rightHalf + thickness), outline);
                    gfx_.drawLine(previousRightX, static_cast<int16_t>(centerY + previousRightHalf + thickness), rightX,
                                  static_cast<int16_t>(centerY + rightHalf + thickness), outline);
                }
                previousLeftX = leftX;
                previousLeftHalf = leftHalf;
                previousRightX = rightX;
                previousRightHalf = rightHalf;
            }

            const int16_t pileWidth = static_cast<int16_t>(chamberWidth * 65 / 100);
            const uint32_t sourceTarget = static_cast<uint32_t>(1000 - progress) * pileWidth * pileWidth;
            int16_t sourceColumns = 0;
            while (sourceColumns < pileWidth) {
                const uint32_t next = static_cast<uint32_t>(sourceColumns + 1);
                if (next * next * 1000U > sourceTarget)
                    break;
                ++sourceColumns;
            }
            const int16_t receivedColumns =
                static_cast<int16_t>((static_cast<uint32_t>(pileWidth) * progress + 999U) / 1000U);
            const int16_t leftGlassEdge = static_cast<int16_t>(left + capWidth / 2 + 2);
            const int16_t rightGlassEdge = static_cast<int16_t>(right - capWidth / 2 - 2);
            for (int16_t column = 1; column <= sourceColumns; ++column) {
                const int16_t half = halfAt(static_cast<int16_t>(chamberWidth - column));
                const int16_t x =
                    reversed ? static_cast<int16_t>(centerX + column) : static_cast<int16_t>(centerX - column);
                gfx_.drawFastVLine(x, static_cast<int16_t>(centerY - half + 3),
                                   std::max<int16_t>(1, static_cast<int16_t>(half * 2 - 5)), ink);
            }
            const int16_t receivedBaseHalf = receivedColumns == 0 ? 0 : chamberHeight;
            const int16_t plateauColumns =
                receivedColumns == 0 ? 0 : std::max<int16_t>(1, static_cast<int16_t>(receivedColumns / 6));
            const int16_t slopeColumns = std::max<int16_t>(1, static_cast<int16_t>(receivedColumns - plateauColumns));
            for (int16_t column = 1; column <= receivedColumns; ++column) {
                const int16_t pileHalf =
                    column <= plateauColumns
                        ? receivedBaseHalf
                        : static_cast<int16_t>(receivedBaseHalf * (receivedColumns - column) / slopeColumns);
                const int16_t half = std::min(halfAt(column), pileHalf);
                const int16_t x = reversed ? static_cast<int16_t>(leftGlassEdge + column)
                                           : static_cast<int16_t>(rightGlassEdge - column);
                gfx_.drawFastVLine(x, static_cast<int16_t>(centerY - half + 3),
                                   std::max<int16_t>(1, static_cast<int16_t>(half * 2 - 5)), ink);
            }
            if (!paused && progress > 0 && progress < 1000) {
                if (reversed) {
                    const int16_t streamX = static_cast<int16_t>(left + receivedColumns + 1);
                    gfx_.drawFastHLine(streamX, centerY, std::max<int16_t>(1, static_cast<int16_t>(centerX - streamX)),
                                       ink);
                } else {
                    gfx_.drawFastHLine(static_cast<int16_t>(centerX + 1), centerY,
                                       std::max<int16_t>(1,
                                                         static_cast<int16_t>(right - receivedColumns - centerX - 2)),
                                       ink);
                }
            }
            if (paused) {
                gfx_.fillRect(static_cast<int16_t>(centerX - 6), static_cast<int16_t>(bottom - 17), 4, 13, ink);
                gfx_.fillRect(static_cast<int16_t>(centerX + 2), static_cast<int16_t>(bottom - 17), 4, 13, ink);
            } else if (complete) {
                gfx_.drawLine(static_cast<int16_t>(centerX - 7), static_cast<int16_t>(bottom - 11),
                              static_cast<int16_t>(centerX - 2), static_cast<int16_t>(bottom - 6), ink);
                gfx_.drawLine(static_cast<int16_t>(centerX - 2), static_cast<int16_t>(bottom - 6),
                              static_cast<int16_t>(centerX + 8), static_cast<int16_t>(bottom - 17), ink);
            }
            markDrawn();
            drawTime();
            return;
        }

        const int16_t chamberHeight = std::max<int16_t>(1, static_cast<int16_t>(centerY - top - 3));

        gfx_.drawFastHLine(left, top, static_cast<int16_t>(right - left + 1), outline);
        gfx_.drawFastHLine(left, bottom, static_cast<int16_t>(right - left + 1), outline);
        gfx_.drawLine(left, static_cast<int16_t>(top + 1), centerX, centerY, outline);
        gfx_.drawLine(right, static_cast<int16_t>(top + 1), centerX, centerY, outline);
        gfx_.drawLine(centerX, centerY, left, static_cast<int16_t>(bottom - 1), outline);
        gfx_.drawLine(centerX, centerY, right, static_cast<int16_t>(bottom - 1), outline);

        const int16_t topRows = static_cast<int16_t>(chamberHeight * (1000 - progress) / 1000);
        for (int16_t row = 0; row < topRows; ++row) {
            const int16_t y = static_cast<int16_t>(centerY - 2 - row);
            const int16_t half =
                std::max<int16_t>(1, static_cast<int16_t>((right - left) * (row + 1) / (2 * chamberHeight)));
            gfx_.drawFastHLine(static_cast<int16_t>(centerX - half), y, static_cast<int16_t>(half * 2 + 1), ink);
        }
        const int16_t bottomRows = static_cast<int16_t>(chamberHeight * progress / 1000);
        for (int16_t row = 0; row < bottomRows; ++row) {
            const int16_t y = static_cast<int16_t>(bottom - 2 - row);
            const int16_t half =
                std::max<int16_t>(1, static_cast<int16_t>((right - left) * (bottomRows - row) / (2 * chamberHeight)));
            gfx_.drawFastHLine(static_cast<int16_t>(centerX - half), y, static_cast<int16_t>(half * 2 + 1), ink);
        }
        if (!paused && progress > 0 && progress < 1000)
            gfx_.drawFastVLine(centerX, static_cast<int16_t>(centerY + 1),
                               std::max<int16_t>(1, static_cast<int16_t>(bottom - bottomRows - centerY - 2)), ink);
        markDrawn();
        drawTime();
    }

    bool Context::redraw(Rect rect, uint32_t state) {
        return claim(Kind::Custom, rect, state).changed;
    }

    uint16_t Context::color(ui::themes::ColorRole role) const {
        if (theme_ == nullptr) {
            return role == ui::themes::ColorRole::Background ? kFallbackBlack : kFallbackWhite;
        }
        return ui::themes::color(theme_->definition.colors, role);
    }

    uint16_t Context::blend(ui::themes::ColorRole role, uint8_t alpha) const {
        const uint16_t foreground = color(role);
        const uint16_t background = color(ui::themes::ColorRole::Background);
        const uint8_t fgR = static_cast<uint8_t>((foreground >> 11) & 0x1F);
        const uint8_t fgG = static_cast<uint8_t>((foreground >> 5) & 0x3F);
        const uint8_t fgB = static_cast<uint8_t>(foreground & 0x1F);
        const uint8_t bgR = static_cast<uint8_t>((background >> 11) & 0x1F);
        const uint8_t bgG = static_cast<uint8_t>((background >> 5) & 0x3F);
        const uint8_t bgB = static_cast<uint8_t>(background & 0x1F);
        return static_cast<uint16_t>((((fgR * alpha + bgR * (255 - alpha)) / 255) << 11)
                                     | (((fgG * alpha + bgG * (255 - alpha)) / 255) << 5)
                                     | ((fgB * alpha + bgB * (255 - alpha)) / 255));
    }

    uint32_t Context::signature(std::string_view text, uint32_t seed) {
        for (const char value: text) {
            seed ^= static_cast<uint8_t>(value);
            seed *= 16777619U;
        }
        return seed;
    }

    uint32_t Context::combine(uint32_t seed, uint32_t value) {
        return (seed ^ value) * 16777619U;
    }

    Context::Claim Context::claim(Kind kind, Rect rect, uint32_t state) {
        const size_t index = nextSlot_++;
        if (index >= kSlotCapacity) {
            clear(rect);
            return {index, true};
        }

        Slot& slot = slots_[index];
        const bool structureChanged = slot.valid && (slot.kind != kind || !(slot.rect == rect));
        if (structureChanged && capturedSlot_ == index) {
            capturedSlot_ = kSlotCapacity;
        }
        const bool changed = !slot.valid || structureChanged || slot.signature != state;
        if (changed) {
            if (slot.valid && slot.kind != Kind::Touch && (!(slot.rect == rect) || kind == Kind::Touch)) {
                clear(slot.rect);
            }
            if (kind != Kind::Touch)
                clear(rect);
            slot = {rect, state, kind, true};
        }
        slotCount_ = std::max(slotCount_, index + 1);
        return {index, changed};
    }

    void Context::clear(Rect rect) {
        if (rect.w <= 0 || rect.h <= 0) {
            return;
        }
        gfx_.fillRect(rect.x, rect.y, rect.w, rect.h, color(ui::themes::ColorRole::Background));
        markDrawn();
    }

    void Context::markDrawn() {
        drew_ = true;
    }

    void Context::drawText(Rect rect, std::string_view text, uint8_t textSize, uint16_t textColor, TextAlign align,
                           uint8_t maxLines) {
        if (rect.w <= 0 || rect.h <= 0)
            return;
        const uint8_t size = std::max<uint8_t>(1, textSize);
        const size_t capacity = static_cast<size_t>(std::max<int16_t>(0, rect.w) / (kUiFontCellWidth * size));
        if (capacity == 0)
            return;
        gfx_.setFont(u8g2_font_rsvpnano_ui_6x9_tf);
        gfx_.setUTF8Print(true);
        gfx_.setTextSize(size);
        gfx_.setTextWrap(false);
        gfx_.setTextColor(textColor);

        std::string_view first = text;
        std::string_view second;
        if (maxLines > 1 && Utf8Text::count(text) > capacity) {
            size_t split = Utf8Text::prefixBytes(text, capacity);
            const size_t space = text.rfind(' ', split);
            if (space != std::string_view::npos && Utf8Text::count(text.substr(0, space)) >= capacity / 2)
                split = space;
            first = text.substr(0, split);
            second = text.substr(split);
            while (!second.empty() && second.front() == ' ')
                second.remove_prefix(1);
        }

        const uint8_t lineCount = second.empty() ? 1 : 2;
        const int16_t lineHeight = textHeight(size);
        const int16_t firstY =
            static_cast<int16_t>(rect.y + std::max<int16_t>(0, (rect.h - lineHeight * lineCount) / 2));
        const auto drawLine = [&](std::string_view line, int16_t y) {
            const bool truncated = Utf8Text::count(line) > capacity;
            const size_t length = fittedLength(line, capacity);
            const size_t dots = truncated ? std::min<size_t>(3, capacity) : 0;
            const int16_t renderedWidth = static_cast<int16_t>(textWidth(line.substr(0, length), size)
                                                               + textWidth(std::string_view{"...", dots}, size));
            const int16_t x = align == TextAlign::Center
                                ? std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + (rect.w - renderedWidth) / 2))
                            : align == TextAlign::Right
                                ? std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + rect.w - renderedWidth))
                                : rect.x;
            gfx_.setCursor(x, static_cast<int16_t>(y + lineHeight - size));
            for (size_t index = 0; index < length; ++index)
                gfx_.write(static_cast<uint8_t>(line[index]));
            for (size_t index = 0; index < dots; ++index)
                gfx_.write('.');
        };
        drawLine(first, firstY);
        if (!second.empty())
            drawLine(second, static_cast<int16_t>(firstY + lineHeight));
        drew_ = true;
    }

    int Context::valueAt(Rect rect, uint16_t x, int minimum, int maximum, int step) const {
        if (rect.w <= 1 || maximum <= minimum) {
            return minimum;
        }
        const int clampedX = std::clamp<int>(x, rect.x, rect.x + rect.w - 1) - rect.x;
        int value = minimum + static_cast<int>((static_cast<int64_t>(maximum - minimum) * clampedX) / (rect.w - 1));
        if (step > 1) {
            value = minimum + ((value - minimum + step / 2) / step) * step;
        }
        return std::clamp(value, minimum, maximum);
    }

    bool Context::tapped(size_t slot, Rect rect) {
        const Touch* event = touch();
        if (event == nullptr || slot >= kSlotCapacity)
            return false;
        if (hasTouch(*event, TouchStart) && contains(rect, event->x, event->y))
            capturedSlot_ = slot;
        if (!hasTouch(*event, TouchRelease) || capturedSlot_ != slot)
            return false;
        capturedSlot_ = kSlotCapacity;
        return hasTouch(*event, TouchTap);
    }

    void Context::resetTouchGesture() {
        touchActive_ = false;
        touchHoldEmitted_ = false;
        touchSlopExceeded_ = false;
        touchPending_ = false;
        touchEmptySamples_ = 0;
        touchStartedAtMs_ = 0;
        touchStartX_ = 0;
        touchStartY_ = 0;
        touchLastX_ = 0;
        touchLastY_ = 0;
        capturedSlot_ = kSlotCapacity;
    }

    bool Context::beginTouch(uint32_t nowMs) {
        resetTouchGesture();
        touchLastPollMs_ = 0;
        touchBackoffUntilMs_ = 0;
        touchReadFailures_ = 0;
        touchInitialized_ = touchSource_.begin != nullptr && touchSource_.begin();
        if (touchInitialized_) {
            touchIgnoreUntilMs_ = nowMs + touchSource_.timing.recoveryEventIgnoreMs;
        }
        return touchInitialized_;
    }

    TouchContact Context::mapTouch(TouchContact contact) const {
        const uint16_t maxX = std::max(touchSource_.surface.width, uint16_t{1}) - 1;
        const uint16_t maxY = std::max(touchSource_.surface.height, uint16_t{1}) - 1;
        const uint16_t rawX = std::clamp<uint16_t>(contact.x, 0, maxX);
        const uint16_t rawY = std::clamp<uint16_t>(contact.y, 0, maxY);
        switch (touchOrientation_) {
        case Orientation::LandscapeFlipped:
            return {true, rawY, static_cast<uint16_t>(maxX - rawX)};
        case Orientation::PortraitFlipped:
            return {true, static_cast<uint16_t>(maxX - rawX), static_cast<uint16_t>(maxY - rawY)};
        case Orientation::Landscape:
            return {true, static_cast<uint16_t>(maxY - rawY), rawX};
        default:
            return {true, rawX, rawY};
        }
    }

    bool Context::updateTouch(const TouchContact& contact, uint32_t nowMs) {
        if (!contact.touched) {
            if (!touchActive_ || ++touchEmptySamples_ < touchSource_.timing.releaseConfirmSamples)
                return false;
            const bool tapped = !touchHoldEmitted_ && nowMs - touchStartedAtMs_ <= touchSource_.timing.tapMaxDurationMs
                             && !touchSlopExceeded_;
            touchActive_ = false;
            touchEmptySamples_ = 0;
            touchEvent_ = {static_cast<uint8_t>(TouchRelease | (tapped ? TouchTap : TouchNone)), touchLastX_,
                           touchLastY_};
            return touchPending_ = true;
        }

        touchEmptySamples_ = 0;
        const TouchContact mapped = mapTouch(contact);
        if (!touchActive_) {
            touchActive_ = true;
            touchHoldEmitted_ = false;
            touchSlopExceeded_ = false;
            touchStartedAtMs_ = nowMs;
            touchStartX_ = touchLastX_ = mapped.x;
            touchStartY_ = touchLastY_ = mapped.y;
            touchEvent_ = {TouchStart, mapped.x, mapped.y};
            return touchPending_ = true;
        }

        touchLastX_ = mapped.x;
        touchLastY_ = mapped.y;
        uint8_t actions = TouchMove;
        const uint16_t dx = std::max(touchLastX_, touchStartX_) - std::min(touchLastX_, touchStartX_);
        const uint16_t dy = std::max(touchLastY_, touchStartY_) - std::min(touchLastY_, touchStartY_);
        touchSlopExceeded_ = touchSlopExceeded_ || dx > touchSource_.timing.tapMoveTolerancePx
                          || dy > touchSource_.timing.tapMoveTolerancePx;
        if (!touchHoldEmitted_ && !touchSlopExceeded_ && nowMs - touchStartedAtMs_ >= touchSource_.timing.holdMs) {
            touchHoldEmitted_ = true;
            actions |= TouchHold;
        }
        touchEvent_ = {actions, mapped.x, mapped.y};
        return touchPending_ = true;
    }

} // namespace ui
