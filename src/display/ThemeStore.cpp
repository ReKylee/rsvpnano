#include "display/ThemeStore.h"

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>

#include "board/BoardStorage.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

namespace {

    constexpr size_t kMaxThemeBytes = 4096;

    struct Repair {
        std::string path;
        size_t themeIndex;
    };

    bool writeThemeFile(const char* path, const ui::themes::Theme& theme) {
        if (!StorageFiles::ensureDirectory(StoragePaths::kThemesPath, "theme"))
            return false;
        const std::string tmpPath = std::string{path} + ".tmp";
        const std::string backupPath = std::string{path} + ".bak";
        auto& filesystem = Board::Storage::filesystem();
        filesystem.remove(tmpPath.c_str());
        filesystem.remove(backupPath.c_str());

        File file = filesystem.open(tmpPath.c_str(), FILE_WRITE);
        if (!file)
            return false;
        file.printf("@rtheme\nname=%s\ntypeface=%s\n", theme.name.c_str(), theme.typeface.c_str());
        for (size_t i = 0; i < theme.colors.size(); ++i) {
            const std::string_view role = ui::themes::colorRoleName(i);
            file.printf("%s=0x%04X\n", role.data(), theme.colors[i]);
        }
        const bool written = file.getWriteError() == 0;
        file.close();
        if (!written) {
            filesystem.remove(tmpPath.c_str());
            return false;
        }

        const bool hadOriginal = StorageFiles::fileExists(path);
        if (hadOriginal && !filesystem.rename(path, backupPath.c_str())) {
            filesystem.remove(tmpPath.c_str());
            return false;
        }
        if (filesystem.rename(tmpPath.c_str(), path)) {
            if (hadOriginal)
                filesystem.remove(backupPath.c_str());
            return true;
        }
        if (hadOriginal)
            filesystem.rename(backupPath.c_str(), path);
        filesystem.remove(tmpPath.c_str());
        return false;
    }

    std::string themePath(std::string_view id) {
        std::string path{StoragePaths::kThemesPath};
        path += '/';
        path += id;
        path += ui::themes::kThemeExtension;
        return path;
    }

} // namespace

void ThemeStore::loadFromSd(const FontCatalog& fonts) {
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
    std::vector<Repair> repairs;
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

        std::string text(std::min(entry.size(), kMaxThemeBytes), '\0');
        text.resize(entry.read(reinterpret_cast<uint8_t*>(text.data()), text.size()));
        entry.close();
        ui::themes::Theme theme;
        std::string error;
        bool hasTypefaceValue = false;
        if (ui::themes::parseThemeText(text, ui::themes::themeIdFromPath(path), theme, error, &hasTypefaceValue)) {
            const FontCatalog::Family* family = fonts.find(theme.typeface);
            const std::string& typeface = family == nullptr ? fonts.families().front().id : family->id;
            if (!hasTypefaceValue || theme.typeface != typeface) {
                theme.typeface = typeface;
                repairs.push_back({path, loaded.size()});
            }
            loaded.push_back(std::move(theme));
        } else {
            Serial.printf("[theme] skipped %s: %s\n", path.c_str(), error.c_str());
        }
    }
    dir.close();

    for (const Repair& repair: repairs) {
        if (!writeThemeFile(repair.path.c_str(), loaded[repair.themeIndex]))
            Serial.printf("[theme] could not repair typeface in %s\n", repair.path.c_str());
    }

    std::ranges::sort(loaded, {}, &ui::themes::Theme::id);
    const auto duplicates = std::ranges::unique(loaded, {}, &ui::themes::Theme::id);
    loaded.erase(duplicates.begin(), duplicates.end());

    const auto defaultTheme = std::ranges::find_if(loaded, [](const ui::themes::Theme& theme) {
        return theme.id == ui::themes::kDefaultThemeId;
    });
    if (defaultTheme != loaded.end()) {
        defaultTheme->builtIn = true;
        themes_.front() = std::move(*defaultTheme);
        loaded.erase(defaultTheme);
    }
    themes_.insert(themes_.end(), std::make_move_iterator(loaded.begin()), std::make_move_iterator(loaded.end()));
    selectById(selectedId);
}

bool ThemeStore::selectById(std::string_view id) {
    const auto found = std::ranges::find_if(themes_, [id](const ui::themes::Theme& theme) {
        return theme.id == id;
    });
    if (found == themes_.end())
        return false;
    selectedIndex_ = found - themes_.begin();
    return true;
}

void ThemeStore::selectNext() {
    selectedIndex_ = (selectedIndex_ + 1) % themes_.size();
}

bool ThemeStore::setSelectedTypeface(std::string_view typeface, const FontCatalog& fonts) {
    const FontCatalog::Family* family = fonts.find(typeface);
    if (family == nullptr)
        return false;

    ui::themes::Theme& theme = themes_[selectedIndex_];
    if (theme.typeface == family->id)
        return true;

    const std::string previous = theme.typeface;
    theme.typeface = family->id;
    const std::string path = themePath(theme.id);
    if (writeThemeFile(path.c_str(), theme))
        return true;

    theme.typeface = previous;
    return false;
}

const ui::themes::Theme& ThemeStore::selected() const {
    return themes_[selectedIndex_];
}

std::span<const ui::themes::Theme> ThemeStore::themes() const {
    return themes_;
}
