#include "ui/screens/PageReaderScreen.h"

#include <algorithm>
#include <iterator>
#include <numeric>

#include "reader/ReadingLoop.h"
#include "text/BidiText.h"
#include "text/UnicodeText.h"

namespace screens::PageReader {
    namespace {

        constexpr size_t kInvalidIndex = std::numeric_limits<size_t>::max();
        constexpr size_t kAnchorLeadWords = 12;
        constexpr size_t kParagraphSnapWords = 12;
        constexpr int16_t kMarginX = 14;
        constexpr int16_t kMarginY = 4;
        constexpr uint8_t kOverlayTextSize = 2;
        const int16_t kOverlayTextHeight = ui::Context::textHeight(kOverlayTextSize);
        constexpr int16_t kLineGap = 2;

        void invalidate(State& state) {
            state.pageStart = kInvalidIndex;
            state.pageEnd = 0;
            state.lineCount = 0;
            state.faces.clear();
            state.words.clear();
            state.glyphs.clear();
            state.characters.clear();
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

        void activateFace(ui::fonts::AlphaTextRenderer<640>& text, const FontCatalog::Face& face) {
            text.setFont(face.raster.get());
        }

        bool sameFace(const FontCatalog::Face& left, const FontCatalog::Face& right) {
            return &left.raster.get() == &right.raster.get()
                && left.shaper.has_value() == right.shaper.has_value()
                && (!left.shaper || &left.shaper->get() == &right.shaper->get());
        }

        uint8_t rememberFace(State& state, const FontCatalog::Face& face) {
            const auto found = std::ranges::find_if(state.faces, [&](const FontCatalog::Face& candidate) {
                return sameFace(candidate, face);
            });
            if (found != state.faces.end())
                return static_cast<uint8_t>(found - state.faces.begin());
            state.faces.push_back(face);
            return static_cast<uint8_t>(state.faces.size() - 1);
        }

        const FontCatalog::Face& faceAt(const State& state, size_t wordIndex) {
            return state.faces[state.words[wordIndex - state.pageStart].faceIndex];
        }

        State::Word& prepareWord(State& state, size_t index, uint8_t faceIndex,
                                 ui::fonts::AlphaTextRenderer<640>& text, const FontCatalog::Face& face,
                                 const settings::TypographySettings& typography,
                                 const ReadingSession& session, ReadingLoop::TextParagraph& paragraph,
                                 BidiText::Analysis& bidiAnalysis, bool bidiReady,
                                 BidiText::Line& bidiLine) {
            const size_t localIndex = index - state.pageStart;
            if (localIndex < state.words.size())
                return state.words[localIndex];

            const std::string_view word = ReadingLoop::wordAt(session, index);
            State::Word prepared{.glyphStart = static_cast<uint32_t>(state.glyphs.size()),
                                 .faceIndex = faceIndex,
                                 .cjk = UnicodeText::isCjkText(word)};
            if (face.shaper) {
                const size_t paragraphWord = index - paragraph.firstWord;
                const size_t offset = paragraph.wordOffsets[paragraphWord];
                const std::string_view locale = session.metadata.localeAt(index);
                const size_t glyphStart = state.glyphs.size();
                bool shaped = false;
                if (state.bidi && bidiReady) {
                    if (const auto direction = bidiAnalysis.uniformRightToLeft(offset, word.size())) {
                        shaped = face.shaper->get()
                                     .shape(paragraph.text, offset, word.size(), *direction, locale,
                                            face.raster.get().pixelsPerEm, text, state.glyphs)
                                     .has_value();
                    } else if (bidiAnalysis.resolve({offset, word.size()}, bidiLine)) {
                        shaped = true;
                        for (const BidiText::Run& run: bidiLine) {
                            if (!face.shaper->get().shape(paragraph.text, run.offset, run.length,
                                                          run.rightToLeft, locale,
                                                          face.raster.get().pixelsPerEm, text,
                                                          state.glyphs)) {
                                shaped = false;
                                break;
                            }
                        }
                    }
                } else {
                    shaped = face.shaper->get()
                                 .shape(paragraph.text, offset, word.size(), false, locale,
                                        face.raster.get().pixelsPerEm, text, state.glyphs)
                                 .has_value();
                }
                const size_t glyphCount = state.glyphs.size() - glyphStart;
                if (shaped && glyphCount > 0 && glyphCount <= std::numeric_limits<uint16_t>::max()) {
                    prepared.glyphCount = static_cast<uint16_t>(glyphCount);
                    const auto glyphs = std::span{state.glyphs}.subspan(glyphStart, glyphCount);
                    prepared.width = static_cast<int16_t>(std::clamp<int32_t>(
                        std::accumulate(glyphs.begin(), glyphs.end(), int32_t{0},
                                        [](int32_t width, const ui::fonts::PositionedGlyph& glyph) {
                                            return width + glyph.xAdvance;
                                        }),
                        0, INT16_MAX));
                    prepared.shaped = true;
                } else
                    state.glyphs.resize(glyphStart);
            }
            if (!prepared.shaped)
                prepared.width = text.textAdvance(word, typography.tracking);
            state.words.push_back(prepared);
            return state.words.back();
        }

        const State::Word& wordAt(const State& state, size_t index) {
            return state.words[index - state.pageStart];
        }

        int16_t lineAdvance(const State& state, const State::Line& line,
                            ui::fonts::AlphaTextRenderer<640>& text,
                            const settings::TypographySettings& typography);

        void appendBidiParagraph(State& state, const ReadingSession& session,
                                 const ReadingLoop::TextParagraph& paragraph, BidiText::Analysis& analysis,
                                 bool bidiReady, size_t firstLine, size_t lastLine,
                                 std::vector<BidiText::Codepoint>& visual, std::vector<BidiText::Line>& bidiLines) {
            if (firstLine == lastLine)
                return;
            std::array<BidiText::LineRange, State::kMaximumLines> ranges{};
            for (size_t lineIndex = firstLine; lineIndex < lastLine; ++lineIndex) {
                const State::Line& line = state.lines[lineIndex];
                const size_t localStart = line.start - paragraph.firstWord;
                const size_t localEnd = line.end - paragraph.firstWord;
                const size_t offset = paragraph.wordOffsets[localStart];
                const size_t end = paragraph.wordOffsets[localEnd - 1]
                                 + ReadingLoop::wordAt(session, line.end - 1).size();
                ranges[lineIndex - firstLine] = {offset, end - offset};
            }

            const auto lineRanges = std::span{ranges}.subspan(0, lastLine - firstLine);
            const bool resolved = bidiReady && analysis.resolve(lineRanges, bidiLines).has_value();
            BidiText::Line logical;
            for (size_t lineIndex = firstLine; lineIndex < lastLine; ++lineIndex) {
                State::Line& line = state.lines[lineIndex];
                const BidiText::LineRange range = ranges[lineIndex - firstLine];
                line.characterStart = state.characters.size();
                line.rightToLeft = resolved && analysis.rightToLeft();
                if (resolved) {
                    BidiText::visualCodepoints(paragraph.text, bidiLines[lineIndex - firstLine], visual);
                } else {
                    logical.assign(1, {range.offset, range.length, false});
                    BidiText::visualCodepoints(paragraph.text, logical, visual);
                }
                for (const BidiText::Codepoint& codepoint: visual) {
                    const auto next = std::ranges::upper_bound(paragraph.wordOffsets, codepoint.offset);
                    const size_t localWord = static_cast<size_t>(next - paragraph.wordOffsets.begin() - 1);
                    const size_t logicalWord = paragraph.firstWord + localWord;
                    const std::string_view word = ReadingLoop::wordAt(session, logicalWord);
                    const bool belongsToWord = codepoint.offset < paragraph.wordOffsets[localWord] + word.size();
                    state.characters.push_back({
                        .codepoint = codepoint.value,
                        .wordOffset = static_cast<uint16_t>((!belongsToWord && logicalWord + 1 < paragraph.lastWord
                                                              ? logicalWord + 1
                                                              : logicalWord)
                                                           - state.pageStart),
                        .belongsToWord = belongsToWord,
                        .rightToLeft = codepoint.rightToLeft,
                    });
                }
                line.characterEnd = state.characters.size();
            }
        }

        void layout(State& state, ui::fonts::AlphaTextRenderer<640>& text,
                    const Typeface& typeface,
                    const settings::TypographySettings& typography, const ReadingSession& session, ui::Rect area,
                    size_t start) {
            const size_t wordCount = ReadingLoop::wordCount(session);
            size_t index = std::min(start, wordCount);
            state.pageStart = index;
            state.lineCount = 0;
            state.bidi = (session.metadata.requiredCapabilities & UnicodeText::CapabilityBidi) != 0
                      || session.metadata.baseDirection == BookDirection::rtl;
            const int16_t maximumWidth = std::max<int16_t>(1, static_cast<int16_t>(area.w - kMarginX * 2));
            const int16_t bottom = static_cast<int16_t>(area.y + area.h - kMarginY);
            int16_t top = static_cast<int16_t>(area.y + kMarginY);
            int16_t previousLineHeight = 0;
            ReadingLoop::TextParagraph shapingParagraph;
            BidiText::Analysis shapingBidi;
            bool shapingBidiReady = !state.bidi;
            BidiText::Line shapingBidiLine;
            std::vector<BidiText::Codepoint> visual;
            std::vector<BidiText::Line> bidiLines;
            size_t bidiFirstLine = 0;
            state.characters.clear();

            while (index < wordCount && state.lineCount < state.lines.size()) {
                if (index < shapingParagraph.firstWord || index >= shapingParagraph.lastWord) {
                    if (state.bidi && shapingParagraph.lastWord > shapingParagraph.firstWord)
                        appendBidiParagraph(state, session, shapingParagraph, shapingBidi, shapingBidiReady,
                                            bidiFirstLine,
                                            state.lineCount, visual, bidiLines);
                    shapingParagraph = ReadingLoop::paragraphAt(session, index);
                    bidiFirstLine = state.lineCount;
                    if (state.bidi)
                        shapingBidiReady = shapingBidi
                                               .reset(shapingParagraph.text,
                                                      session.metadata.directionAt(shapingParagraph.firstWord))
                                               .has_value();
                }
                const bool startsParagraph = paragraphStart(session, index);
                if (state.lineCount > 0 && startsParagraph)
                    top = static_cast<int16_t>(top + std::max<int16_t>(4, previousLineHeight / 3));

                State::Line& line = state.lines[state.lineCount];
                line.start = index;
                line.paragraphStart = startsParagraph;
                int16_t width = 0;
                uint8_t ascent = 0;
                uint8_t descent = 0;
                uint8_t yAdvance = 0;
                while (index < wordCount) {
                    if (index > line.start && paragraphStart(session, index))
                        break;
                    const std::string_view word = ReadingLoop::wordAt(session, index);
                    const FontCatalog::Face face = typeface(index);
                    const uint8_t faceIndex = rememberFace(state, face);
                    const ui::fonts::AlphaFont& font = face.raster.get();
                    activateFace(text, face);
                    State::Word& prepared = prepareWord(state, index, faceIndex, text, face, typography, session,
                                                        shapingParagraph, shapingBidi, shapingBidiReady,
                                                        shapingBidiLine);
                    const int16_t spaceWidth = std::max<int16_t>(1, text.glyphAdvance(' '));
                    const bool joinsCjk = index > line.start && prepared.cjk && wordAt(state, index - 1).cjk;
                    const int16_t gap = index == line.start ? startsParagraph ? spaceWidth * 2 : 0
                                                           : joinsCjk ? 0 : spaceWidth;
                    prepared.x = static_cast<int16_t>(area.x + kMarginX + width + gap);
                    const int16_t widthWithWord = static_cast<int16_t>(width + gap + prepared.width);
                    if (index > line.start && widthWithWord > maximumWidth)
                        break;
                    width = widthWithWord;
                    ascent = std::max(ascent, font.ascent);
                    descent = std::max(descent, font.descent);
                    yAdvance = std::max(yAdvance, font.yAdvance);
                    ++index;
                }
                if (index == line.start)
                    ++index;
                const int16_t textHeight = static_cast<int16_t>(ascent + descent);
                if (top + textHeight > bottom && state.lineCount > 0) {
                    index = line.start;
                    break;
                }
                line.y = static_cast<int16_t>(top + ascent);
                line.end = index;
                for (size_t word = line.start; word < line.end; ++word)
                    state.words[word - state.pageStart].y = line.y;
                ++state.lineCount;
                previousLineHeight = std::max<int16_t>(yAdvance, textHeight) + kLineGap;
                top = static_cast<int16_t>(top + previousLineHeight);
            }
            if (state.bidi && shapingParagraph.lastWord > shapingParagraph.firstWord)
                appendBidiParagraph(state, session, shapingParagraph, shapingBidi, shapingBidiReady, bidiFirstLine,
                                    state.lineCount, visual, bidiLines);
            state.pageEnd = index;
            const size_t visibleWords = state.pageEnd - state.pageStart;
            if (state.words.size() > visibleWords) {
                const uint32_t glyphEnd = state.words[visibleWords].glyphStart;
                state.words.resize(visibleWords);
                state.glyphs.resize(glyphEnd);
            }
            if (state.bidi) {
                for (size_t lineIndex = 0; lineIndex < state.lineCount; ++lineIndex)
                    state.lines[lineIndex].width =
                        lineAdvance(state, state.lines[lineIndex], text, typography);
            }
        }

        bool logicalWordPosition(const State& state, size_t index, int16_t& x, int16_t& y) {
            if (index < state.pageStart || index >= state.pageEnd)
                return false;
            const State::Word& word = wordAt(state, index);
            x = word.x;
            y = word.y;
            return true;
        }

        void drawShapedWord(const State& state, ui::Context& ui, ui::fonts::AlphaTextRenderer<640>& text,
                            size_t index, int16_t x, int16_t baseline, ui::themes::ColorRole role) {
            text.setTextColor(ui.color(role), ui.color(ui::themes::ColorRole::Background));
            const State::Word& word = wordAt(state, index);
            const auto glyphs = std::span{state.glyphs}.subspan(word.glyphStart, word.glyphCount);
            for (const auto& glyph: glyphs) {
                text.drawGlyphIndex(glyph.glyphIndex, static_cast<int16_t>(x + glyph.xOffset),
                                    static_cast<int16_t>(baseline - glyph.yOffset));
                x = static_cast<int16_t>(x + glyph.xAdvance);
            }
        }

        void drawWord(const State& state, ui::Context& ui, ui::fonts::AlphaTextRenderer<640>& text,
                      const settings::TypographySettings& typography, const ReadingSession& session,
                      size_t index, int16_t x, int16_t baseline, ui::themes::ColorRole role) {
            if (wordAt(state, index).shaped) {
                drawShapedWord(state, ui, text, index, x, baseline, role);
                return;
            }
            text.setTextColor(ui.color(role), ui.color(ui::themes::ColorRole::Background));
            text.drawString(ReadingLoop::wordAt(session, index), x, baseline, typography.tracking);
        }

        int16_t lineAdvance(const State& state, const State::Line& line,
                            ui::fonts::AlphaTextRenderer<640>& text,
                            const settings::TypographySettings& typography) {
            int16_t advance = 0;
            size_t activeWord = kInvalidIndex;
            for (size_t index = line.characterStart; index < line.characterEnd; ++index) {
                const State::Character& character = state.characters[index];
                const size_t wordIndex = state.pageStart + character.wordOffset;
                if (wordIndex != activeWord) {
                    activateFace(text, faceAt(state, wordIndex));
                    activeWord = wordIndex;
                    if (character.belongsToWord && wordAt(state, wordIndex).shaped) {
                        advance = static_cast<int16_t>(advance + wordAt(state, wordIndex).width);
                        continue;
                    }
                }
                if (character.belongsToWord && wordAt(state, wordIndex).shaped)
                    continue;
                if (index > line.characterStart) {
                    const State::Character& previous = state.characters[index - 1];
                    if (character.belongsToWord && previous.belongsToWord
                        && character.wordOffset == previous.wordOffset && !character.rightToLeft)
                        advance = static_cast<int16_t>(advance
                                                       + text.kerningAdjust(previous.codepoint, character.codepoint));
                }
                advance = static_cast<int16_t>(advance + text.glyphAdvance(character.codepoint));
                if (index + 1 < line.characterEnd && character.belongsToWord
                    && state.characters[index + 1].belongsToWord
                    && character.wordOffset == state.characters[index + 1].wordOffset)
                    advance = static_cast<int16_t>(advance + typography.tracking);
            }
            return advance;
        }

        void drawLine(const State& state, const State::Line& line, ui::Context& ui,
                      ui::fonts::AlphaTextRenderer<640>& text,
                      const settings::TypographySettings& typography, ui::Rect area, size_t highlighted) {
            int16_t indent = 0;
            activateFace(text, faceAt(state, line.start));
            if (line.paragraphStart)
                indent = std::max<int16_t>(1, text.glyphAdvance(' ')) * 2;
            int16_t x = line.rightToLeft
                          ? static_cast<int16_t>(area.x + area.w - kMarginX - indent - line.width)
                          : static_cast<int16_t>(area.x + kMarginX + indent);
            size_t activeWord = kInvalidIndex;
            for (size_t index = line.characterStart; index < line.characterEnd; ++index) {
                const State::Character& character = state.characters[index];
                const size_t wordIndex = state.pageStart + character.wordOffset;
                if (wordIndex != activeWord) {
                    activateFace(text, faceAt(state, wordIndex));
                    activeWord = wordIndex;
                    if (character.belongsToWord && wordAt(state, wordIndex).shaped) {
                        drawShapedWord(state, ui, text, wordIndex, x, line.y,
                                       wordIndex == highlighted ? ui::themes::ColorRole::Accent
                                                                : ui::themes::ColorRole::Foreground);
                        x = static_cast<int16_t>(x + wordAt(state, wordIndex).width);
                        continue;
                    }
                }
                if (character.belongsToWord && wordAt(state, wordIndex).shaped)
                    continue;
                if (index > line.characterStart) {
                    const State::Character& previous = state.characters[index - 1];
                    if (character.belongsToWord && previous.belongsToWord
                        && character.wordOffset == previous.wordOffset && !character.rightToLeft)
                        x = static_cast<int16_t>(x + text.kerningAdjust(previous.codepoint, character.codepoint));
                }
                text.setTextColor(ui.color(character.belongsToWord && wordIndex == highlighted
                                               ? ui::themes::ColorRole::Accent
                                               : ui::themes::ColorRole::Foreground),
                                  ui.color(ui::themes::ColorRole::Background));
                x = static_cast<int16_t>(x + text.drawCodepoint(character.codepoint, x, line.y));
                if (index + 1 < line.characterEnd && character.belongsToWord
                    && state.characters[index + 1].belongsToWord
                    && character.wordOffset == state.characters[index + 1].wordOffset)
                    x = static_cast<int16_t>(x + typography.tracking);
            }
        }

    } // namespace

