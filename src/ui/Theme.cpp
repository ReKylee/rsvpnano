#include "ui/Theme.h"

#include <algorithm>
#include <cstring>
#include <iterator>

namespace ui::themes {
    namespace {

        constexpr const char* kColorRoleNames[kColorRoleCount] = {
            "background",   "foreground",  "muted",   "subtle",         "accent",         "accent_bar",
            "break_accent", "on_accent",   "surface", "surface_muted",  "surface_active", "outline",
            "guide",        "guide_focus", "phantom", "progress_track",
        };

        constexpr std::array<uint16_t, kColorRoleCount> kDefaultThemeColors = {
            0x0000, 0xFFFF, 0x8410, 0x528A, 0xF800, 0xF800, rgb565(68, 132, 88), 0xFFFF, 0x0000, 0x2104,
            0x4208, 0x8410, 0x8410, 0xF800, 0x8410, 0x8410,
        };
        constexpr ReaderTypeface kDefaultThemeTypeface = ReaderTypeface::Standard;
        constexpr bool kDefaultThemeLowBrightness = false;

        bool isHex(char c) {
            return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
        }

        uint8_t hexValue(char c) {
            if (c >= '0' && c <= '9') {
                return static_cast<uint8_t>(c - '0');
            }
            if (c >= 'a' && c <= 'f') {
                return static_cast<uint8_t>(10 + c - 'a');
            }
            if (c >= 'A' && c <= 'F') {
                return static_cast<uint8_t>(10 + c - 'A');
            }
            return 0;
        }

        bool parseHexByte(const String& value, size_t offset, uint8_t& out) {
            if (offset + 1 >= value.length() || !isHex(value[offset]) || !isHex(value[offset + 1])) {
                return false;
            }
            out = static_cast<uint8_t>((hexValue(value[offset]) << 4) | hexValue(value[offset + 1]));
            return true;
        }

        bool parseRgb565(const String& value, uint16_t& out) {
            if (value.length() != 6 || value[0] != '0' || (value[1] != 'x' && value[1] != 'X')) {
                return false;
            }

            uint16_t parsed = 0;
            for (size_t i = 2; i < value.length(); ++i) {
                if (!isHex(value[i])) {
                    return false;
                }
                parsed = static_cast<uint16_t>((parsed << 4) | hexValue(value[i]));
            }
            out = parsed;
            return true;
        }

        bool parseColor(const String& value, uint16_t& out) {
            if (value.length() == 7 && value[0] == '#') {
                uint8_t red = 0;
                uint8_t green = 0;
                uint8_t blue = 0;
                if (!parseHexByte(value, 1, red) || !parseHexByte(value, 3, green) || !parseHexByte(value, 5, blue)) {
                    return false;
                }
                out = rgb565(red, green, blue);
                return true;
            }

            return parseRgb565(value, out);
        }

        String stripBom(String line) {
            if (line.length() >= 3 && static_cast<uint8_t>(line[0]) == 0xEF && static_cast<uint8_t>(line[1]) == 0xBB
                && static_cast<uint8_t>(line[2]) == 0xBF) {
                line.remove(0, 3);
            }
            return line;
        }

        String nextLine(const String& text, size_t& offset) {
            if (offset >= text.length()) {
                return "";
            }
            int end = text.indexOf('\n', static_cast<unsigned int>(offset));
            if (end < 0) {
                String line = text.substring(static_cast<unsigned int>(offset));
                offset = text.length();
                return line;
            }
            String line = text.substring(static_cast<unsigned int>(offset), static_cast<unsigned int>(end));
            offset = static_cast<size_t>(end) + 1;
            return line;
        }

        String lowerTrimmed(String value) {
            value.trim();
            value.toLowerCase();
            return value;
        }

        bool parseBool(const String& value, bool& out) {
            const String key = lowerTrimmed(value);
            if (key == "true" || key == "yes" || key == "1" || key == "on") {
                out = true;
                return true;
            }
            if (key == "false" || key == "no" || key == "0" || key == "off") {
                out = false;
                return true;
            }
            return false;
        }

        bool firstContentLineIsMagic(const String& text, size_t& offset, String& error) {
            offset = 0;
            while (offset < text.length()) {
                String line = stripBom(nextLine(text, offset));
                line.trim();
                if (line.isEmpty() || line.startsWith("#")) {
                    continue;
                }
                if (line == kThemeMagic) {
                    return true;
                }
                error = "first content line must be @rtheme";
                return false;
            }
            error = "missing @rtheme";
            return false;
        }

    } // namespace

