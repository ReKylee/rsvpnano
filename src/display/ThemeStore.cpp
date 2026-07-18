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
        auto encoded = ui::themes::encodeToml(theme.definition);
        if (!encoded) {
            Serial.printf("[theme] encode failed: %s\n", encoded.error().message.c_str());
            return false;
        }
        const std::string tmpPath = std::string{path} + ".tmp";
        const std::string backupPath = std::string{path} + ".bak";
        auto& filesystem = Board::Storage::filesystem();
        filesystem.remove(tmpPath.c_str());
        filesystem.remove(backupPath.c_str());

        File file = filesystem.open(tmpPath.c_str(), FILE_WRITE);
        if (!file)
            return false;
        const size_t count = file.write(reinterpret_cast<const uint8_t*>(encoded->data()), encoded->size());
        const bool written = count == encoded->size() && file.getWriteError() == 0;
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

} // namespace

void ThemeStore::loadFromSd(const FontCatalog& fonts, const settings::TypographySettings& defaults) {
    const std::string selectedId = selected().id;
    themes_ = {ui::themes::defaultTheme(defaults)};
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

        if (entry.size() > kMaxThemeBytes) {
            Serial.printf("[theme] skipped %s: file exceeds %u bytes\n", path.c_str(),
                          static_cast<unsigned>(kMaxThemeBytes));
            entry.close();
            continue;
        }
        std::string text(entry.size(), '\0');
        const size_t count = entry.read(reinterpret_cast<uint8_t*>(text.data()), text.size());
        entry.close();
        if (count != text.size()) {
            Serial.printf("[theme] skipped %s: incomplete read\n", path.c_str());
            continue;
        }
        auto parsed = ui::themes::decodeToml(text, ui::themes::themeIdFromPath(path), defaults);
        if (!parsed) {
            Serial.printf("[theme] skipped %s: %s\n", path.c_str(), parsed.error().message.c_str());
            continue;
        }

        auto& fontId = parsed->definition.typography.fontId;
        if (fonts.find(fontId) == nullptr && !fonts.families().empty()) {
            Serial.printf("[theme] %s references missing font '%s'; using '%s'\n", path.c_str(), fontId.c_str(),
                          fonts.families().front().id.c_str());
            fontId = fonts.families().front().id;
            repairs.push_back({path, loaded.size()});
        }
        loaded.push_back(std::move(*parsed));
    }
    dir.close();

    for (const Repair& repair: repairs) {
        if (!writeThemeFile(repair.path.c_str(), loaded[repair.themeIndex]))
            Serial.printf("[theme] could not repair typeface in %s\n", repair.path.c_str());
    }

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
