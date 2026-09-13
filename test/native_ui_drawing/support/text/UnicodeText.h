#pragma once
#include <cstdint>
#include <string_view>
namespace UnicodeText {
    inline constexpr uint32_t ScriptLatin = 1, ScriptCyrillic = 2;
    inline uint32_t scriptsIn(std::string_view) { return ScriptLatin; }
}
