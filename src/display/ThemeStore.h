#pragma once

#include <Arduino.h>

#include <vector>

#include "display/DisplayTheme.h"

class ThemeStore {
public:
  ThemeStore();

  void reset();
  void loadFromSd();
  bool selectById(const String &id);
  void selectNext();
  const DisplayTheme::Theme &selected() const;
  const std::vector<DisplayTheme::Theme> &themes() const;
  size_t selectedIndex() const;
  bool contains(const String &id) const;

private:
  size_t indexOf(const String &id) const;

  std::vector<DisplayTheme::Theme> themes_;
  size_t selectedIndex_ = 0;
};
