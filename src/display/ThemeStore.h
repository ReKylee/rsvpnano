#pragma once

#include <string_view>
#include <vector>

#include "ui/Theme.h"

class ThemeStore {
public:
    ThemeStore();

    void reset();
    void loadFromSd();
    bool selectById(std::string_view id);
    void selectNext();
    const ui::themes::Theme& selected() const;
    const std::vector<ui::themes::Theme>& themes() const;
    size_t selectedIndex() const;
    bool contains(std::string_view id) const;

private:
    size_t indexOf(std::string_view id) const;

    std::vector<ui::themes::Theme> themes_;
    size_t selectedIndex_ = 0;
};
