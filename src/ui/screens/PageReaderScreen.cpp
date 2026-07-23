#include "ui/screens/PageReaderScreen.h"

#include <algorithm>
#include <iterator>

#include "reader/ReadingLoop.h"

namespace screens::PageReader {
    namespace {

        constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
        constexpr size_t kAnchorLeadWords = 12;
        constexpr size_t kParagraphSnapWords = 12;
        constexpr int16_t kMarginX = 14;
        constexpr int16_t kMarginY = 4;
        constexpr uint8_t kTextSize = 2;
        const int16_t kCharacterWidth = ui::Context::textWidth(" ", kTextSize);
        const int16_t kTextHeight = ui::Context::textHeight(kTextSize);
        const int16_t kParagraphIndent = kCharacterWidth * 2;
        constexpr int16_t kLineGap = 2;

        void invalidate(State& state) {
            state.pageStart = kInvalidIndex;
            state.pageEnd = 0;
            state.lineCount = 0;
            state.highlighted = kInvalidIndex;
        }

        bool paragraphStart(const ReadingSession& session, size_t index) {
            return index == 0 || std::ranges::binary_search(session.metadata.paragraphStarts, index);
        }

        size_t anchorIndex(const ReadingSession& session, size_t currentIndex) {
            const size_t anchor = currentIndex > kAnchorLeadWords ? currentIndex - kAnchorLeadWords : 0;
            const auto& starts = session.metadata.paragraphStarts;
            const auto next = std::ranges::upper_bound(starts, anchor);
            if (next == starts.begin())
                return anchor;
            const size_t paragraph = *std::prev(next);
            return anchor - paragraph <= kParagraphSnapWords ? paragraph : anchor;
        }

        void layout(State& state, const ReadingSession& session, ui::Rect area, size_t start) {
            const size_t wordCount = ReadingLoop::wordCount(session);
            size_t index = std::min(start, wordCount);
            state.pageStart = index;
            state.lineCount = 0;
            const int16_t maximumWidth = std::max<int16_t>(1, static_cast<int16_t>(area.w - kMarginX * 2));
            const int16_t lineHeight = kTextHeight + kLineGap;
            const int16_t paragraphGap = static_cast<int16_t>(std::max<int16_t>(4, lineHeight / 3));
            const int16_t bottom = static_cast<int16_t>(area.y + area.h - kMarginY);
            int16_t top = static_cast<int16_t>(area.y + kMarginY);

            while (index < wordCount && state.lineCount < state.lines.size()) {
                const bool startsParagraph = paragraphStart(session, index);
                if (state.lineCount > 0 && startsParagraph)
                    top = static_cast<int16_t>(top + paragraphGap);
                if (top + kTextHeight > bottom)
                    break;

                State::Line& line = state.lines[state.lineCount];
                line.start = index;
                line.paragraphStart = startsParagraph;
                line.y = top;
                int16_t width = startsParagraph ? kParagraphIndent : 0;
                while (index < wordCount) {
                    if (index > line.start && paragraphStart(session, index))
                        break;
                    const std::string_view word = ReadingLoop::wordAt(session, index);
                    const int16_t gap = index == line.start ? 0 : kCharacterWidth;
                    const int16_t widthWithWord =
                        static_cast<int16_t>(width + gap + ui::Context::textWidth(word, kTextSize));
                    if (index > line.start && widthWithWord > maximumWidth)
                        break;
                    width = widthWithWord;
                    ++index;
                }
                if (index == line.start)
                    ++index;
                line.end = index;
                ++state.lineCount;
                top = static_cast<int16_t>(top + lineHeight);
            }
            state.pageEnd = index;
        }

