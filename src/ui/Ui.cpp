#include "ui/Ui.h"

#include <algorithm>
#include <cmath>

namespace ui {
    namespace {

        constexpr uint16_t kFallbackBlack = 0x0000;
        constexpr uint16_t kFallbackWhite = 0xFFFF;
        constexpr uint16_t kBatteryGood = ui::themes::rgb565(126, 176, 92);
        constexpr uint16_t kBatteryMedium = ui::themes::rgb565(214, 163, 58);
        constexpr uint16_t kBatteryLow = ui::themes::rgb565(200, 82, 82);

        int16_t textWidth(std::string_view text, uint8_t size) {
            return static_cast<int16_t>(text.size() * 6U * std::max<uint8_t>(1, size));
        }

        int16_t textHeight(uint8_t size) {
            return static_cast<int16_t>(8U * std::max<uint8_t>(1, size));
        }

        size_t fittedLength(std::string_view text, size_t capacity) {
            if (text.size() <= capacity)
                return text.size();
            if (capacity <= 3)
                return 0;
            size_t length = capacity - 3;
            while (length > 0 && (static_cast<uint8_t>(text[length]) & 0xC0U) == 0x80U)
                --length;
            return length;
        }

    } // namespace

    Context::Context(Arduino_GFX& gfx, Flush flush, FlushRegion flushRegion) :
            gfx_(gfx),
            flush_(flush),
            flushRegion_(flushRegion) {}

    void Context::setTheme(const ui::themes::Theme& theme) {
        if (theme_ != &theme) {
            theme_ = &theme;
            invalidate();
        }
    }

    void Context::setTouchSource(TouchSource source, uint32_t nowMs) {
        touchSource_ = source;
        touchOrientation_ = source.orientation == nullptr ? Orientation::Portrait : source.orientation();
        beginTouch(nowMs);
    }

    bool Context::pollTouch(uint32_t nowMs) {
        touchPending_ = false;
        if (touchSource_.read == nullptr)
            return false;

        const Orientation orientation =
            touchSource_.orientation == nullptr ? Orientation::Portrait : touchSource_.orientation();
        if (orientation != touchOrientation_) {
            touchOrientation_ = orientation;
            resetTouchGesture();
        }
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
        if (touchSource_.ready != nullptr && !touchSource_.ready()) {
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
        hasDirty_ = false;
        if (screen_ != screen) {
            screen_ = screen;
            invalid_ = true;
            capturedSlot_ = kSlotCapacity;
        }
        if (invalid_) {
            gfx_.fillScreen(color(ui::themes::ColorRole::Background));
            markDirty({0, 0, width(), height()});
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
                clear(slots_[index].rect);
                slots_[index].valid = false;
            }
        }
        if (capturedSlot_ >= nextSlot_) {
            capturedSlot_ = kSlotCapacity;
        }
        slotCount_ = std::min(nextSlot_, kSlotCapacity);
        if (drew_ && hasDirty_) {
            const bool flushed = flushRegion_ != nullptr
                              && flushRegion_(static_cast<uint16_t>(dirty_.x), static_cast<uint16_t>(dirty_.y),
                                              static_cast<uint16_t>(dirty_.w), static_cast<uint16_t>(dirty_.h));
            if (!flushed && flush_ != nullptr) {
                flush_();
            }
        }
        touchPending_ = false;
    }

    void Context::invalidate() {
        invalid_ = true;
    }

    void Context::label(Rect rect, std::string_view text, uint8_t textSize, ui::themes::ColorRole role,
                        TextAlign align) {
        uint32_t state = combine(signature(text), textSize);
        state = combine(state, static_cast<uint8_t>(role));
        state = combine(state, static_cast<uint8_t>(align));
        if (!claim(Kind::Label, rect, state).changed) {
            return;
        }
        drawText(rect, text, textSize, color(role), align);
    }

