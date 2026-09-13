#pragma once

#include <algorithm>
#include <span>
#include <vector>
#include "settings/SettingsModel.h"
#include "localization/LocaleCatalog.h"
#include "ui/Layouts.h"

// Recording boundaries for screen composition, not a graphics or filesystem simulation.
class ThemeStore {
public:
    std::vector<ui::themes::Theme> entries{{"default", {"Default"}}};
    mutable int resolutions = 0;
    void loadFromSd() {}
    const ui::themes::Theme& resolve(std::string_view id) const {
        ++resolutions;
        const auto found = std::ranges::find(entries, id, &ui::themes::Theme::id);
        return found == entries.end() ? entries.front() : *found;
    }
    const ui::themes::Theme& next(std::string_view id) const {
        const auto found = std::ranges::find(entries, id, &ui::themes::Theme::id);
        const size_t index = found == entries.end() ? 0 : static_cast<size_t>(found - entries.begin());
        return entries[(index + 1) % entries.size()];
    }
};

namespace screens {
    enum class Screen : uint8_t { Settings = 3, InterfaceSettings = 5 };
    class InterfaceScreen {
    public:
        ThemeStore themes;
        bool begin(ui::Context&, settings::InterfaceSettings&, const locales::Catalog&, void (*)(uint8_t));
        bool draw(ui::Context&, settings::InterfaceSettings&, std::span<const uint32_t>, void (*)(uint8_t), Screen&);
    private:
        const locales::Catalog* languages_ = nullptr;
    };
    namespace detail { ui::Rect content(ui::Context&); }
}
