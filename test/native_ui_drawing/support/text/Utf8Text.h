#pragma once
#include <algorithm>
#include <array>
#include <cstdint>
#include <string_view>
namespace Utf8Text {
    inline size_t count(std::string_view text) { return text.size(); }
    inline size_t prefixBytes(std::string_view text, size_t count) { return std::min(count, text.size()); }
    inline bool next(std::string_view& text, uint32_t& value) {
        if (text.empty()) return false;
        value = static_cast<unsigned char>(text.front()); text.remove_prefix(1); return true;
    }
    inline size_t encode(uint32_t value, std::array<char,4>& output) { output[0]=static_cast<char>(value); return 1; }
}
