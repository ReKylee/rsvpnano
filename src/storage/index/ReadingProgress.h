#pragma once

#include <Arduino.h>
#include <Preferences.h>
#include <cstdint>
#include <expected>
#include <string>
#include <string_view>
#include <system_error>

#include "reader/ReadingSession.h"
#include "storage/StorageManager.h"
#include "storage/index/IndexedBookStore.h"

namespace ReadingProgress {

    inline constexpr uint32_t kNoSavedWordIndex = UINT32_MAX;

    struct BookIdentity {
        uint32_t sourceSize = 0;
        uint32_t sourceFingerprint = 0;
        uint32_t wordCount = 0;
    };

    std::expected<uint32_t, std::error_code> readBookStatePosition(std::string_view bookPath,
                                                                   const BookIdentity& identity);
    std::expected<void, std::error_code> writeBookStatePosition(std::string_view bookPath, const BookIdentity& identity,
                                                                uint32_t wordIndex);
    void save(ReadingSession& session, Preferences& preferences, bool force, uint32_t nowMs);
    void cache(ReadingSession& session, Preferences& preferences, uint32_t wordIndex);
    void mirror(const ReadingSession& session, const IndexedBookStore& store);
    uint32_t restore(ReadingSession& session, const IndexedBookStore& store);
    bool saveChapterTransition(ReadingSession& session, Preferences& preferences, const IndexedBookStore& store,
                               size_t previousWordIndex, size_t currentWordIndex, uint32_t nowMs);
    std::string title(const ReadingSession& session, const StorageManager& storage);
    uint8_t percent(uint32_t wordIndex, uint32_t wordCount);

} // namespace ReadingProgress
