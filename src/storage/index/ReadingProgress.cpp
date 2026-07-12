#include "storage/index/ReadingProgress.h"

#include <FS.h>
#include <algorithm>
#include <cstdio>

#include "board/BoardStorage.h"
#include "reader/ReadingLoop.h"
#include "settings/PreferenceSpecs.h"
#include "storage/StorageManager.h"
#include "storage/fs/StoragePaths.h"
#include "storage/index/IndexedBookStore.h"

namespace ReadingProgress {
    namespace {

        constexpr const char* kMagic = "rpos";
        constexpr uint32_t kVersion = 1;
        constexpr uint32_t kSaveIntervalMs = 15000;

        bool isValidIdentity(const BookIdentity& identity) {
            return identity.sourceSize > 0 && identity.wordCount > 0;
        }

        uint32_t hashPath(const String& path) {
            uint32_t hash = 2166136261UL;
            for (size_t index = 0; index < path.length(); ++index) {
                hash ^= static_cast<uint8_t>(path[index]);
                hash *= 16777619UL;
            }
            return hash;
        }

        String key(char prefix, const String& path) {
            char value[11];
            std::snprintf(value, sizeof(value), "%c%08lx", prefix, static_cast<unsigned long>(hashPath(path)));
            return String(value);
        }

        bool writeSessionSidecar(const Session& session, const IndexedBookStore& store, uint32_t wordIndex,
                                 uint32_t wordCount) {
            return session.fromStorage && !session.path.empty() && store.isOpen() && wordCount > 0
                && writePositionSidecar(session.path.c_str(), {store.sourceSize(), store.sourceFingerprint(), wordCount},
                                        wordIndex);
        }

        bool readSessionSidecar(const Session& session, const IndexedBookStore& store, const ReadingLoop& reader,
                                uint32_t& wordIndex) {
            wordIndex = kNoSavedWordIndex;
            const uint32_t wordCount = static_cast<uint32_t>(reader.wordCount());
            return session.fromStorage && !session.path.empty() && store.isOpen() && wordCount > 0
                && readPositionSidecar(session.path.c_str(), {store.sourceSize(), store.sourceFingerprint(), wordCount},
                                       wordIndex);
        }

    } // namespace

    bool readPositionSidecar(const String& bookPath, const BookIdentity& identity, uint32_t& wordIndex) {
        wordIndex = 0;
        if (bookPath.isEmpty() || !isValidIdentity(identity)) {
            return false;
        }

        File sidecar = Board::Storage::filesystem().open(StoragePaths::progressSidecarPathFor(bookPath), FILE_READ);
        if (!sidecar || sidecar.isDirectory()) {
            if (sidecar) {
                sidecar.close();
            }
            return false;
        }

        const String line = sidecar.readStringUntil('\n');
        sidecar.close();

        char magic[8] = {};
        unsigned long version = 0;
        unsigned long sourceSize = 0;
        unsigned long sourceFingerprint = 0;
        unsigned long wordCount = 0;
        unsigned long savedWordIndex = 0;
        const int parsed = std::sscanf(line.c_str(), "%7s %lu %lu %lu %lu %lu", magic, &version, &sourceSize,
                                       &sourceFingerprint, &wordCount, &savedWordIndex);

        if (parsed != 6 || String(magic) != kMagic || version != kVersion || sourceSize != identity.sourceSize
            || sourceFingerprint != identity.sourceFingerprint || wordCount != identity.wordCount) {
            Serial.printf("[storage-progress] ignored stale progress sidecar: %s\n", bookPath.c_str());
            return false;
        }

        wordIndex = std::min<uint32_t>(static_cast<uint32_t>(savedWordIndex), identity.wordCount - 1);
        return true;
    }

