#include "text/BidiText.h"

#include <limits>

#include "text/Utf8Text.h"

namespace BidiText {
    Analysis::~Analysis() {
        clear();
    }

    void Analysis::clear() {
        if (paragraph_)
            SBParagraphRelease(paragraph_);
        if (algorithm_)
            SBAlgorithmRelease(algorithm_);
        text_ = {};
        algorithm_ = nullptr;
        paragraph_ = nullptr;
        rightToLeft_ = false;
    }

    std::expected<void, std::string> Analysis::reset(std::string_view text, TextDirection baseDirection) {
        clear();
        text_ = text;
        rightToLeft_ = baseDirection == TextDirection::rtl;
        if (text.empty())
            return {};
        if (text.size() > std::numeric_limits<SBUInteger>::max()) {
            clear();
            return std::unexpected("bidi paragraph is too large");
        }

        SBCodepointSequence sequence{SBStringEncodingUTF8, const_cast<char*>(text.data()), text.size()};
        algorithm_ = SBAlgorithmCreate(&sequence);
        if (!algorithm_) {
            clear();
            return std::unexpected("bidi analysis allocation failed");
        }

        const SBLevel level = baseDirection == TextDirection::ltr ? 0
                            : baseDirection == TextDirection::rtl ? 1
                                                                  : SBLevelDefaultLTR;
        paragraph_ = SBAlgorithmCreateParagraph(algorithm_, 0, text.size(), level);
        if (!paragraph_) {
            clear();
            return std::unexpected("bidi paragraph allocation failed");
        }

        rightToLeft_ = (SBParagraphGetBaseLevel(paragraph_) & 1U) != 0;
        return {};
    }

    std::optional<bool> Analysis::uniformRightToLeft(size_t offset, size_t length) const {
        if (!paragraph_ || length == 0 || offset > text_.size() || length > text_.size() - offset)
            return std::nullopt;
        const SBLevel* levels = SBParagraphGetLevelsPtr(paragraph_);
        const bool rightToLeft = (levels[offset] & 1U) != 0;
        for (size_t index = offset + 1; index < offset + length; ++index) {
            if (((levels[index] & 1U) != 0) != rightToLeft)
                return std::nullopt;
        }
        return rightToLeft;
    }

    std::expected<void, std::string> Analysis::resolve(LineRange range, Line& output) {
        output.clear();
        if (!paragraph_)
            return std::unexpected("bidi paragraph is not initialized");

        const SBUInteger paragraphLength = SBParagraphGetLength(paragraph_);
        if (range.offset > paragraphLength || range.length > paragraphLength - range.offset)
            return std::unexpected("bidi line is outside its paragraph");
        if (range.length == 0)
            return {};

        const SBLineRef line = SBParagraphCreateLine(paragraph_, range.offset, range.length);
        if (!line)
            return std::unexpected("bidi line allocation failed");
        const SBUInteger runCount = SBLineGetRunCount(line);
        const SBRun* runs = SBLineGetRunsPtr(line);
        output.reserve(runCount);
        for (SBUInteger runIndex = 0; runIndex < runCount; ++runIndex)
            output.push_back(
                {runs[runIndex].offset, runs[runIndex].length, (runs[runIndex].level & 1U) != 0});
        SBLineRelease(line);
        return {};
    }

    void visualCodepoints(std::string_view text, const Line& line, std::vector<Codepoint>& output) {
        output.clear();
        for (const Run& run: line) {
            if (run.offset > text.size() || run.length > text.size() - run.offset)
                continue;
            if (run.rightToLeft) {
                size_t end = run.offset + run.length;
                while (end > run.offset) {
                    const size_t relative = Utf8Text::lastCodepointStart(text.substr(run.offset, end - run.offset));
                    const size_t offset = run.offset + relative;
                    std::string_view encoded = text.substr(offset, end - offset);
                    uint32_t codepoint = 0;
                    Utf8Text::next(encoded, codepoint);
                    const uint32_t mirror = SBCodepointGetMirror(codepoint);
                    output.push_back({offset, mirror == 0 ? codepoint : mirror, true});
                    end = offset;
                }
            } else {
                size_t offset = run.offset;
                std::string_view encoded = text.substr(run.offset, run.length);
                while (!encoded.empty()) {
                    const size_t bytes = encoded.size();
                    uint32_t codepoint = 0;
                    Utf8Text::next(encoded, codepoint);
                    output.push_back({offset, codepoint, false});
                    offset += bytes - encoded.size();
                }
            }
        }
    }

} // namespace BidiText