    const char* colorRoleName(ColorRole role) {
        const auto index = static_cast<size_t>(role);
        return index < kColorRoleCount ? kColorRoleNames[index] : "";
    }

    int colorRoleIndexForName(const String& name) {
        const String key = lowerTrimmed(name);
        const auto begin = std::begin(kColorRoleNames);
        const auto end = std::end(kColorRoleNames);
        const auto found = std::find_if(begin, end, [&key](const char* roleName) {
            return key == roleName;
        });
        if (found == end) {
            return -1;
        }
        return static_cast<int>(found - begin);
    }

    const char* readerTypefaceName(ReaderTypeface typeface) {
        switch (typeface) {
        case ReaderTypeface::Standard:
            return "standard";
        case ReaderTypeface::AtkinsonHyperlegible:
            return "atkinson";
        case ReaderTypeface::OpenDyslexic:
            return "open_dyslexic";
        default:
            return "standard";
        }
    }

    bool readerTypefaceForName(const String& name, ReaderTypeface& typeface) {
        const String key = lowerTrimmed(name);
        if (key == "standard") {
            typeface = ReaderTypeface::Standard;
            return true;
        }
        if (key == "atkinson" || key == "atkinson_hyperlegible" || key == "atkinson-hyperlegible") {
            typeface = ReaderTypeface::AtkinsonHyperlegible;
            return true;
        }
        if (key == "opendyslexic" || key == "open_dyslexic" || key == "open-dyslexic") {
            typeface = ReaderTypeface::OpenDyslexic;
            return true;
        }
        return false;
    }

    Theme defaultTheme() {
        Theme theme;
        theme.id = kDefaultThemeId;
        theme.name = "Default";
        theme.builtIn = true;
        theme.colors = kDefaultThemeColors;
        theme.typeface = kDefaultThemeTypeface;
        theme.lowBrightness = kDefaultThemeLowBrightness;
        return theme;
    }

    bool hasThemeExtension(const String& path) {
        String lowered = path;
        lowered.toLowerCase();
        return lowered.endsWith(kThemeExtension);
    }

    String themeIdFromPath(const String& path) {
        const int separator = path.lastIndexOf('/');
        String id = separator >= 0 ? path.substring(static_cast<unsigned int>(separator + 1)) : path;
        String lowered = id;
        lowered.toLowerCase();
        if (lowered.endsWith(kThemeExtension)) {
            id.remove(id.length() - std::strlen(kThemeExtension));
        }
        id.toLowerCase();
        return id;
    }

    bool parseThemeText(const String& text, const String& id, Theme& theme, String& error) {
        size_t offset = 0;
        if (!firstContentLineIsMagic(text, offset, error)) {
            return false;
        }

        Theme parsed;
        parsed.id = id;
        parsed.name = id;
        std::array<bool, kColorRoleCount> seen = {};
        bool hasName = false;
        bool hasTypeface = false;

        while (offset < text.length()) {
            String line = nextLine(text, offset);
            line.trim();
            if (line.isEmpty() || line.startsWith("#")) {
                continue;
            }

            const int equals = line.indexOf('=');
            if (equals <= 0) {
                continue;
            }

            String key = line.substring(0, static_cast<unsigned int>(equals));
            String value = line.substring(static_cast<unsigned int>(equals + 1));
            key = lowerTrimmed(key);
            value.trim();

            if (key == "name") {
                if (value.isEmpty()) {
                    error = "missing name";
                    return false;
                }
                parsed.name = value;
                hasName = true;
                continue;
            }
            if (key == "typeface") {
                if (!readerTypefaceForName(value, parsed.typeface)) {
                    error = "typeface must be standard, open_dyslexic, or atkinson";
                    return false;
                }
                hasTypeface = true;
                continue;
            }
            if (key == "low_brightness") {
                if (!parseBool(value, parsed.lowBrightness)) {
                    error = "low_brightness must be true or false";
                    return false;
                }
                continue;
            }

            const int roleIndex = colorRoleIndexForName(key);
            if (roleIndex < 0) {
                continue;
            }

            uint16_t color = 0;
            if (!parseColor(value, color)) {
                error = String("invalid color for ") + key;
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
            error = String("missing color ") + kColorRoleNames[missing - seen.begin()];
            return false;
        }

        theme = parsed;
        return true;
    }

} // namespace ui::themes
