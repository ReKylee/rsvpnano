#include "ui/Inputs.h"
#include "Recording.h"
#include "settings/SettingsRules.h"

#include <array>
#include <cassert>
#include <climits>
#include <iostream>
#include <vector>

namespace {
    constexpr ui::Rect area{0, 0, 280, 72};
    enum class Mode { First = 3, Second = 9, Missing = 100 };
    constexpr std::array modes{Mode::First, Mode::Second};
    constexpr auto labelFor = [](Mode mode) { return mode == Mode::First ? "First" : "Second"; };
    using Scalar = settings::BoundedValue<int, 10, 1000, 10>;

    void selections(ui::Context& ui) {
        using namespace testui;
        for (auto layout : {ui::ValueLayout::Inline, ui::ValueLayout::Stacked, ui::ValueLayout::Card}) {
            recording = {};
            Mode value = Mode::First;
            const ui::ValueStyle style{layout, 2};
            assert(!ui::select(ui, area, "Mode", value, modes, labelFor, style));
            assert(value == Mode::First && recording.calls.back().value == "First");
            recording.activate = "Mode";
            assert(ui::select(ui, area, "Mode", value, modes, labelFor, style) && value == Mode::Second);
            assert(ui::select(ui, area, "Mode", value, modes, labelFor, style) && value == Mode::First);
            value = Mode::Missing;
            assert(ui::select(ui, area, "Mode", value, modes, labelFor, style) && value == Mode::First);
            assert(recording.calls.back().value == caption(UiText::Unknown));
            assert(recording.calls.back().kind == (layout == ui::ValueLayout::Card ? Kind::Card : Kind::Setting));
            if (layout == ui::ValueLayout::Card)
                assert(recording.calls.back().textSize == 2);
            else
                assert(recording.calls.back().layout == (layout == ui::ValueLayout::Inline
                                                          ? ui::SettingLayout::Inline : ui::SettingLayout::Stacked));
        }

        recording = {};
        Mode value = Mode::First;
        int labels = 0;
        auto counted = [&](Mode) { ++labels; return "label"; };
        recording.activate = std::string{caption(UiText::ReadingMode)};
        assert(!ui::select(ui, {}, UiText::ReadingMode, value, modes, counted));
        assert(!ui::select(ui, area, UiText::ReadingMode, value, std::array<Mode, 0>{}, counted));
        assert(recording.calls.empty() && recording.translations == 0 && labels == 0);
        assert(!ui::select(ui, area, UiText::ReadingMode, value, std::array{Mode::First}, counted));
        assert(labels == 1 && value == Mode::First);

        // A caller's existing records work directly; no Choice/Option allocation or registry.
        struct Device { int id; std::string name; };
        const std::vector<Device> devices{{42, "Keyboard"}, {81, "Display"}};
        int selected = 42;
        recording.activate = "Device";
        assert(ui::select(ui, area, "Device", selected, devices, &Device::name, {}, &Device::id));
        assert(selected == 81 && recording.calls.back().value == "Keyboard");
        assert(ui::select(ui, area, "Device", selected, devices, &Device::name, {}, &Device::id));
        assert(selected == 42);

        // Non-common forward ranges and dynamically formatted labels remain synchronous.
        std::array values{1, 2, 3, 4};
        auto visible = values | std::views::take_while([](int x) { return x < 3; });
        selected = 2;
        recording.activate = "Number";
        auto format = [](int x) { return std::string(128, 'x') + std::to_string(x); };
        assert(ui::select(ui, area, "Number", selected, visible, format));
        assert(selected == 1 && recording.calls.back().value == std::string(128, 'x') + "2");

        std::string color = "red";
        const std::array<std::string_view, 2> colors{"red", "blue"};
        recording.activate = "Color";
        assert(ui::select(ui, area, "Color", color, colors) && color == "blue");
    }

