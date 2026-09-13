#include "ui/Inputs.h"
#include "Recording.h"
#include "settings/SettingsRules.h"

#include <array>
#include <cassert>
#include <iostream>

namespace {
    constexpr ui::Rect area{0, 0, 280, 72};
    using Scalar = settings::BoundedValue<int, 10, 1000, 10>;

    void normalizedSelection(ui::Context& ui) {
        using namespace testui;
        constexpr std::array aboveMaximum{1010};
        constexpr std::array belowMinimum{0};
        const auto label = [](int) { return "Value"; };
        for (auto layout : {ui::ValueLayout::Inline, ui::ValueLayout::Stacked, ui::ValueLayout::Card}) {
            recording = {};
            recording.activate = "Rate";
            const ui::ValueStyle style{layout, 2};
            Scalar value{1000};
            assert(!ui::select(ui, area, "Rate", value, aboveMaximum, label, style));
            assert(static_cast<int>(value) == 1000);
            value = 10;
            assert(!ui::select(ui, area, "Rate", value, belowMinimum, label, style));
            assert(static_cast<int>(value) == 10);
            value = 300;
            assert(ui::select(ui, area, "Rate", value, aboveMaximum, label, style));
            assert(static_cast<int>(value) == 1000);
            value = 300;
            assert(ui::select(ui, area, "Rate", value, belowMinimum, label, style));
            assert(static_cast<int>(value) == 10);
        }
    }

    struct CountedValue {
        static inline int copies = 0;
        int value;
        explicit CountedValue(int initial) : value(initial) {}
        CountedValue(const CountedValue& other) : value(other.value) { ++copies; }
        CountedValue& operator=(int next) { value = next; return *this; }
        bool operator==(const CountedValue& other) const { return value == other.value; }
        bool operator==(int other) const { return value == other; }
    };

    void noIdleSnapshots(ui::Context& ui) {
        using namespace testui;
        recording = {};
        CountedValue::copies = 0;
        CountedValue value{1};
        constexpr std::array items{1, 2};
        const auto label = [](int) { return "Item"; };
        for (int frame = 0; frame < 100; ++frame)
            assert(!ui::select(ui, area, "Item", value, items, label));
        assert(CountedValue::copies == 0);
        recording.activate = "Item";
        assert(!ui::select(ui, area, "Item", value, std::array{1}, label));
        assert(CountedValue::copies == 0);
        assert(ui::select(ui, area, "Item", value, items, label));
        assert(value.value == 2 && CountedValue::copies == 1);
    }

    void tinyRotaryBounds(ui::Context& ui) {
        using namespace testui;
        for (int16_t width = -2; width <= 2; ++width) {
            recording = {};
            recording.activate = "+";
            Scalar value{300};
            assert(!ui::rotaryStepper(ui, {4, 8, width, 72}, "Rate", value));
            assert(recording.calls.empty() && static_cast<int>(value) == 300);
        }
        for (int16_t width = 3; width <= 640; ++width) {
            for (int16_t height : {1, 32, 56, 72, 160}) {
                recording = {};
                Scalar value{300};
                assert(!ui::rotaryStepper(ui, {4, 8, width, height}, "Rate", value));
                assert(recording.calls.size() == 3);
                const auto dial = recording.calls[0].rect;
                const auto left = recording.calls[1].rect;
                const auto right = recording.calls[2].rect;
                assert(dial.w > 0 && left.w > 0 && right.w > 0);
                assert(left.x == 4 && right.x + right.w == 4 + width);
                assert(left.x + left.w <= dial.x && dial.x + dial.w <= right.x);
                assert(dial.y == 8 && dial.h == height);
            }
        }
    }
}

int main() {
    Arduino_GFX gfx;
    ui::Context ui{gfx};
    normalizedSelection(ui);
    noIdleSnapshots(ui);
    tinyRotaryBounds(ui);
    std::cout << "Input edges: normalized edits, zero idle snapshots and 3,190 positive disjoint layouts passed\n";
}
