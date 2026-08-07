#include "display/ThemeStore.h"
#include <esp_log.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>

#include "board/BoardStorage.h"
#include "storage/fs/StoragePaths.h"

namespace {

    constexpr size_t kMaxThemeBytes = 4096;

} // namespace

void ThemeStore::loadFromSd() {
    const std::string selectedId = selected().id;
    themes_ = {ui::themes::defaultTheme()};
    selectedIndex_ = 0;

    File dir = Board::Storage::filesystem().open(StoragePaths::kThemesPath);
    if (!dir || !dir.isDirectory()) {
        if (dir) {
            dir.close();
        }
        selectById(selectedId);
        return;
    }

    std::vector<ui::themes::Theme> loaded;
    while (true) {
        File entry = dir.openNextFile();
        if (!entry) {
            break;
        }
        if (entry.isDirectory()) {
            entry.close();
            continue;
        }

        const std::string path = entry.path();
        if (!ui::themes::hasThemeExtension(path)) {
            entry.close();
            continue;
        }

        if (entry.size() > kMaxThemeBytes) {
            ESP_LOGW("theme", "skipped %s: file exceeds %u bytes", path.c_str(), static_cast<unsigned>(kMaxThemeBytes));
            entry.close();
            continue;
        }
        std::string text(entry.size(), '\0');
        const size_t count = entry.read(reinterpret_cast<uint8_t*>(text.data()), text.size());
        entry.close();
        if (count != text.size()) {
            ESP_LOGW("theme", "skipped %s: incomplete read", path.c_str());
            continue;
        }
        auto parsed = ui::themes::decodeToml(text, ui::themes::themeIdFromPath(path));
        if (!parsed) {
            ESP_LOGW("theme", "skipped %s: %s", path.c_str(), parsed.error().message.c_str());
            continue;
        }

        loaded.push_back(std::move(*parsed));
    }
    dir.close();

    std::ranges::sort(loaded, {}, &ui::themes::Theme::id);
    const auto duplicates = std::ranges::unique(loaded, {}, &ui::themes::Theme::id);
    loaded.erase(duplicates.begin(), duplicates.end());

    const auto defaultTheme = std::ranges::find(loaded, ui::themes::kDefaultThemeId, &ui::themes::Theme::id);
    if (defaultTheme != loaded.end()) {
        defaultTheme->builtIn = true;
        themes_.front() = std::move(*defaultTheme);
        loaded.erase(defaultTheme);
    }
    themes_.insert(themes_.end(), std::make_move_iterator(loaded.begin()), std::make_move_iterator(loaded.end()));
    selectById(selectedId);
}

bool ThemeStore::selectById(std::string_view id) {
    const auto found = std::ranges::find(themes_, id, &ui::themes::Theme::id);
    if (found == themes_.end())
        return false;
    selectedIndex_ = found - themes_.begin();
    return true;
}

void ThemeStore::selectNext() {
    selectedIndex_ = (selectedIndex_ + 1) % themes_.size();
}

const ui::themes::Theme& ThemeStore::selected() const {
    return themes_[selectedIndex_];
}

std::span<const ui::themes::Theme> ThemeStore::themes() const {
    return themes_;
}
