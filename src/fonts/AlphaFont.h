#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>
#include <FS.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string_view>

#include "fonts/UiFont6x9.h"
#include "fonts/RFont4Format.h"
#include "text/Utf8Text.h"

namespace ui::fonts {

    using AlphaGlyph = RFont4::GlyphRecord;
    using AlphaKerningPair = RFont4::KerningRecord;
    using AlphaGlyphId = RFont4::GlyphIdRecord;

    struct PositionedGlyph {
        uint32_t glyphIndex = 0;
        uint32_t cluster = 0;
        int16_t xAdvance = 0;
        int16_t xOffset = 0;
        int16_t yOffset = 0;
    };
    static_assert(sizeof(PositionedGlyph) == 16);

    struct AlphaFont {
        std::string_view name;
        const uint8_t* bitmap = nullptr;
        const AlphaGlyph* glyphs = nullptr;
        uint32_t glyphCount = 0;
        uint8_t yAdvance = 0;
        uint8_t ascent = 0;
        uint8_t descent = 0;
        const uint8_t* pageMap = nullptr;
        const uint16_t* const* pageTables = nullptr;
        uint32_t pageTableCount = 0;
        const AlphaKerningPair* kerningPairs = nullptr;
        uint32_t kerningPairCount = 0;
        int8_t wordInkTop = 0;
        int8_t wordInkBottom = -1;
        const AlphaGlyphId* glyphIds = nullptr;
        uint32_t glyphIdCount = 0;
        uint32_t scriptMask = 0;
        uint8_t pixelsPerEm = 0;
        std::optional<std::reference_wrapper<File>> file;
        RFont4::StrikeRecord fileStrike;
    };

    struct AlphaByteInfo {
        uint8_t left = 0;
        uint8_t right = 0;
        bool hasInk = false;
        bool isSolid = false;
    };

    constexpr std::array<AlphaByteInfo, 256> makeAlphaByteInfoTable() {
        std::array<AlphaByteInfo, 256> table{};
        for (std::size_t i = 0; i < table.size(); ++i) {
            const auto packed = static_cast<uint8_t>(i);
            table[i] = AlphaByteInfo{
                static_cast<uint8_t>(packed >> 4U),
                static_cast<uint8_t>(packed & 0x0FU),
                packed != 0x00,
                packed == 0xFF,
            };
        }
        return table;
    }

    inline constexpr auto kAlphaByteInfo = makeAlphaByteInfoTable();

    template<int16_t MaxRowWidth, uint8_t MaxStripRows = 8>
    class AlphaTextRenderer {
        static_assert(MaxRowWidth > 0);
        static_assert(MaxStripRows > 0);

    public:
        explicit AlphaTextRenderer(Arduino_GFX& output) : output_(output) {}

        AlphaTextRenderer(const AlphaTextRenderer&) = delete;
        AlphaTextRenderer& operator=(const AlphaTextRenderer&) = delete;

        bool begin() {
            ready_ = true;
            return ready_;
        }

        void setFont(const AlphaFont& font) {
            if (font_ == &font)
                return;
            font_ = &font;
            for (auto& entry: fileGlyphCache_)
                entry.index = UINT32_MAX;
            fileKerningKeys_.fill(UINT64_MAX);
        }

        void setTextColor(uint16_t fg, uint16_t bg) {
            if (fg_ == fg && bg_ == bg && blendTableValid_) {
                return;
            }

            fg_ = fg;
            bg_ = bg;
            rebuildBlendTable();
        }

        int16_t drawString(std::string_view text, int16_t x, int16_t baseline, int8_t tracking = 0) {
            if (!ready_ || font_ == nullptr)
                return -1;
            Bounds bounds;
            if (!measure(text, x, baseline, bounds, tracking))
                return drawMixedString(text, x, baseline, tracking);

            if (bounds.w == 0 || bounds.h == 0) {
                return bounds.advance;
            }

            if (!drawGlyphsToStrips(text, x, baseline, bounds, tracking)) {
                drawGlyphs(text, x, baseline, tracking);
            }
            return bounds.advance;
        }

        int16_t drawCodepoint(uint32_t codepoint, int16_t x, int16_t baseline) {
            if (codepoint == '\n' || codepoint == '\r')
                return 0;
            const AlphaGlyph* glyph = findGlyph(codepoint);
            if (glyph == nullptr)
                return drawU8g2(codepoint, u8g2Style(), x, baseline);
            return drawAlpha(*glyph, x, baseline);
        }

        int16_t glyphAdvance(uint32_t codepoint) const {
            if (codepoint == '\n' || codepoint == '\r')
                return 0;
            const AlphaGlyph* glyph = findGlyph(codepoint);
            if (glyph != nullptr)
                return readGlyph(*glyph).xAdvance;
            const U8g2Style fallback = u8g2Style();
            return static_cast<int16_t>(fallback.cellWidth * fallback.scale);
        }

