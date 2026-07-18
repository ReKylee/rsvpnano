#pragma once

#include <Arduino.h>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <system_error>

#include "book/BookMetadata.h"
#include "settings/SettingsModel.h"

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

    struct BookState {
        uint32_t sourceSize = 0;
        uint32_t sourceFingerprint = 0;
        uint32_t wordCount = 0;
        uint32_t wordIndex = 0;
        std::optional<settings::TypographySettings> bookTypographyOverride;

        bool operator==(const BookState&) const = default;
    };

    struct Session {
        BookMetadata metadata;
        std::string path;
        size_t index = 0;
        size_t lastSavedWordIndex = static_cast<size_t>(-1);
        uint32_t lastSaveMs = 0;
        bool fromStorage = false;
        BookState state;

        void save(Preferences& preferences, const ReadingLoop& reader, bool force, uint32_t nowMs);
        void cache(Preferences& preferences, const ReadingLoop& reader, uint32_t wordIndex);
        void mirror(const IndexedBookStore& store, const ReadingLoop& reader) const;
        uint32_t restore(const IndexedBookStore& store, const ReadingLoop& reader);
        bool saveChapterTransition(Preferences& preferences, const IndexedBookStore& store, ReadingLoop& reader,
                                   size_t previousWordIndex, size_t currentWordIndex, uint32_t nowMs);
        std::string title(const StorageManager& storage) const;
    };

    std::expected<uint32_t, std::error_code> readBookStatePosition(const String& bookPath,
                                                                   const BookIdentity& identity);
    std::expected<void, std::error_code> writeBookStatePosition(const String& bookPath, const BookIdentity& identity,
                                                                uint32_t wordIndex);
    uint8_t percent(uint32_t wordIndex, uint32_t wordCount);

} // namespace ReadingProgress