    void draw(State& state, ui::Context& ui, ui::fonts::AlphaTextRenderer<640>& text,
              const Typeface& typeface,
              const settings::TypographySettings& typography, uint32_t typographyRevision,
              const ReadingSession& session, ui::Rect area, std::string_view overlay) {
        const size_t wordCount = ReadingLoop::wordCount(session);
        if (wordCount == 0 || area.w <= kMarginX * 2 || area.h <= kMarginY * 2) {
            ui.redraw(area, 0);
            return;
        }

        if (state.layoutArea != area || state.layoutRevision != typographyRevision) {
            state.layoutArea = area;
            state.layoutRevision = typographyRevision;
            invalidate(state);
        }
        const size_t current = std::min(session.currentIndex, wordCount - 1);
        if (state.pageStart == kInvalidIndex)
            layout(state, text, typeface, typography, session, area, anchorIndex(session, current));
        if (current < state.pageStart)
            layout(state, text, typeface, typography, session, area, anchorIndex(session, current));
        while (current >= state.pageEnd && state.pageEnd > state.pageStart)
            layout(state, text, typeface, typography, session, area, state.pageEnd);
        if (current >= state.pageEnd)
            layout(state, text, typeface, typography, session, area, current);

        uint32_t pageSignature = ui::Context::combine(2166136261U, static_cast<uint32_t>(state.pageStart));
        pageSignature = ui::Context::combine(pageSignature, static_cast<uint32_t>(state.pageEnd));
        pageSignature = ui::Context::combine(pageSignature, typographyRevision);
        const uint32_t signature = ui::Context::signature(overlay, pageSignature);
        if (!ui.redraw(area, signature)) {
            if (state.highlighted == current)
                return;
            if (!state.bidi) {
                int16_t x = 0;
                int16_t y = 0;
                if (logicalWordPosition(state, state.highlighted, x, y)) {
                    activateFace(text, faceAt(state, state.highlighted));
                    drawWord(state, ui, text, typography, session, state.highlighted, x, y,
                             ui::themes::ColorRole::Foreground);
                }
                if (logicalWordPosition(state, current, x, y)) {
                    activateFace(text, faceAt(state, current));
                    drawWord(state, ui, text, typography, session, current, x, y,
                             ui::themes::ColorRole::Accent);
                }
                state.highlighted = current;
                ui.markDrawn();
                return;
            }
            const auto previousLine = std::ranges::find_if(state.lines.begin(), state.lines.begin() + state.lineCount,
                                                           [&](const State::Line& line) {
                                                               return state.highlighted >= line.start
                                                                   && state.highlighted < line.end;
                                                           });
            const auto currentLine = std::ranges::find_if(state.lines.begin(), state.lines.begin() + state.lineCount,
                                                          [&](const State::Line& line) {
                                                              return current >= line.start && current < line.end;
                                                          });
            if (previousLine != state.lines.begin() + state.lineCount)
                drawLine(state, *previousLine, ui, text, typography, area, current);
            if (currentLine != state.lines.begin() + state.lineCount && currentLine != previousLine)
                drawLine(state, *currentLine, ui, text, typography, area, current);
            state.highlighted = current;
            ui.markDrawn();
            return;
        }

        if (state.bidi) {
            for (size_t lineIndex = 0; lineIndex < state.lineCount; ++lineIndex)
                drawLine(state, state.lines[lineIndex], ui, text, typography, area, current);
        } else {
            for (size_t lineIndex = 0; lineIndex < state.lineCount; ++lineIndex) {
                const State::Line& line = state.lines[lineIndex];
                for (size_t index = line.start; index < line.end; ++index) {
                    activateFace(text, faceAt(state, index));
                    const State::Word& word = wordAt(state, index);
                    drawWord(state, ui, text, typography, session, index, word.x, word.y,
                             index == current ? ui::themes::ColorRole::Accent
                                              : ui::themes::ColorRole::Foreground);
                }
            }
        }
        if (!overlay.empty()) {
            const int16_t width = ui::Context::textWidth(overlay, kOverlayTextSize);
            const int16_t x = static_cast<int16_t>(area.x + (area.w - width) / 2);
            const int16_t y = static_cast<int16_t>(area.y + area.h - kMarginY - kOverlayTextHeight);
            ui.gfx().fillRect(static_cast<int16_t>(x - 4), static_cast<int16_t>(y - 2), static_cast<int16_t>(width + 8),
                              static_cast<int16_t>(kOverlayTextHeight + 4), ui.color(ui::themes::ColorRole::Background));
            ui.drawText({x, y, width, kOverlayTextHeight}, overlay, kOverlayTextSize,
                        ui.color(ui::themes::ColorRole::Accent));
        }
        state.highlighted = current;
    }

} // namespace screens::PageReader
