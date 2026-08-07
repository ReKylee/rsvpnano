#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

#include "book/BookMetadata.h"
#include "settings/SettingsModel.h"
#include "storage/index/IndexedBookStore.h"

struct ReadingSession {
    struct BookState {
        uint32_t sourceSize = 0;
        uint32_t sourceFingerprint = 0;
        uint32_t wordCount = 0;
        uint32_t wordIndex = 0;
        settings::ReadingOverrides overrides;

        bool operator==(const BookState&) const = default;
    };

    BookMetadata metadata;
    std::string path;
    size_t bookIndex = 0;
    size_t currentIndex = 0;
    size_t lastSavedWordIndex = static_cast<size_t>(-1);
    uint32_t lastAdvanceMs = 0;
    uint32_t lastSaveMs = 0;
    std::string currentWord;
    std::span<const std::string> words;
    const IndexedBookStore* bookStore = nullptr;
    bool playing = false;
    bool fromStorage = false;
    BookState state;
};
