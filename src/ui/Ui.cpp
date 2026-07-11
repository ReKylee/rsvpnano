#include "ui/Ui.h"

#include <algorithm>
#include <cmath>

namespace ui {
    namespace {

        constexpr uint16_t kFallbackBlack = 0x0000;
        constexpr uint16_t kFallbackWhite = 0xFFFF;

        int16_t textWidth(std::string_view text, uint8_t size) {
            return static_cast<int16_t>(text.size() * 6U * std::max<uint8_t>(1, size));
        }

        int16_t textHeight(uint8_t size) {
            return static_cast<int16_t>(8U * std::max<uint8_t>(1, size));
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

    void Context::label(Rect rect, std::string_view text, uint8_t textSize, ui::themes::ColorRole role) {
        uint32_t state = combine(signature(text), textSize);
        state = combine(state, static_cast<uint8_t>(role));
        if (!claim(Kind::Label, rect, state).changed) {
            return;
        }
        drawText(rect, text, textSize, color(role));
    }

    bool Context::button(Rect rect, std::string_view text) {
        const Claim widget = claim(Kind::Button, rect, signature(text));
        if (widget.changed) {
            gfx_.drawRoundRect(rect.x, rect.y, rect.w, rect.h, 5, color(ui::themes::ColorRole::Outline));
            drawText(rect, text, 2, color(ui::themes::ColorRole::Foreground), true);
        }
        return tapped(widget.index, rect);
    }

    bool Context::tab(Rect rect, std::string_view text, bool active) {
        const uint32_t state = combine(signature(text), active);
        const Claim widget = claim(Kind::Tab, rect, state);
        if (widget.changed) {
            gfx_.fillRect(rect.x, rect.y, rect.w, rect.h,
                          color(active ? ui::themes::ColorRole::Surface : ui::themes::ColorRole::SurfaceMuted));
            gfx_.drawRect(rect.x, rect.y, rect.w, rect.h, color(ui::themes::ColorRole::Outline));
            if (active) {
                gfx_.fillRect(rect.x, static_cast<int16_t>(rect.y + 5), 3, static_cast<int16_t>(rect.h - 10),
                              color(ui::themes::ColorRole::Accent));
            }
            drawText(rect, text, 1, color(active ? ui::themes::ColorRole::Foreground : ui::themes::ColorRole::Muted),
                     true);
        }
        return tapped(widget.index, rect);
    }

    void Context::progress(Rect rect, int value, int minimum, int maximum) {
        value = std::clamp(value, minimum, maximum);
        uint32_t state = combine(static_cast<uint32_t>(value), static_cast<uint32_t>(minimum));
        state = combine(state, static_cast<uint32_t>(maximum));
        if (!claim(Kind::Progress, rect, state).changed) {
            return;
        }
        gfx_.drawRect(rect.x, rect.y, rect.w, rect.h, color(ui::themes::ColorRole::ProgressTrack));
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
                     color(ui::themes::ColorRole::Muted), true);
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

    void Context::drawText(Rect rect, std::string_view text, uint8_t textSize, uint16_t textColor, bool centered) {
        gfx_.setFont(static_cast<const GFXfont*>(nullptr));
        gfx_.setTextSize(textSize);
        gfx_.setTextWrap(false);
        gfx_.setTextColor(textColor);
        const int16_t x =
            centered
                ? std::max<int16_t>(rect.x, static_cast<int16_t>(rect.x + (rect.w - textWidth(text, textSize)) / 2))
                : rect.x;
        const int16_t y = centered ? static_cast<int16_t>(rect.y + (rect.h - textHeight(textSize)) / 2) : rect.y;
        gfx_.setCursor(x, y);
        for (char value: text) {
            gfx_.write(static_cast<uint8_t>(value));
        }
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
