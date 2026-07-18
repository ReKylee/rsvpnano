#pragma once

#include <glaze/core/custom.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

#include "text/AsciiText.h"

namespace ui::themes {

    constexpr uint16_t rgb565(uint8_t red, uint8_t green, uint8_t blue) {
        return static_cast<uint16_t>(((red & 0xF8U) << 8) | ((green & 0xFCU) << 3) | (blue >> 3));
    }

    struct Rgb565 {
        uint16_t value = 0;

        constexpr Rgb565() = default;
        constexpr Rgb565(uint16_t value) : value(value) {}
        constexpr operator uint16_t() const { return value; }
        bool operator==(const Rgb565&) const = default;
    };

    namespace detail {

        inline constexpr auto readRgb565 = [](Rgb565& output, const std::string& text, glz::context& context) {
            if (text.size() != 7 || text.front() != '#') {
                context.error = glz::error_code::constraint_violated;
                context.custom_error_message = "RGB565 color must be a quoted #RRGGBB string";
                return;
            }

            uint32_t color = 0;
            if (!AsciiText::parseUnsigned(std::string_view{text}.substr(1), color, 16)) {
                context.error = glz::error_code::constraint_violated;
                context.custom_error_message = "RGB565 color contains a non-hexadecimal digit";
                return;
            }
            output.value = rgb565(color >> 16U, color >> 8U, color);
        };

        inline constexpr auto writeRgb565 = [](const Rgb565& color) {
            constexpr char digits[] = "0123456789ABCDEF";
            const std::array<uint8_t, 3> channels{
                static_cast<uint8_t>(((color.value >> 11) & 0x1F) * 255 / 31),
                static_cast<uint8_t>(((color.value >> 5) & 0x3F) * 255 / 63),
                static_cast<uint8_t>((color.value & 0x1F) * 255 / 31),
            };
            std::string result = "#000000";
            for (size_t channel = 0; channel < channels.size(); ++channel) {
                result[1 + channel * 2] = digits[channels[channel] >> 4];
                result[2 + channel * 2] = digits[channels[channel] & 0x0F];
            }
            return result;
        };

    } // namespace detail

} // namespace ui::themes

template<>
struct glz::meta<ui::themes::Rgb565> {
    static constexpr auto value =
        glz::custom<ui::themes::detail::readRgb565, ui::themes::detail::writeRgb565>;
};