        int16_t drawGlyphIndex(uint32_t glyphIndex, int16_t x, int16_t baseline) {
            const AlphaGlyph* glyph = glyphAt(glyphIndex);
            if (glyph == nullptr)
                return 0;
            const AlphaGlyph metrics = readGlyph(*glyph);
            if (metrics.width > 0 && metrics.height > 0)
                drawGlyph(metrics, static_cast<int16_t>(x + metrics.xOffset),
                          static_cast<int16_t>(baseline + metrics.yOffset));
            return metrics.xAdvance;
        }

        int16_t glyphIdAdvance(uint32_t glyphId) const {
            const AlphaGlyph* glyph = findGlyphId(glyphId);
            return glyph == nullptr ? 0 : readGlyph(*glyph).xAdvance;
        }

        int16_t glyphIndexAdvance(uint32_t glyphIndex) const {
            const AlphaGlyph* glyph = glyphAt(glyphIndex);
            return glyph == nullptr ? 0 : readGlyph(*glyph).xAdvance;
        }

        bool resolveGlyphId(uint32_t glyphId, uint32_t& glyphIndex) const {
            if (font_ == nullptr || font_->glyphIdCount == 0
                || (!font_->file && (font_->glyphs == nullptr || font_->glyphIds == nullptr)))
                return false;
            if (font_->file) {
                for (const FileGlyphCacheEntry& cached: fileGlyphCache_) {
                    if (cached.index != UINT32_MAX && cached.glyph.glyphId == glyphId) {
                        glyphIndex = cached.index;
                        return true;
                    }
                }
                uint32_t first = 0;
                uint32_t last = font_->glyphIdCount;
                RFont4::GlyphIdRecord record;
                while (first < last) {
                    const uint32_t middle = first + (last - first) / 2;
                    if (!readFile(font_->fileStrike.glyphIdsOffset + middle * sizeof(record), &record,
                                  sizeof(record)))
                        return false;
                    if (record.glyphId < glyphId)
                        first = middle + 1;
                    else
                        last = middle;
                }
                if (first >= font_->glyphIdCount
                    || !readFile(font_->fileStrike.glyphIdsOffset + first * sizeof(record), &record,
                                 sizeof(record))
                    || record.glyphId != glyphId || record.glyphIndex >= font_->glyphCount)
                    return false;
                glyphIndex = record.glyphIndex;
                return fileGlyph(glyphIndex) != nullptr;
            }
            const std::span ids{font_->glyphIds, static_cast<size_t>(font_->glyphIdCount)};
            const auto record = std::ranges::lower_bound(ids, glyphId, {}, [](const AlphaGlyphId& candidate) {
                return readPacked(candidate).glyphId;
            });
            if (record == ids.end())
                return false;
            const AlphaGlyphId resolved = readPacked(*record);
            if (resolved.glyphId != glyphId)
                return false;
            glyphIndex = resolved.glyphIndex;
            return glyphIndex < font_->glyphCount;
        }

        uint8_t pixelsPerEm() const {
            return font_ == nullptr ? 0 : font_->pixelsPerEm;
        }

        bool nominalGlyph(uint32_t codepoint, uint32_t& glyphId) const {
            const AlphaGlyph* glyph = findGlyph(codepoint);
            if (glyph == nullptr)
                return false;
            glyphId = readGlyph(*glyph).glyphId;
            return glyphId != 0;
        }

        bool hasGlyph(uint32_t codepoint) const {
            return findGlyph(codepoint) != nullptr;
        }

        int16_t kerningAdjust(uint32_t leftCodepoint, uint32_t rightCodepoint) const {
            if (font_ == nullptr || font_->kerningPairCount == 0
                || (!font_->file && font_->kerningPairs == nullptr)) {
                return 0;
            }

            const AlphaGlyph* leftGlyph = findGlyph(leftCodepoint);
            if (leftGlyph == nullptr) {
                return 0;
            }

            const AlphaGlyph left = readGlyph(*leftGlyph);
            if (left.kernCount == 0) {
                return 0;
            }

            if (font_->file)
                return fileKerningAdjust(left, rightCodepoint);

            const std::span pairs{font_->kerningPairs + left.kernOffset, static_cast<size_t>(left.kernCount)};
            const auto pair =
                std::ranges::lower_bound(pairs, rightCodepoint, {}, [](const AlphaKerningPair& candidate) {
                    return readPacked(candidate).rightCodepoint;
                });
            if (pair == pairs.end())
                return 0;
            const AlphaKerningPair resolved = readPacked(*pair);
            return resolved.rightCodepoint == rightCodepoint ? resolved.xAdjust : 0;
        }

        int16_t textAdvance(std::string_view text, int8_t tracking = 0) const {
            if (font_ == nullptr)
                return 0;
            return measureAdvance(text, tracking);
        }

    private:
        static constexpr uint16_t kMissingGlyphIndex = 0xFFFFU;
        static constexpr uint8_t kMissingPageIndex = 0xFFU;

        struct Bounds {
            int16_t x1 = 0;
            int16_t y1 = 0;
            uint16_t w = 0;
            uint16_t h = 0;
            int16_t advance = 0;
        };

        struct FileGlyphCacheEntry {
            uint32_t index = UINT32_MAX;
            AlphaGlyph glyph;
        };

        struct U8g2Style {
            const uint8_t* font = nullptr;
            uint8_t scale = 1;
            uint8_t cellWidth = 6;
        };

