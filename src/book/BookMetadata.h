#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "text/TextDirection.h"
#include "text/UnicodeText.h"

struct ChapterMarker {
    std::string title;
    size_t wordIndex = 0;
};

struct BookTextRun {
    size_t wordIndex = 0;
    std::string locale;
    TextDirection direction = TextDirection::automatic;
    uint32_t scriptMask = 0;
};

struct BookMetadata {
    std::string title;
    std::string author;
    std::string locale;
    TextDirection baseDirection = TextDirection::automatic;
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
        baseDirection = TextDirection::automatic;
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

    TextDirection directionAt(size_t wordIndex) const {
        const auto next = std::ranges::upper_bound(textRuns, wordIndex, {}, &BookTextRun::wordIndex);
        if (next == textRuns.begin())
            return baseDirection;
        const TextDirection runDirection = std::prev(next)->direction;
        return runDirection == TextDirection::automatic ? baseDirection : runDirection;
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

    bool requiresBidi(size_t firstWord, size_t lastWord) const {
        if (firstWord >= lastWord)
            return false;
        if (textRuns.empty())
            return (requiredCapabilities & UnicodeText::CapabilityBidi) != 0
                || baseDirection == TextDirection::rtl;

        const auto needsBidi = [this](const BookTextRun* run) {
            const TextDirection direction = run != nullptr && run->direction != TextDirection::automatic
                                              ? run->direction
                                              : baseDirection;
            const uint32_t scripts = run != nullptr ? run->scriptMask : scriptMask;
            return direction == TextDirection::rtl
                || (scripts & (UnicodeText::ScriptHebrew | UnicodeText::ScriptArabic)) != 0;
        };
        auto next = std::ranges::upper_bound(textRuns, firstWord, {}, &BookTextRun::wordIndex);
        const BookTextRun* run = next == textRuns.begin() ? nullptr : &*std::prev(next);
        if (needsBidi(run))
            return true;
        while (next != textRuns.end() && next->wordIndex < lastWord) {
            if (needsBidi(&*next))
                return true;
            ++next;
        }
        return false;
    }
};
