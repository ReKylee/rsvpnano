#pragma once

#include <Arduino.h>
#include <cstdint>

#include "book/BookMetadata.h"

class Preferences;
class IndexedBookStore;
class ReadingLoop;
class StorageManager;

namespace ReadingProgress {

    inline constexpr uint32_t kNoSavedWordIndex = UINT32_MAX;

    struct BookIdentity {
        uint32_t sourceSize = 0;
        uint32_t sourceFingerprint = 0;
        uint32_t wordCount = 0;
    };

    struct Session {
        BookMetadata metadata;
        String path;
        size_t index = 0;
        size_t lastSavedWordIndex = static_cast<size_t>(-1);
        uint32_t lastSaveMs = 0;
        bool fromStorage = false;

        void save(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader, bool force,
                  uint32_t nowMs);
        void cache(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader,
                   uint32_t wordIndex);
        void mirror(const IndexedBookStore& store, const ReadingLoop& reader) const;
        uint32_t restore(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader);
        bool saveChapterTransition(Preferences& preferences, const IndexedBookStore& store, ReadingLoop& reader,
                                   size_t previousWordIndex, size_t currentWordIndex, uint32_t nowMs);
        String title(const StorageManager& storage) const;
    };

    bool readPositionSidecar(const String& bookPath, const BookIdentity& identity, uint32_t& wordIndex);
    bool writePositionSidecar(const String& bookPath, const BookIdentity& identity, uint32_t wordIndex);
    String positionKey(const String& bookPath);
    String wordCountKey(const String& bookPath);
    String sourceSizeKey(const String& bookPath);
    String sourceFingerprintKey(const String& bookPath);
    String bookId(const String& bookPath);
    uint8_t percent(uint32_t wordIndex, uint32_t wordCount);

} // namespace ReadingProgress
