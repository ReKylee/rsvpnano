#include "display/ThemeStore.h"

#include <algorithm>
#include <string>

#include "board/BoardStorage.h"
#include "storage/fs/StoragePaths.h"

namespace {

    constexpr size_t kMaxThemeBytes = 4096;

    std::string readSmallFile(File& file) {
        std::string text;
        text.reserve(std::min(static_cast<size_t>(file.size()), kMaxThemeBytes));
        while (file.available() && text.size() < kMaxThemeBytes)
            text.push_back(static_cast<char>(file.read()));
        return text;
    }

} // namespace

ThemeStore::ThemeStore() {
    reset();
}

void ThemeStore::reset() {
    themes_.clear();
    themes_.push_back(ui::themes::defaultTheme());
    selectedIndex_ = 0;
}

void ThemeStore::loadFromSd() {
    const std::string selectedId = selected().id;
    reset();

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

        const String path = entry.path();
        const std::string_view pathView{path.c_str(), path.length()};
        if (!ui::themes::hasThemeExtension(pathView)) {
            entry.close();
            continue;
        }

        ui::themes::Theme theme;
        std::string error;
        if (ui::themes::parseThemeText(readSmallFile(entry), ui::themes::themeIdFromPath(pathView), theme, error)) {
            loaded.push_back(theme);
        } else {
            Serial.printf("[theme] skipped %s: %s\n", path.c_str(), error.c_str());
        }
        entry.close();
    }
    dir.close();

    std::sort(loaded.begin(), loaded.end(), [](const ui::themes::Theme& a, const ui::themes::Theme& b) {
        return a.id < b.id;
    });
    for (const ui::themes::Theme& theme: loaded) {
        if (!contains(theme.id)) {
            themes_.push_back(theme);
        }
    }
    selectById(selectedId);
}

bool ThemeStore::selectById(std::string_view id) {
    const size_t index = indexOf(id);
    if (index >= themes_.size()) {
        return false;
    }
    selectedIndex_ = index;
    return true;
}

void ThemeStore::selectNext() {
    if (themes_.empty()) {
        reset();
        return;
    }
    selectedIndex_ = (selectedIndex_ + 1) % themes_.size();
}

const ui::themes::Theme& ThemeStore::selected() const {
    return themes_[selectedIndex_];
}

const std::vector<ui::themes::Theme>& ThemeStore::themes() const {
    return themes_;
}

size_t ThemeStore::selectedIndex() const {
    return selectedIndex_;
}

bool ThemeStore::contains(std::string_view id) const {
    return indexOf(id) < themes_.size();
}

size_t ThemeStore::indexOf(std::string_view id) const {
    const auto found = std::find_if(themes_.begin(), themes_.end(), [&id](const ui::themes::Theme& theme) {
        return theme.id == id;
    });
    if (found == themes_.end()) {
        return themes_.size();
    }
    return static_cast<size_t>(found - themes_.begin());
}
