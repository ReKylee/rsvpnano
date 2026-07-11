#include "ui/Theme.h"

#include <algorithm>

namespace ui::themes {
    namespace {

        constexpr std::array<std::string_view, kColorRoleCount> kColorRoleNames = {
            "background",   "foreground",  "muted",   "subtle",         "accent",         "accent_bar",
            "break_accent", "on_accent",   "surface", "surface_muted",  "surface_active", "outline",
            "guide",        "guide_focus", "phantom", "progress_track",
        };

        constexpr std::array<uint16_t, kColorRoleCount> kDefaultThemeColors = {
            0x0000, 0xFFFF, 0x8410, 0x528A, 0xF800, 0xF800, rgb565(68, 132, 88), 0xFFFF, 0x0000, 0x2104,
            0x4208, 0x8410, 0x8410, 0xF800, 0x8410, 0x8410,
        };

        constexpr char lower(char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value - 'A' + 'a') : value;
        }

        constexpr bool whitespace(char value) {
            return value == ' ' || value == '\t' || value == '\r' || value == '\n';
        }

        std::string_view trim(std::string_view value) {
            while (!value.empty() && whitespace(value.front()))
                value.remove_prefix(1);
            while (!value.empty() && whitespace(value.back()))
                value.remove_suffix(1);
            return value;
        }

