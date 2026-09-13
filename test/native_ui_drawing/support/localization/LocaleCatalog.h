#pragma once
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>
enum class TextDirection { ltr, rtl };
namespace locales {
    struct InstalledPack { std::string id, locale; TextDirection direction = TextDirection::ltr; };
    struct Catalog {};
    struct UiAssets {
        std::vector<uint8_t> font{};
        TextDirection direction = TextDirection::ltr;
        bool owns(std::string_view) const { return false; }
        std::string_view text(size_t) const { return {}; }
    };
    inline const InstalledPack* findPackForScripts(const Catalog&, std::string_view, uint32_t) { return nullptr; }
    inline uint8_t uiFontCellWidth(const std::vector<uint8_t>&) { return 6; }
    inline uint8_t uiFontHeight(const std::vector<uint8_t>&) { return 9; }
}
