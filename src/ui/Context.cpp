#include "ui/Ui.h"

namespace ui {
    namespace {
        constexpr uint16_t kFallbackBlack = 0x0000;
        constexpr uint16_t kFallbackWhite = 0xFFFF;
    } // namespace

    Context::Context(Arduino_GFX& gfx) : gfx_(gfx) {}

    void Context::setTheme(const ui::themes::Theme& theme) {
        // Installing/removing themes can move another theme into the same catalog address.
        theme_ = &theme;
        invalidate();
    }

    void Context::setOrientation(Orientation orientation) {
        if (touchOrientation_ == orientation)
            return;
        touchOrientation_ = orientation;
        if constexpr (displayWriteAlignment() == 1)
            gfx_.setRotation(static_cast<uint8_t>(orientation));
        resetTouchGesture();
        invalidate();
    }

    void Context::beginFrame(uint8_t screen) {
        nextSlot_ = 0;
        drew_ = false;
        if (screen_ != screen) {
            screen_ = screen;
            gridPage_ = 0;
            rotaryDragging_ = false;
            contentFonts_.clear();
            invalid_ = true;
            capturedSlot_ = kSlotCapacity;
        }
        if (invalid_) {
            invalid_ = false;
            if constexpr (displayWriteAlignment() == 1)
                gfx_.fillScreen(color(ui::themes::ColorRole::Background));
            else
                clear({0, 0, width(), height()});
            markDrawn();
            for (Slot& slot: slots_) {
                slot.valid = false;
            }
            slotCount_ = 0;
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

    bool Context::redraw(Rect rect, uint32_t state, bool opaque) {
        return claim(opaque ? Kind::Opaque : Kind::Custom, rect, state).changed;
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
        // Include each field boundary: ("ab", "c") must not alias ("a", "bc").
        return Fnv1a::append(combine(seed, static_cast<uint32_t>(text.size())), text);
    }

    uint32_t Context::combine(uint32_t seed, uint32_t value) {
        return (seed ^ value) * Fnv1a::kPrime;
    }

    Context::Claim Context::claim(Kind kind, Rect rect, uint32_t state) {
        rect = paintBounds(rect);
        const size_t index = nextSlot_++;
        // Partial custom drawing requests a clear; widgets paint their own complete surface.
        const bool clearNew = kind == Kind::Custom;
        if (index >= kSlotCapacity) {
            if (clearNew)
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
            if (clearNew)
                clear(rect);
            slot = {rect, state, kind, true};
        }
        slotCount_ = std::max(slotCount_, index + 1);
        return {index, changed};
    }

    void Context::markDrawn() {
        drew_ = true;
    }

} // namespace ui
