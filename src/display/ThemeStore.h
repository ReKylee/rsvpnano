#pragma once

#include <expected>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "ui/Theme.h"

class ThemeStore {
public:
    static constexpr size_t kMaximumFileBytes = 4096;

    void loadFromSd();
    std::expected<ui::themes::Theme, std::string> install(std::string_view stagedPath,
                                                          std::string_view finalPath);
    std::expected<void, std::string> remove(std::string_view id);
    bool selectById(std::string_view id);
    void selectNext();
    const ui::themes::Theme& selected() const;
    std::span<const ui::themes::Theme> themes() const;

private:
    std::vector<ui::themes::Theme> themes_{ui::themes::defaultTheme()};
    size_t selectedIndex_ = 0;
};