        U8g2Style u8g2Style() const {
            constexpr uint8_t builtInCellWidth = 6;
            constexpr uint8_t builtInHeight = 9;
            const uint8_t targetHeight = font_ != nullptr ? font_->yAdvance : builtInHeight;
            return {u8g2_font_rsvpnano_ui_6x9_tf,
                    static_cast<uint8_t>(std::max<int>(1, targetHeight / builtInHeight)), builtInCellWidth};
        }

        int16_t drawU8g2(std::string_view text, size_t codepoints, const U8g2Style& style,
                         int16_t x, int16_t baseline) {
            output_.setFont(style.font);
            output_.setUTF8Print(true);
            output_.setTextSize(style.scale);
            output_.setTextWrap(false);
            output_.setTextColor(fg_, bg_);
            output_.setCursor(x, baseline);
            for (const unsigned char byte: text)
                output_.write(byte);
            return static_cast<int16_t>(codepoints * style.cellWidth * style.scale);
        }

        int16_t drawU8g2(uint32_t codepoint, const U8g2Style& style, int16_t x, int16_t baseline) {
            std::array<char, 4> encoded{};
            const size_t size = Utf8Text::encode(codepoint, encoded);
            return drawU8g2({encoded.data(), size}, 1, style, x, baseline);
        }

        int16_t drawAlpha(const AlphaGlyph& glyph, int16_t x, int16_t baseline) {
            const AlphaGlyph metrics = readGlyph(glyph);
            if (metrics.width > 0 && metrics.height > 0)
                drawGlyph(metrics, static_cast<int16_t>(x + metrics.xOffset),
                          static_cast<int16_t>(baseline + metrics.yOffset));
            return metrics.xAdvance;
        }

        int16_t measureAdvance(std::string_view text, int8_t tracking) const {
            int16_t advance = 0;
            uint32_t previous = 0;
            bool previousWasAlpha = false;
            const U8g2Style fallback = u8g2Style();
            while (!text.empty()) {
                uint32_t codepoint = 0;
                Utf8Text::next(text, codepoint);
                const AlphaGlyph* glyph = findGlyph(codepoint);
                const bool currentIsAlpha = glyph != nullptr;
                if (currentIsAlpha && previousWasAlpha)
                    advance = static_cast<int16_t>(advance + kerningAdjust(previous, codepoint));
                int16_t glyphWidth = 0;
                if (glyph != nullptr)
                    glyphWidth = readGlyph(*glyph).xAdvance;
                else if (codepoint != '\n' && codepoint != '\r')
                    glyphWidth = static_cast<int16_t>(fallback.cellWidth * fallback.scale);
                advance = static_cast<int16_t>(advance + glyphWidth + (text.empty() ? 0 : tracking));
                previous = codepoint;
                previousWasAlpha = currentIsAlpha;
            }
            return advance;
        }

        int16_t drawMixedString(std::string_view text, int16_t x, int16_t baseline, int8_t tracking) {
            const int16_t start = x;
            uint32_t previous = 0;
            bool previousWasAlpha = false;
            const U8g2Style fallback = u8g2Style();
            while (!text.empty()) {
                const char* runStart = text.data();
                uint32_t codepoint = 0;
                Utf8Text::next(text, codepoint);
                const AlphaGlyph* glyph = findGlyph(codepoint);
                const bool currentIsAlpha = glyph != nullptr;
                if (currentIsAlpha && previousWasAlpha)
                    x = static_cast<int16_t>(x + kerningAdjust(previous, codepoint));
                if (glyph != nullptr) {
                    x = static_cast<int16_t>(x + drawAlpha(*glyph, x, baseline));
                } else if (codepoint != '\n' && codepoint != '\r') {
                    size_t codepoints = 1;
                    if (tracking == 0) {
                        while (!text.empty()) {
                            std::string_view next = text;
                            uint32_t nextCodepoint = 0;
                            Utf8Text::next(next, nextCodepoint);
                            if (nextCodepoint == '\n' || nextCodepoint == '\r' || findGlyph(nextCodepoint) != nullptr)
                                break;
                            text = next;
                            ++codepoints;
                        }
                    }
                    x = static_cast<int16_t>(x + drawU8g2(
                        {runStart, static_cast<size_t>(text.data() - runStart)}, codepoints, fallback, x, baseline));
                }
                if (!text.empty())
                    x = static_cast<int16_t>(x + tracking);
                previous = codepoint;
                previousWasAlpha = currentIsAlpha;
            }
            return static_cast<int16_t>(x - start);
        }

