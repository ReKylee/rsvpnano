#include "ui/Ui.h"

#include "text/Utf8Text.h"

namespace ui {
    namespace {
        class PortraitGfx final : public Arduino_GFX {
        public:
            explicit PortraitGfx(Arduino_GFX& output) : PortraitGfx(output, output.height(), output.width()) {}

            PortraitGfx(Arduino_GFX& output, int16_t width, int16_t height, int16_t offsetX = 0, int16_t offsetY = 0) :
                    Arduino_GFX(width, height),
                    output_(output),
                    portraitWidth_(width),
                    offsetX_(offsetX),
                    offsetY_(offsetY) {}

            bool begin(int32_t = -1) override {
                return true;
            }

            void writePixelPreclipped(int16_t x, int16_t y, uint16_t color) override {
                const int16_t mappedX = static_cast<int16_t>(y + offsetX_);
                const int16_t mappedY = static_cast<int16_t>(portraitWidth_ - 1 - x + offsetY_);
                if constexpr (Context::displayWriteAlignment() == 2)
                    output_.drawPixel(mappedX, mappedY, color);
                else
                    output_.writePixelPreclipped(mappedX, mappedY, color);
            }

            void writeFastHLine(int16_t x, int16_t y, int16_t width, uint16_t color) override {
                const int16_t mappedX = static_cast<int16_t>(y + offsetX_);
                const int16_t mappedY = static_cast<int16_t>(portraitWidth_ - x - width + offsetY_);
                if constexpr (Context::displayWriteAlignment() == 2)
                    output_.drawFastVLine(mappedX, mappedY, width, color);
                else
                    output_.writeFastVLine(mappedX, mappedY, width, color);
            }

            void writeFastVLine(int16_t x, int16_t y, int16_t height, uint16_t color) override {
                const int16_t mappedX = static_cast<int16_t>(y + offsetX_);
                const int16_t mappedY = static_cast<int16_t>(portraitWidth_ - 1 - x + offsetY_);
                if constexpr (Context::displayWriteAlignment() == 2)
                    output_.drawFastHLine(mappedX, mappedY, height, color);
                else
                    output_.writeFastHLine(mappedX, mappedY, height, color);
            }

            void writeFillRectPreclipped(int16_t x, int16_t y, int16_t width, int16_t height, uint16_t color) override {
                const int16_t mappedX = static_cast<int16_t>(y + offsetX_);
                const int16_t mappedY = static_cast<int16_t>(portraitWidth_ - x - width + offsetY_);
                if constexpr (Context::displayWriteAlignment() == 2)
                    output_.fillRect(mappedX, mappedY, height, width, color);
                else
                    output_.writeFillRectPreclipped(mappedX, mappedY, height, width, color);
            }

        private:
            Arduino_GFX& output_;
            int16_t portraitWidth_;
            int16_t offsetX_;
            int16_t offsetY_;
        };

    } // namespace

    void Context::portraitText(Rect rect, std::string_view text, uint8_t textSize, uint16_t textColor, TextAlign align,
                               uint8_t maxLines, std::string_view textLocale) {
        Rect destination{};
        if constexpr (displayWriteAlignment() == 2) {
            destination = paintBounds(rotateClockwise(rect, height()));
            rect = {static_cast<int16_t>(height() - destination.y - destination.h), destination.x, destination.h,
                    destination.w};
        }
        const auto layout = prepareText(rect, text, textSize, align, maxLines, textLocale);
        if constexpr (displayWriteAlignment() == 2) {
            paint(destination, [&](Arduino_GFX& output, Rect translated) {
                PortraitGfx portrait{output, height(), width(), static_cast<int16_t>(translated.x - destination.x),
                                     static_cast<int16_t>(translated.y - destination.y)};
                drawText(portrait, layout, textColor);
            });
        } else {
            PortraitGfx portrait{gfx_};
            drawText(portrait, layout, textColor);
        }
    }

    void Context::portraitVerticalText(Rect rect, std::string_view text, uint8_t textSize, uint16_t textColor,
                                       std::string_view textLocale) {
        const int16_t lineHeight = textHeightFor(text, textSize);
        if (lineHeight <= 0)
            return;
        Rect destination{};
        if constexpr (displayWriteAlignment() == 2) {
            destination = paintBounds(rotateClockwise(rect, height()));
            rect = {static_cast<int16_t>(height() - destination.y - destination.h), destination.x, destination.h,
                    destination.w};
        }
        TextLayout layout;
        layout.lines.reserve(std::min<size_t>(Utf8Text::count(text), std::max<int16_t>(0, rect.h) / lineHeight));
        for (int16_t y = rect.y; !text.empty() && y + lineHeight <= rect.y + rect.h;
             y = static_cast<int16_t>(y + lineHeight)) {
            const char* glyph = text.data();
            const size_t remaining = text.size();
            uint32_t codepoint = 0;
            Utf8Text::next(text, codepoint);
            appendText(layout, {rect.x, y, rect.w, lineHeight}, {glyph, remaining - text.size()}, textSize,
                       TextAlign::Center, 1, textLocale);
        }
        if constexpr (displayWriteAlignment() == 2) {
            paint(destination, [&](Arduino_GFX& output, Rect translated) {
                PortraitGfx portrait{output, height(), width(), static_cast<int16_t>(translated.x - destination.x),
                                     static_cast<int16_t>(translated.y - destination.y)};
                drawText(portrait, layout, textColor);
            });
        } else {
            PortraitGfx portrait{gfx_};
            drawText(portrait, layout, textColor);
        }
    }

    void Context::portraitBattery(Rect rect, uint8_t percent, bool charging, std::string_view labelText,
                                  bool showIcon) {
        if (!showIcon && labelText.empty())
            return;
        Rect destination{};
        if constexpr (displayWriteAlignment() == 2) {
            destination = paintBounds(rotateClockwise(rect, height()));
            rect = {static_cast<int16_t>(height() - destination.y - destination.h), destination.x, destination.h,
                    destination.w};
        }
        constexpr int16_t iconWidth = 29;
        constexpr int16_t iconHeight = 13;
        constexpr int16_t labelGap = 5;
        const int16_t iconAreaWidth = showIcon ? iconWidth + labelGap : 0;
        const uint16_t ink = color(ui::themes::ColorRole::Muted);
        const uint16_t surface = color(ui::themes::ColorRole::Background);
        const int16_t iconY = static_cast<int16_t>(rect.y + std::max<int16_t>(0, (rect.h - iconHeight) / 2));
        const auto label = prepareText({static_cast<int16_t>(rect.x + iconAreaWidth), rect.y,
                                        static_cast<int16_t>(std::max<int16_t>(0, rect.w - iconAreaWidth)), rect.h},
                                       labelText, 2, TextAlign::Left);
        const auto draw = [&](Arduino_GFX& output) {
            if (showIcon)
                drawBatteryIcon(output, {rect.x, iconY, iconWidth, iconHeight}, percent, charging, ink, surface);
            drawText(output, label, ink);
        };
        if constexpr (displayWriteAlignment() == 2) {
            paint(destination, [&](Arduino_GFX& output, Rect translated) {
                PortraitGfx portrait{output, height(), width(), static_cast<int16_t>(translated.x - destination.x),
                                     static_cast<int16_t>(translated.y - destination.y)};
                draw(portrait);
            });
        } else {
            PortraitGfx portrait{gfx_};
            draw(portrait);
        }
    }

} // namespace ui
