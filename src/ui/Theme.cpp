#include "ui/Theme.h"

#include <algorithm>
#include <cctype>

#include "text/AsciiText.h"

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

        char lower(unsigned char value) {
            return std::tolower(value);
        }

        bool whitespace(unsigned char value) {
            return std::isspace(value);
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

        bool parseColor(std::string_view value, uint16_t& out) {
            const bool rgb = value.size() == 7 && value.front() == '#';
            if (rgb)
                value.remove_prefix(1);
            else if (value.size() == 6 && value[0] == '0' && lower(value[1]) == 'x')
                value.remove_prefix(2);
            else
                return false;

            uint32_t parsed = 0;
            if (!AsciiText::parseUnsigned(value, parsed, 16))
                return false;
            out = rgb ? rgb565(parsed >> 16U, parsed >> 8U, parsed) : parsed;
            return true;
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

    std::string_view colorRoleName(size_t role) {
        return role < kColorRoleNames.size() ? kColorRoleNames[role] : std::string_view{};
    }

    size_t colorRoleIndexForName(std::string_view name) {
        name = trim(name);
        const auto found = std::find_if(kColorRoleNames.begin(), kColorRoleNames.end(), [name](std::string_view role) {
            return equalIgnoreCase(name, role);
        });
        return found - kColorRoleNames.begin();
    }

    Theme defaultTheme() {
        return {std::string{kDefaultThemeId}, "Default", kDefaultThemeColors, std::string{kDefaultTypefaceId}, true};
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

    bool parseThemeText(std::string_view text, std::string_view id, Theme& theme, std::string& error,
                        bool* hasTypefaceValue) {
        if (hasTypefaceValue != nullptr)
            *hasTypefaceValue = false;
        if (!consumeMagic(text, error))
            return false;

        Theme parsed;
        parsed.id = id;
        parsed.name = id;
        std::array<bool, kColorRoleCount> seen{};
        bool hasName = false;

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
                if (hasTypefaceValue != nullptr)
                    *hasTypefaceValue = !value.empty();
                parsed.typeface = value.empty() ? std::string{kDefaultTypefaceId} : std::string{value};
                continue;
            }
            const size_t roleIndex = colorRoleIndexForName(key);
            if (roleIndex == kColorRoleCount)
                continue;
            uint16_t color = 0;
            if (!parseColor(value, color)) {
                error = "invalid color for ";
                error.append(key);
                return false;
            }
            parsed.colors[roleIndex] = color;
            seen[roleIndex] = true;
        }

        if (!hasName) {
            error = "missing name";
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
