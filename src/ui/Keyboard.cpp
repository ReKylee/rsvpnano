#include "ui/Ui.h"

#include <algorithm>
#include <array>

namespace ui {
    namespace {

        constexpr std::array<std::string_view, 10> kNumbers = {"1", "2", "3", "4", "5", "6", "7", "8", "9", "0"};
        constexpr std::array<std::string_view, 10> kTopLetters = {"q", "w", "e", "r", "t", "y", "u", "i", "o", "p"};
        constexpr std::array<std::string_view, 9> kMiddleLetters = {"a", "s", "d", "f", "g", "h", "j", "k", "l"};
        constexpr std::array<std::string_view, 7> kBottomLetters = {"z", "x", "c", "v", "b", "n", "m"};
        constexpr std::array<std::string_view, 10> kTopSymbols = {"!", "@", "#", "$", "%", "^", "&", "*", "(", ")"};
        constexpr std::array<std::string_view, 10> kMiddleSymbols = {"[", "]", "{", "}", "<", ">", "+", "=", "?", "~"};
        constexpr std::array<std::string_view, 9> kLowerSymbols = {"\\", "|", ";", ":", "'", "\"", ",", "/", "`"};
        constexpr std::array<std::string_view, 10> kBottomSymbols = {"_", "-", ".", ",", "@", ":", "/", "?", "=", "+"};

    } // namespace

    KeyboardAction Context::keyboard(Rect rect, std::string& value, size_t maxLength, KeyboardState& state,
                                     bool masked) {
        constexpr int16_t gap = 3;
        const int16_t inputHeight = std::min<int16_t>(26, std::max<int16_t>(16, rect.h / 5));
        const int16_t rowHeight = std::max<int16_t>(1, static_cast<int16_t>((rect.h - inputHeight - gap * 5) / 5));
        const int16_t firstRowY = static_cast<int16_t>(rect.y + inputHeight + gap);
        const int16_t keyWidth = std::max<int16_t>(1, static_cast<int16_t>((rect.w - gap * 9) / 10));
        const bool shifted = state.shifted;
        const bool symbols = state.symbols;

        std::array<char, 64> hidden{};
        const size_t hiddenLength = std::min(value.size(), hidden.size());
        std::fill_n(hidden.begin(), hiddenLength, '*');
        const std::string_view shownValue = masked ? std::string_view{hidden.data(), hiddenLength}
                                                  : std::string_view{value};
        label({rect.x, rect.y, rect.w, inputHeight}, shownValue.empty() ? std::string_view{"_"} : shownValue, 2,
              ui::themes::ColorRole::Accent);

        const auto append = [&](std::string_view key) {
            if (key.empty() || value.size() + key.size() > maxLength)
                return;
            if (shifted && key.size() == 1 && key.front() >= 'a' && key.front() <= 'z')
                value.push_back(static_cast<char>(key.front() - 'a' + 'A'));
            else
                value.append(key);
            if (shifted)
                state.shifted = false;
        };

        const auto eraseLast = [&] {
            if (value.empty())
                return;
            size_t start = value.size() - 1;
            while (start > 0 && (static_cast<uint8_t>(value[start]) & 0xC0U) == 0x80U)
                --start;
            value.erase(start);
        };

        const auto drawCharacters = [&]<size_t N>(const std::array<std::string_view, N>& keys, int16_t y,
                                                   uint8_t firstColumn = 0) {
            for (size_t index = 0; index < N; ++index) {
                const std::string_view key = keys[index];
                char uppercase[2] = {key.empty() ? '\0' : key.front(), '\0'};
                if (shifted && uppercase[0] >= 'a' && uppercase[0] <= 'z')
                    uppercase[0] = static_cast<char>(uppercase[0] - 'a' + 'A');
                const std::string_view shown = shifted && key.size() == 1 ? std::string_view{uppercase, 1} : key;
                const int16_t x = static_cast<int16_t>(rect.x + (firstColumn + index) * (keyWidth + gap));
                if (button({x, y, keyWidth, rowHeight}, shown))
                    append(key);
            }
        };

        if (symbols) {
            drawCharacters(kTopSymbols, firstRowY);
            drawCharacters(kMiddleSymbols, static_cast<int16_t>(firstRowY + rowHeight + gap));
            drawCharacters(kLowerSymbols, static_cast<int16_t>(firstRowY + (rowHeight + gap) * 2));
            const int16_t backX = static_cast<int16_t>(rect.x + 9 * (keyWidth + gap));
            if (button({backX, static_cast<int16_t>(firstRowY + (rowHeight + gap) * 2), keyWidth, rowHeight}, "<"))
                eraseLast();
            drawCharacters(kBottomSymbols, static_cast<int16_t>(firstRowY + (rowHeight + gap) * 3));
        } else {
            drawCharacters(kNumbers, firstRowY);
            drawCharacters(kTopLetters, static_cast<int16_t>(firstRowY + rowHeight + gap));
            drawCharacters(kMiddleLetters, static_cast<int16_t>(firstRowY + (rowHeight + gap) * 2));
            const int16_t backX = static_cast<int16_t>(rect.x + 9 * (keyWidth + gap));
            if (button({backX, static_cast<int16_t>(firstRowY + (rowHeight + gap) * 2), keyWidth, rowHeight}, "<"))
                eraseLast();

            const int16_t bottomY = static_cast<int16_t>(firstRowY + (rowHeight + gap) * 3);
            if (button({rect.x, bottomY, keyWidth, rowHeight}, shifted ? "a" : "A"))
                state.shifted = !state.shifted;
            drawCharacters(kBottomLetters, bottomY, 1);
            if (button({static_cast<int16_t>(rect.x + 8 * (keyWidth + gap)), bottomY, keyWidth, rowHeight}, "-"))
                append("-");
            if (button({static_cast<int16_t>(rect.x + 9 * (keyWidth + gap)), bottomY, keyWidth, rowHeight}, "'"))
                append("'");
        }

        const int16_t controlsY = static_cast<int16_t>(firstRowY + (rowHeight + gap) * 4);
        const int16_t controlWidth = std::max<int16_t>(1, static_cast<int16_t>((rect.w - gap * 5) / 6));
        if (button({rect.x, controlsY, controlWidth, rowHeight}, symbols ? "ABC" : "123"))
            state.symbols = !state.symbols;
        if (button({static_cast<int16_t>(rect.x + controlWidth + gap), controlsY, controlWidth, rowHeight}, "."))
            append(".");
        if (button({static_cast<int16_t>(rect.x + (controlWidth + gap) * 2), controlsY, controlWidth, rowHeight}, "_"))
            append("_");
        if (button({static_cast<int16_t>(rect.x + (controlWidth + gap) * 3), controlsY, controlWidth, rowHeight},
                   text(UiText::Space)))
            append(" ");
        const bool cancel = button(
            {static_cast<int16_t>(rect.x + (controlWidth + gap) * 4), controlsY, controlWidth, rowHeight}, "X");
        const bool submit = button(
            {static_cast<int16_t>(rect.x + (controlWidth + gap) * 5), controlsY, controlWidth, rowHeight}, "OK");
        return submit ? KeyboardAction::Submit : cancel ? KeyboardAction::Cancel : KeyboardAction::None;
    }

} // namespace ui
