#include "ui/Theme.h"

#include <glaze/toml.hpp>

#include <algorithm>
#include <utility>

#include "settings/SettingsGlaze.h"
#include "settings/SettingsRules.h"

namespace ui::themes {
    namespace {

        bool endsWithIgnoreCase(std::string_view value, std::string_view suffix) {
            if (value.size() < suffix.size())
                return false;
            value.remove_prefix(value.size() - suffix.size());
            return std::ranges::equal(value, suffix, [](char left, char right) {
                if (left >= 'A' && left <= 'Z')
                    left += 'a' - 'A';
                if (right >= 'A' && right <= 'Z')
                    right += 'a' - 'A';
                return left == right;
            });
        }

        settings::SettingsError errorFrom(glz::error_ctx error, std::string_view input) {
            const settings::SettingsErrorCategory category =
                error.ec == glz::error_code::unexpected_enum       ? settings::SettingsErrorCategory::InvalidEnum
                : error.ec == glz::error_code::constraint_violated ? settings::SettingsErrorCategory::Constraint
                                                                   : settings::SettingsErrorCategory::Syntax;
            return {.category = category,
                    .source = settings::SettingsSource::Theme,
                    .message = glz::format_error(error, input),
                    .glazeCode = error.ec};
        }

    } // namespace

    uint16_t color(const ThemeColors& colors, ColorRole role) {
        switch (role) {
        case Background:
            return colors.background;
        case Foreground:
            return colors.foreground;
        case Muted:
            return colors.muted;
        case Subtle:
            return colors.subtle;
        case Accent:
            return colors.accent;
        case AccentBar:
            return colors.accentBar;
        case BreakAccent:
            return colors.breakAccent;
        case OnAccent:
            return colors.onAccent;
        case Surface:
            return colors.surface;
        case SurfaceMuted:
            return colors.surfaceMuted;
        case SurfaceActive:
            return colors.surfaceActive;
        case Outline:
            return colors.outline;
        case Guide:
            return colors.guide;
        case GuideFocus:
            return colors.guideFocus;
        case Phantom:
            return colors.phantom;
        case ProgressTrack:
            return colors.progressTrack;
        }
        return colors.foreground;
    }

    ThemeEntry defaultTheme() {
        return {.id = std::string{kDefaultThemeId}, .definition = {}, .builtIn = true};
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
        std::ranges::transform(id, id.begin(), [](char value) {
            return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
        });
        return id;
    }

    settings::SettingsResult<ThemeEntry> decodeToml(std::string_view text, std::string_view id) {
        ThemeFile candidate;
        if (const glz::error_ctx error =
                glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(candidate, text))
            return std::unexpected(errorFrom(error, text));
        if (candidate.name.empty())
            candidate.name = "Unnamed";
        if (candidate.name.size() > settings::rules::kThemeNameMaxLength)
            candidate.name.resize(settings::rules::kThemeNameMaxLength);
        return ThemeEntry{.id = std::string{id}, .definition = std::move(candidate), .builtIn = false};
    }

    settings::SettingsResult<std::string> encodeToml(const ThemeFile& theme) {
        ThemeFile mutableTheme = theme;
        std::string output;
        if (const glz::error_ctx error = glz::write_toml(mutableTheme, output))
            return std::unexpected(errorFrom(error, output));
        return output;
    }

} // namespace ui::themes