        bool equalIgnoreCase(std::string_view left, std::string_view right) {
            return left.size() == right.size()
                && std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
                       return lower(a) == lower(b);
                   });
        }

        bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
            return value.size() >= suffix.size() && equalIgnoreCase(value.substr(value.size() - suffix.size()), suffix);
        }

        bool hex(char value) {
            return (value >= '0' && value <= '9') || (lower(value) >= 'a' && lower(value) <= 'f');
        }

        uint8_t hexValue(char value) {
            return value >= '0' && value <= '9' ? static_cast<uint8_t>(value - '0')
                                                : static_cast<uint8_t>(10 + lower(value) - 'a');
        }

        bool parseHexByte(std::string_view value, size_t offset, uint8_t& out) {
            if (offset + 1 >= value.size() || !hex(value[offset]) || !hex(value[offset + 1]))
                return false;
            out = static_cast<uint8_t>((hexValue(value[offset]) << 4U) | hexValue(value[offset + 1]));
            return true;
        }

        bool parseColor(std::string_view value, uint16_t& out) {
            if (value.size() == 7 && value.front() == '#') {
                uint8_t red = 0, green = 0, blue = 0;
                if (!parseHexByte(value, 1, red) || !parseHexByte(value, 3, green) || !parseHexByte(value, 5, blue))
                    return false;
                out = rgb565(red, green, blue);
                return true;
            }
            if (value.size() != 6 || value[0] != '0' || lower(value[1]) != 'x')
                return false;
            uint16_t parsed = 0;
            for (const char digit: value.substr(2)) {
                if (!hex(digit))
                    return false;
                parsed = static_cast<uint16_t>((parsed << 4U) | hexValue(digit));
            }
            out = parsed;
            return true;
        }

        bool parseBool(std::string_view value, bool& out) {
            value = trim(value);
            if (equalIgnoreCase(value, "true") || equalIgnoreCase(value, "yes") || value == "1"
                || equalIgnoreCase(value, "on")) {
                out = true;
                return true;
            }
            if (equalIgnoreCase(value, "false") || equalIgnoreCase(value, "no") || value == "0"
                || equalIgnoreCase(value, "off")) {
                out = false;
                return true;
            }
            return false;
        }

        std::string_view nextLine(std::string_view& text) {
            const size_t end = text.find('\n');
            if (end == std::string_view::npos) {
                const std::string_view line = text;
                text = {};
                return line;
            }
            const std::string_view line = text.substr(0, end);
            text.remove_prefix(end + 1);
            return line;
        }

        bool consumeMagic(std::string_view& text, std::string& error) {
            while (!text.empty()) {
                std::string_view line = nextLine(text);
                if (line.starts_with("\xEF\xBB\xBF"))
                    line.remove_prefix(3);
                line = trim(line);
                if (line.empty() || line.front() == '#')
                    continue;
                if (line == kThemeMagic)
                    return true;
                error = "first content line must be @rtheme";
                return false;
            }
            error = "missing @rtheme";
            return false;
        }

    } // namespace

    std::string_view colorRoleName(ColorRole role) {
        const size_t index = static_cast<size_t>(role);
        return index < kColorRoleNames.size() ? kColorRoleNames[index] : std::string_view{};
    }

    int colorRoleIndexForName(std::string_view name) {
        name = trim(name);
        const auto found = std::find_if(kColorRoleNames.begin(), kColorRoleNames.end(), [name](std::string_view role) {
            return equalIgnoreCase(name, role);
        });
        return found == kColorRoleNames.end() ? -1 : static_cast<int>(found - kColorRoleNames.begin());
    }

    std::string_view readerTypefaceName(ReaderTypeface typeface) {
        switch (typeface) {
        case ReaderTypeface::OpenDyslexic:
            return "open_dyslexic";
        case ReaderTypeface::AtkinsonHyperlegible:
            return "atkinson";
        default:
            return "standard";
        }
    }

    bool readerTypefaceForName(std::string_view name, ReaderTypeface& typeface) {
        name = trim(name);
        if (equalIgnoreCase(name, "standard")) {
            typeface = ReaderTypeface::Standard;
            return true;
        }
        if (equalIgnoreCase(name, "atkinson") || equalIgnoreCase(name, "atkinson_hyperlegible")
            || equalIgnoreCase(name, "atkinson-hyperlegible")) {
            typeface = ReaderTypeface::AtkinsonHyperlegible;
            return true;
        }
        if (equalIgnoreCase(name, "opendyslexic") || equalIgnoreCase(name, "open_dyslexic")
            || equalIgnoreCase(name, "open-dyslexic")) {
            typeface = ReaderTypeface::OpenDyslexic;
            return true;
        }
        return false;
    }

    Theme defaultTheme() {
        return {std::string{kDefaultThemeId}, "Default", kDefaultThemeColors, ReaderTypeface::Standard, true, false};
    }

    bool hasThemeExtension(std::string_view path) {
        return endsWithIgnoreCase(path, kThemeExtension);
    }

    std::string themeIdFromPath(std::string_view path) {
        const size_t separator = path.find_last_of("/\\");
        if (separator != std::string_view::npos)
            path.remove_prefix(separator + 1);
        if (endsWithIgnoreCase(path, kThemeExtension))
            path.remove_suffix(kThemeExtension.size());
        std::string id{path};
        std::transform(id.begin(), id.end(), id.begin(), lower);
        return id;
    }

    bool parseThemeText(std::string_view text, std::string_view id, Theme& theme, std::string& error) {
        if (!consumeMagic(text, error))
            return false;

        Theme parsed;
        parsed.id = id;
        parsed.name = id;
        std::array<bool, kColorRoleCount> seen{};
        bool hasName = false;
        bool hasTypeface = false;

        while (!text.empty()) {
            std::string_view line = trim(nextLine(text));
            if (line.empty() || line.front() == '#')
                continue;
            const size_t equals = line.find('=');
            if (equals == std::string_view::npos || equals == 0)
                continue;
            const std::string_view key = trim(line.substr(0, equals));
            const std::string_view value = trim(line.substr(equals + 1));

            if (equalIgnoreCase(key, "name")) {
                if (value.empty()) {
                    error = "missing name";
                    return false;
                }
                parsed.name = value;
                hasName = true;
                continue;
            }
            if (equalIgnoreCase(key, "typeface")) {
                if (!readerTypefaceForName(value, parsed.typeface)) {
                    error = "typeface must be standard, open_dyslexic, or atkinson";
                    return false;
                }
                hasTypeface = true;
                continue;
            }
            if (equalIgnoreCase(key, "low_brightness")) {
                if (!parseBool(value, parsed.lowBrightness)) {
                    error = "low_brightness must be true or false";
                    return false;
                }
                continue;
            }

            const int roleIndex = colorRoleIndexForName(key);
            if (roleIndex < 0)
                continue;
            uint16_t color = 0;
            if (!parseColor(value, color)) {
                error = "invalid color for ";
                error.append(key);
                return false;
            }
            parsed.colors[static_cast<size_t>(roleIndex)] = color;
            seen[static_cast<size_t>(roleIndex)] = true;
        }

        if (!hasName) {
            error = "missing name";
            return false;
        }
        if (!hasTypeface) {
            error = "missing typeface";
            return false;
        }
        const auto missing = std::find(seen.begin(), seen.end(), false);
        if (missing != seen.end()) {
            error = "missing color ";
            error.append(kColorRoleNames[missing - seen.begin()]);
            return false;
        }
        theme = std::move(parsed);
        return true;
    }

} // namespace ui::themes