        bool measure(std::string_view text, int16_t x, int16_t baseline, Bounds& bounds, int8_t tracking = 0) const {
            if (!ready_ || font_ == nullptr) {
                bounds = {};
                return false;
            }

            int16_t cursorX = x;
            int16_t minX = INT16_MAX;
            int16_t minY = INT16_MAX;
            int16_t maxX = INT16_MIN;
            int16_t maxY = INT16_MIN;

            std::string_view cursor = text;
            uint32_t previousCodepoint = 0;
            bool hasPrevious = false;
            uint32_t codepoint = 0;
            while (Utf8Text::next(cursor, codepoint)) {
                if (hasPrevious) {
                    cursorX = static_cast<int16_t>(cursorX + kerningAdjust(previousCodepoint, codepoint));
                }

                if (codepoint == '\n' || codepoint == '\r') {
                    if (!cursor.empty())
                        cursorX = static_cast<int16_t>(cursorX + tracking);
                    previousCodepoint = codepoint;
                    hasPrevious = true;
                    continue;
                }
                const AlphaGlyph* glyph = findGlyph(codepoint);
                if (glyph == nullptr)
                    return false;

                const AlphaGlyph metrics = readGlyph(*glyph);
                if (metrics.width > 0 && metrics.height > 0) {
                    const int16_t x1 = static_cast<int16_t>(cursorX + metrics.xOffset);
                    const int16_t y1 = static_cast<int16_t>(baseline + metrics.yOffset);
                    const int16_t x2 = static_cast<int16_t>(x1 + metrics.width - 1);
                    const int16_t y2 = static_cast<int16_t>(y1 + metrics.height - 1);

                    minX = std::min(minX, x1);
                    minY = std::min(minY, y1);
                    maxX = std::max(maxX, x2);
                    maxY = std::max(maxY, y2);
                }

                cursorX = static_cast<int16_t>(cursorX + metrics.xAdvance);
                if (!cursor.empty())
                    cursorX = static_cast<int16_t>(cursorX + tracking);
                previousCodepoint = codepoint;
                hasPrevious = true;
            }

            bounds.x1 = x;
            bounds.y1 = baseline;
            bounds.w = 0;
            bounds.h = 0;
            bounds.advance = static_cast<int16_t>(cursorX - x);

            if (maxX >= minX) {
                bounds.x1 = minX;
                bounds.w = static_cast<uint16_t>(maxX - minX + 1);
            }
            if (maxY >= minY) {
                bounds.y1 = minY;
                bounds.h = static_cast<uint16_t>(maxY - minY + 1);
            }

            return true;
        }

        void drawGlyphs(std::string_view text, int16_t x, int16_t baseline, int8_t tracking) {
            if (font_ == nullptr) {
                return;
            }

            int16_t cursorX = x;
            std::string_view cursor = text;
            uint32_t previousCodepoint = 0;
            bool hasPrevious = false;
            uint32_t codepoint = 0;
            while (Utf8Text::next(cursor, codepoint)) {
                if (hasPrevious) {
                    cursorX = static_cast<int16_t>(cursorX + kerningAdjust(previousCodepoint, codepoint));
                }

                const AlphaGlyph* glyph = findGlyph(codepoint);
                if (glyph == nullptr) {
                    if (!cursor.empty())
                        cursorX = static_cast<int16_t>(cursorX + tracking);
                    previousCodepoint = codepoint;
                    hasPrevious = true;
                    continue;
                }

                const AlphaGlyph metrics = readGlyph(*glyph);
                if (metrics.width > 0 && metrics.height > 0) {
                    drawGlyph(metrics, static_cast<int16_t>(cursorX + metrics.xOffset),
                              static_cast<int16_t>(baseline + metrics.yOffset));
                }

                cursorX = static_cast<int16_t>(cursorX + metrics.xAdvance);
                if (!cursor.empty())
                    cursorX = static_cast<int16_t>(cursorX + tracking);
                previousCodepoint = codepoint;
                hasPrevious = true;
            }
        }

        bool drawGlyphsToStrips(std::string_view text, int16_t x, int16_t baseline, const Bounds& bounds,
                                int8_t tracking) {
            if (font_ == nullptr || bounds.w == 0 || bounds.h == 0) {
                return false;
            }

            const int16_t displayW = output_.width();
            const int16_t displayH = output_.height();
            const int16_t boundsX2 = static_cast<int16_t>(bounds.x1 + bounds.w);
            const int16_t boundsY2 = static_cast<int16_t>(bounds.y1 + bounds.h);
            const int16_t visibleX0 = std::max<int16_t>(bounds.x1, 0);
            const int16_t visibleX1 = std::min<int16_t>(boundsX2, displayW);
            const int16_t visibleY0 = std::max<int16_t>(bounds.y1, 0);
            const int16_t visibleY1 = std::min<int16_t>(boundsY2, displayH);

            if (visibleX1 <= visibleX0 || visibleY1 <= visibleY0) {
                return true;
            }

            const uint16_t stripWidth = static_cast<uint16_t>(visibleX1 - visibleX0);
            if (stripWidth > MaxRowWidth) {
                return false;
            }

            for (int16_t stripY = visibleY0; stripY < visibleY1;) {
                const uint8_t stripRows =
                    static_cast<uint8_t>(std::min<int16_t>(MaxStripRows, static_cast<int16_t>(visibleY1 - stripY)));

                clearStrip(stripWidth, stripRows);
                compositeGlyphsIntoStrip(text, x, baseline, visibleX0, stripY, stripWidth, stripRows, tracking);
                flushStrip(visibleX0, stripY, stripWidth, stripRows);

                stripY = static_cast<int16_t>(stripY + stripRows);
            }

            return true;
        }

