#pragma once

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
        const ChapterMarker* result = nullptr;
        for (const ChapterMarker& chapter: chapters) {
            if (chapter.wordIndex > wordIndex)
                break;
            result = &chapter;
        }
        return result;
    }

    void clear() {
        title.clear();
        author.clear();
        wordCount = 0;
        chapters.clear();
        paragraphStarts.clear();
    }
};