    bool writePositionSidecar(const String& bookPath, const BookIdentity& identity, uint32_t wordIndex) {
        if (bookPath.isEmpty() || !isValidIdentity(identity)) {
            return false;
        }

        const String sidecarPath = StoragePaths::progressSidecarPathFor(bookPath);
        File sidecar = Board::Storage::filesystem().open(sidecarPath, FILE_WRITE);
        if (!sidecar || sidecar.isDirectory()) {
            if (sidecar) {
                sidecar.close();
            }
            Serial.printf("[storage-progress] progress sidecar open failed: %s\n", sidecarPath.c_str());
            return false;
        }

        wordIndex = std::min<uint32_t>(wordIndex, identity.wordCount - 1);
        const size_t written =
            sidecar.printf("%s %lu %lu %lu %lu %lu\n", kMagic, static_cast<unsigned long>(kVersion),
                           static_cast<unsigned long>(identity.sourceSize),
                           static_cast<unsigned long>(identity.sourceFingerprint),
                           static_cast<unsigned long>(identity.wordCount), static_cast<unsigned long>(wordIndex));
        sidecar.close();

        if (written == 0) {
            Serial.printf("[storage-progress] progress sidecar write failed: %s\n", sidecarPath.c_str());
            return false;
        }

        Serial.printf("[storage-progress] mirrored position word=%u count=%u sidecar=%s\n",
                      static_cast<unsigned int>(wordIndex), static_cast<unsigned int>(identity.wordCount),
                      sidecarPath.c_str());
        return true;
    }

    bool readCachedPosition(Preferences& preferences, const String& bookPath, const BookIdentity& identity,
                            uint32_t& wordIndex) {
        if (bookPath.isEmpty() || !isValidIdentity(identity))
            return false;

        const String savedPositionKey = positionKey(bookPath);
        if (!settings::nvs::contains(preferences, savedPositionKey.c_str()))
            return false;

        const String savedCountKey = wordCountKey(bookPath);
        if (settings::nvs::contains(preferences, savedCountKey.c_str())
            && settings::nvs::get(preferences, savedCountKey.c_str(), uint32_t{0}) != identity.wordCount)
            return false;

        const String savedSizeKey = sourceSizeKey(bookPath);
        const String savedFingerprintKey = sourceFingerprintKey(bookPath);
        if (settings::nvs::contains(preferences, savedSizeKey.c_str())
            && settings::nvs::contains(preferences, savedFingerprintKey.c_str())
            && (settings::nvs::get(preferences, savedSizeKey.c_str(), uint32_t{0}) != identity.sourceSize
                || settings::nvs::get(preferences, savedFingerprintKey.c_str(), uint32_t{0})
                       != identity.sourceFingerprint))
            return false;

        wordIndex = std::min<uint32_t>(settings::nvs::get(preferences, savedPositionKey.c_str(), uint32_t{0}),
                                       identity.wordCount - 1);
        return true;
    }

    void cachePosition(Preferences& preferences, const String& bookPath, const BookIdentity& identity,
                       uint32_t wordIndex) {
        if (bookPath.isEmpty() || !isValidIdentity(identity))
            return;
        settings::nvs::put(preferences, positionKey(bookPath).c_str(),
                           std::min<uint32_t>(wordIndex, identity.wordCount - 1));
        settings::nvs::put(preferences, wordCountKey(bookPath).c_str(), identity.wordCount);
        settings::nvs::put(preferences, sourceSizeKey(bookPath).c_str(), identity.sourceSize);
        settings::nvs::put(preferences, sourceFingerprintKey(bookPath).c_str(), identity.sourceFingerprint);
    }

    uint8_t cachedPercent(Preferences& preferences, const String& bookPath) {
        const String savedPositionKey = positionKey(bookPath);
        const String savedCountKey = wordCountKey(bookPath);
        if (!settings::nvs::contains(preferences, savedPositionKey.c_str())
            || !settings::nvs::contains(preferences, savedCountKey.c_str()))
            return 0;
        return percent(settings::nvs::get(preferences, savedPositionKey.c_str(), uint32_t{0}),
                       settings::nvs::get(preferences, savedCountKey.c_str(), uint32_t{0}));
    }