        void clearStrip(uint16_t width, uint8_t rows) {
            for (uint8_t row = 0; row < rows; ++row) {
                std::ranges::fill_n(strip_[row], width, bg_);
                std::ranges::fill_n(stripInk_[row], width, static_cast<uint8_t>(0));
            }
        }

        void compositeGlyphsIntoStrip(std::string_view text, int16_t x, int16_t baseline, int16_t stripX,
                                      int16_t stripY, uint16_t stripWidth, uint8_t stripRows, int8_t tracking) {
            int16_t cursorX = x;
            std::string_view cursor = text;
            uint32_t previousCodepoint = 0;
            bool hasPrevious = false;
            uint32_t codepoint = 0;

            while (Utf8Text::next(cursor, codepoint)) {
                if (hasPrevious) {
                    cursorX = static_cast<int16_t>(cursorX + kerningAdjust(previousCodepoint, codepoint));
                }

                const AlphaGlyph* glyph = findGlyph(codepoint);
                if (glyph != nullptr) {
                    const AlphaGlyph metrics = readGlyph(*glyph);
                    if (metrics.width > 0 && metrics.height > 0) {
                        compositeGlyphIntoStrip(metrics, static_cast<int16_t>(cursorX + metrics.xOffset),
                                                static_cast<int16_t>(baseline + metrics.yOffset), stripX, stripY,
                                                stripWidth, stripRows);
                    }
                    cursorX = static_cast<int16_t>(cursorX + metrics.xAdvance);
                }
                if (!cursor.empty())
                    cursorX = static_cast<int16_t>(cursorX + tracking);

                previousCodepoint = codepoint;
                hasPrevious = true;
            }
        }

        void compositeGlyphIntoStrip(const AlphaGlyph& glyph, int16_t glyphX, int16_t glyphY, int16_t stripX,
                                     int16_t stripY, uint16_t stripWidth, uint8_t stripRows) {
            if (glyph.width == 0 || glyph.height == 0 || (!font_->file && font_->bitmap == nullptr)) {
                return;
            }

            const int16_t glyphX2 = static_cast<int16_t>(glyphX + glyph.width);
            const int16_t glyphY2 = static_cast<int16_t>(glyphY + glyph.height);
            const int16_t stripX2 = static_cast<int16_t>(stripX + stripWidth);
            const int16_t stripY2 = static_cast<int16_t>(stripY + stripRows);

            const int16_t overlapX0 = std::max<int16_t>(glyphX, stripX);
            const int16_t overlapX1 = std::min<int16_t>(glyphX2, stripX2);
            const int16_t overlapY0 = std::max<int16_t>(glyphY, stripY);
            const int16_t overlapY1 = std::min<int16_t>(glyphY2, stripY2);

            if (overlapX1 <= overlapX0 || overlapY1 <= overlapY0) {
                return;
            }

            const int16_t overlapSrcX0 = static_cast<int16_t>(overlapX0 - glyphX);
            const int16_t overlapSrcX1 = static_cast<int16_t>(overlapX1 - glyphX);

            for (int16_t dstY = overlapY0; dstY < overlapY1; ++dstY) {
                const uint8_t srcY = static_cast<uint8_t>(dstY - glyphY);
                const uint8_t stripRow = static_cast<uint8_t>(dstY - stripY);
                const uint8_t* packedRow = nullptr;
                if (!prepareRow(glyph, srcY, packedRow)) {
                    continue;
                }

                forEachVisibleSpan(packedRow, overlapSrcX0, overlapSrcX1,
                                   [&](int16_t srcX0, int16_t srcX1) {
                    const uint16_t dstCol = static_cast<uint16_t>(glyphX + srcX0 - stripX);
                    const uint16_t count = static_cast<uint16_t>(srcX1 - srcX0);
                    compositePackedRowSpanIntoStrip(packedRow, srcX0, stripRow, dstCol, count);
                });
            }
        }

        void compositePackedRowSpanIntoStrip(const uint8_t* packedRow, int16_t srcX, uint8_t stripRow, uint16_t dstCol,
                                             uint16_t count) {
            uint16_t out = dstCol;
            uint16_t remaining = count;

            if ((srcX & 1) != 0 && remaining > 0) {
                const uint8_t coverage = coverageAt(packedRow, static_cast<uint8_t>(srcX));
                strip_[stripRow][out] = blend_[coverage];
                stripInk_[stripRow][out] = 1;
                ++srcX;
                ++out;
                --remaining;
            }

            while (remaining >= 2) {
                const uint8_t packed = pgm_read_byte(packedRow + (srcX >> 1));
                const AlphaByteInfo info = kAlphaByteInfo[packed];
                if (info.isSolid) {
                    strip_[stripRow][out] = fg_;
                    strip_[stripRow][out + 1] = fg_;
                    stripInk_[stripRow][out] = 1;
                    stripInk_[stripRow][out + 1] = 1;
                } else if (info.hasInk) {
                    if (info.left != 0) {
                        strip_[stripRow][out] = blendPair_[packed][0];
                        stripInk_[stripRow][out] = 1;
                    }
                    if (info.right != 0) {
                        strip_[stripRow][out + 1] = blendPair_[packed][1];
                        stripInk_[stripRow][out + 1] = 1;
                    }
                }

                srcX = static_cast<int16_t>(srcX + 2);
                out = static_cast<uint16_t>(out + 2);
                remaining = static_cast<uint16_t>(remaining - 2);
            }

            if (remaining > 0) {
                const uint8_t coverage = coverageAt(packedRow, static_cast<uint8_t>(srcX));
                strip_[stripRow][out] = blend_[coverage];
                stripInk_[stripRow][out] = 1;
            }
        }