    void lazyValuesAndNumbers(ui::Context& ui) {
        using namespace testui;
        for (const auto layout : {ui::ValueLayout::Inline, ui::ValueLayout::Card}) {
            recording = {};
            int calls = 0;
            const auto value = [&] { ++calls; return std::string(128, 'x'); };
            const ui::ValueStyle style{layout, 2};
            assert(!ui::valueButton(ui, {}, UiText::Theme, value, style));
            assert(calls == 0 && recording.translations == 0 && recording.calls.empty());
            assert(!ui::valueButton(ui, area, UiText::Theme, value, style));
            assert(calls == 1 && recording.calls.back().value == std::string(128, 'x'));
        }
        for (const auto input : {ui::NumberInput::Slider, ui::NumberInput::Stepper}) {
            recording = {};
            Scalar value{300};
            assert(!ui::number(ui, {}, UiText::Brightness, value, "%", input));
            assert(recording.calls.empty() && recording.translations == 0);
            assert(!ui::number(ui, area, UiText::Brightness, value, "%", input));
            assert(recording.calls.size() == 1);
            assert(recording.calls.back().kind == (input == ui::NumberInput::Stepper ? Kind::Stepper : Kind::Slider));
            recording.numericValue = 350;
            assert(ui::number(ui, area, "Level", value, {}, input));
            assert(static_cast<int>(value) == 350);
        }
    }

    void numerics(ui::Context& ui) {
        using namespace testui;
        recording = {};
        Scalar value;
        assert(!ui::rotaryStepper(ui, {}, "Rate", value) && recording.calls.empty());
        assert(!ui.rotary({}, value, "Rate") && recording.calls.empty());
        recording.numericValue = 420;
        assert(ui.rotary(area, value, "Rate") && static_cast<int>(value) == 420);
        assert(recording.minimum == 10 && recording.maximum == 1000 && recording.step == 10);
        assert(!ui.rotary(area, value, "Rate"));
        recording.numericValue.reset();

        for (bool upper : {false, true}) {
            value = upper ? Scalar::max() : Scalar::min();
            recording.activate = upper ? "+" : "-";
            assert(!ui::rotaryStepper(ui, area, "Rate", value));
            recording.activate = upper ? "-" : "+";
            assert(ui::rotaryStepper(ui, area, "Rate", value));
            assert(static_cast<int>(value) == (upper ? 990 : 20));
        }
        recording = {};
        int raw = INT_MAX - 1;
        recording.activate = "+";
        assert(ui::rotaryStepper(ui, area, "Rate", raw, INT_MAX - 10, INT_MAX, 10));
        assert(raw == INT_MAX);
        raw = INT_MIN + 1;
        recording.activate = "-";
        assert(ui::rotaryStepper(ui, area, "Rate", raw, INT_MIN, INT_MIN + 10, 10));
        assert(raw == INT_MIN);
        recording = {};
        assert(!ui::rotaryStepper(ui, area, "Rate", raw, 10, 10, 1));
        assert(!ui::rotaryStepper(ui, area, "Rate", raw, 10, 100, 0));
        assert(recording.calls.empty());
        for (int16_t width : {20, 96, 124, 280}) {
            recording = {};
            ui::rotaryStepper(ui, {4, 8, width, 72}, "Rate", value);
            assert(recording.calls.size() == 3);
            const auto dial = recording.calls[0].rect;
            const auto left = recording.calls[1].rect;
            const auto right = recording.calls[2].rect;
            assert(left.x + left.w <= dial.x && dial.x + dial.w <= right.x);
            assert(left.x == 4 && right.x + right.w == 4 + width);
        }
    }
}
int main() {
    Arduino_GFX gfx;
    ui::Context ui{gfx};
    selections(ui);
    lazyValuesAndNumbers(ui);
    numerics(ui);
    std::cout << "Inputs: general ranges, projections, hidden work, lifetimes and bounded numerics passed\n";
}
