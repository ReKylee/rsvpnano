#pragma once

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

struct ChapterMarker {
    std::string title;
    size_t wordIndex = 0;
};

struct BookMetadata {
    std::string title;
    std::string author;
    size_t wordCount = 0;
    std::vector<ChapterMarker> chapters;
    std::vector<size_t> paragraphStarts;

    const ChapterMarker* chapterAt(size_t wordIndex) const {
        const auto next = std::ranges::upper_bound(chapters, wordIndex, {}, &ChapterMarker::wordIndex);
        return next == chapters.begin() ? nullptr : &*std::prev(next);
    }

    void clear() {
        title.clear();
        author.clear();
        wordCount = 0;
        chapters.clear();
        paragraphStarts.clear();
    }
};