        void flushStrip(int16_t stripX, int16_t stripY, uint16_t stripWidth, uint8_t stripRows) {
            for (uint8_t row = 0; row < stripRows; ++row) {
                uint16_t x = 0;
                while (x < stripWidth) {
                    while (x < stripWidth && stripInk_[row][x] == 0) {
                        ++x;
                    }
                    if (x >= stripWidth) {
                        break;
                    }

                    const uint16_t spanStart = x;
                    while (x < stripWidth && stripInk_[row][x] != 0) {
                        ++x;
                    }

                    const int16_t spanWidth = static_cast<int16_t>(x - spanStart);
                    if (spanWidth > 0) {
                        // Pass mutable RAM pointer intentionally: Arduino_GFX selects the
                        // bulk writePixels() path.
                        output_.draw16bitRGBBitmap(static_cast<int16_t>(stripX + spanStart),
                                                   static_cast<int16_t>(stripY + row), strip_[row] + spanStart,
                                                   spanWidth, 1);
                    }
                }
            }
        }

        void drawGlyph(const AlphaGlyph& glyph, int16_t x, int16_t y) {
            if (glyph.width == 0 || glyph.height == 0 || glyph.width > MaxRowWidth
                || (!font_->file && font_->bitmap == nullptr)) {
                return;
            }

            const int16_t displayW = output_.width();
            const int16_t displayH = output_.height();
            for (uint8_t row = 0; row < glyph.height; ++row) {
                drawPackedRow(glyph, row, x, static_cast<int16_t>(y + row), displayW, displayH);
            }
        }

        void drawPackedRow(const AlphaGlyph& glyph, uint8_t row, int16_t dstX, int16_t dstY,
                           int16_t displayW, int16_t displayH) {
            if (dstY < 0 || dstY >= displayH) {
                return;
            }

            int16_t srcStart = 0;
            int16_t drawX = dstX;
            int16_t drawW = glyph.width;
            if (drawX < 0) {
                srcStart = static_cast<int16_t>(-drawX);
                drawW = static_cast<int16_t>(drawW - srcStart);
                drawX = 0;
            }
            if (drawX + drawW > displayW) {
                drawW = static_cast<int16_t>(displayW - drawX);
            }
            if (drawW <= 0) {
                return;
            }

            const int16_t srcEnd = static_cast<int16_t>(srcStart + drawW);
            const uint8_t* packedRow = nullptr;
            if (!prepareRow(glyph, row, packedRow)) {
                return;
            }

            forEachVisibleSpan(packedRow, srcStart, srcEnd,
                               [&](int16_t clippedX0, int16_t clippedX1) {
                const int16_t spanWidth = static_cast<int16_t>(clippedX1 - clippedX0);
                if (spanWidth > MaxRowWidth) {
                    return;
                }
                renderSpan(packedRow, clippedX0, spanWidth);
                output_.draw16bitRGBBitmap(static_cast<int16_t>(dstX + clippedX0), dstY, row_, spanWidth, 1);
            });
        }

        void renderSpan(const uint8_t* packedRow, int16_t srcStart, int16_t spanWidth) {
            int16_t src = srcStart;
            int16_t out = 0;

            if ((src & 1) != 0 && out < spanWidth) {
                const uint8_t coverage = coverageAt(packedRow, static_cast<uint8_t>(src));
                row_[out++] = blend_[coverage];
                ++src;
            }

            while (out + 1 < spanWidth) {
                const uint8_t packed = pgm_read_byte(packedRow + (src >> 1));
                const AlphaByteInfo info = kAlphaByteInfo[packed];
                if (info.isSolid) {
                    row_[out] = fg_;
                    row_[out + 1] = fg_;
                } else {
                    row_[out] = blendPair_[packed][0];
                    row_[out + 1] = blendPair_[packed][1];
                }
                src = static_cast<int16_t>(src + 2);
                out = static_cast<int16_t>(out + 2);
            }

            if (out < spanWidth) {
                const uint8_t coverage = coverageAt(packedRow, static_cast<uint8_t>(src));
                row_[out] = blend_[coverage];
            }
        }

        static uint8_t coverageAt(const uint8_t* packedRow, uint8_t x) {
            const uint8_t packed = pgm_read_byte(packedRow + (x >> 1U));
            return (x & 1U) == 0 ? static_cast<uint8_t>(packed >> 4U) : static_cast<uint8_t>(packed & 0x0FU);
        }

        AlphaGlyph readGlyph(const AlphaGlyph& glyph) const {
            return font_->file ? glyph : readPacked(glyph);
        }

        template<typename T>
        static T readPacked(const T& value) {
            T copy;
            std::memcpy(&copy, &value, sizeof(copy));
            return copy;
        }

