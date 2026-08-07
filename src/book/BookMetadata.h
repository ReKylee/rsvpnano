#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

struct ChapterMarker {
    std::string title;
    size_t wordIndex = 0;
};

enum class BookDirection : uint8_t {
    automatic,
    ltr,
    rtl,
};

constexpr std::string_view toString(BookDirection direction) {
    switch (direction) {
    case BookDirection::ltr:
        return "ltr";
    case BookDirection::rtl:
        return "rtl";
    default:
        return "auto";
    }
}

constexpr std::optional<BookDirection> bookDirection(std::string_view value) {
    if (value == "auto")
        return BookDirection::automatic;
    if (value == "ltr")
        return BookDirection::ltr;
    if (value == "rtl")
        return BookDirection::rtl;
    return std::nullopt;
}

struct BookTextRun {
    size_t wordIndex = 0;
    std::string locale;
    BookDirection direction = BookDirection::automatic;
    uint32_t scriptMask = 0;
};

struct BookMetadata {
    std::string title;
    std::string author;
    std::string locale;
    BookDirection baseDirection = BookDirection::automatic;
    uint32_t scriptMask = 0;
    uint32_t requiredCapabilities = 0;
    size_t wordCount = 0;
    std::vector<ChapterMarker> chapters;
    std::vector<size_t> paragraphStarts;
    std::vector<BookTextRun> textRuns;

    const ChapterMarker* chapterAt(size_t wordIndex) const {
        const auto next = std::ranges::upper_bound(chapters, wordIndex, {}, &ChapterMarker::wordIndex);
        return next == chapters.begin() ? nullptr : &*std::prev(next);
    }

    void clear() {
        title.clear();
        author.clear();
        locale.clear();
        baseDirection = BookDirection::automatic;
        scriptMask = 0;
        requiredCapabilities = 0;
        wordCount = 0;
        chapters.clear();
        paragraphStarts.clear();
        textRuns.clear();
    }

    std::string_view localeAt(size_t wordIndex) const {
        const auto next = std::ranges::upper_bound(textRuns, wordIndex, {}, &BookTextRun::wordIndex);
        if (next == textRuns.begin())
            return locale;
        const std::string& runLocale = std::prev(next)->locale;
        return runLocale.empty() ? std::string_view{locale} : std::string_view{runLocale};
    }

    BookDirection directionAt(size_t wordIndex) const {
        const auto next = std::ranges::upper_bound(textRuns, wordIndex, {}, &BookTextRun::wordIndex);
        if (next == textRuns.begin())
            return baseDirection;
        const BookDirection runDirection = std::prev(next)->direction;
        return runDirection == BookDirection::automatic ? baseDirection : runDirection;
    }

    uint32_t scriptMaskAt(size_t wordIndex) const {
        const auto next = std::ranges::upper_bound(textRuns, wordIndex, {}, &BookTextRun::wordIndex);
        return next == textRuns.begin() ? scriptMask : std::prev(next)->scriptMask;
    }

    uint32_t scriptsForLocale(std::string_view value) const {
        uint32_t result = 0;
        for (const BookTextRun& run: textRuns) {
            if (run.locale == value)
                result |= run.scriptMask;
        }
        return result != 0 || (!textRuns.empty() && value != locale) ? result : scriptMask;
    }
};
