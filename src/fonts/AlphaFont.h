#pragma once

#include <Arduino.h>
#include <Arduino_GFX_Library.h>

#include <algorithm>
#include <array>
#include <climits>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

#include "text/Utf8Text.h"

namespace ui::fonts {

    struct AlphaGlyph {
        uint16_t codepoint = 0;
        uint32_t bitmapOffset = 0;
        uint32_t rowOffset = 0;
        uint16_t kernOffset = 0;
        uint8_t width = 0;
        uint8_t height = 0;
        uint8_t rowStride = 0;
        uint8_t xAdvance = 0;
        int8_t xOffset = 0;
        int8_t yOffset = 0;
        uint8_t kernCount = 0;
    };

    struct AlphaRow {
        uint32_t spanOffset = 0;
        uint8_t spanCount = 0;
    };

    struct AlphaSpan {
        uint8_t x = 0;
        uint8_t width = 0;
    };

    struct AlphaKerningPair {
        uint16_t rightCodepoint = 0;
        int8_t xAdjust = 0;
    };

    struct AlphaFont {
        std::string_view name;
        const uint8_t* bitmap = nullptr;
        const AlphaGlyph* glyphs = nullptr;
        uint16_t glyphCount = 0;
        uint8_t yAdvance = 0;
        uint8_t ascent = 0;
        uint8_t descent = 0;
        const AlphaRow* rows = nullptr;
        const AlphaSpan* spans = nullptr;
        const uint8_t* pageMap = nullptr;
        const uint16_t* const* pageTables = nullptr;
        uint16_t pageTableCount = 0;
        const AlphaKerningPair* kerningPairs = nullptr;
        uint16_t kerningPairCount = 0;
        int8_t wordInkTop = 0;
        int8_t wordInkBottom = -1;
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

        void setFont(const AlphaFont* font) {
            font_ = font;
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
            Bounds bounds;
            if (!measure(text, x, baseline, bounds, tracking)) {
                return -1;
            }

            if (bounds.w == 0 || bounds.h == 0) {
                return bounds.advance;
            }

            if (!drawGlyphsToStrips(text, x, baseline, bounds, tracking)) {
                drawGlyphs(text, x, baseline, tracking);
            }
            return bounds.advance;
        }

        int16_t drawCodepoint(uint16_t codepoint, int16_t x, int16_t baseline) {
            const AlphaGlyph* glyph = glyphOrFallback(codepoint);
            if (glyph == nullptr) {
                return 0;
            }

            const GlyphMetrics metrics = readGlyphMetrics(*glyph);
            if (metrics.width > 0 && metrics.height > 0) {
                drawGlyph(metrics, static_cast<int16_t>(x + metrics.xOffset),
                          static_cast<int16_t>(baseline + metrics.yOffset));
            }
            return metrics.xAdvance;
        }

        int16_t glyphAdvance(uint16_t codepoint) const {
            const AlphaGlyph* glyph = glyphOrFallback(codepoint);
            if (glyph == nullptr) {
                return 0;
            }
            return readGlyphMetrics(*glyph).xAdvance;
        }

        int16_t kerningAdjust(uint16_t leftCodepoint, uint16_t rightCodepoint) const {
            if (font_ == nullptr || font_->kerningPairs == nullptr || font_->kerningPairCount == 0) {
                return 0;
            }

            const AlphaGlyph* leftGlyph = findGlyph(leftCodepoint);
            if (leftGlyph == nullptr) {
                return 0;
            }

            const GlyphMetrics left = readGlyphMetrics(*leftGlyph);
            if (left.kernCount == 0) {
                return 0;
            }

            const std::span pairs{font_->kerningPairs + left.kernOffset, static_cast<size_t>(left.kernCount)};
            const auto pair =
                std::ranges::lower_bound(pairs, rightCodepoint, {}, [](const AlphaKerningPair& candidate) {
                    return pgm_read_word(&candidate.rightCodepoint);
                });
            return pair != pairs.end() && pgm_read_word(&pair->rightCodepoint) == rightCodepoint
                     ? static_cast<int8_t>(pgm_read_byte(reinterpret_cast<const uint8_t*>(&pair->xAdjust)))
                     : 0;
        }

        void getTextBounds(std::string_view text, int16_t x, int16_t baseline, int16_t* x1, int16_t* y1, uint16_t* w,
                           uint16_t* h) const {
            Bounds bounds;
            if (!measure(text, x, baseline, bounds)) {
                bounds = {};
            }

            if (x1 != nullptr) {
                *x1 = bounds.x1;
            }
            if (y1 != nullptr) {
                *y1 = bounds.y1;
            }
            if (w != nullptr) {
                *w = bounds.w;
            }
            if (h != nullptr) {
                *h = bounds.h;
            }
        }