        bool readFile(uint32_t offset, void* out, size_t bytes) const {
            if (font_ == nullptr || !font_->file)
                return false;
            File& file = font_->file->get();
            return file.seek(offset) && file.read(static_cast<uint8_t*>(out), bytes) == bytes;
        }

        const AlphaGlyph* fileGlyph(uint32_t index) const {
            if (font_ == nullptr || !font_->file || index >= font_->glyphCount)
                return nullptr;
            FileGlyphCacheEntry& cached = fileGlyphCache_[index % fileGlyphCache_.size()];
            if (cached.index == index)
                return &cached.glyph;

            RFont4::GlyphRecord record;
            const uint32_t offset = font_->fileStrike.glyphsOffset + index * sizeof(record);
            if (!readFile(offset, &record, sizeof(record))
                || static_cast<uint64_t>(record.bitmapOffset)
                           + static_cast<uint64_t>(record.rowStride) * record.height
                       > font_->fileStrike.bitmapSize
                || static_cast<uint64_t>(record.kernOffset) + record.kernCount
                       > font_->fileStrike.kerningPairCount)
                return nullptr;

            cached = {
                .index = index,
                .glyph = {record.codepoint, record.bitmapOffset, record.kernOffset,
                          record.width, record.height, record.rowStride, record.xAdvance,
                          record.xOffset, record.yOffset, record.kernCount, record.glyphId},
            };
            return &cached.glyph;
        }

        const AlphaGlyph* glyphAt(uint32_t index) const {
            if (font_ == nullptr || index >= font_->glyphCount)
                return nullptr;
            return font_->file ? fileGlyph(index) : font_->glyphs + index;
        }

        bool prepareRow(const AlphaGlyph& glyph, uint8_t row, const uint8_t*& packedRow) {
            if (row >= glyph.height || glyph.rowStride > packedRow_.size())
                return false;
            if (!font_->file) {
                packedRow = font_->bitmap + glyph.bitmapOffset + static_cast<uint32_t>(row) * glyph.rowStride;
                return true;
            }

            const uint32_t bitmapOffset = glyph.bitmapOffset + static_cast<uint32_t>(row) * glyph.rowStride;
            if (glyph.rowStride > 0
                && !readFile(font_->fileStrike.bitmapOffset + bitmapOffset, packedRow_.data(), glyph.rowStride))
                return false;
            packedRow = packedRow_.data();
            return true;
        }

        template<typename Function>
        static void forEachVisibleSpan(const uint8_t* packedRow, int16_t first, int16_t last, Function&& function) {
            int16_t x = first;
            while (x < last) {
                while (x < last && coverageAt(packedRow, static_cast<uint8_t>(x)) == 0)
                    ++x;
                const int16_t begin = x;
                while (x < last && coverageAt(packedRow, static_cast<uint8_t>(x)) != 0)
                    ++x;
                if (begin < x)
                    function(begin, x);
            }
        }

        const AlphaGlyph* findGlyph(uint32_t codepoint) const {
            if (font_ == nullptr || font_->glyphCount == 0 || (!font_->file && font_->glyphs == nullptr)) {
                return nullptr;
            }
            if (font_->file) {
                for (const auto& cached: fileGlyphCache_) {
                    if (cached.index != UINT32_MAX && cached.glyph.codepoint == codepoint)
                        return &cached.glyph;
                }
                if (codepoint <= UINT16_MAX && font_->pageMap != nullptr && font_->pageTableCount > 0) {
                    const uint8_t pageIndex = font_->pageMap[codepoint >> 8U];
                    if (pageIndex == kMissingPageIndex || pageIndex >= font_->pageTableCount)
                        return nullptr;
                    uint16_t index = kMissingGlyphIndex;
                    const uint32_t entry = static_cast<uint32_t>(pageIndex) * RFont4::kPageTableEntries
                                         + (codepoint & 0xFFU);
                    if (!readFile(font_->fileStrike.pageTablesOffset + entry * sizeof(index), &index, sizeof(index))
                        || index == kMissingGlyphIndex || index >= font_->glyphCount)
                        return nullptr;
                    return fileGlyph(index);
                }
                return findGlyphBinary(codepoint);
            }
            if (codepoint <= UINT16_MAX && font_->pageMap != nullptr && font_->pageTables != nullptr
                && font_->pageTableCount > 0) {
                const uint8_t pageIndex = pgm_read_byte(font_->pageMap + (codepoint >> 8U));
                if (pageIndex == kMissingPageIndex || pageIndex >= font_->pageTableCount) {
                    return nullptr;
                }

                const auto* page = reinterpret_cast<const uint16_t*>(pgm_read_ptr(font_->pageTables + pageIndex));
                if (page == nullptr) {
                    return nullptr;
                }

                const uint16_t index = pgm_read_word(page + (codepoint & 0xFFU));
                if (index == kMissingGlyphIndex || index >= font_->glyphCount) {
                    return nullptr;
                }
                return font_->glyphs + index;
            }

            return findGlyphBinary(codepoint);
        }

