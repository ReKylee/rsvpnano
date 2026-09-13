#include "Recording.h"
#include "ui/Inputs.h"

#include <array>
#include <cassert>
#include <iostream>
#include <memory>
#include <ranges>
#include <string>
#include <string_view>

namespace {
    constexpr ui::Rect area{0, 0, 280, 72};

    void generatedScalars(ui::Context& ui) {
        auto values = std::views::iota(1) | std::views::take(3);
        static_assert(std::ranges::forward_range<decltype(values)>);
        static_assert(!std::ranges::common_range<decltype(values)>);
        testui::recording = {};
        testui::recording.activate = "Number";
        int selected = 1;
        for (const int expected : {2, 3, 1}) {
            assert(ui::select(ui, area, "Number", selected, values,
                              [](int n) { return std::to_string(n); }));
            assert(selected == expected);
        }
        selected = 99;
        assert(ui::select(ui, area, "Number", selected, values,
                          [](int) { return "Generated"; }));
        assert(selected == 1);
    }

    struct Record {
        std::string key;
        std::string label;
    };

    auto records() {
        return std::views::iota(0, 3) | std::views::transform([](int index) {
            return Record{std::string(128, 'k') + std::to_string(index),
                          std::string(128, 'l') + std::to_string(index)};
        });
    }

    void generatedRecords(ui::Context& ui) {
        for (const auto layout : {ui::ValueLayout::Inline, ui::ValueLayout::Stacked, ui::ValueLayout::Card}) {
            const ui::ValueStyle style{layout, 2};
            for (const bool viewKey : {false, true}) {
                testui::recording = {};
                testui::recording.activate = "Record";
                std::string selected = std::string(128, 'k') + "0";
                auto items = records();
                for (const int expected : {1, 2, 0}) {
                    const auto label = [](const Record& item) { return std::string_view{item.label}; };
                    const bool changed = viewKey
                        ? ui::select(ui, area, "Record", selected, items, label, style,
                                     [](const Record& item) { return std::string_view{item.key}; })
                        : ui::select(ui, area, "Record", selected, items, label, style, &Record::key);
                    assert(changed && selected == std::string(128, 'k') + std::to_string(expected));
                    assert(testui::recording.calls.back().value
                           == std::string(128, 'l') + std::to_string((expected + 2) % 3));
                }
            }
        }
    }

    void borrowedNoncopyableRecords(ui::Context& ui) {
        struct Item {
            std::unique_ptr<int> key;
            const char* label;
        };
        const std::array items{Item{std::make_unique<int>(1), "First"},
                               Item{std::make_unique<int>(2), "Second"}};
        testui::recording = {};
        testui::recording.activate = "Item";
        int selected = 1;
        assert(ui::select(ui, area, "Item", selected, items, &Item::label, {},
                          [](const Item& item) -> const int& { return *item.key; }));
        assert(selected == 2 && *items[0].key == 1 && *items[1].key == 2);
    }

    void hiddenAndIdleGeneration(ui::Context& ui) {
        int generated = 0;
        auto items = std::views::iota(0, 3) | std::views::transform([&](int value) {
            ++generated;
            return Record{std::to_string(value), "Label"};
        });
        testui::recording = {};
        std::string selected = "0";
        assert(!ui::select(ui, {}, "Record", selected, items, &Record::label, {}, &Record::key));
        assert(generated == 0 && testui::recording.calls.empty());
        assert(!ui::select(ui, area, "Record", selected, items, &Record::label, {}, &Record::key));
        assert(selected == "0" && generated == 2);
    }
}

int main() {
    Arduino_GFX gfx;
    ui::Context ui{gfx};
    generatedScalars(ui);
    generatedRecords(ui);
    borrowedNoncopyableRecords(ui);
    hiddenAndIdleGeneration(ui);
    std::cout << "Input lifetimes: generated scalars/records, projected references/views and borrowed items passed\n";
}
