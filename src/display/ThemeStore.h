#pragma once

#include <span>
#include <string_view>
#include <vector>

#include "fonts/FontCatalog.h"
#include "ui/Theme.h"

class ThemeStore {
public:
    void loadFromSd(const FontCatalog& fonts);
    bool selectById(std::string_view id);
    void selectNext();
    bool setSelectedTypeface(std::string_view typeface, const FontCatalog& fonts);
    const ui::themes::Theme& selected() const;
    std::span<const ui::themes::Theme> themes() const;

private:
    std::vector<ui::themes::Theme> themes_{ui::themes::defaultTheme()};
    size_t selectedIndex_ = 0;
};
