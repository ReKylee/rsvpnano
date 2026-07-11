#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <array>
#include <cstdint>
#include <string_view>

#include "ui/Theme.h"
#include "ui/Touch.h"

namespace ui {

    struct Rect {
        int16_t x = 0;
        int16_t y = 0;
        int16_t w = 0;
        int16_t h = 0;
    };

    constexpr bool operator==(Rect left, Rect right) {
        return left.x == right.x && left.y == right.y && left.w == right.w && left.h == right.h;
    }

    constexpr bool contains(Rect rect, uint16_t x, uint16_t y) {
        return x >= rect.x && y >= rect.y && x < rect.x + rect.w && y < rect.y + rect.h;
    }

    struct Column {
        Rect bounds;
        int16_t gap = 0;
        int16_t cursor = 0;

        constexpr Rect next(int16_t height) {
            const Rect result{bounds.x, static_cast<int16_t>(bounds.y + cursor), bounds.w, height};
            cursor = static_cast<int16_t>(cursor + height + gap);
            return result;
        }
    };

    struct Row {
        Rect bounds;
        int16_t gap = 0;
        int16_t cursor = 0;

        constexpr Rect next(int16_t width) {
            const Rect result{static_cast<int16_t>(bounds.x + cursor), bounds.y, width, bounds.h};
            cursor = static_cast<int16_t>(cursor + width + gap);
            return result;
        }
    };

    struct Grid {
        Rect bounds;
        uint8_t columns = 1;
        int16_t rowHeight = 0;
        int16_t gap = 0;
        uint16_t index = 0;

        constexpr Rect next() {
            const uint8_t safeColumns = columns == 0 ? 1 : columns;
            const int16_t cellWidth = static_cast<int16_t>((bounds.w - gap * (safeColumns - 1)) / safeColumns);
            const uint16_t column = index % safeColumns;
            const uint16_t row = index / safeColumns;
            ++index;
            return {static_cast<int16_t>(bounds.x + column * (cellWidth + gap)),
                    static_cast<int16_t>(bounds.y + row * (rowHeight + gap)), cellWidth, rowHeight};
        }
    };

    struct SliderResult {
        int value = 0;
        bool changed = false;
    };

    enum class Icon : uint8_t {
        None,
        Bookmark,
        Power,
    };

    enum class TextAlign : uint8_t {
        Left,
        Center,
        Right,
    };

    class Context {
    public:
        static constexpr size_t kSlotCapacity = 64;
        using Flush = void (*)();
        using FlushRegion = bool (*)(uint16_t x, uint16_t y, uint16_t width, uint16_t height);

        explicit Context(Arduino_GFX& gfx, Flush flush, FlushRegion flushRegion = nullptr);

        void setTheme(const ui::themes::Theme& theme);
        void setTouchSource(TouchSource source, uint32_t nowMs);
        bool pollTouch(uint32_t nowMs);
        const Touch* touch() const {
            return touchPending_ ? &touchEvent_ : nullptr;
        }
        void beginFrame(uint8_t screen);
        void endFrame();
        void invalidate();

        void label(Rect rect, std::string_view text, uint8_t textSize = 2,
                   ui::themes::ColorRole role = ui::themes::ColorRole::Foreground, TextAlign align = TextAlign::Left);
        bool button(Rect rect, std::string_view text, Icon icon = Icon::None);
        bool iconButton(Rect rect, Icon icon);
        bool tab(Rect rect, std::string_view text, bool active);
        void battery(Rect rect, uint8_t percent, bool charging, std::string_view label);
        void progress(Rect rect, int value, int minimum = 0, int maximum = 100);
        SliderResult slider(Rect rect, int value, int minimum, int maximum, int step = 1);
        void dial(Rect rect, int value, int minimum, int maximum, std::string_view label = {});
        bool redraw(Rect rect, uint32_t signature);

        uint16_t color(ui::themes::ColorRole role) const;
        uint16_t blend(ui::themes::ColorRole role, uint8_t alpha) const;
        Arduino_GFX& gfx() {
            return gfx_;
        }
        int16_t width() const {
            return gfx_.width();
        }
        int16_t height() const {
            return gfx_.height();
        }
        bool drew() const {
            return drew_;
        }

        static uint32_t signature(std::string_view text, uint32_t seed = 2166136261U);
        static uint32_t combine(uint32_t seed, uint32_t value);

    private:
        enum class Kind : uint8_t {
            Label,
            Button,
            Tab,
            Progress,
            Slider,
            Dial,
            Battery,
            Custom
        };

        struct Slot {
            Rect rect;
            uint32_t signature = 0;
            Kind kind = Kind::Custom;
            bool valid = false;
        };

        struct Claim {
            size_t index = kSlotCapacity;
            bool changed = true;
        };

        Claim claim(Kind kind, Rect rect, uint32_t signature);
        void clear(Rect rect);
        void markDirty(Rect rect);
        void drawText(Rect rect, std::string_view text, uint8_t textSize, uint16_t color,
                      TextAlign align = TextAlign::Left);
        void drawIcon(Rect rect, Icon icon, uint16_t color, uint16_t surface);
        int valueAt(Rect rect, uint16_t x, int minimum, int maximum, int step) const;
        bool tapped(size_t slot, Rect rect);
        void resetTouchGesture();
        bool beginTouch(uint32_t nowMs);
        TouchContact mapTouch(TouchContact contact) const;
        bool updateTouch(const TouchContact& contact, uint32_t nowMs);

        Arduino_GFX& gfx_;
        Flush flush_ = nullptr;
        FlushRegion flushRegion_ = nullptr;
        const ui::themes::Theme* theme_ = nullptr;
        TouchSource touchSource_{};
        Touch touchEvent_{};
        Orientation touchOrientation_ = Orientation::Portrait;
        uint8_t touchEmptySamples_ = 0;
        uint8_t touchReadFailures_ = 0;
        uint32_t touchStartedAtMs_ = 0;
        uint32_t touchLastPollMs_ = 0;
        uint32_t touchBackoffUntilMs_ = 0;
        uint32_t touchIgnoreUntilMs_ = 0;
        uint16_t touchStartX_ = 0;
        uint16_t touchStartY_ = 0;
        uint16_t touchLastX_ = 0;
        uint16_t touchLastY_ = 0;
        bool touchInitialized_ = false;
        bool touchActive_ = false;
        bool touchHoldEmitted_ = false;
        bool touchPending_ = false;
        std::array<Slot, kSlotCapacity> slots_{};
        size_t slotCount_ = 0;
        size_t nextSlot_ = 0;
        size_t capturedSlot_ = kSlotCapacity;
        uint8_t screen_ = 0xFF;
        bool invalid_ = true;
        bool drew_ = false;
        Rect dirty_{};
        bool hasDirty_ = false;
    };

} // namespace ui
