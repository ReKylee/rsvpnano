#include <cassert>
#include <iostream>
#include "ui/SettingsControls.h"
#include "ui/screens/SettingsMenu.h"

enum class Mode { First = 3, Second = 9, Invalid = 100 };
constexpr std::array modes{ui::Choice{Mode::First, UiText::On}, ui::Choice{Mode::Second, UiText::Off}};

struct Scalar {
    int value;
    static constexpr int min() { return 10; }
    static constexpr int max() { return 1000; }
    static constexpr int step() { return 10; }
    operator int() const { return value; }
    Scalar& operator=(int next) { value = next; return *this; }
};

constexpr bool constexprChoices() {
    auto mode = Mode::First;
    const std::span<const ui::Choice<Mode>, 2> options{modes};
    if (!ui::cycleChoice(mode, options) || mode != Mode::Second) return false;
    if (!ui::cycleChoice(mode, options) || mode != Mode::First) return false;
    mode = Mode::Invalid;
    return ui::cycleChoice(mode, options) && mode == Mode::First;
}
static_assert(constexprChoices());

void testChoices() {
    auto mode = Mode::First;
    constexpr std::array<ui::Choice<Mode>, 0> empty{};
    assert(!ui::cycleChoice(mode, std::span<const ui::Choice<Mode>, 0>{empty}));
    constexpr std::array one{ui::Choice{Mode::First, UiText::On}};
    assert(!ui::cycleChoice(mode, std::span<const ui::Choice<Mode>, 1>{one}));
    for (const auto presentation : {ui::SettingPresentation::Inline, ui::SettingPresentation::Stacked,
                                    ui::SettingPresentation::Card}) {
        ui::Context context;
        ui::SettingsControls form{context, {presentation, 2}};
        assert(!form.choice({0, 0, 100, 40}, UiText::ReadingMode, mode, modes));
        assert(!form.changed() && mode == Mode::First);
        context.activated = true;
        assert(form.choice({0, 0, 100, 40}, UiText::ReadingMode, mode, modes));
        assert(form.changed() && mode == Mode::Second);
        assert(form.choice({0, 0, 100, 40}, UiText::ReadingMode, mode, modes));
        assert(mode == Mode::First);
        if (presentation == ui::SettingPresentation::Card) {
            assert(context.cards == 3 && context.settings == 0 && context.cardSize == 2);
        } else {
            assert(context.settings == 3 && context.cards == 0);
            assert(context.layout == (presentation == ui::SettingPresentation::Inline
                                          ? ui::SettingLayout::Inline : ui::SettingLayout::Stacked));
        }
    }
}

void testHiddenWork() {
    ui::Context context;
    context.activated = true;
    ui::SettingsControls form{context};
    auto mode = Mode::First;
    Scalar scalar{300};
    assert(!form.choice({}, UiText::Pause, mode, modes));
    assert(!form.slider({}, UiText::WordsPerMinute, scalar));
    assert(!form.stepper({}, UiText::WordsPerMinute, scalar));
    assert(!form.rotary({}, "WPM", scalar));
    assert(!form.rotaryStepper({}, "WPM", scalar));
    assert(!form.changed() && context.translations == 0 && context.settings == 0 && context.numerics == 0);
}

void testNumericLimits() {
    for (const bool upper : {false, true}) {
        ui::Context context;
        context.buttonToActivate = upper ? "+" : "-";
        Scalar value{upper ? Scalar::max() : Scalar::min()};
        ui::SettingsControls form{context};
        assert(!form.rotaryStepper({0, 0, 280, 72}, "WPM", value));
        assert(!form.changed());
        context.buttonToActivate = upper ? "-" : "+";
        assert(form.rotaryStepper({0, 0, 280, 72}, "WPM", value));
        assert(form.changed());
        assert(value.value == (upper ? 990 : 20));
        assert(context.lastMinimum == 10 && context.lastMaximum == 1000 && context.lastStep == 10);
    }
}

void testMenuGeometry() {
    static_assert(screens::settingsEntryCount == 5);
    static_assert(screens::settingsRows(screens::readingEntries, 2) == 2);
    static_assert(screens::settingsRows(screens::readingEntries, 1) == 3);
    static_assert(screens::settingsEntry(2).destination == screens::Screen::ReaderAppearance);
    static_assert(screens::settingsSection(3) == UiText::SystemSection);
    constexpr std::array entries{screens::readingEntries[0], screens::readingEntries[2], screens::readingEntries[1]};
    static_assert(screens::settingsRows(entries, 2) == 3);
    ui::Grid grid{{4, 8, 200, 180}, 2, 30, 4};
    const auto first = screens::settingsItem(grid, entries[0]);
    const auto spanning = screens::settingsItem(grid, entries[1]);
    const auto last = screens::settingsItem(grid, entries[2]);
    assert(first.x == 4 && first.y == 8 && first.w == 98);
    assert(spanning.x == 4 && spanning.y == 42 && spanning.w == 200);
    assert(last.x == 4 && last.y == 76 && last.w == 98);
}

void testLazyValuesAndNumericPresentation() {
    for (const auto presentation : {ui::SettingPresentation::Inline, ui::SettingPresentation::Card}) {
        ui::Context context;
        ui::SettingsControls form{context, {presentation, 2}};
        int calls = 0;
        const auto value = [&] {
            ++calls;
            return std::string(128, 'x');
        };
        assert(!form.setting({}, UiText::Theme, value));
        assert(calls == 0 && context.translations == 0);
        assert(!form.setting({0, 0, 100, 40}, UiText::Theme, value));
        assert(calls == 1 && context.lastValue == std::string(128, 'x'));
    }
    for (const auto number : {ui::NumberPresentation::Slider, ui::NumberPresentation::Stepper}) {
        ui::Context context;
        ui::SettingsControls form{context, {ui::SettingPresentation::Inline, 2, number}};
        Scalar value{300};
        assert(!form.number({}, UiText::Brightness, value, "%"));
        assert(context.numerics == 0 && context.translations == 0);
        assert(!form.number({0, 0, 100, 40}, UiText::Brightness, value, "%"));
        assert(context.numerics == 1);
        assert(context.sliders == (number == ui::NumberPresentation::Slider));
        assert(context.steppers == (number == ui::NumberPresentation::Stepper));
    }
}

int main() {
    testChoices(); testHiddenWork(); testNumericLimits(); testMenuGeometry();
    testLazyValuesAndNumericPresentation();
    std::cout << "Settings controls: typed choices, presentation, hidden work and bounded edits passed\n";
}
