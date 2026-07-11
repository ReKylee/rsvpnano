#pragma once

#include "ui/fonts/AlphaFont.h"

namespace ui::fonts {

    struct Font {
        enum class Kind : uint8_t {
            Gfx,
            Alpha4
        };
        static constexpr uint8_t kMinimumAlpha4Pixels = 24;

        Kind kind = Kind::Gfx;
        const GFXfont* gfx = nullptr;
        const AlphaFont* alpha4 = nullptr;
        uint8_t scale = 1;
        int8_t inkTop = -8;
        int8_t inkBottom = 0;

        static Font gfxFont(const GFXfont* value = nullptr, uint8_t scale = 1, int8_t inkTop = -8,
                            int8_t inkBottom = 0) {
            return {Kind::Gfx, value, nullptr, scale, inkTop, inkBottom};
        }

        static Font alphaFont(const AlphaFont* value) {
            if (value == nullptr || value->yAdvance < kMinimumAlpha4Pixels) {
                return gfxFont(nullptr, value == nullptr ? 1 : std::max<uint8_t>(1, value->yAdvance / 8));
            }
            return {Kind::Alpha4, nullptr, value, 1, value->wordInkTop, value->wordInkBottom};
        }
    };

    template<int16_t MaxRowWidth>
    class TextRenderer {
    public:
        explicit TextRenderer(Arduino_GFX& output) : output_(output), alpha_(&output) {}

        bool begin() {
            return alpha_.begin();
        }
        void setFont(Font font) {
            font_ = font;
            alpha_.setFont(font.alpha4);
        }
        Font font() const {
            return font_;
        }

        void setColors(uint16_t foreground, uint16_t background) {
            foreground_ = foreground;
            background_ = background;
            alpha_.setTextColor(foreground, background);
        }

        int16_t draw(const char* text, int16_t x, int16_t baseline) {
            if (font_.kind == Font::Kind::Alpha4)
                return alpha_.drawString(text, x, baseline);
            prepareGfx();
            output_.setCursor(x, baseline);
            output_.print(text == nullptr ? "" : text);
            return advance(text);
        }

        int16_t drawCodepoint(uint16_t codepoint, int16_t x, int16_t baseline) {
            if (font_.kind == Font::Kind::Alpha4)
                return alpha_.drawCodepoint(codepoint, x, baseline);
            char encoded[4] = {};
            encode(codepoint, encoded);
            return draw(encoded, x, baseline);
        }

        int16_t glyphAdvance(uint16_t codepoint) const {
            if (font_.kind == Font::Kind::Alpha4)
                return alpha_.glyphAdvance(codepoint);
            char encoded[4] = {};
            encode(codepoint, encoded);
            return advance(encoded);
        }

        int16_t kerning(uint16_t left, uint16_t right) const {
            return font_.kind == Font::Kind::Alpha4 ? alpha_.kerningAdjust(left, right) : 0;
        }

        int16_t advance(const char* text) const {
            if (font_.kind == Font::Kind::Alpha4)
                return alpha_.textAdvance(text);
            if (text == nullptr || *text == '\0')
                return 0;
            prepareGfx();
            int16_t x = 0, y = 0;
            uint16_t width = 0, height = 0;
            output_.getTextBounds(text, 0, 0, &x, &y, &width, &height);
            return static_cast<int16_t>(width);
        }

    private:
        void prepareGfx() const {
            output_.setFont(font_.gfx);
            output_.setTextSize(font_.scale);
            output_.setTextWrap(false);
            output_.setTextColor(foreground_, background_);
        }

        static void encode(uint16_t codepoint, char (&out)[4]) {
            if (codepoint < 0x80U) {
                out[0] = static_cast<char>(codepoint);
            } else if (codepoint < 0x800U) {
                out[0] = static_cast<char>(0xC0U | (codepoint >> 6U));
                out[1] = static_cast<char>(0x80U | (codepoint & 0x3FU));
            } else {
                out[0] = static_cast<char>(0xE0U | (codepoint >> 12U));
                out[1] = static_cast<char>(0x80U | ((codepoint >> 6U) & 0x3FU));
                out[2] = static_cast<char>(0x80U | (codepoint & 0x3FU));
            }
        }

        Arduino_GFX& output_;
        mutable AlphaTextRenderer<MaxRowWidth> alpha_;
        Font font_;
        mutable uint16_t foreground_ = 0xFFFF;
        mutable uint16_t background_ = 0;
    };

} // namespace ui::fonts
