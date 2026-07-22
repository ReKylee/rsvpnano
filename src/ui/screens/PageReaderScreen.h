#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string_view>

#include "reader/ReadingSession.h"
#include "ui/Ui.h"

namespace screens::PageReader {

    struct State {
        struct Line {
            size_t start = 0;
            size_t end = 0;
            int16_t y = 0;
            bool paragraphStart = false;
        };

        static constexpr size_t kMaximumLines = 24;

        std::array<Line, kMaximumLines> lines;
        size_t lineCount = 0;
        size_t pageStart = std::numeric_limits<size_t>::max();
        size_t pageEnd = 0;
        size_t highlighted = std::numeric_limits<size_t>::max();
        ui::Rect layoutArea;
    };

    void draw(State& state, ui::Context& ui, const ReadingSession& session, ui::Rect area,
              std::string_view overlay = {});

} // namespace screens::PageReader
