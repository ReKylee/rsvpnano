#pragma once

#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include <SheenBidi/SheenBidi.h>

#include "text/TextDirection.h"

namespace BidiText {

    struct Run {
        size_t offset = 0;
        size_t length = 0;
        bool rightToLeft = false;
    };

    struct LineRange {
        size_t offset = 0;
        size_t length = 0;
    };

    struct Codepoint {
        size_t offset = 0;
        uint32_t value = 0;
        bool rightToLeft = false;
    };

    using Line = std::vector<Run>;

    class Analysis {
    public:
        Analysis() = default;
        ~Analysis();
        Analysis(const Analysis&) = delete;
        Analysis& operator=(const Analysis&) = delete;
        Analysis(Analysis&&) = delete;
        Analysis& operator=(Analysis&&) = delete;

        void clear();
        std::expected<void, std::string> reset(std::string_view text, TextDirection baseDirection);
        std::expected<void, std::string> resolve(LineRange line, Line& output);
        std::expected<void, std::string> resolve(std::span<const LineRange> lines,
                                                 std::vector<Line>& output);
        bool rightToLeft() const {
            return rightToLeft_;
        }
        std::optional<bool> uniformRightToLeft(size_t offset, size_t length) const;

    private:
        std::string_view text_;
        SBAlgorithmRef algorithm_ = nullptr;
        SBParagraphRef paragraph_ = nullptr;
        bool rightToLeft_ = false;
    };

    void visualCodepoints(std::string_view text, const Line& line, std::vector<Codepoint>& output);

} // namespace BidiText