    bool Context::button(Rect rect, std::string_view text, Icon icon, uint8_t textLines, std::string_view detailLeft,
                         std::string_view detailRight) {
        const size_t slot = nextSlot_;
        const bool activated = tapped(slot, rect);
        uint32_t state = combine(signature(text), static_cast<uint8_t>(icon));
        state = combine(state, textLines);
        state = signature(detailLeft, state);
        state = signature(detailRight, state);
        const Claim widget = claim(Kind::Button, rect, state);
        if (widget.changed) {
            const uint16_t surface = color(ui::themes::ColorRole::SurfaceMuted);
            gfx_.fillRoundRect(rect.x, rect.y, rect.w, rect.h, 5, surface);
            gfx_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, color(ui::themes::ColorRole::Outline));
            if (rect.w > 16 && rect.h >= 28)
                gfx_.fillRect(static_cast<int16_t>(rect.x + 8), static_cast<int16_t>(rect.y + rect.h - 3),
                              static_cast<int16_t>(rect.w - 16), 2, color(ui::themes::ColorRole::Accent));
            const int16_t iconWidth = icon == Icon::None ? 0 : std::min<int16_t>(34, rect.w / 3);
            const bool hasDetail = !detailLeft.empty() || !detailRight.empty();
            const int16_t textHeight = hasDetail ? static_cast<int16_t>(rect.h - 18) : rect.h;
            const ui::Rect textRect{static_cast<int16_t>(rect.x + 6), rect.y,
                                    static_cast<int16_t>(std::max<int16_t>(0, rect.w - iconWidth - 12)), textHeight};
            drawText(textRect, text, 2, color(ui::themes::ColorRole::Foreground), TextAlign::Center, textLines);
            if (hasDetail) {
                const int16_t detailWidth = static_cast<int16_t>((textRect.w - 8) / 2);
                const int16_t detailY = static_cast<int16_t>(rect.y + rect.h - 20);
                drawText({textRect.x, detailY, detailWidth, 16}, detailLeft, 1, color(ui::themes::ColorRole::Muted));
                drawText({static_cast<int16_t>(textRect.x + textRect.w - detailWidth), detailY, detailWidth, 16},
                         detailRight, 1, color(ui::themes::ColorRole::Muted), TextAlign::Right);
            }
            if (icon != Icon::None)
                drawIcon({static_cast<int16_t>(rect.x + rect.w - iconWidth), rect.y, iconWidth, rect.h}, icon,
                         color(ui::themes::ColorRole::Accent), surface);
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

    void Context::battery(Rect rect, uint8_t percent, bool charging, std::string_view labelText) {
        percent = std::min<uint8_t>(percent, 100);
        uint32_t state = combine(signature(labelText), percent);
        state = combine(state, charging);
        if (!claim(Kind::Battery, rect, state).changed || (labelText.empty() && percent == 0))
            return;

        constexpr int16_t iconWidth = 26;
        constexpr int16_t iconHeight = 13;
        constexpr int16_t capWidth = 3;
        constexpr int16_t gap = 7;
        const int16_t labelWidth = textWidth(labelText, 2);
        const int16_t totalWidth = static_cast<int16_t>(iconWidth + capWidth + gap + labelWidth);
        const int16_t x = std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + rect.w - totalWidth));
        const int16_t iconY = static_cast<int16_t>(rect.y + std::max<int16_t>(0, (rect.h - iconHeight) / 2));
        const uint16_t outline = color(ui::themes::ColorRole::Muted);
        const uint16_t fillColor = charging || percent > 35 ? kBatteryGood
                                 : percent <= 18            ? kBatteryLow
                                                            : kBatteryMedium;

