#pragma once

#include <Arduino.h>
#include <vector>

struct ChapterMarker {
  String title;
  size_t wordIndex = 0;
};

struct BookMetadata {
  String title;
  String author;
  size_t wordCount = 0;
  std::vector<ChapterMarker> chapters;
  std::vector<size_t> paragraphStarts;

  const ChapterMarker* chapterAt(size_t wordIndex) const {
    const ChapterMarker* result = nullptr;
    for (const ChapterMarker& chapter : chapters) {
      if (chapter.wordIndex > wordIndex) break;
      result = &chapter;
    }
    return result;
  }

  void clear() {
    title = "";
    author = "";
    wordCount = 0;
    chapters.clear();
    paragraphStarts.clear();
  }
};
