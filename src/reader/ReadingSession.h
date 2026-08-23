#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "book/BookMetadata.h"
#include "reader/ReadingState.h"
#include "storage/index/IndexedBookStore.h"

struct ReadingSession {
    BookMetadata metadata;
    size_t lastSavedWordIndex = static_cast<size_t>(-1);
    uint32_t lastAdvanceMs = 0;
    uint32_t lastSaveMs = 0;
    std::string currentWord;
    std::span<const std::string> words;
    const IndexedBookStore* bookStore = nullptr;
    bool playing = false;
    reading::State state;

    bool stored() const {
        return bookStore != nullptr && bookStore->isOpen();
    }
    std::string_view sourcePath() const {
        return stored() ? bookStore->sourcePath() : std::string_view{};
    }
};