        bool wordPosition(const State& state, const ReadingSession& session, size_t index, ui::Rect area, int16_t& x,
                          int16_t& y) {
            const auto line = std::ranges::find_if(state.lines.begin(), state.lines.begin() + state.lineCount,
                                                   [index](const State::Line& candidate) {
                                                       return index >= candidate.start && index < candidate.end;
                                                   });
            if (line == state.lines.begin() + state.lineCount)
                return false;
            x = static_cast<int16_t>(area.x + kMarginX + (line->paragraphStart ? kParagraphIndent : 0));
            for (size_t word = line->start; word < index; ++word)
                x = static_cast<int16_t>(x + ui::Context::textWidth(ReadingLoop::wordAt(session, word), kTextSize)
                                         + kCharacterWidth);
            y = line->y;
            return true;
        }

        void drawWord(ui::Context& ui, const ReadingSession& session, size_t index, int16_t x, int16_t y,
                      ui::themes::ColorRole role) {
            const std::string_view word = ReadingLoop::wordAt(session, index);
            ui.drawText({x, y, ui::Context::textWidth(word, kTextSize), kTextHeight}, word, kTextSize, ui.color(role));
        }

    } // namespace

    void draw(State& state, ui::Context& ui, const ReadingSession& session, ui::Rect area, std::string_view overlay) {
        const size_t wordCount = ReadingLoop::wordCount(session);
        if (wordCount == 0 || area.w <= kMarginX * 2 || area.h <= kMarginY * 2) {
            ui.redraw(area, 0);
            return;
        }

        if (state.layoutArea != area) {
            state.layoutArea = area;
            invalidate(state);
        }
        const size_t current = std::min(session.currentIndex, wordCount - 1);
        if (state.pageStart == kInvalidIndex)
            layout(state, session, area, anchorIndex(session, current));
        if (current < state.pageStart)
            layout(state, session, area, anchorIndex(session, current));
        while (current >= state.pageEnd && state.pageEnd > state.pageStart)
            layout(state, session, area, state.pageEnd);
        if (current >= state.pageEnd)
            layout(state, session, area, current);

        uint32_t pageSignature = ui::Context::combine(2166136261U, static_cast<uint32_t>(state.pageStart));
        pageSignature = ui::Context::combine(pageSignature, static_cast<uint32_t>(state.pageEnd));
        const uint32_t signature = ui::Context::signature(overlay, pageSignature);
        if (!ui.redraw(area, signature)) {
            if (state.highlighted == current)
                return;
            int16_t x = 0;
            int16_t y = 0;
            if (wordPosition(state, session, state.highlighted, area, x, y))
                drawWord(ui, session, state.highlighted, x, y, ui::themes::ColorRole::Foreground);
            if (wordPosition(state, session, current, area, x, y))
                drawWord(ui, session, current, x, y, ui::themes::ColorRole::Accent);
            state.highlighted = current;
            ui.markDrawn();
            return;
        }

        for (size_t lineIndex = 0; lineIndex < state.lineCount; ++lineIndex) {
            const State::Line& line = state.lines[lineIndex];
            int16_t x = static_cast<int16_t>(area.x + kMarginX + (line.paragraphStart ? kParagraphIndent : 0));
            for (size_t index = line.start; index < line.end; ++index) {
                drawWord(ui, session, index, x, line.y,
                         index == current ? ui::themes::ColorRole::Accent : ui::themes::ColorRole::Foreground);
                x = static_cast<int16_t>(x + ui::Context::textWidth(ReadingLoop::wordAt(session, index), kTextSize)
                                         + kCharacterWidth);
            }
        }
        if (!overlay.empty()) {
            const int16_t width = ui::Context::textWidth(overlay, kTextSize);
            const int16_t x = static_cast<int16_t>(area.x + (area.w - width) / 2);
            const int16_t y = static_cast<int16_t>(area.y + area.h - kMarginY - kTextHeight);
            ui.gfx().fillRect(static_cast<int16_t>(x - 4), static_cast<int16_t>(y - 2), static_cast<int16_t>(width + 8),
                              static_cast<int16_t>(kTextHeight + 4), ui.color(ui::themes::ColorRole::Background));
            ui.drawText({x, y, width, kTextHeight}, overlay, kTextSize, ui.color(ui::themes::ColorRole::Accent));
        }
        state.highlighted = current;
    }

} // namespace screens::PageReader