        int16_t textWidth(std::string_view text) const {
            Bounds bounds;
            if (!measure(text, 0, 0, bounds)) {
                return 0;
            }
            return static_cast<int16_t>(bounds.w);
        }

        int16_t textAdvance(std::string_view text, int8_t tracking = 0) const {
            Bounds bounds;
            if (!measure(text, 0, 0, bounds, tracking)) {
                return 0;
            }
            return bounds.advance;
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

        struct GlyphMetrics {
            uint32_t bitmapOffset = 0;
            uint32_t rowOffset = 0;
            uint16_t kernOffset = 0;
            uint8_t width = 0;
            uint8_t height = 0;
            uint8_t rowStride = 0;
            uint8_t xAdvance = 0;
            int8_t xOffset = 0;
            int8_t yOffset = 0;
            uint8_t kernCount = 0;
        };

        struct RowMetrics {
            uint32_t spanOffset = 0;
            uint8_t spanCount = 0;
        };

        struct SpanMetrics {
            uint8_t x = 0;
            uint8_t width = 0;
        };

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
            uint16_t previousCodepoint = 0;
            bool hasPrevious = false;
            uint16_t codepoint = 0;
            while (Utf8Text::next(cursor, codepoint)) {
                if (hasPrevious) {
                    cursorX = static_cast<int16_t>(cursorX + kerningAdjust(previousCodepoint, codepoint));
                }

                const AlphaGlyph* glyph = glyphOrFallback(codepoint);
                if (glyph == nullptr) {
                    if (!cursor.empty())
                        cursorX = static_cast<int16_t>(cursorX + tracking);
                    previousCodepoint = codepoint;
                    hasPrevious = true;
                    continue;
                }

                const GlyphMetrics metrics = readGlyphMetrics(*glyph);
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
            uint16_t previousCodepoint = 0;
            bool hasPrevious = false;
            uint16_t codepoint = 0;
            while (Utf8Text::next(cursor, codepoint)) {
                if (hasPrevious) {
                    cursorX = static_cast<int16_t>(cursorX + kerningAdjust(previousCodepoint, codepoint));
                }

                const AlphaGlyph* glyph = glyphOrFallback(codepoint);
                if (glyph == nullptr) {
                    if (!cursor.empty())
                        cursorX = static_cast<int16_t>(cursorX + tracking);
                    previousCodepoint = codepoint;
                    hasPrevious = true;
                    continue;
                }

                const GlyphMetrics metrics = readGlyphMetrics(*glyph);
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
            uint16_t previousCodepoint = 0;
            bool hasPrevious = false;
            uint16_t codepoint = 0;

            while (Utf8Text::next(cursor, codepoint)) {
                if (hasPrevious) {
                    cursorX = static_cast<int16_t>(cursorX + kerningAdjust(previousCodepoint, codepoint));
                }

                const AlphaGlyph* glyph = glyphOrFallback(codepoint);
                if (glyph != nullptr) {
                    const GlyphMetrics metrics = readGlyphMetrics(*glyph);
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

        void compositeGlyphIntoStrip(const GlyphMetrics& glyph, int16_t glyphX, int16_t glyphY, int16_t stripX,
                                     int16_t stripY, uint16_t stripWidth, uint8_t stripRows) {
            if (glyph.width == 0 || glyph.height == 0 || font_->rows == nullptr || font_->spans == nullptr) {
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
                const RowMetrics row = readRowMetrics(*(font_->rows + glyph.rowOffset + srcY));
                if (row.spanCount == 0) {
                    continue;
                }

                const uint8_t* packedRow =
                    font_->bitmap + glyph.bitmapOffset + static_cast<uint32_t>(srcY) * glyph.rowStride;
                const AlphaSpan* spans = font_->spans + row.spanOffset;
                for (uint8_t i = 0; i < row.spanCount; ++i) {
                    const SpanMetrics span = readSpanMetrics(*(spans + i));
                    const int16_t spanX0 = span.x;
                    const int16_t spanX1 = static_cast<int16_t>(span.x + span.width);
                    const int16_t srcX0 = std::max<int16_t>(spanX0, overlapSrcX0);
                    const int16_t srcX1 = std::min<int16_t>(spanX1, overlapSrcX1);
                    if (srcX1 <= srcX0) {
                        continue;
                    }

                    const uint16_t dstCol = static_cast<uint16_t>(glyphX + srcX0 - stripX);
                    const uint16_t count = static_cast<uint16_t>(srcX1 - srcX0);
                    compositePackedRowSpanIntoStrip(packedRow, srcX0, stripRow, dstCol, count);
                }
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

        void drawGlyph(const GlyphMetrics& glyph, int16_t x, int16_t y) {
            if (glyph.width == 0 || glyph.height == 0 || glyph.width > MaxRowWidth || font_->rows == nullptr
                || font_->spans == nullptr) {
                return;
            }

            const int16_t displayW = output_.width();
            const int16_t displayH = output_.height();
            for (uint8_t row = 0; row < glyph.height; ++row) {
                drawPackedRowFromSpans(glyph, row, x, static_cast<int16_t>(y + row), displayW, displayH);
            }
        }

        void drawPackedRowFromSpans(const GlyphMetrics& glyph, uint8_t row, int16_t dstX, int16_t dstY,
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
            const RowMetrics rowInfo = readRowMetrics(*(font_->rows + glyph.rowOffset + row));
            if (rowInfo.spanCount == 0) {
                return;
            }

            const uint8_t* packedRow =
                font_->bitmap + glyph.bitmapOffset + static_cast<uint32_t>(row) * glyph.rowStride;
            const AlphaSpan* spans = font_->spans + rowInfo.spanOffset;
            for (uint8_t i = 0; i < rowInfo.spanCount; ++i) {
                const SpanMetrics span = readSpanMetrics(*(spans + i));
                const int16_t spanX0 = span.x;
                const int16_t spanX1 = static_cast<int16_t>(span.x + span.width);
                const int16_t clippedX0 = std::max<int16_t>(spanX0, srcStart);
                const int16_t clippedX1 = std::min<int16_t>(spanX1, srcEnd);
                if (clippedX1 <= clippedX0) {
                    continue;
                }

                const int16_t spanWidth = static_cast<int16_t>(clippedX1 - clippedX0);
                if (spanWidth > MaxRowWidth) {
                    continue;
                }
                renderSpan(packedRow, clippedX0, spanWidth);
                output_.draw16bitRGBBitmap(static_cast<int16_t>(dstX + clippedX0), dstY, row_, spanWidth, 1);
            }
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

        static GlyphMetrics readGlyphMetrics(const AlphaGlyph& glyph) {
            return GlyphMetrics{
                pgm_read_dword(&glyph.bitmapOffset),
                pgm_read_dword(&glyph.rowOffset),
                pgm_read_word(&glyph.kernOffset),
                pgm_read_byte(&glyph.width),
                pgm_read_byte(&glyph.height),
                pgm_read_byte(&glyph.rowStride),
                pgm_read_byte(&glyph.xAdvance),
                static_cast<int8_t>(pgm_read_byte(reinterpret_cast<const uint8_t*>(&glyph.xOffset))),
                static_cast<int8_t>(pgm_read_byte(reinterpret_cast<const uint8_t*>(&glyph.yOffset))),
                pgm_read_byte(&glyph.kernCount),
            };
        }

        static RowMetrics readRowMetrics(const AlphaRow& row) {
            return RowMetrics{pgm_read_dword(&row.spanOffset), pgm_read_byte(&row.spanCount)};
        }

        static SpanMetrics readSpanMetrics(const AlphaSpan& span) {
            return SpanMetrics{pgm_read_byte(&span.x), pgm_read_byte(&span.width)};
        }

        const AlphaGlyph* glyphOrFallback(uint16_t codepoint) const {
            if (codepoint == '\n' || codepoint == '\r') {
                return nullptr;
            }

            const AlphaGlyph* glyph = findGlyph(codepoint);
            if (glyph != nullptr) {
                return glyph;
            }

            if (codepoint == 0x00A0) {
                glyph = findGlyph(' ');
                if (glyph != nullptr) {
                    return glyph;
                }
            }

            if (codepoint != '?') {
                return findGlyph('?');
            }

            return nullptr;
        }

        const AlphaGlyph* findGlyph(uint16_t codepoint) const {
            if (font_ == nullptr || font_->glyphs == nullptr || font_->glyphCount == 0) {
                return nullptr;
            }

            if (font_->pageMap != nullptr && font_->pageTables != nullptr && font_->pageTableCount > 0) {
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

        const AlphaGlyph* findGlyphBinary(uint16_t codepoint) const {
            const std::span glyphs{font_->glyphs, static_cast<size_t>(font_->glyphCount)};
            const auto glyph = std::ranges::lower_bound(glyphs, codepoint, {}, [](const AlphaGlyph& candidate) {
                return pgm_read_word(&candidate.codepoint);
            });
            return glyph != glyphs.end() && pgm_read_word(&glyph->codepoint) == codepoint ? &*glyph : nullptr;
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
