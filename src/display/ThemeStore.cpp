#include "display/ThemeStore.h"

#include <algorithm>
#include <cstring>

#include "board/BoardStorage.h"
#include "storage/fs/StoragePaths.h"

namespace {

constexpr size_t kMaxThemeBytes = 4096;

String readSmallFile(File &file) {
  String text;
  text.reserve(std::min(static_cast<size_t>(file.size()), kMaxThemeBytes));
  while (file.available() && text.length() < kMaxThemeBytes) {
    text += static_cast<char>(file.read());
  }
  return text;
}

} // namespace

ThemeStore::ThemeStore() { reset(); }

void ThemeStore::reset() {
  themes_.clear();
  themes_.push_back(DisplayTheme::defaultTheme());
  selectedIndex_ = 0;
}

void ThemeStore::loadFromSd() {
  const String selectedId = selected().id;
  reset();

  File dir = Board::Storage::filesystem().open(StoragePaths::kThemesPath);
  if (!dir || !dir.isDirectory()) {
    if (dir) {
      dir.close();
    }
    selectById(selectedId);
    return;
  }

  std::vector<DisplayTheme::Theme> loaded;
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
    if (!DisplayTheme::hasThemeExtension(path)) {
      entry.close();
      continue;
    }

    DisplayTheme::Theme theme;
    String error;
    if (DisplayTheme::parseThemeText(readSmallFile(entry), DisplayTheme::themeIdFromPath(path), theme, error)) {
      loaded.push_back(theme);
    } else {
      Serial.printf("[theme] skipped %s: %s\n", path.c_str(), error.c_str());
    }
    entry.close();
  }
  dir.close();

  std::sort(loaded.begin(), loaded.end(), [](const DisplayTheme::Theme &a, const DisplayTheme::Theme &b) {
    return std::strcmp(a.id.c_str(), b.id.c_str()) < 0;
  });
  for (const DisplayTheme::Theme &theme : loaded) {
    if (!contains(theme.id)) {
      themes_.push_back(theme);
    }
  }
  selectById(selectedId);
}

bool ThemeStore::selectById(const String &id) {
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

const DisplayTheme::Theme &ThemeStore::selected() const { return themes_[selectedIndex_]; }

const std::vector<DisplayTheme::Theme> &ThemeStore::themes() const { return themes_; }

size_t ThemeStore::selectedIndex() const { return selectedIndex_; }

bool ThemeStore::contains(const String &id) const { return indexOf(id) < themes_.size(); }

size_t ThemeStore::indexOf(const String &id) const {
  const auto found = std::find_if(themes_.begin(), themes_.end(), [&id](const DisplayTheme::Theme &theme) {
    return theme.id == id;
  });
  if (found == themes_.end()) {
    return themes_.size();
  }
  return static_cast<size_t>(found - themes_.begin());
}