        gfx_.drawRect(x, iconY, iconWidth, iconHeight, outline);
        gfx_.fillRect(static_cast<int16_t>(x + iconWidth), static_cast<int16_t>(iconY + 4), capWidth, 5, outline);
        const int16_t fill = charging ? iconWidth - 4 : static_cast<int16_t>((iconWidth - 4) * percent / 100);
        if (fill > 0)
            gfx_.fillRect(static_cast<int16_t>(x + 2), static_cast<int16_t>(iconY + 2), fill, iconHeight - 4,
                          fillColor);
        if (charging) {
            const uint16_t background = color(ui::themes::ColorRole::Background);
            gfx_.drawLine(static_cast<int16_t>(x + 15), static_cast<int16_t>(iconY + 2), static_cast<int16_t>(x + 11),
                          static_cast<int16_t>(iconY + 7), background);
            gfx_.drawLine(static_cast<int16_t>(x + 11), static_cast<int16_t>(iconY + 7), static_cast<int16_t>(x + 16),
                          static_cast<int16_t>(iconY + 7), background);
            gfx_.drawLine(static_cast<int16_t>(x + 16), static_cast<int16_t>(iconY + 7), static_cast<int16_t>(x + 12),
                          static_cast<int16_t>(iconY + 12), background);
        }
        drawText({static_cast<int16_t>(x + iconWidth + capWidth + gap), rect.y, labelWidth, rect.h}, labelText, 2,
                 outline);
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

