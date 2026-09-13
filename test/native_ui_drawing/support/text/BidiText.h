#pragma once
#include <cstdint>
#include <string_view>
#include <vector>
#include "localization/LocaleCatalog.h"
namespace BidiText {
    struct Line {};
    struct Codepoint { uint32_t value; };
    struct Range { size_t offset, length; };
    struct Analysis {
        bool reset(std::string_view, TextDirection) { return false; }
        bool resolve(Range, Line&) { return false; }
    };
    inline void visualCodepoints(std::string_view, const Line&, std::vector<Codepoint>&) {}
}
