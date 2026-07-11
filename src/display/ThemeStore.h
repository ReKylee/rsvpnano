#pragma once

#include <Arduino.h>

#include <vector>

#include "ui/Theme.h"

class ThemeStore {
public:
    ThemeStore();

    void reset();
    void loadFromSd();
    bool selectById(const String& id);
    void selectNext();
    const ui::themes::Theme& selected() const;
    const std::vector<ui::themes::Theme>& themes() const;
    size_t selectedIndex() const;
    bool contains(const String& id) const;

private:
    size_t indexOf(const String& id) const;

    std::vector<ui::themes::Theme> themes_;
    size_t selectedIndex_ = 0;
};
