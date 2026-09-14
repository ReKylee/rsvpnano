#include "ui/Ui.h"

#include <cmath>
#include <cstdio>

namespace ui {
    bool Context::sliderValue(Rect rect, std::string_view label, int& value, int minimum, int maximum, int step,
                              std::string_view suffix, ui::themes::ColorRole activeRole) {
        rect = paintBounds(rect);
        const size_t slot = nextSlot_;
        const Touch* event = touch();
        const bool labeled = !label.empty();
        const int16_t visualHeight = labeled ? std::min<int16_t>(50, rect.h) : rect.h;
        const int16_t trackInset = labeled ? 8 : (displayWriteAlignment() == 2 ? 7 : 0);
        const Rect visual{rect.x, static_cast<int16_t>(rect.y + (rect.h - visualHeight) / 2), rect.w, visualHeight};
        const Rect track{static_cast<int16_t>(visual.x + trackInset),
                         static_cast<int16_t>(visual.y + (labeled ? visual.h - 8 : visual.h / 2 - 1)),
                         static_cast<int16_t>(visual.w - 2 * trackInset), 3};
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
            const Rect bounds = rect;
            TextLayout labelText, valueLayout;
            if (labeled) {
                char valueText[24];
                std::snprintf(valueText, sizeof(valueText), "%d%.*s", displayedValue, static_cast<int>(suffix.size()),
                              suffix.data());
                const std::string_view valueView{valueText};
                const int16_t headerWidth = static_cast<int16_t>(visual.w - 14);
                const bool largeInline = visual.h >= 40 && textHeightFor(label, 3) <= visual.h - 10
                                      && textHeightFor(valueView, 3) <= visual.h - 10
                                      && textWidthFor(label, 3) + textWidthFor(valueView, 3) + 8 <= headerWidth;
                if (largeInline) {
                    const int16_t valueWidth = textWidthFor(valueView, 3);
                    const int16_t labelWidth = static_cast<int16_t>(headerWidth - valueWidth - 8);
                    const int16_t textHeight = static_cast<int16_t>(visual.h - 10);
                    labelText = prepareText({static_cast<int16_t>(visual.x + 7), static_cast<int16_t>(visual.y + 1),
                                             labelWidth, textHeight},
                                            label, 3);
                    valueLayout = prepareText({static_cast<int16_t>(visual.x + visual.w - valueWidth - 7),
                                               static_cast<int16_t>(visual.y + 1), valueWidth, textHeight},
                                              valueView, 3, TextAlign::Right);
                } else if (visual.h >= 44) {
                    labelText = prepareText({static_cast<int16_t>(visual.x + 7), static_cast<int16_t>(visual.y + 2),
                                             headerWidth, 16},
                                            label, 2);
                    valueLayout = prepareText({static_cast<int16_t>(visual.x + 7), static_cast<int16_t>(visual.y + 18),
                                               headerWidth, 16},
                                              valueView, 2, TextAlign::Right);
                } else {
                    uint8_t labelSize = visual.h >= 30 ? 2 : 1;
                    uint8_t valueSize = labelSize;
                    int16_t valueWidth = textWidthFor(valueView, valueSize);
                    if (headerWidth < textWidthFor(label, labelSize) + valueWidth + 8) {
                        valueSize = 1;
                        valueWidth = textWidthFor(valueView, 1);
                    }
                    if (headerWidth < textWidthFor(label, labelSize) + valueWidth + 8)
                        labelSize = 1;
                    const int16_t labelWidth = std::max<int16_t>(0, static_cast<int16_t>(headerWidth - valueWidth - 8));
                    const int16_t textY = static_cast<int16_t>(visual.y + 2);
                    labelText =
                        prepareText({static_cast<int16_t>(visual.x + 7), textY, labelWidth, 16}, label, labelSize);
                    valueLayout =
                        prepareText({static_cast<int16_t>(visual.x + visual.w - valueWidth - 7), textY, valueWidth, 16},
                                    valueView, valueSize, TextAlign::Right);
                }
            }
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const int16_t dx = rect.x - bounds.x, dy = rect.y - bounds.y;
                const Rect visual{rect.x, static_cast<int16_t>(rect.y + (rect.h - visualHeight) / 2), rect.w,
                                  visualHeight};
                const Rect track{static_cast<int16_t>(visual.x + trackInset),
                                 static_cast<int16_t>(visual.y + (labeled ? visual.h - 8 : visual.h / 2 - 1)),
                                 static_cast<int16_t>(visual.w - 2 * trackInset), 3};
                if (labeled) {
                    const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
                    output.fillRoundRect(visual.x, visual.y, visual.w, visual.h, 5, surface);
                    output.drawRoundRect(visual.x, visual.y, visual.w, visual.h, 5,
                                         color(ui::themes::ColorRole::Outline));
                    drawText(output, labelText, color(themes::Foreground), dx, dy);
                    drawText(output, valueLayout, color(activeRole), dx, dy);
                }
                output.fillRect(track.x, track.y, track.w, track.h, color(ui::themes::ColorRole::ProgressTrack));
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
                        output.drawFastVLine(tickX, static_cast<int16_t>(trackCenterY - 3), 7,
                                             color(ui::themes::ColorRole::Outline));
                        if (interval == intervalCount)
                            break;
                    }
                }
                output.fillRect(track.x, track.y, static_cast<int16_t>(knobX - track.x + 1), track.h,
                                color(activeRole));
                const int16_t knobRadius = labeled ? 5 : 7;
                output.fillCircle(knobX, trackCenterY, knobRadius, color(activeRole));
                output.drawCircle(knobX, trackCenterY, knobRadius, color(ui::themes::ColorRole::OnAccent));
            });
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
        rect = paintBounds(rect);
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
            const Rect bounds = rect;
            const auto minusText = prepareText(decrement, "-", 2, TextAlign::Center);
            const auto plusText = prepareText(increment, "+", 2, TextAlign::Center);
            char valueText[24];
            std::snprintf(valueText, sizeof(valueText), "%d%.*s", displayedValue, static_cast<int>(suffix.size()),
                          suffix.data());
            const Rect middle{static_cast<int16_t>(decrement.x + decrement.w + 6), rect.y,
                              static_cast<int16_t>(rect.w - buttonWidth * 2 - 12), rect.h};
            TextLayout labelText, valueLayout;
            if (rect.h >= 44) {
                labelText = prepareText({middle.x, static_cast<int16_t>(middle.y + 2), middle.w, 16}, label, 2,
                                        TextAlign::Center);
                valueLayout = prepareText({middle.x, static_cast<int16_t>(middle.y + 20), middle.w,
                                           static_cast<int16_t>(middle.h - 20)},
                                          valueText, 2, TextAlign::Center);
            } else {
                const int16_t valueWidth = std::min<int16_t>(middle.w / 2, textWidth(valueText, 2));
                labelText = prepareText({middle.x, middle.y, static_cast<int16_t>(middle.w - valueWidth - 6), middle.h},
                                        label, 2);
                valueLayout = prepareText({static_cast<int16_t>(middle.x + middle.w - valueWidth), middle.y, valueWidth,
                                           middle.h},
                                          valueText, 2, TextAlign::Right);
            }
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const int16_t dx = rect.x - bounds.x, dy = rect.y - bounds.y;
                const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
                const uint16_t outline = color(ui::themes::ColorRole::Outline);
                output.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
                output.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, outline);
                output.drawFastVLine(static_cast<int16_t>(rect.x + buttonWidth), static_cast<int16_t>(rect.y + 4),
                                     static_cast<int16_t>(rect.h - 8), outline);
                output.drawFastVLine(static_cast<int16_t>(rect.x + rect.w - buttonWidth),
                                     static_cast<int16_t>(rect.y + 4), static_cast<int16_t>(rect.h - 8), outline);

                const uint16_t muted = color(ui::themes::ColorRole::Muted);
                drawText(output, minusText, displayedValue > minimum ? color(activeRole) : muted, dx, dy);
                drawText(output, plusText, displayedValue < maximum ? color(activeRole) : muted, dx, dy);
                drawText(output, labelText, color(themes::Foreground), dx, dy);
                drawText(output, valueLayout, color(activeRole), dx, dy);
            });
        }
        if (changed)
            value = displayedValue;
        return changed;
    }

    void Context::dial(Rect rect, int value, int minimum, int maximum, std::string_view labelText) {
        rect = paintBounds(rect);
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
        int16_t needleX = cx, needleY = cy;
        if (maximum > minimum) {
            constexpr float kPi = 3.14159265358979323846f;
            const float angle = (-135.0f + 270.0f * (value - minimum) / (maximum - minimum)) * kPi / 180.0f;
            needleX = static_cast<int16_t>(cx + std::cos(angle) * (radius - 4));
            needleY = static_cast<int16_t>(cy + std::sin(angle) * (radius - 4));
        }
        const auto label = prepareText({rect.x, static_cast<int16_t>(cy + radius / 2), rect.w, textHeight(1)},
                                       labelText, 1, TextAlign::Center);
        const Rect bounds = rect;
        paint(rect, [&](Arduino_GFX& output, Rect rect) {
            const int16_t dx = rect.x - bounds.x, dy = rect.y - bounds.y;
            output.drawCircle(cx + dx, cy + dy, radius, color(ui::themes::ColorRole::ProgressTrack));
            if (maximum > minimum)
                output.drawLine(cx + dx, cy + dy, needleX + dx, needleY + dy, color(ui::themes::ColorRole::Accent));
            drawText(output, label, color(themes::Muted), dx, dy);
        });
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

    bool Context::rotary(Rect rect, int& value, int minimum, int maximum, int step, std::string_view label) {
        rect = paintBounds(rect);
        if (rect.w <= 0 || rect.h <= 0 || maximum <= minimum || step <= 0)
            return false;
        const int before = value;
        const Touch* event = touch();
        if (event && hasTouch(*event, TouchStart) && contains(rect, event->x, event->y)) {
            rotaryDragging_ = true;
            rotaryRect_ = rect;
            rotaryStartX_ = event->x;
            rotaryStartValue_ = value;
        }
        if (rotaryDragging_ && rotaryRect_ == rect && event) {
            const int delta = (static_cast<int>(event->x) - rotaryStartX_) / 8;
            value = std::clamp(rotaryStartValue_ + delta * step, minimum, maximum);
            if (hasTouch(*event, TouchRelease))
                rotaryDragging_ = false;
        }
        auto state = combine(signature("rotary"), value);
        state = combine(state, minimum);
        state = combine(state, maximum);
        state = signature(label, state);
        if (claim(Kind::Rotary, rect, state).changed) {
            const int16_t labelHeight = label.empty() ? 0 : textHeightFor(label, 2) + 2;
            const int16_t diameter = std::min<int16_t>(rect.w, rect.h - labelHeight);
            const int16_t r = std::max<int16_t>(6, diameter / 2 - 3);
            const int16_t cx = rect.x + rect.w / 2, cy = rect.y + (rect.h - labelHeight) / 2;
            const int fill = (value - minimum) * 48 / (maximum - minimum);
            std::array<std::array<int16_t, 4>, 49> ticks{};
            for (int i = 0; i <= 48; ++i) {
                const float a = (130 + i * 280.f / 48) * 0.01745329252f;
                ticks[i] = {static_cast<int16_t>(cx + std::cos(a) * (r - 5)),
                            static_cast<int16_t>(cy + std::sin(a) * (r - 5)),
                            static_cast<int16_t>(cx + std::cos(a) * r), static_cast<int16_t>(cy + std::sin(a) * r)};
            }
            const auto number = std::to_string(value);
            const uint8_t size = textWidth(number, 3) <= diameter - 16 && diameter >= 44 ? 3 : 2;
            const auto valueText =
                prepareText({static_cast<int16_t>(rect.x + 4), static_cast<int16_t>(cy - textHeight(size) / 2),
                             static_cast<int16_t>(rect.w - 8), textHeight(size)},
                            number, size, TextAlign::Center);
            const auto labelText =
                prepareFixedText({rect.x, static_cast<int16_t>(rect.y + rect.h - labelHeight), rect.w, labelHeight},
                                 label, 2, TextAlign::Center, 1);
            const Rect bounds = rect;
            paint(rect, [&](Arduino_GFX& output, Rect rect) {
                const int16_t dx = rect.x - bounds.x, dy = rect.y - bounds.y;
                for (int i = 0; i <= 48; ++i) {
                    const auto& tick = ticks[i];
                    output.drawLine(tick[0] + dx, tick[1] + dy, tick[2] + dx, tick[3] + dy,
                                    color(i <= fill ? themes::Accent : themes::ProgressTrack));
                }
                drawText(output, valueText, color(themes::Foreground), dx, dy);
                drawText(output, labelText, color(themes::Muted), dx, dy);
            });
        }
        return before != value;
    }

} // namespace ui