    SliderResult Context::slider(Rect rect, int value, int minimum, int maximum, int step) {
        const size_t slot = nextSlot_;
        const Touch* event = touch();
        const bool started = event != nullptr && hasTouch(*event, TouchStart) && contains(rect, event->x, event->y);
        if (started && slot < kSlotCapacity) {
            capturedSlot_ = slot;
        }

        SliderResult result{std::clamp(value, minimum, maximum), false};
        const bool moving =
            event != nullptr
            && (hasTouch(*event, TouchStart) || hasTouch(*event, TouchMove) || hasTouch(*event, TouchRelease));
        if (capturedSlot_ == slot && moving) {
            result.value = valueAt(rect, event->x, minimum, maximum, step);
            result.changed = result.value != value;
        }

        uint32_t state = combine(static_cast<uint32_t>(result.value), static_cast<uint32_t>(minimum));
        state = combine(state, static_cast<uint32_t>(maximum));
        state = combine(state, static_cast<uint32_t>(step));
        if (claim(Kind::Slider, rect, state).changed) {
            const int16_t trackY = static_cast<int16_t>(rect.y + rect.h / 2 - 2);
            gfx_.fillRect(rect.x, trackY, rect.w, 4, color(ui::themes::ColorRole::ProgressTrack));
            const int16_t knobX =
                maximum == minimum
                    ? rect.x
                    : static_cast<int16_t>(rect.x
                                           + (static_cast<int32_t>(rect.w - 1) * (result.value - minimum))
                                                 / (maximum - minimum));
            gfx_.fillCircle(knobX, static_cast<int16_t>(rect.y + rect.h / 2), 7, color(ui::themes::ColorRole::Accent));
        }

        if (capturedSlot_ == slot && event != nullptr && hasTouch(*event, TouchRelease)) {
            capturedSlot_ = kSlotCapacity;
        }
        return result;
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

    bool Context::redraw(Rect rect, uint32_t state) {
        return claim(Kind::Custom, rect, state).changed;
    }

    uint16_t Context::color(ui::themes::ColorRole role) const {
        if (theme_ == nullptr) {
            return role == ui::themes::ColorRole::Background ? kFallbackBlack : kFallbackWhite;
        }
        return theme_->colors[static_cast<size_t>(role)];
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
            if (slot.valid && !(slot.rect == rect)) {
                clear(slot.rect);
            }
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
        markDirty(rect);
        drew_ = true;
    }

    void Context::markDirty(Rect rect) {
        if (rect.w <= 0 || rect.h <= 0)
            return;
        const int16_t left = std::max<int16_t>(0, rect.x);
        const int16_t top = std::max<int16_t>(0, rect.y);
        const int16_t right = std::min<int16_t>(width(), static_cast<int16_t>(rect.x + rect.w));
        const int16_t bottom = std::min<int16_t>(height(), static_cast<int16_t>(rect.y + rect.h));
        if (right <= left || bottom <= top)
            return;
        if (!hasDirty_) {
            dirty_ = {left, top, static_cast<int16_t>(right - left), static_cast<int16_t>(bottom - top)};
            hasDirty_ = true;
            return;
        }
        const int16_t oldRight = static_cast<int16_t>(dirty_.x + dirty_.w);
        const int16_t oldBottom = static_cast<int16_t>(dirty_.y + dirty_.h);
        dirty_.x = std::min(dirty_.x, left);
        dirty_.y = std::min(dirty_.y, top);
        dirty_.w = static_cast<int16_t>(std::max(oldRight, right) - dirty_.x);
        dirty_.h = static_cast<int16_t>(std::max(oldBottom, bottom) - dirty_.y);
    }

    void Context::drawText(Rect rect, std::string_view text, uint8_t textSize, uint16_t textColor, TextAlign align,
                           uint8_t maxLines) {
        if (rect.w <= 0 || rect.h <= 0)
            return;
        const uint8_t size = std::max<uint8_t>(1, textSize);
        const size_t capacity = static_cast<size_t>(std::max<int16_t>(0, rect.w) / (6 * size));
        if (capacity == 0)
            return;
        gfx_.setFont(static_cast<const GFXfont*>(nullptr));
        gfx_.setTextSize(size);
        gfx_.setTextWrap(false);
        gfx_.setTextColor(textColor);

        std::string_view first = text;
        std::string_view second;
        if (maxLines > 1 && text.size() > capacity) {
            size_t split = capacity;
            while (split > 0 && (static_cast<uint8_t>(text[split]) & 0xC0U) == 0x80U)
                --split;
            const size_t space = text.rfind(' ', split);
            if (space != std::string_view::npos && space >= capacity / 2)
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
            const bool truncated = line.size() > capacity;
            const size_t length = fittedLength(line, capacity);
            const size_t dots = truncated ? std::min<size_t>(3, capacity) : 0;
            const int16_t renderedWidth = static_cast<int16_t>((length + dots) * 6U * size);
            const int16_t x = align == TextAlign::Center
                                ? std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + (rect.w - renderedWidth) / 2))
                            : align == TextAlign::Right
                                ? std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + rect.w - renderedWidth))
                                : rect.x;
            gfx_.setCursor(x, y);
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
        return hasTouch(*event, TouchTap) && contains(rect, event->x, event->y);
    }

    void Context::resetTouchGesture() {
        touchActive_ = false;
        touchHoldEmitted_ = false;
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
            return {true, static_cast<uint16_t>(maxY - rawY), rawX};
        case Orientation::PortraitFlipped:
            return {true, static_cast<uint16_t>(maxX - rawX), static_cast<uint16_t>(maxY - rawY)};
        case Orientation::Landscape:
            return {true, rawY, static_cast<uint16_t>(maxX - rawX)};
        default:
            return {true, rawX, rawY};
        }
    }

    bool Context::updateTouch(const TouchContact& contact, uint32_t nowMs) {
        if (!contact.touched) {
            if (!touchActive_ || ++touchEmptySamples_ < touchSource_.timing.releaseConfirmSamples)
                return false;
            const uint16_t dx = std::max(touchLastX_, touchStartX_) - std::min(touchLastX_, touchStartX_);
            const uint16_t dy = std::max(touchLastY_, touchStartY_) - std::min(touchLastY_, touchStartY_);
            const bool tapped = nowMs - touchStartedAtMs_ <= touchSource_.timing.tapMaxDurationMs
                             && dx <= touchSource_.timing.tapMoveTolerancePx
                             && dy <= touchSource_.timing.tapMoveTolerancePx;
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
        if (!touchHoldEmitted_ && nowMs - touchStartedAtMs_ >= touchSource_.timing.holdMs
            && dx <= touchSource_.timing.tapMoveTolerancePx && dy <= touchSource_.timing.tapMoveTolerancePx) {
            touchHoldEmitted_ = true;
            actions |= TouchHold;
        }
        touchEvent_ = {actions, mapped.x, mapped.y};
        return touchPending_ = true;
    }

} // namespace ui