    String positionKey(const String& bookPath) {
        return key('p', bookPath);
    }
    String wordCountKey(const String& bookPath) {
        return key('c', bookPath);
    }
    String sourceSizeKey(const String& bookPath) {
        return key('s', bookPath);
    }
    String sourceFingerprintKey(const String& bookPath) {
        return key('f', bookPath);
    }
    String bookId(const String& bookPath) {
        return key('b', bookPath);
    }

    uint8_t percent(uint32_t wordIndex, uint32_t wordCount) {
        if (wordCount <= 1)
            return 0;
        return static_cast<uint8_t>(std::min<uint32_t>((wordIndex * 100UL) / (wordCount - 1), 100));
    }

    void Session::save(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader, bool force,
                       uint32_t nowMs) {
        if (!fromStorage || path.empty() || (!force && nowMs - lastSaveMs < kSaveIntervalMs))
            return;
        const size_t wordIndex = reader.currentIndex();
        if (!force && wordIndex == lastSavedWordIndex)
            return;
        lastSaveMs = nowMs;
        cache(preferences, store, reader, static_cast<uint32_t>(wordIndex));
        Serial.printf("[storage-progress] saved position word=%u path=%s\n", static_cast<unsigned int>(wordIndex),
                      path.c_str());
    }

    void Session::cache(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader,
                        uint32_t wordIndex) {
        if (!fromStorage || path.empty())
            return;
        const size_t wordCount = reader.wordCount();
        if (wordCount > 0)
            wordIndex = std::min<uint32_t>(wordIndex, static_cast<uint32_t>(wordCount - 1));
        settings::save<settings::prefs::BookPath>(preferences, path.c_str());
        if (store.isOpen())
            cachePosition(preferences, path.c_str(),
                          {store.sourceSize(), store.sourceFingerprint(), static_cast<uint32_t>(wordCount)}, wordIndex);
        settings::save<settings::prefs::Wpm>(preferences, reader.wpm());
        lastSavedWordIndex = wordIndex;
    }

    void Session::mirror(const IndexedBookStore& store, const ReadingLoop& reader) const {
        writeSessionSidecar(*this, store, static_cast<uint32_t>(reader.currentIndex()),
                            static_cast<uint32_t>(reader.wordCount()));
    }

    uint32_t Session::restore(Preferences& preferences, const IndexedBookStore& store, const ReadingLoop& reader) {
        uint32_t wordIndex = kNoSavedWordIndex;
        if (readSessionSidecar(*this, store, reader, wordIndex))
            return wordIndex;

        if (!readCachedPosition(preferences, path.c_str(),
                                {store.sourceSize(), store.sourceFingerprint(),
                                 static_cast<uint32_t>(reader.wordCount())},
                                wordIndex))
            return kNoSavedWordIndex;
        writeSessionSidecar(*this, store, wordIndex, static_cast<uint32_t>(reader.wordCount()));
        return wordIndex;
    }

    bool Session::saveChapterTransition(Preferences& preferences, const IndexedBookStore& store, ReadingLoop& reader,
                                        size_t previousWordIndex, size_t currentWordIndex, uint32_t nowMs) {
        if (!fromStorage || path.empty())
            return false;
        for (const ChapterMarker& chapter: metadata.chapters) {
            if (chapter.wordIndex == 0 || chapter.wordIndex <= previousWordIndex
                || chapter.wordIndex > currentWordIndex)
                continue;
            reader.seekTo(chapter.wordIndex);
            save(preferences, store, reader, true, nowMs);
            mirror(store, reader);
            return true;
        }
        return false;
    }

    std::string Session::title(const StorageManager& storage) const {
        if (!metadata.title.empty())
            return metadata.title;
        if (!fromStorage)
            return "Demo";
        return storage.bookDisplayName(index);
    }

} // namespace ReadingProgress