        const AlphaGlyph* findGlyphBinary(uint32_t codepoint) const {
            if (font_->file) {
                uint32_t first = 0;
                uint32_t last = font_->glyphCount;
                while (first < last) {
                    const uint32_t middle = first + (last - first) / 2;
                    const AlphaGlyph* candidate = fileGlyph(middle);
                    if (candidate == nullptr)
                        return nullptr;
                    if (candidate->codepoint < codepoint)
                        first = middle + 1;
                    else
                        last = middle;
                }
                const AlphaGlyph* glyph = first < font_->glyphCount ? fileGlyph(first) : nullptr;
                return glyph != nullptr && glyph->codepoint == codepoint ? glyph : nullptr;
            }
            const std::span glyphs{font_->glyphs, static_cast<size_t>(font_->glyphCount)};
            const auto glyph = std::ranges::lower_bound(glyphs, codepoint, {}, [](const AlphaGlyph& candidate) {
                return readPacked(candidate).codepoint;
            });
            return glyph != glyphs.end() && readPacked(*glyph).codepoint == codepoint ? &*glyph : nullptr;
        }

        const AlphaGlyph* findGlyphId(uint32_t glyphId) const {
            uint32_t index = 0;
            return resolveGlyphId(glyphId, index) ? glyphAt(index) : nullptr;
        }

        int16_t fileKerningAdjust(const AlphaGlyph& left, uint32_t rightCodepoint) const {
            const uint64_t key = static_cast<uint64_t>(left.codepoint) << 32U | rightCodepoint;
            const size_t cacheIndex = static_cast<size_t>((left.codepoint * 31U) ^ rightCodepoint)
                                    & (fileKerningKeys_.size() - 1U);
            if (fileKerningKeys_[cacheIndex] == key)
                return fileKerningValues_[cacheIndex];

            uint32_t first = left.kernOffset;
            uint32_t last = first + left.kernCount;
            RFont4::KerningRecord record;
            while (first < last) {
                const uint32_t middle = first + (last - first) / 2;
                if (!readFile(font_->fileStrike.kerningOffset + middle * sizeof(record), &record, sizeof(record)))
                    return 0;
                if (record.rightCodepoint < rightCodepoint)
                    first = middle + 1;
                else
                    last = middle;
            }
            const int8_t adjustment = first < left.kernOffset + left.kernCount
                                           && readFile(font_->fileStrike.kerningOffset + first * sizeof(record),
                                                       &record, sizeof(record))
                                           && record.rightCodepoint == rightCodepoint
                                        ? record.xAdjust
                                        : 0;
            fileKerningKeys_[cacheIndex] = key;
            fileKerningValues_[cacheIndex] = adjustment;
            return adjustment;
        }

        void rebuildBlendTable() {
            const uint8_t fgR = static_cast<uint8_t>((fg_ >> 11U) & 0x1FU);
            const uint8_t fgG = static_cast<uint8_t>((fg_ >> 5U) & 0x3FU);
            const uint8_t fgB = static_cast<uint8_t>(fg_ & 0x1FU);
            const uint8_t bgR = static_cast<uint8_t>((bg_ >> 11U) & 0x1FU);
            const uint8_t bgG = static_cast<uint8_t>((bg_ >> 5U) & 0x3FU);
            const uint8_t bgB = static_cast<uint8_t>(bg_ & 0x1FU);

            for (uint8_t coverage = 0; coverage < 16; ++coverage) {
                const uint8_t r = blendChannel(bgR, fgR, coverage, 15);
                const uint8_t g = blendChannel(bgG, fgG, coverage, 15);
                const uint8_t b = blendChannel(bgB, fgB, coverage, 15);
                blend_[coverage] =
                    static_cast<uint16_t>((static_cast<uint16_t>(r) << 11U) | (static_cast<uint16_t>(g) << 5U) | b);
            }

            for (uint16_t packed = 0; packed < 256; ++packed) {
                blendPair_[packed][0] = blend_[packed >> 4U];
                blendPair_[packed][1] = blend_[packed & 0x0FU];
            }

            blendTableValid_ = true;
        }

        static uint8_t blendChannel(uint8_t bg, uint8_t fg, uint8_t coverage, uint8_t maxCoverage) {
            const uint16_t inv = static_cast<uint16_t>(maxCoverage - coverage);
            return static_cast<uint8_t>((bg * inv + fg * coverage + maxCoverage / 2) / maxCoverage);
        }

        Arduino_GFX& output_;
        const AlphaFont* font_ = nullptr;
        mutable std::array<FileGlyphCacheEntry, 32> fileGlyphCache_{};
        mutable std::array<uint64_t, 16> fileKerningKeys_{};
        mutable std::array<int8_t, 16> fileKerningValues_{};
        std::array<uint8_t, (MaxRowWidth + 1) / 2> packedRow_{};
        uint16_t row_[MaxRowWidth]{};
        uint16_t strip_[MaxStripRows][MaxRowWidth]{};
        uint8_t stripInk_[MaxStripRows][MaxRowWidth]{};
        uint16_t blend_[16]{};
        uint16_t blendPair_[256][2]{};
        uint16_t fg_ = 0xFFFF;
        uint16_t bg_ = 0x0000;
        bool ready_ = false;
        bool blendTableValid_ = false;
    };

} // namespace ui::fonts
