#include "display/ThemeStore.h"
#include <esp_log.h>

#include <algorithm>
#include <iterator>
#include <ranges>
#include <string>

#include "board/BoardStorage.h"
#include "storage/fs/StorageFiles.h"
#include "storage/fs/StoragePaths.h"

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

        if (entry.size() > kMaximumFileBytes) {
            ESP_LOGW("theme", "skipped %s: file exceeds %u bytes", path.c_str(),
                     static_cast<unsigned>(kMaximumFileBytes));
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

std::expected<ui::themes::Theme, std::string> ThemeStore::install(std::string_view stagedPath,
                                                                  std::string_view finalPath) {
    const std::string staged{stagedPath};
    const std::string final{finalPath};
    const std::string id = ui::themes::themeIdFromPath(final);
    return StorageFiles::ensureDirectory(StoragePaths::kThemesPath)
        .transform_error([](std::error_code) { return std::string{"Theme folder could not be created"}; })
        .and_then([&]() -> std::expected<ui::themes::Theme, std::string> {
            if (StorageFiles::fileExists(final.c_str()))
                return std::unexpected("Theme already exists");
            return StorageFiles::readTextFile(Board::Storage::filesystem(), staged.c_str(), kMaximumFileBytes)
                .transform_error([](std::error_code) { return std::string{"Theme file could not be read"}; })
                .and_then([&](const std::string& text) -> std::expected<ui::themes::Theme, std::string> {
                    if (text.empty())
                        return std::unexpected("Theme file is empty");
                    return ui::themes::decodeToml(text, id)
                        .transform_error([](const auto& error) { return error.message; });
                });
        })
        .and_then([&](const ui::themes::Theme&) {
            return StorageFiles::replaceFileAtomic(Board::Storage::filesystem(), final.c_str(), staged.c_str(),
                                                   (final + ".bak").c_str())
                .transform_error([](std::error_code) { return std::string{"Theme file could not be installed"}; });
        })
        .and_then([&]() -> std::expected<ui::themes::Theme, std::string> {
            loadFromSd();
            const auto theme = std::ranges::find(themes_, id, &ui::themes::Theme::id);
            if (theme != themes_.end())
                return *theme;

            Board::Storage::filesystem().remove(final.c_str());
            loadFromSd();
            return std::unexpected("Installed theme could not be loaded");
        });
}

std::expected<void, std::string> ThemeStore::remove(std::string_view id) {
    loadFromSd();
    const auto theme = std::ranges::find(themes_, id, &ui::themes::Theme::id);
    if (theme == themes_.end())
        return std::unexpected("Theme not found");
    if (theme->builtIn)
        return std::unexpected("Built-in theme cannot be removed");

    File directory = Board::Storage::filesystem().open(StoragePaths::kThemesPath);
    while (directory && directory.isDirectory()) {
        File entry = directory.openNextFile();
        if (!entry)
            break;
        const std::string path = entry.path();
        const bool matches = !entry.isDirectory() && ui::themes::hasThemeExtension(path)
                          && ui::themes::themeIdFromPath(path) == id;
        entry.close();
        if (!matches)
            continue;
        const bool removed = Board::Storage::filesystem().remove(path.c_str());
        directory.close();
        if (!removed)
            return std::unexpected("Theme file could not be removed");
        loadFromSd();
        return {};
    }
    if (directory)
        directory.close();
    return std::unexpected("Theme file not found");
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
