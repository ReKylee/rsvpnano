#include "ui/Ui.h"

namespace ui {
    namespace {
        constexpr uint8_t kTapCancelOutsideSamples = 2;
    } // namespace

    void Context::setTouchSource(TouchSource source) {
        touchSource_ = source;
        resetTouchGesture();
    }

    bool Context::pollTouch(uint32_t nowMs) {
        touchPending_ = false;
        if (touchSource_.poll == nullptr)
            return false;

        TouchContact contact;
        switch (touchSource_.poll(contact)) {
        case TouchSampleResult::None:
            return false;
        case TouchSampleResult::Reset:
            if (!touchActive_) {
                resetTouchGesture();
                return false;
            }
            touchActive_ = false;
            touchHoldEmitted_ = false;
            touchOutsideSamples_ = 0;
            touchStartedAtMs_ = 0;
            touchEvent_ = {TouchRelease, touchLastX_, touchLastY_};
            return touchPending_ = true;
        case TouchSampleResult::Contact:
            break;
        }

        const uint32_t sampledAtMs = contact.sampledAtMs == 0 ? nowMs : contact.sampledAtMs;
        touchLastPollMs_ = sampledAtMs;
        return updateTouch(contact, sampledAtMs);
    }

    bool Context::tap(Rect rect, bool enabled) {
        rect = paintBounds(rect);
        const size_t slot = nextSlot_;
        claim(Kind::Touch, rect, enabled);
        return tapped(slot, rect, enabled);
    }

    bool Context::tapped(size_t slot, Rect rect, bool enabled) {
        if (!enabled) {
            if (capturedSlot_ == slot)
                capturedSlot_ = kSlotCapacity;
            return false;
        }
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
        rotaryDragging_ = false;
        touchActive_ = false;
        touchHoldEmitted_ = false;
        touchOutsideSamples_ = 0;
        touchPending_ = false;
        touchStartedAtMs_ = 0;
        touchStartX_ = 0;
        touchStartY_ = 0;
        touchLastX_ = 0;
        touchLastY_ = 0;
        capturedSlot_ = kSlotCapacity;
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
            if (!touchActive_)
                return false;
            const bool tapped = !touchHoldEmitted_ && nowMs - touchStartedAtMs_ <= touchSource_.timing.tapMaxDurationMs
                             && touchOutsideSamples_ < kTapCancelOutsideSamples;
            touchActive_ = false;
            touchEvent_ = {static_cast<uint8_t>(TouchRelease | (tapped ? TouchTap : TouchNone)), touchLastX_,
                           touchLastY_};
            return touchPending_ = true;
        }

        const TouchContact mapped = mapTouch(contact);
        if (!touchActive_) {
            touchActive_ = true;
            touchHoldEmitted_ = false;
            touchOutsideSamples_ = 0;
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
        const bool outside = dx > touchSource_.timing.tapMoveTolerancePx || dy > touchSource_.timing.tapMoveTolerancePx;
        if (outside) {
            touchOutsideSamples_ = std::min<uint8_t>(touchOutsideSamples_ + 1, kTapCancelOutsideSamples);
        } else if (touchOutsideSamples_ < kTapCancelOutsideSamples) {
            touchOutsideSamples_ = 0;
        }
        if (!touchHoldEmitted_ && touchOutsideSamples_ < kTapCancelOutsideSamples
            && nowMs - touchStartedAtMs_ >= touchSource_.timing.holdMs) {
            touchHoldEmitted_ = true;
            actions |= TouchHold;
        }
        touchEvent_ = {actions, mapped.x, mapped.y};
        return touchPending_ = true;
    }

} // namespace ui
